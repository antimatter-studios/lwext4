# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a journal without checksums, whose journal
# superblock is then made to claim a journal of 5 blocks (the superblock
# and 4 for transactions): too small for creating a directory. $1.mkfs is
# an empty file of 4 MiB for ext4_mkfs.
lwext4_mke2fs -t ext4 -b 1024 -O ^metadata_csum "$1" 8M
jsb=$(debugfs -R 'bmap <8> 0' "$1" 2>/dev/null)
[ -n "$jsb" ] && [ "$jsb" -gt 0 ]
# s_maxlen: big endian, at byte 16 of the journal superblock
printf '\000\000\000\005' |
	dd of="$1" bs=1 seek=$((jsb * 1024 + 16)) conv=notrunc 2>/dev/null
dd if=/dev/zero of="$1.mkfs" bs=1024 count=4096 2>/dev/null
