# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a directory of 100 files ($1), and one of 300
# files ($1.big): ext4_dir_rm() of either takes many transactions, over
# more blocks than the cache holds.
for size in 100 300; do
	img="$1"
	[ $size = 300 ] && img="$1.big"
	dir="$img.d"
	mkdir -p "$dir/many"
	for i in $(seq 1 $size); do
		echo $i > "$dir/many/file_with_long_name_$i"
	done
	mb=8; [ $size = 300 ] && mb=16
	lwext4_mke2fs -t ext4 -b 1024 -d "$dir" "$img" ${mb}M
	cp "$img" "$img.pristine"
done
