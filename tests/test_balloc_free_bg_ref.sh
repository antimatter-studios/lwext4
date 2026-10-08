# SPDX-License-Identifier: BSD-3-Clause
# ext4, 1 KiB blocks, 1024 blocks per group, flex_bg: a deleted 3 MiB file
# left on the orphan list, whose second extent runs from group 1 into
# group 2. All the group descriptors are in block 2.
dir="$1.d"
mkdir -p "$dir"
awk 'BEGIN { for (i = 0; i < 3145728; i++) printf "%c", 65 + i % 26 }' >"$dir/big"
lwext4_mke2fs -t ext4 -b 1024 -g 1024 -O ^has_journal,^resize_inode \
	-d "$dir" "$1" 8M
debugfs -R 'stat /big' "$1" 2>/dev/null | grep -q '(480-2525):1027-3072' ||
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
