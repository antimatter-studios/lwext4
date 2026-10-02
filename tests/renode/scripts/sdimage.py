#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""SD card images for the Renode lwext4 tests, and the host side oracle.

  sdimage.py create  <card.img> <MiB> [--mbr-only]
        Card image with an MBR and one Linux partition starting at 1 MiB
        that holds an ext4 file system made by mke2fs -d (see HOST_FILES),
        directories indexed by e2fsck -D. With --mbr-only the card is left
        blank (zeroed), for the on-device mkfs tests.

  sdimage.py check <card.img> <mode>
        Extracts the first partition and checks it with e2fsck -fn and
        debugfs. <mode> is one of
          hostimg   files from `create` plus what the firmware changed/added
          mkfs      what the firmware wrote after its own mkfs
          recover   state after the power cut test
          replay    host journal replay (e2fsck -fy) of a power cut image
                    must yield a clean file system

The data pattern and the workload constants mirror
tests/renode/common/workload.[ch].
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 512
PART_START = 2048  # sectors, 1 MiB

HOST_PATTERN_SEED = 4242
HOST_PATTERN_SIZE = 100000
HOST_MANY_FILES = 300

WL_SUBDIRS = 12
WL_SMALL_FILES = 48
WL_BIG_SEED = 1000
MARKER_SEED = 777
MARKER_SIZE = 4096

SMALL_NAME = "fw/small/file_%03u_with_a_long_name_to_fill_dir_blocks.bin"


def pattern(seed, off, length):
    out = bytearray(length)
    m = 0xFFFFFFFF
    base = (seed * 0x9E3779B9) & m
    for i in range(length):
        x = (off + i + base) & m
        x ^= x >> 15
        x = (x * 0x2C1B3C6D) & m
        x ^= x >> 12
        out[i] = x & 0xFF
    return bytes(out)


def small_file_size(i):
    return 1 + (i * 997) % 5000


def big_file_size(big_kib):
    return big_kib * 1024 + 123


def run(*cmd, check=True, quiet=False):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode(errors="replace")
    if not quiet or (check and p.returncode):
        print("$ " + " ".join(cmd))
        print(out, end="")
    if check and p.returncode:
        raise SystemExit("command failed with %d: %s" % (p.returncode, cmd[0]))
    return p.returncode, out


def write_mbr(img, part_start, part_sectors):
    mbr = bytearray(SECTOR)
    struct.pack_into("<I", mbr, 440, 0x4C574558)
    # status, CHS first (unused), type, CHS last (unused), LBA, sectors
    entry = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 0x83, b"\xfe\xff\xff",
                        part_start, part_sectors)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xaa"
    with open(img, "r+b") as f:
        f.write(mbr)


def read_partition(img):
    with open(img, "rb") as f:
        mbr = f.read(SECTOR)
    if mbr[510:512] != b"\x55\xaa":
        raise SystemExit("no MBR signature")
    _, _, ptype, _, start, count = struct.unpack_from("<B3sB3sII", mbr, 446)
    if ptype != 0x83:
        raise SystemExit("partition 1 type 0x%02x, expected 0x83" % ptype)
    return start, count


