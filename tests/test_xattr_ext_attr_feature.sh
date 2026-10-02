# SPDX-License-Identifier: BSD-3-Clause
# ext4 without the ext_attr feature, as lwext4's mkfs made it (fork issue
# #98); the test stores an attribute on /a.txt.
dir="$1.d"
mkdir -p "$dir"
echo hello > "$dir/a.txt"
lwext4_mke2fs -t ext4 -O ^ext_attr -I 256 -b 1024 -d "$dir" "$1" 8M
dumpe2fs -h "$1" 2>/dev/null | grep -q '^Filesystem features:.*ext_attr' &&
	{ echo "ext_attr still set" >&2; exit 1; }
true
