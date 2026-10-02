# SPDX-License-Identifier: BSD-3-Clause
# A linear directory of 300 entries over many 1 KiB blocks, with a
# subdirectory at its end.
dir="$1.d"
mkdir -p "$dir/many"
(cd "$dir/many" && seq -f 'file_with_a_long_name_%05g' 1 300 | xargs touch)
mkdir "$dir/many/zz_existing"
lwext4_mke2fs -t ext4 -b 1024 -O ^dir_index -d "$dir" "$1" 8M
cp "$1" "$1.pristine"