def create(img, mib, mbr_only):
    with open(img, "wb") as f:
        f.truncate(mib * 1024 * 1024)
    if mbr_only:
        return
    sectors = mib * 2048 - PART_START
    write_mbr(img, PART_START, sectors)
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "root")
        host = os.path.join(root, "host")
        os.makedirs(os.path.join(host, "deep/a/b/c/d/e"))
        os.makedirs(os.path.join(host, "many"))
        with open(os.path.join(host, "hello.txt"), "w") as f:
            f.write("hello from mke2fs -d\n")
        with open(os.path.join(host, "pattern.bin"), "wb") as f:
            f.write(pattern(HOST_PATTERN_SEED, 0, HOST_PATTERN_SIZE))
        with open(os.path.join(host, "deep/a/b/c/d/e/leaf.txt"), "w") as f:
            f.write("leaf\n")
        for i in range(HOST_MANY_FILES):
            with open(os.path.join(host, "many/f_%04u" % i), "w") as f:
                f.write("file %u\n" % i)
        part = os.path.join(tmp, "part.img")
        with open(part, "wb") as f:
            f.truncate(sectors * SECTOR)
        # mke2fs defaults for the size: 1 KiB blocks below 512 MiB ("small"
        # type), metadata_csum, 64bit, extents, flex_bg, journal - minus the
        # two features e2fsprogs >= 1.47 enables by default that lwext4 does
        # not implement (mounting fails with ENOTSUP otherwise).
        run("mke2fs", "-q", "-F", "-t", "ext4", "-L", "hostimg",
            "-O", "^orphan_file,^metadata_csum_seed",
            "-E", "root_owner=0:0", "-d", root, part, quiet=True)
        # -D builds htree indexes for the directories created by -d
        rc, _ = run("e2fsck", "-fyD", part, check=False, quiet=True)
        if rc not in (0, 1):
            raise SystemExit("e2fsck -D failed: %d" % rc)
        run("e2fsck", "-fn", part, quiet=True)
        with open(part, "rb") as src, open(img, "r+b") as dst:
            dst.seek(PART_START * SECTOR)
            shutil.copyfileobj(src, dst, 1 << 20)
        run("dumpe2fs", "-h", part, quiet=True)


