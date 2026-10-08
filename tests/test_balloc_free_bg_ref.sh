# SPDX-License-Identifier: BSD-3-Clause
# ext4, 1 KiB blocks, 1024 blocks per group, flex_bg: a deleted file left on
# the orphan list, with one extent of 2046 blocks from group 1 into group 2
# (blocks 1027-3072, after group 1's backup superblock and descriptors).
# The extent is written into the inode by hand, so where mke2fs would have
# put the data does not matter. All the group descriptors are in block 2.
lwext4_mke2fs -t ext4 -b 1024 -g 1024 -O ^has_journal,^resize_inode "$1" 8M
: >"$1.empty"
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
write $1.empty big
sif /big block[0] 0x0001f30a
sif /big block[1] 0x00000004
sif /big block[2] 0
sif /big block[3] 0
sif /big block[4] 2046
sif /big block[5] 1027
sif /big size 2095104
setb 1027 2046
CMDS
rm -f "$1.empty"
# Block counts of the inode and the groups (exit status 1: fixed)
e2fsck -fy "$1" >/dev/null 2>&1 || [ $? -eq 1 ]
debugfs -R 'stat /big' "$1" 2>/dev/null | grep -q '(0-2045):1027-3072' ||
	{ echo "big: no extent across groups 1 and 2" >&2; exit 1; }
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
unlink /big
sif <12> links_count 0
sif <12> dtime 0
ssv last_orphan 12
CMDS
debugfs -R 'stats' "$1" 2>/dev/null | grep -q 'First orphan inode: *12' ||
	{ echo "big: not on the orphan list" >&2; exit 1; }
cp "$1" "$1.fault"
