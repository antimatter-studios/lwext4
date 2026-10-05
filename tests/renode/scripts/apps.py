#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Card images for the example applications (examples/firmware) on the
Renode boards, and the host side checks of what they leave (apps.robot).

  apps.py create <card.img> <MiB> blank|reader
        A zeroed card, or for reader a card made by mke2fs -d (no MBR: the
        applications use the whole card) with /config.txt and /assets.

  apps.py check <card.img> <what> <console log>...
        hello            /hello.txt reads back, e2fsck clean
        reader           the console shows the config lines and the sizes
                         and checksums of /assets that the host computes
        datalogger       the first run logged records 1..120
        datalogger-cut   <cut log> <run log>: every record the cut run
                         reported appended was found by the next run, at
                         most one more; e2fsck clean. The last record is
                         kept in <card.img>.last for the next cut
        datalogger-files the log files, read by debugfs: whole records,
                         numbered without a gap, the last the firmware's

The console logs are the UART file backends of Renode (CRLF lines).
"""

import os
import re
import subprocess
import sys
import tempfile

CONFIG = b"name=lwext4 reader\nversion=3\n# a comment\nbaud=115200\n"
CONFIG_LINES = ["config: name=lwext4 reader", "config: version=3",
                "config: baud=115200"]
ASSETS = {
    "logo.bin": bytes((i * 31 + i // 7) & 255 for i in range(5000)),
    "note.txt": b"the quick brown fox\n",
}


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def lines(path):
    with open(path, "rb") as f:
        text = f.read().decode("ascii", "replace")
    return [l.rstrip("\r") for l in text.split("\n")]


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True,
                          env=dict(os.environ, PATH=os.environ["PATH"] +
                                   ":/sbin:/usr/sbin"))


def fsck(img, when):
    r = run("e2fsck", "-fn", img)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        fail("e2fsck " + when)


def debugfs(img, request):
    return run("debugfs", "-R", request, img).stdout


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def create(img, mib, kind):
    with open(img, "wb") as f:
        f.truncate(mib << 20)
    if kind == "blank":
        return
    with tempfile.TemporaryDirectory() as tree:
        os.mkdir(os.path.join(tree, "assets"))
        with open(os.path.join(tree, "config.txt"), "wb") as f:
            f.write(CONFIG)
        for name, data in ASSETS.items():
            with open(os.path.join(tree, "assets", name), "wb") as f:
                f.write(data)
        r = run("mke2fs", "-q", "-F", "-t", "ext4", "-b", "1024", "-d",
                tree, img)
        if r.returncode != 0:
            fail("mke2fs: " + r.stderr)


def found(log):
    """The last record of 'datalogger: found records a..b ...' in log."""
    for l in lines(log):
        m = re.match(r"datalogger: found records \d+\.\.(\d+) ", l)
        if m:
            return int(m.group(1))
    return None


def logged(log):
    """The last record of the final 'datalogger: records a..b' line."""
    last = None
    for l in lines(log):
        m = re.match(r"datalogger: records \d+\.\.(\d+) ", l)
        if m:
            last = int(m.group(1))
    return last


def check(img, what, logs):
    out = lines(logs[0]) if logs else []
    if what == "hello":
        if "hello: wrote and read back /hello.txt" not in out:
            fail("hello did not finish")
        fsck(img, "after hello")
        text = debugfs(img, "cat /hello.txt")
        if text != "Hello from lwext4\n":
            fail("/hello.txt is %r" % text)
    elif what == "reader":
        config = [l for l in out if l.startswith("config: ")]
        if config != CONFIG_LINES:
            fail("config lines: %r" % config)
        assets = sorted(l for l in out if l.startswith("asset: "))
        want = sorted("asset: %s %d %08x" % (n, len(d), fnv(d))
                      for n, d in ASSETS.items())
        if assets != want:
            fail("assets: %r, want %r" % (assets, want))
        fsck(img, "after reader")
    elif what == "datalogger":
        if logged(logs[0]) != 120:
            fail("the first run did not log records 1..120")
        fsck(img, "after the first run")
        with open(img + ".last", "w") as f:
            f.write("120\n")
    elif what == "datalogger-cut":
        cut, after = logs
        if not any(l.startswith("POWER CUT") for l in lines(cut)):
            fail("no power cut")
        with open(img + ".last") as f:
            last = int(f.read())
        acked = [int(l.split()[1]) for l in lines(cut)
                 if re.match(r"appended \d+$", l)]
        acked = acked[-1] if acked else last
        got = found(after)
        if got is None or not acked <= got <= acked + 1:
            fail("%d records appended before the cut, %s found after it" %
                 (acked, got))
        fsck(img, "after the power cut")
        print("cut: %d appended before the cut, %d found after it" %
              (acked, got))
        with open(img + ".last", "w") as f:
            f.write("%d\n" % logged(after))
    elif what == "datalogger-files":
        with open(img + ".last") as f:
            last = int(f.read())
        names = sorted(re.findall(r"\b(\d{4}\.csv)\b",
                                  debugfs(img, "ls /log")))
        records = "".join(debugfs(img, "cat /log/" + n) for n in names)
        prev = None
        for l in records.split("\n")[:-1]:
            m = re.match(r"^(\d{6}),(\d{4})$", l)
            if not m or int(m.group(2)) != int(m.group(1)) * 7 % 10000:
                fail("bad record %r" % l)
            seq = int(m.group(1))
            if prev is not None and seq != prev + 1:
                fail("gap after %d" % prev)
            prev = seq
        if prev != last:
            fail("last record %s, the firmware said %d" % (prev, last))
        print("datalogger: %d records in %d files, the last %d" %
              (len(records.split("\n")) - 1, len(names), last))
    else:
        fail("unknown check " + what)
    print("PASS: " + what)


def main(argv):
    if len(argv) >= 4 and argv[0] == "create":
        create(argv[1], int(argv[2]), argv[3])
    elif len(argv) >= 3 and argv[0] == "check":
        check(argv[1], argv[2], argv[3:])
    else:
        sys.stderr.write(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
