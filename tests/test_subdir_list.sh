# ext4 image with nested directories and one large (multi-block) directory.
dir="$1.d"
mkdir -p "$dir/dir1/dir2" "$dir/dir1/empty" "$dir/big"
printf 'deep\n' > "$dir/dir1/dir2/deep.txt"
i=0
while [ $i -lt 1000 ]; do
	: > "$dir/big/$(printf 'file_%04d' $i)"
	i=$((i + 1))
done
lwext4_mke2fs -t ext4 -b 1024 -d "$dir" "$1" 8M
