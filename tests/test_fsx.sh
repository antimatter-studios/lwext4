# SPDX-License-Identifier: BSD-3-Clause
# The geometries test_fsx exercises, made by mke2fs: ext4 with 1 KiB and
# 4 KiB blocks (extents, journal), ext4 without a journal, ext2 (block
# maps, indirect blocks).
lwext4_mke2fs -t ext4 -b 1024 "$1" 32M
lwext4_mke2fs -t ext4 -b 4096 "$1.4k" 32M
lwext4_mke2fs -t ext4 -b 1024 -O ^has_journal "$1.nojournal" 32M
lwext4_mke2fs -t ext2 -b 1024 "$1.ext2" 32M
