#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Boot the lwext4 Zephyr example in QEMU, copy its RAM disk out of the
emulated memory and check the file system on the host.

Run inside the zephyr environment after `west build` (ci/run.sh
zephyr-qemu <board> does both):

    examples/zephyr/host/run_qemu_test.py --build-dir <build dir> [--timeout 900]

1. QEMU is started by Zephyr's own run target, `west build -t run`. The
   board's configuration (boards/<board>.conf, CONFIG_QEMU_EXTRA_FLAGS)
   adds a QEMU monitor on the socket <build dir>/qemu-monitor.sock. The
   firmware formats its RAM disk, works on it and prints
   "LWEXT4-TEST: PASS" or "LWEXT4-TEST: FAIL: <reason>", then idles.
2. After a PASS the monitor saves the RAM disk (the driver's buffer
   disk_buf_0, whose address and size zephyr.elf gives) to disk.img
   ("pmemsave"). Then QEMU is stopped (SIGTERM); also when the timeout
   expires.
3. disk.img must be an ext4 file system that `e2fsck -fn` finds clean,
   labelled and with a journal as ext4_mkfs made it, holding exactly the
   directories and files the firmware wrote (debugfs).

The console log and disk.img are left in <build dir>/qemu-test/.
Exits 0 only if everything passed.
"""
import argparse
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading

from elftools.elf.elffile import ELFFile

os.environ["PATH"] += os.pathsep + "/sbin" + os.pathsep + "/usr/sbin"

# The RAM disk's buffer in the Zephyr RAM disk driver (drivers/disk/
# ramdisk.c: disk_buf_<instance>)
RAM_DISK_SYMBOL = "disk_buf_0"
MONITOR = "qemu-monitor.sock"
RESULT_RE = re.compile(r"LWEXT4-TEST: (PASS|FAIL.*)")
# Zephyr's fatal error handler, e.g. a failed lwext4 assertion or a fault
CRASH_RE = re.compile(r">>> ZEPHYR FATAL ERROR|ASSERTION FAIL")

# Must match src/main.c.
LABEL = "lwext4-zephyr"
FILES = {
    "/docs/hello.txt": b"Hello from lwext4 on Zephyr!\n",
    "/docs/readme.txt": b"This file is renamed below.\n",
    "/data/pattern.bin": bytes(
        (i * 7 + i // 256) & 0xFF for i in range(64 * 1024 + 123)
    ),
}
DIRS = {
    "/": ["lost+found", "docs", "data"],
    "/docs": ["hello.txt", "readme.txt"],
    "/data": ["pattern.bin"],
}

failures = []


def log(msg):
    print(f"== {msg}", flush=True)


def fail(msg):
    print(f"FAIL: {msg}", flush=True)
    failures.append(msg)


def run(cmd):
    return subprocess.run(cmd, check=False, text=True, capture_output=True)


def ram_disk(elf):
    """Address and size of the RAM disk buffer in the firmware."""
    with open(elf, "rb") as f:
        symtab = ELFFile(f).get_section_by_name(".symtab")
        for sym in symtab.iter_symbols():
            if sym.name == RAM_DISK_SYMBOL:
                return sym["st_value"], sym["st_size"]
    return None


def monitor(path, command, timeout=60):
    """Runs one command on the QEMU monitor (HMP) at the socket path and
    returns its output."""

    def until_prompt(sock):
        data = b""
        while not data.endswith(b"(qemu) "):
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", "replace")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout)
        sock.connect(path)
        until_prompt(sock)  # the greeting
        sock.sendall(command.encode() + b"\n")
        return until_prompt(sock)


def save_disk(args, img):
    """Copies the RAM disk out of the running QEMU into img."""
    elf = os.path.join(args.build_dir, "zephyr", "zephyr.elf")
    found = ram_disk(elf)
    if found is None:
        fail(f"no {RAM_DISK_SYMBOL} in {elf}")
        return
    addr, size = found
    log(f"RAM disk: {size} bytes at {addr:#x} -> {img}")
    try:
        out = monitor(
            os.path.join(args.build_dir, MONITOR),
            f"pmemsave {addr:#x} {size} \"{os.path.abspath(img)}\"",
        )
    except OSError as e:
        fail(f"QEMU monitor: {e}")
        return
    # QEMU writes the file before it prints the next prompt
    if not os.path.exists(img) or os.path.getsize(img) != size:
        fail(f"pmemsave did not save the RAM disk: {out.strip()}")


def boot(args, work, on_pass):
    """Runs the firmware until it prints its verdict (or crashes, or the
    timeout expires), calling on_pass() while QEMU still runs if the
    verdict is PASS. Returns the verdict line or None."""
    cmd = ["west", "build", "--build-dir", args.build_dir, "--target", "run"]
    log(f"boot: {' '.join(cmd)} (timeout {args.timeout}s)")
    # A session of its own, so that the whole process group (west, CMake,
    # ninja, QEMU) can be stopped at once.
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    verdict = []
    done = threading.Event()

    def reader():
        with open(os.path.join(work, "console.log"), "wb") as console:
            for raw in proc.stdout:
                console.write(raw)
                console.flush()
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                print(f"    {line}", flush=True)
                m = RESULT_RE.search(line)
                if m and not verdict:
                    verdict.append(m.group(0))
                    done.set()
                elif CRASH_RE.search(line) and not verdict:
                    verdict.append(f"LWEXT4-TEST: FAIL: firmware crash: {line}")
                    # let the fatal error report finish
                    threading.Timer(2, done.set).start()
        done.set()

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    finished = done.wait(args.timeout)
    status = proc.poll()
    if finished and status is None and verdict == ["LWEXT4-TEST: PASS"]:
        on_pass()
    if status is None:
        # SIGTERM: QEMU flushes its drives and exits
        os.killpg(proc.pid, signal.SIGTERM)
        try:
            proc.wait(30)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    t.join(10)
    if not finished:
        fail(f"no verdict within {args.timeout}s")
    elif not verdict:
        fail(f"QEMU ended (status {status}) before the firmware's verdict")
    return verdict[0] if verdict else None


def debugfs(img, request):
    return run(["debugfs", "-R", request, img])


def check_disk(img, work):
    log("disk image")
    r = run(["e2fsck", "-fn", img])
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        fail(f"e2fsck -fn finds errors (status {r.returncode})")
    else:
        print("    e2fsck -fn: clean")

    header = run(["dumpe2fs", "-h", img]).stdout
    m = re.search(r"^Filesystem volume name:\s*(.*)$", header, re.M)
    label = m.group(1).strip() if m else None
    if label != LABEL:
        fail(f"label is {label!r}, expected {LABEL!r}")
    else:
        print(f"    label: {label}")
    m = re.search(r"^Filesystem features:\s*(.*)$", header, re.M)
    features = m.group(1).split() if m else []
    if "has_journal" not in features:
        fail(f"no journal (features: {' '.join(features)})")
    else:
        print("    journal: yes")

    for d, expected in DIRS.items():
        # ls -p prints /inode/mode/uid/gid/name/size/ per entry
        out = debugfs(img, f"ls -p {d}").stdout
        names = sorted(
            f[5]
            for f in (line.split("/") for line in out.splitlines())
            if len(f) > 6 and f[5] not in (".", "..")
        )
        if names != sorted(expected):
            fail(f"debugfs: {d} contains {names}, expected {sorted(expected)}")
        else:
            print(f"    debugfs: {d} contains {' '.join(names)}")

    for path, content in FILES.items():
        out = os.path.join(work, "dump")
        if os.path.exists(out):
            os.remove(out)
        debugfs(img, f"dump {path} {out}")
        data = open(out, "rb").read() if os.path.exists(out) else None
        if data != content:
            fail(f"debugfs: {path} is missing or has the wrong content")
        else:
            print(f"    debugfs: {path} ok ({len(data)} bytes)")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--build-dir", required=True)
    p.add_argument("--timeout", type=int, default=900)
    args = p.parse_args()

    work = os.path.join(args.build_dir, "qemu-test")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    sock = os.path.join(args.build_dir, MONITOR)
    if os.path.exists(sock):
        os.remove(sock)
    img = os.path.join(work, "disk.img")

    verdict = boot(args, work, lambda: save_disk(args, img))
    if verdict is not None:
        log(f"verdict: {verdict}")
        if verdict != "LWEXT4-TEST: PASS":
            fail(f"the firmware reports {verdict}")
    if not failures:
        check_disk(img, work)

    if failures:
        print(f"run_qemu_test: {len(failures)} failed")
        return 1
    print("run_qemu_test: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
