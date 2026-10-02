# SPDX-License-Identifier: BSD-3-Clause
# Fast (inline) and slow (one data block) symlinks made by mke2fs, 1 KiB
# blocks.
dir="$1.d"
mkdir -p "$dir"
ln -s "short/target.txt" "$dir/fast"
ln -s "$(printf 'd%.0s' $(seq 1 1000))/end" "$dir/slow"
lwext4_mke2fs -t ext4 -b 1024 -d "$dir" "$1" 8M
