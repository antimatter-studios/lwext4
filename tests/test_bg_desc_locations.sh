# SPDX-License-Identifier: BSD-3-Clause
# ext2 with 1 KiB blocks and one block group, whose descriptor (block 2,
# after the superblock) gets block bitmap, inode bitmap and inode table 0.
dir="$1.d"
mkdir -p "$dir"
echo hello > "$dir/a.txt"
lwext4_mke2fs -t ext2 -b 1024 -d "$dir" "$1" 1M
dd if=/dev/zero of="$1" bs=1 seek=2048 count=12 conv=notrunc status=none
