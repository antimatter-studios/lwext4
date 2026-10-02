# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 256 byte inodes (in-inode attributes) made by mke2fs, so that
# e2fsprogs reads what lwext4 writes (fork issue #97).
lwext4_mke2fs -t ext4 -I 256 -b 1024 -N 256 "$1" 8M
