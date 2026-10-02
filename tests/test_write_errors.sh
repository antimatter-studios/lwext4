# SPDX-License-Identifier: BSD-3-Clause
# ext4 without a journal and ext2, with a large file, a small file, a
# directory tree and a 300 entry directory for the operations to work on.
dir="$1.d"
mkdir -p "$dir/d/sub" "$dir/many"
head -c 300000 /dev/zero | tr '\000' 'b' > "$dir/big"
head -c 5000 /dev/zero | tr '\000' 's' > "$dir/small"
for i in $(seq 1 300); do echo $i > "$dir/many/file_with_long_name_$i"; done
echo x > "$dir/d/f1"; echo y > "$dir/d/sub/f2"
lwext4_mke2fs -t ext4 -O ^has_journal -b 1024 -d "$dir" "$1" 16M
cp "$1" "$1.pristine"
lwext4_mke2fs -t ext2 -b 1024 -d "$dir" "$1.ext2" 16M
cp "$1.ext2" "$1.ext2.pristine"
