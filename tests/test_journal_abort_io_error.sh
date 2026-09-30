# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a directory of 300 files (ext4_dir_rm() of it
# takes many transactions).
dir="$1.d"
mkdir -p "$dir/many"
for i in $(seq 1 300); do echo $i > "$dir/many/file_with_long_name_$i"; done
lwext4_mke2fs -t ext4 -b 1024 -d "$dir" "$1" 16M
cp "$1" "$1.pristine"
