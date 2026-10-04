# SPDX-License-Identifier: BSD-3-Clause
# Journalled ext4 with 1 KiB blocks (fork issue #157): $1's journal
# superblock says its block size is 4096, $1.first's that the log starts
# at block 0. The journal superblock is block 0 of the journal i-node
# (big endian fields: blocksize at 12, maxlen at 16, first at 20).
for img in "$1" "$1.first"; do
	lwext4_mke2fs -t ext4 -b 1024 "$img" 8M
	blk=$(debugfs -R "bmap <8> 0" "$img" 2>/dev/null)
	[ -n "$blk" ] && [ "$blk" -gt 0 ]
	if [ "$img" = "$1" ]; then
		printf '\000\000\020\000' |
			dd of="$img" bs=1 seek=$((blk * 1024 + 12)) count=4 conv=notrunc 2>/dev/null
	else
		printf '\000\000\000\000' |
			dd of="$img" bs=1 seek=$((blk * 1024 + 20)) count=4 conv=notrunc 2>/dev/null
	fi
done
