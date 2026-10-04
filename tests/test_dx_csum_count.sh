# SPDX-License-Identifier: BSD-3-Clause
# A metadata_csum htree directory /dir/sub (fork issue #155) whose root
# says it holds 0xFFFF entries, more than its limit (damage).
d="$1.tree"
mkdir -p "$d/dir/sub"
for i in $(seq 1 60); do echo $i >"$d/dir/sub/entry_with_a_long_name_$i"; done
lwext4_mke2fs -t ext4 -b 1024 -O metadata_csum -d "$d" "$1" 8M
rm -rf "$d"
# Index it (mke2fs -d writes linear directories)
e2fsck -fyD "$1" >/dev/null 2>&1 || [ $? -le 1 ]
debugfs -R 'htree /dir/sub' "$1" 2>/dev/null | grep -q 'Number of entries (count)'
blk=$(debugfs -R 'bmap /dir/sub 0' "$1" 2>/dev/null)
[ -n "$blk" ]
# count (u16) after limit (u16) at offset 32 of the root block
printf '\377\377' | dd of="$1" bs=1 seek=$((blk * 1024 + 34)) count=2 conv=notrunc 2>/dev/null
