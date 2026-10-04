# SPDX-License-Identifier: BSD-3-Clause
# Files with a few blocks and a damaged size of about 2^62 (fork issue
# #152), with extents:
#   /orphan  deleted while open: on the orphan list, released at mount
#   /big     a normal file, removed by the test
head -c 3000 /dev/zero | tr '\000' 'x' >"$1.data"
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
debugfs -w -R "write $1.data orphan" "$1" >/dev/null 2>&1
debugfs -w -R "write $1.data big" "$1" >/dev/null 2>&1
o=$(debugfs -R "stat /orphan" "$1" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')
[ -n "$o" ]
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
sif /big size 6872879406625325062
sif /orphan size 6872879406625325062
unlink /orphan
sif <$o> links_count 0
sif <$o> dtime 0
ssv last_orphan $o
CMDS
rm -f "$1.data"
debugfs -R "stat /big" "$1" 2>/dev/null | grep -q 'Size: 6872879406625325062'
