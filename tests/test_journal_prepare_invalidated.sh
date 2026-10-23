# SPDX-License-Identifier: BSD-3-Clause
# Two allocated data blocks, ext3 block maps and a journal, 1 KiB blocks.
dir="$1.d"
mkdir -p "$dir"
awk 'BEGIN { for (i = 0; i < 1024; i++) printf "a" }' >"$dir/a"
awk 'BEGIN { for (i = 0; i < 1024; i++) printf "b" }' >"$dir/b"
lwext4_mke2fs -t ext3 -b 1024 -d "$dir" "$1" 4M
