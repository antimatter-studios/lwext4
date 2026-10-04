# SPDX-License-Identifier: BSD-3-Clause
# Two directories with the same entries (fork issue #147): /normal, and
# /huge, whose size claims 4 GiB - 1 KiB although only its first block is
# mapped (damage: the rest is one hole of four million blocks). In $1 the
# directories use extents (ext4), in $1.ext2 block maps.
for img in "$1" "$1.ext2"; do
	if [ "$img" = "$1" ]; then
		lwext4_mke2fs -t ext4 -b 1024 "$img" 8M
	else
		lwext4_mke2fs -t ext2 -b 1024 "$img" 8M
	fi
	debugfs -w -f - "$img" >/dev/null 2>&1 <<'CMDS'
mkdir /normal
mkdir /huge
sif /huge size 4294966272
CMDS
	debugfs -R 'stat /huge' "$img" 2>/dev/null | grep -q 'Size: 4294966272'
done
