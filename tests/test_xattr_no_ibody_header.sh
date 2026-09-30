# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 256 byte inodes (room for attributes in the inode) and one
# file made by mke2fs, without attributes.
dir="$1.d"
mkdir -p "$dir"
printf 'hello lwext4\n' > "$dir/hello.txt"
lwext4_mke2fs -t ext4 -I 256 -b 4096 -d "$dir" "$1" 8M
