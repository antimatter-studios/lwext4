#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Write the blocks of a linear ext4 directory with many entries.

Usage: mkdir_blocks.py <out> <count> <target inode> <own inode>

Entry k is named 'n' * 240 + '_%07d' % k and points to <target inode> (hard
links of one regular file); three entries per 1 KiB block, after a first
block with "." and "..". Copied into an image as a regular file and turned
into a directory with debugfs, then indexed by e2fsck -D, this builds big
htrees in a second, where adding the entries one by one (mke2fs -d, debugfs
ln) takes minutes.
"""
import struct
import sys

BS = 1024


def entry(ino, name, rec, ftype):
    n = name.encode()
    return struct.pack('<IHBB', ino, rec, len(n), ftype) + n + \
        b'\0' * (rec - 8 - len(n))


def main():
    out, count = sys.argv[1], int(sys.argv[2])
    target, own = int(sys.argv[3]), int(sys.argv[4])
    with open(out, 'wb') as f:
        f.write(entry(own, '.', 12, 2) + entry(2, '..', BS - 12, 2))
        for i in range(0, count, 3):
            block = b''
            ks = range(i, min(i + 3, count))
            for j, k in enumerate(ks):
                rec = BS - len(block) if j == len(ks) - 1 else 264
                block += entry(target, 'n' * 240 + '_%07d' % k, rec, 1)
            f.write(block)


if __name__ == '__main__':
    main()
