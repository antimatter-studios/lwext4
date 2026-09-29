#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Run the lwext4 ESP-IDF example in Espressif's QEMU and check the
filesystems it wrote on the host with e2fsck and debugfs.

Run inside the ESP-IDF environment (ci/run.sh esp32-qemu <target> does all
of this) after `idf.py build`:

    host/run_qemu_test.py --build-dir build-esp32 \\
        [--flash-image dist/esp32/lwext4-example-esp32.bin] [--timeout 600]

--flash-image is the merged image a user writes to a board at offset 0
(bootloader + partition table + app + host-made ext4 partition). Without it,
one is merged from the build directory's flash_args the same way.

Sequence (each boot is `idf.py qemu --flash-file <image>`, i.e. a power-on
reset of the same emulated board; on esp32 an SD card image with an MBR is
attached with -drive if=sd):
  boot 1   verify the host-made filesystems, write to them, ext4_mkfs the
           others and fill them
  host     extract every ext4 filesystem from the flash/SD images, e2fsck -fn,
           check the files the firmware wrote with debugfs; add fromhost.bin
           with debugfs and write the filesystems back
  boot 2   verify everything after the reset, read fromhost.bin, truncate a
           file and remove a directory
  host     e2fsck -fn, debugfs checks
  boot 3   read-only check of the final state
  host     e2fsck -fn, debugfs checks
