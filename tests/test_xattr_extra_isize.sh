# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 256 byte inodes, no metadata checksums (the inode is changed by
# hand); a file whose i_extra_isize is 60000.
dir="$1.d"
mkdir -p "$dir"
echo hello > "$dir/a.txt"
lwext4_mke2fs -t ext4 -O ^metadata_csum -I 256 -b 4096 -d "$dir" "$1" 8M
debugfs -w -R "set_inode_field /a.txt extra_isize 60000" "$1" 2>/dev/null
debugfs -R "stat /a.txt" "$1" 2>/dev/null | grep -q "Size of extra inode fields: 60000" ||
	{ echo "extra_isize not set" >&2; exit 1; }
