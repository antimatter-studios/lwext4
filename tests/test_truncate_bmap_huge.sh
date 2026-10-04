# SPDX-License-Identifier: BSD-3-Clause
# Block mapped files (ext2, 1 KiB blocks) of 300 KiB, mapped up to the
# double indirect level, with a damaged size near what a block map
# addresses (fork issue #162): /a is truncated, /b removed by the test.
# Each also has a triple indirect block that maps nothing (all zeros):
# every block of the triple indirect range then takes a block read.
head -c 307200 /dev/zero | tr '\000' 'y' >"$1.data"
lwext4_mke2fs -t ext2 -b 1024 "$1" 8M
for f in a b; do
	debugfs -w -R "write $1.data $f" "$1" >/dev/null 2>&1
done
rm -f "$1.data"
goal=4000
for f in a b; do
	blk=$(debugfs -R "ffb 1 $goal" "$1" 2>/dev/null | tr -cs '0-9' ' ' | awk '{print $NF}')
	[ -n "$blk" ]
	dd if=/dev/zero of="$1" bs=1024 seek="$blk" count=1 conv=notrunc 2>/dev/null
	cnt=$(debugfs -R "stat /$f" "$1" 2>/dev/null | sed -n 's/.*Blockcount: \([0-9]*\).*/\1/p')
	debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
setb $blk
sif /$f block[TIND] $blk
sif /$f blocks $((cnt + 2))
sif /$f size 17247252000
CMDS
	goal=$((blk + 1))
done
# setb leaves the free block counts as they were
e2fsck -fy "$1" >/dev/null 2>&1 || [ $? -le 1 ]
e2fsck -fn "$1" >/dev/null
debugfs -R 'stat /b' "$1" 2>/dev/null | grep -q '(TIND)'
