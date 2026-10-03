# SPDX-License-Identifier: BSD-3-Clause
# A file with two xattrs, in the inode body (ext4, 256 byte inodes) and in
# an xattr block (ext2, 128 byte inodes). The test changes the name index
# of one of them on disk.
for img in "$1" "$1.ext2"; do
	if [ "$img" = "$1" ]; then
		lwext4_mke2fs -t ext4 -b 1024 -I 256 -O ^metadata_csum "$img" 8M
	else
		lwext4_mke2fs -t ext2 -b 1024 -I 128 "$img" 8M
	fi
	echo data >"$img.f"
	debugfs -w -R "write $img.f f" "$img" >/dev/null 2>&1
	debugfs -w -R "ea_set /f user.ok 1" "$img" >/dev/null 2>&1
	debugfs -w -R "ea_set /f user.zq9 2" "$img" >/dev/null 2>&1
	rm -f "$img.f"
done