"""
import argparse
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import pattern  # noqa: E402

os.environ["PATH"] += os.pathsep + "/sbin" + os.pathsep + "/usr/sbin"

# Must match main/lwext4_example_main.c.
DEV_SEED = 1
HOST_INJECT_SEED = 3
HOST_INJECT_SIZE = 50000
TRUNC_SIZE = 100000
BIG_SIZE = {"flash": 256 * 1024 + 123, "sd": 1024 * 1024 + 4321}
DEV_MANY_FILES = 100

RESULT_RE = re.compile(r"LWEXT4-TEST: (PASS.*|FAIL.*)")
CRASH_RE = re.compile(
    r"Guru Meditation|abort\(\) was called|Backtrace:|Rebooting\.\.\.|"
    r"assertion failed|Stack canary|stack overflow|CORRUPT HEAP"
)

failures = []


def log(msg):
    print(f"== {msg}", flush=True)


def fail(msg):
    print(f"FAIL: {msg}", flush=True)
    failures.append(msg)


def run(cmd, **kw):
    return subprocess.run(cmd, check=False, text=True, capture_output=True, **kw)


# --------------------------------------------------------------------- QEMU


def boot(args, n):
    """Power on the emulated board and run until the firmware prints its
    verdict (or crashes, or the timeout expires). Returns the console log."""
    cmd = ["idf.py", "-B", args.build_dir, "qemu", "--flash-file", args.flash]
    if args.sd:
        cmd += ["--qemu-extra-args", f"-drive file={args.sd},if=sd,format=raw"]
    log(f"boot {n}: {' '.join(cmd)}")
    logf = os.path.join(args.work, f"boot{n}.log")
    proc = subprocess.Popen(
        cmd,
        cwd=PROJECT,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        bufsize=1,
        start_new_session=True,
    )
    verdict = None
    console = []
    start = time.monotonic()
    timer = threading.Timer(args.timeout, lambda: os.killpg(proc.pid, signal.SIGKILL))
    timer.start()
    with open(logf, "w", buffering=1) as out:
        for line in proc.stdout:
            out.write(line)
            console.append(line)
            sys.stdout.write(f"  | {line}")
            sys.stdout.flush()
            m = RESULT_RE.search(line)
            if m:
                verdict = m.group(1)
                break
            if CRASH_RE.search(line):
                verdict = f"FAIL: firmware crashed: {line.strip()}"
                time.sleep(2)  # let the backtrace reach the log
                break
    timer.cancel()
    # SIGTERM: QEMU shuts down cleanly and flushes the drive images.
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        rest, _ = proc.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        rest, _ = proc.communicate()
    with open(logf, "a") as out:
        out.write(rest or "")
    elapsed = time.monotonic() - start
    if verdict is None:
        if elapsed >= args.timeout:
            verdict = f"FAIL: timeout after {args.timeout} s"
        else:
            verdict = f"FAIL: QEMU exited ({proc.returncode}) without a verdict"
    if not verdict.startswith("PASS"):
        fail(f"boot {n}: {verdict}")
        return None
    log(f"boot {n}: {verdict} ({elapsed:.0f} s)")
    return "".join(console)


# ------------------------------------------------------------ disk images


def flash_partitions(flash):
    """Parse the partition table at 0x8000 of a flash image."""
    parts = {}
    with open(flash, "rb") as f:
        f.seek(0x8000)
        table = f.read(0xC00)
    for i in range(0, len(table), 32):
        magic, _, _, offset, size, label, _ = struct.unpack("<HBBII16sI", table[i : i + 32])
        if magic != 0x50AA:
            break
        parts[label.rstrip(b"\0").decode()] = (offset, size)
    return parts


def mbr_partitions(img):
    with open(img, "rb") as f:
        mbr = f.read(512)
    assert mbr[510:512] == b"\x55\xaa", "SD image lost its MBR"
    parts = []
    for i in range(4):
        _, _, _, _, lba, count = struct.unpack_from("<B3sB3sII", mbr, 446 + 16 * i)
        if count:
            parts.append((lba * 512, count * 512))
    return parts


def extract(img, offset, size, out):
    with open(img, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    with open(out, "wb") as f:
        f.write(data)


def put_back(img, offset, src):
    with open(src, "rb") as f:
        data = f.read()
    with open(img, "r+b") as f:
        f.seek(offset)
        f.write(data)


# ----------------------------------------------------------------- checks


def debugfs(img, request, write=False):
    cmd = ["debugfs"] + (["-w"] if write else []) + ["-R", request, img]
    r = run(cmd)
    if r.returncode != 0:
        fail(f"{' '.join(cmd)}: exit {r.returncode}: {r.stderr.strip()}")
    return r.stdout


def e2fsck(name, img):
    r = run(["e2fsck", "-fn", img])
    if r.returncode != 0:
        fail(f"{name}: e2fsck -fn exit {r.returncode}")
        print("\n".join("    " + l for l in (r.stdout + r.stderr).splitlines()[:60]))
    else:
        print(f"  {name}: e2fsck -fn clean: {r.stdout.strip().splitlines()[-1]}")


def listing(img, path):
    names = set()
    for line in debugfs(img, f"ls -p {path}").splitlines():
        fields = line.split("/")  # /inode/mode/uid/gid/name/size/
        if len(fields) >= 7:
            names.add(fields[5])
    return names


def check_file(name, img, path, seed, size):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "f")
        debugfs(img, f"dump {path} {out}")
        if not os.path.exists(out):
            fail(f"{name}: {path} missing")
            return
        with open(out, "rb") as f:
            got = f.read()
    if got != pattern.pattern(seed, size):
        fail(f"{name}: {path}: content differs ({len(got)} bytes, expected {size})")


def check_string(name, img, path, want):
    got = debugfs(img, f"cat {path}")
    if got != want:
        fail(f"{name}: {path}: {got!r}, expected {want!r}")


def check_fs(name, medium, img, boot, device_made):
    """Independent host-side view of what the firmware wrote."""
    e2fsck(name, img)
    dev = listing(img, "/device")
    subdirs = {f"sub{i:02d}" for i in range(20)}
    if boot >= 2:
        subdirs.discard("sub05")
    want = {".", "..", "data.bin", "renamed.txt", "boot1.done"} | subdirs
    if device_made:
        want.add("many")
    if boot >= 2:
        want.add("boot2.done")
    if dev != want:
        fail(f"{name}: /device: extra {sorted(dev - want)}, missing {sorted(want - dev)}")
    size = BIG_SIZE[medium] if boot == 1 else TRUNC_SIZE
    check_file(name, img, "/device/data.bin", DEV_SEED, size)
    check_string(name, img, "/device/renamed.txt", medium)
    if device_made:
        many = listing(img, "/device/many")
        if len(many) != DEV_MANY_FILES + 2:
            fail(f"{name}: /device/many has {len(many) - 2} entries, expected {DEV_MANY_FILES}")
        stats = debugfs(img, "stats")
        if "has_journal" not in stats:
            fail(f"{name}: ext4_mkfs made no journal")
        feats = next((l for l in stats.splitlines() if "features" in l), "")
        print(f"  {name}: {feats.strip()}")
    else:
        check_string(name, img, "/hello.txt", "Hello from mke2fs on the build host\n")
    if boot >= 2:
        check_file(name, img, "/fromhost.bin", HOST_INJECT_SEED, HOST_INJECT_SIZE)


def filesystems(args):
    """(name, medium, container image, offset, size, device_made)"""
    fl = flash_partitions(args.flash)
    out = [
        ("flash/ext4host", "flash", args.flash, *fl["ext4host"], False),
        ("flash/ext4dev", "flash", args.flash, *fl["ext4dev"], True),
    ]
    if args.sd:
        p = mbr_partitions(args.sd)
        out += [
            ("sd/p1", "sd", args.sd, *p[0], False),
            ("sd/p2", "sd", args.sd, *p[1], True),
        ]
    return out


def host_pass(args, boot):
    log(f"host checks after boot {boot}")
    inject = os.path.join(args.work, "fromhost.bin")
    pattern.main(["", "gen", str(HOST_INJECT_SEED), str(HOST_INJECT_SIZE), inject])
    for name, medium, container, off, size, device_made in filesystems(args):
        img = os.path.join(args.work, name.replace("/", "-") + f".boot{boot}.img")
        extract(container, off, size, img)
        check_fs(name, medium, img, boot, device_made)
        if boot == 1:
            # The host adds a file of its own for boot 2 to find.
            debugfs(img, f"write {inject} fromhost.bin", write=True)
            e2fsck(name + " after debugfs write", img)
            put_back(container, off, img)


# ------------------------------------------------------------------- main


def sdkconfig_value(build_dir, name):
    with open(os.path.join(build_dir, "config", "sdkconfig.json")) as f:
        return json.load(f).get(name)


def prepare_flash(args, target):
    size_mb = int(sdkconfig_value(args.build_dir, "ESPTOOLPY_FLASHSIZE").rstrip("MB"))
    args.flash = os.path.join(args.work, "flash.bin")
    if args.flash_image:
        shutil.copyfile(args.flash_image, args.flash)
    else:
        subprocess.check_call(
            [sys.executable, "-m", "esptool", "--chip", target, "merge_bin",
             "-o", args.flash, "@flash_args"],
            cwd=args.build_dir,
        )
    # A flash chip has a fixed size; unprogrammed NOR flash reads 0xff.
    with open(args.flash, "r+b") as f:
        f.seek(0, os.SEEK_END)
        pad = size_mb * 1024 * 1024 - f.tell()
        if pad < 0:
            sys.exit(f"{args.flash} is larger than the {size_mb} MB flash")
        f.write(b"\xff" * pad)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--build-dir", default=os.path.join(PROJECT, "build"))
    ap.add_argument("--flash-image", help="merged flash image (write at 0x0)")
    ap.add_argument("--timeout", type=int, default=900, help="seconds per boot")
    args = ap.parse_args()
    args.build_dir = os.path.abspath(args.build_dir)
    args.work = os.path.join(args.build_dir, "qemu-test")
    shutil.rmtree(args.work, ignore_errors=True)
    os.makedirs(args.work)

    with open(os.path.join(args.build_dir, "project_description.json")) as f:
        target = json.load(f)["target"]
    use_sd = bool(sdkconfig_value(args.build_dir, "EXAMPLE_SD_SDMMC"))
    log(f"target {target}, SD card on SDMMC: {'yes' if use_sd else 'no'}")

    prepare_flash(args, target)
    args.sd = None
    if use_sd:
        args.sd = os.path.join(args.work, "sd.img")
        subprocess.check_call([os.path.join(HERE, "mkimage.sh"), "sd", args.sd])

    media = 2 if use_sd else 1
    for n in (1, 2, 3):
        console = boot(args, n)
        if console is None:
            break
        if n == 2:
            seen = console.count("fromhost.bin: verified")
            if seen != 2 * media:
                fail(f"boot 2 verified fromhost.bin on {seen} filesystems, expected {2 * media}")
        host_pass(args, n)
        if failures:
            break

    if failures:
        print(f"\nlwext4 ESP-IDF QEMU test ({target}): {len(failures)} failure(s)")
        for f in failures:
            print(f"  - {f}")
        return 1
    print(f"\nlwext4 ESP-IDF QEMU test ({target}): PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
