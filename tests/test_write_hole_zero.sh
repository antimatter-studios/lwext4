# SPDX-License-Identifier: BSD-3-Clause
# ext4 (extents) and ext2 (block maps) on a device full of 0xaa bytes
# (-E nodiscard keeps them in the free blocks): a 100000 byte sparse file
# and a 2048 byte file that ends on a block boundary.
dir="$1.d"
mkdir -p "$dir"
truncate -s 100000 "$dir/sparse"
head -c 2048 /dev/zero | tr '\000' 'f' > "$dir/full"
for img in "$1" "$1.ext2"; do
	head -c 16777216 /dev/zero | tr '\000' '\252' > "$img"
done
lwext4_mke2fs -E nodiscard -t ext4 -b 1024 -d "$dir" "$1" 16M
lwext4_mke2fs -E nodiscard -t ext2 -b 1024 -d "$dir" "$1.ext2" 16M
