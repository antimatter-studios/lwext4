# SPDX-License-Identifier: BSD-3-Clause
# A directory of 8000 entries with a two level htree (e2fsck -D indexes
# it): a lookup reads the root, an index node and a leaf.
dir="$1.d"
mkdir -p "$dir/many"
(cd "$dir/many" && seq -f 'file_with_a_long_name_%05g' 1 8000 | xargs touch)
lwext4_mke2fs -t ext4 -b 1024 -N 9000 -d "$dir" "$1" 32M
e2fsck -fyD "$1" >/dev/null 2>&1 || true
debugfs -R "htree_dump /many" "$1" 2>/dev/null |
	grep -q "Indirect levels: 1" || { echo "no two level htree" >&2; exit 1; }
cp "$1" "$1.pristine"
