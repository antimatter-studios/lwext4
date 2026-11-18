# SPDX-License-Identifier: BSD-3-Clause
# Journals holding one committed transaction that logs filesystem block 0
# (fork issue #230). The journal superblock is block 0 of the journal i-node
# (big endian fields: sequence at 24, start at 28, feature words at 36..47);
# the features are cleared so that no block needs a checksum and a tag is
# 8 bytes. Log block 1 is the descriptor, 2 the logged block, 3 the commit.
#
#   $1      1 KiB blocks: block 0 is the boot block, the logged block is a
#           marker that must not reach the disk
#   $1.4k   4 KiB blocks: block 0 holds the superblock, the logged block is
#           block 0 with the volume label changed
put()
{
	dd of="$img" bs=1 seek="$1" conv=notrunc 2>/dev/null
}

jblock()
{
	n=$(debugfs -R "bmap <8> $1" "$img" 2>/dev/null)
	[ -n "$n" ] && [ "$n" -gt 0 ]
	echo $((n * bs))
}

header()
{
	# magic, block type, sequence 1
	printf '\300\073\071\230\000\000\000'"$2"'\000\000\000\001' | put "$1"
}

journal()
{
	sb=$(jblock 0)
	desc=$(jblock 1)
	data=$(jblock 2)
	commit=$(jblock 3)

	dd if=/dev/zero bs=1 count=12 2>/dev/null | put $((sb + 36))
	printf '\000\000\000\001\000\000\000\001' | put $((sb + 24))
	header "$desc" '\001'
	# Tag: block 0, no checksum, flags SAME_UUID | LAST_TAG
	printf '\000\000\000\000\000\000\000\012' | put $((desc + 12))
	header "$commit" '\002'
}

img=$1
bs=1024
lwext4_mke2fs -t ext4 -b $bs "$img" 8M
journal
printf 'REPLAYED-INTO-BOOT-BLOCK' | put "$data"

img=$1.4k
bs=4096
lwext4_mke2fs -t ext4 -b $bs "$img" 16M
journal
dd if="$img" bs=$bs count=1 2>/dev/null | put "$data"
printf 'replayed' | put $((data + 1024 + 120))
