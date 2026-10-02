# SPDX-License-Identifier: BSD-3-Clause
# ext4 and ext2 (block maps; without large_file, so that a file of 2 GiB
# or more must turn it on), each with a 3000 byte file of 'x'.
dir="$1.d"
mkdir -p "$dir"
head -c 3000 /dev/zero | tr '\000' 'x' > "$dir/f"
lwext4_mke2fs -t ext4 -b 1024 -d "$dir" "$1" 16M
lwext4_mke2fs -t ext2 -O ^large_file -b 1024 -d "$dir" "$1.ext2" 16M
dumpe2fs -h "$1.ext2" 2>/dev/null | grep -q '^Filesystem features:.*large_file' &&
	{ echo "large_file still set" >&2; exit 1; }
true