class Fs:
    def __init__(self, part):
        self.part = part

    def debugfs(self, req):
        p = subprocess.run(["debugfs", "-R", req, self.part],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.stdout, p.stderr.decode(errors="replace")

    def features(self):
        p = subprocess.run(["dumpe2fs", "-h", self.part], stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL)
        for line in p.stdout.decode(errors="replace").splitlines():
            if line.startswith("Filesystem features:"):
                return line.split(":", 1)[1].split()
        return []

    def cat(self, path):
        out, err = self.debugfs("cat " + path)
        if "not found" in err or "File not found" in err:
            raise SystemExit("missing file %s" % path)
        return out

    def exists(self, path):
        _, err = self.debugfs("stat " + path)
        return "not found" not in err

    def ls(self, path):
        out, err = self.debugfs("ls -p " + path)
        if "not found" in err:
            raise SystemExit("missing directory %s" % path)
        names = []
        for line in out.decode(errors="replace").splitlines():
            # /inode/mode/uid/gid/name/size/ ; inode 0 marks an unused
            # slot (e.g. a removed first entry of a directory block)
            f = line.strip("/").split("/")
            if len(f) >= 5 and f[0] != "0":
                names.append(f[4])
        return names

    def expect_file(self, path, data):
        got = self.cat(path)
        if got != data:
            for i, (a, b) in enumerate(zip(got, data)):
                if a != b:
                    break
            else:
                i = min(len(got), len(data))
            raise SystemExit("%s: content differs (size %d, expected %d, "
                             "first difference at %d)"
                             % (path, len(got), len(data), i))


failures = []


def expect(cond, what):
    if not cond:
        failures.append(what)
        print("CHECK FAILED: " + what)


def check_workload(fs, big_kib):
    names = set(fs.ls("/fw"))
    want = {".", "..", "small", "big.bin", "trunc.bin", "renamed.bin", "link"}
    want |= {"sub%02u" % i for i in range(WL_SUBDIRS) if i != 5}
    expect(names == want, "/fw entries %s" % sorted(names ^ want))
    small = set(fs.ls("/fw/small"))
    want = {".", ".."}
    for i in range(WL_SMALL_FILES):
        if i % 3 and i != 1:
            want.add(os.path.basename(SMALL_NAME % i))
            fs.expect_file("/" + SMALL_NAME % i,
                           pattern(i + 1, 0, small_file_size(i)))
    expect(small == want, "/fw/small entries %s" % sorted(small ^ want))
    fs.expect_file("/fw/renamed.bin", pattern(2, 0, small_file_size(1)))
    fs.expect_file("/fw/big.bin", pattern(WL_BIG_SEED, 0, big_file_size(big_kib)))
    fs.expect_file("/fw/trunc.bin", pattern(7, 0, 5000) + pattern(8, 5000, 3000))
    out, _ = fs.debugfs("stat /fw/link")
    expect(b"Fast link dest: \"big.bin\"" in out or b"big.bin" in out,
           "symlink target")
    if "ext_attr" in fs.features():
        out, err = fs.debugfs("ea_get /fw/big.bin user.lwext4")
        expect(b"renode" in out, "xattr user.lwext4: %r %s" % (out, err))
    else:
        # Without the ext_attr feature e2fsprogs ignores the attribute
        # lwext4 stored. ext4_mkfs and ext4_setxattr set it since fork
        # issue #98, so only older images get here.
        print("note: no ext_attr feature, xattr not checked on the host")


def check_host_files(fs):
    fs.expect_file("/host/hello.txt",
                   b"hello from mke2fs -d\nlwext4 was here\n")
    data = bytearray(pattern(HOST_PATTERN_SEED, 0, HOST_PATTERN_SIZE))
    data[50000:51000] = pattern(99, 50000, 1000)
    fs.expect_file("/host/pattern.bin", bytes(data))
    fs.expect_file("/host/deep/a/b/c/d/e/leaf.txt", b"leaf\n")
    many = set(fs.ls("/host/many"))
    want = {".", ".."} | {"f_%04u" % i for i in range(HOST_MANY_FILES) if i != 100}
    expect(many == want, "/host/many entries %s" % sorted(many ^ want)[:10])
    for i in range(0, HOST_MANY_FILES, 50):
        if i != 100:
            fs.expect_file("/host/many/f_%04u" % i, b"file %u\n" % i)


def check_recover(fs):
    fs.expect_file("/tort/marker", pattern(MARKER_SEED, 0, MARKER_SIZE))
    fs.expect_file("/tort/after_recovery", pattern(4711, 0, 12345))


def check(img, mode, big_kib):
    start, count = read_partition(img)
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, "part.img")
        with open(img, "rb") as src, open(part, "wb") as dst:
            src.seek(start * SECTOR)
            left = count * SECTOR
            while left:
                buf = src.read(min(left, 1 << 20))
                if not buf:
                    break
                dst.write(buf)
                left -= len(buf)
        if mode == "replay":
            # A power cut image: e2fsprogs must be able to replay the
            # journal lwext4 wrote and end up with a clean file system.
            rc, out = run("e2fsck", "-fy", part, check=False)
            expect(rc in (0, 1), "e2fsck -fy (journal replay) exit %d" % rc)
            rc, out = run("e2fsck", "-fn", part, check=False)
            expect(rc == 0, "e2fsck -fn after replay exit %d" % rc)
        else:
            rc, out = run("e2fsck", "-fn", part, check=False)
            expect(rc == 0, "e2fsck -fn exit %d" % rc)
            fs = Fs(part)
            if mode == "hostimg":
                check_host_files(fs)
                check_workload(fs, big_kib)
            elif mode == "mkfs":
                check_workload(fs, big_kib)
                run("dumpe2fs", "-h", part)
            elif mode == "recover":
                check_recover(fs)
            else:
                raise SystemExit("unknown mode " + mode)
    if failures:
        raise SystemExit("%d check(s) failed" % len(failures))
    print("host check (%s): OK" % mode)


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == "create":
        create(a[1], int(a[2]), "--mbr-only" in a)
    elif len(a) >= 3 and a[0] == "check":
        big_kib = int(os.environ.get("BIG_FILE_KIB", "256"))
        check(a[1], a[2], big_kib)
    else:
        print(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
