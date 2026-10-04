# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 2 KiB blocks (first_data_block 0: block 0 holds the
# superblock) whose group 0 block bitmap marks blocks 0 to 3 (superblock,
# group descriptors, reserved descriptors) free (fork issue #142). Only the
# bitmap is damaged; the free block counts stay right.
lwext4_mke2fs -t ext4 -b 2048 -O ^has_journal "$1" 2M
bmp=$(dumpe2fs "$1" 2>/dev/null | sed -n 's/^ *Block bitmap at \([0-9]*\).*/\1/p' | head -n 1)
[ -n "$bmp" ]
off=$((bmp * 2048))
byte=$(od -An -tu1 -j "$off" -N1 "$1" | tr -d ' ')
[ $((byte & 15)) -eq 15 ]
printf "\\$(printf %03o $((byte & 240)))" |
	dd of="$1" bs=1 seek="$off" count=1 conv=notrunc 2>/dev/null
