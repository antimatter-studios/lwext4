#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Deterministic file contents shared by the host scripts and the firmware.

Must match pattern() in main/lwext4_test.c:
    byte(off) = (off * 7 + (off >> 9) + seed * 31) & 0xff

Usage:
    pattern.py gen <seed> <size> <out>      write a pattern file
    pattern.py check <seed> <size> <file>   verify a pattern file
"""
import sys


def pattern(seed: int, size: int) -> bytes:
    return bytes(((off * 7 + (off >> 9) + seed * 31) & 0xFF) for off in range(size))


def main(argv):
    if len(argv) != 5 or argv[1] not in ("gen", "check"):
        sys.exit(__doc__)
    cmd, seed, size, path = argv[1], int(argv[2]), int(argv[3]), argv[4]
    want = pattern(seed, size)
    if cmd == "gen":
        with open(path, "wb") as f:
            f.write(want)
        return 0
    with open(path, "rb") as f:
        got = f.read()
    if len(got) != size:
        print(f"{path}: size {len(got)}, expected {size}")
        return 1
    if got != want:
        bad = next(i for i in range(size) if got[i] != want[i])
        print(f"{path}: content mismatch at offset {bad}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
