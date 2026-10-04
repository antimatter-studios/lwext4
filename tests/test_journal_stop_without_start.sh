# SPDX-License-Identifier: BSD-3-Clause
# ext4 with a journal whose superblock magic is overwritten, so that
# ext4_journal_start() cannot load it.
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
blk=$(debugfs -R "bmap <8> 0" "$1" 2>/dev/null)
[ -n "$blk" ] && [ "$blk" -gt 0 ]
printf '\000\000\000\000' |
	dd of="$1" bs=1 seek=$((blk * 1024)) count=4 conv=notrunc 2>/dev/null
