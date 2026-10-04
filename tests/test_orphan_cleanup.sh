# SPDX-License-Identifier: BSD-3-Clause
# Orphan lists as Linux leaves them when it stops (fork issue #128), made
# with debugfs:
#   $1      a 1 MiB file deleted while open (no links), followed by a 64 KiB
#           file whose truncate to 1 KiB was interrupted (i_size 1024, all
#           blocks still allocated)
#   $1.loop a deleted file whose list entry points to itself
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
head -c 1048576 /dev/zero | tr '\000' 'a' >"$1.data"
debugfs -w -R "write $1.data a" "$1" >/dev/null 2>&1
head -c 65536 "$1.data" >"$1.data64"
debugfs -w -R "write $1.data64 b" "$1" >/dev/null 2>&1
a=$(debugfs -R "stat /a" "$1" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')
b=$(debugfs -R "stat /b" "$1" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')
[ -n "$a" ] && [ -n "$b" ]
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
unlink /a
sif <$a> links_count 0
sif <$a> dtime $b
sif /b size 1024
sif /b dtime 0
ssv last_orphan $a
CMDS

lwext4_mke2fs -t ext4 -b 1024 "$1.loop" 8M
debugfs -w -R "write $1.data64 c" "$1.loop" >/dev/null 2>&1
c=$(debugfs -R "stat /c" "$1.loop" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')
[ -n "$c" ]
debugfs -w -f - "$1.loop" >/dev/null 2>&1 <<CMDS
unlink /c
sif <$c> links_count 0
sif <$c> dtime $c
ssv last_orphan $c
CMDS
rm -f "$1.data" "$1.data64"
