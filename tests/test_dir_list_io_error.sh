# SPDX-License-Identifier: BSD-3-Clause
# A linear directory of 300 entries over many 1 KiB blocks, more than the
# block cache holds. Removing entries leaves deleted entries (inode 0):
# a run of names in the middle, and the first names of the directory's
# later blocks.
dir="$1.d"
mkdir -p "$dir/many"
(cd "$dir/many" && seq -f 'file_with_a_long_name_%05g' 1 300 | xargs touch)
lwext4_mke2fs -t ext4 -b 1024 -O ^dir_index -d "$dir" "$1" 8M
for i in $(seq 100 140); do
	echo "rm /many/$(printf 'file_with_a_long_name_%05d' $i)"
done | debugfs -w -f - "$1" >/dev/null 2>&1
