#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Seed corpus of the fuzz targets, made with e2fsprogs:
#
#   tests/fuzz/make-seeds.sh <out dir>
#
# Small images of each layout lwext4 handles (ext2 block maps, ext3 journal,
# ext4 extents, metadata_csum, htree directories, xattrs in the inode and in
# a block, fast and slow symlinks, 1 KiB and 2 KiB blocks), each also with
# operation scripts appended for fuzz_rw (see the layout there). The images
# are reproducible: fixed UUID, hash seed and time, so a run of the same
# e2fsprogs version makes the same bytes.
set -eu
export LC_ALL=C
out=$1
mkdir -p "$out"
work=$(mktemp -d "$out/.work.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

export E2FSPROGS_FAKE_TIME=1700000000 E2FSCK_TIME=1700000000
uuid=6c77e7c4-0000-4000-8000-000000000001
hseed=6c77e7c4-0000-4000-8000-000000000002

# The tree every image starts from.
d=$work/tree
mkdir -p "$d/dir/sub"
echo hello >"$d/a.txt"
awk 'BEGIN { srand(1); for (i = 0; i < 5000; i++) printf "%c", 65 + int(rand() * 26) }' \
	>"$d/dir/b.bin"
ln -s a.txt "$d/sl"
ln -s "$(printf 'x%.0s' $(seq 1 100))" "$d/dir/long"
for i in $(seq 1 40); do
	echo "$i" >"$d/dir/sub/f_with_a_longer_name_$i"
done
# Nothing of the host may reach the images: no inherited ACLs or other
# xattrs (the ones the images have are set below), fixed times and modes.
if command -v setfacl >/dev/null 2>&1; then
	setfacl -R -b "$d"
fi
chmod -R u=rwX,go=rX "$d"
find "$d" -exec touch -h -d @1700000000 {} +

# A POSIX ACL in the Linux xattr format (user::rw-, group::r--, other::r--);
# debugfs stores it in the ext4 format.
printf '\002\000\000\000\001\000\006\000\377\377\377\377\004\000\004\000\377\377\377\377\040\000\004\000\377\377\377\377' \
	>"$work/acl"

image()
{
	name=$1 size=$2
	shift 2
	rm -f "$work/$name"
	mke2fs -q -F -U "$uuid" -E "hash_seed=$hseed" -N 96 -d "$d" "$@" \
		"$work/$name" "$size" 2>/dev/null
	# mke2fs -d copies the host's ctime (which touch cannot set) and
	# owner: make them fixed
	(cd "$d" && find . | sed 's|^\.|/|; s|^//|/|') |
		sed 's/.*/sif "&" ctime 1700000000\nsif "&" uid 0\nsif "&" gid 0/' \
		>"$work/fixed"
	debugfs -w -f "$work/fixed" "$work/$name" >/dev/null 2>&1
	# mke2fs -d writes linear directories: index the ones of more than a
	# block (dir/sub) as htrees where the layout has dir_index.
	e2fsck -fyD "$work/$name" >/dev/null 2>&1 || [ $? -le 1 ]
	# xattrs: a small one (in the inode when there is room) and one that
	# needs an xattr block
	debugfs -w -R "ea_set /a.txt user.small v" "$work/$name" >/dev/null 2>&1
	debugfs -w -R "ea_set /dir/b.bin user.big $(printf 'v%.0s' $(seq 1 600))" \
		"$work/$name" >/dev/null 2>&1
	debugfs -w -R "ea_set -f $work/acl /dir system.posix_acl_access" \
		"$work/$name" >/dev/null 2>&1
	cp "$work/$name" "$out/$name"
}

image ext2-1k.img 200K -t ext2 -b 1024
# A journal needs 1024 blocks: mke2fs adds one from 2304 KiB up.
image ext3-1k.img 2304K -t ext3 -b 1024
image ext4-1k.img 256K -t ext4 -b 1024 -I 256 -O ^has_journal,^metadata_csum,^metadata_csum_seed,^orphan_file
image ext4-csum-1k.img 256K -t ext4 -b 1024 -I 256 -O ^has_journal,metadata_csum,^metadata_csum_seed,^orphan_file
image ext4-journal-1k.img 2304K -t ext4 -b 1024 -I 256 -O metadata_csum,^metadata_csum_seed,^orphan_file
image ext4-2k.img 256K -t ext4 -b 2048 -I 128 -O ^has_journal,^metadata_csum_seed,^orphan_file
# The default of e2fsprogs 1.47: checksums from the seed in the superblock
image ext4-csum-seed-1k.img 256K -t ext4 -b 1024 -I 256 -O ^has_journal,metadata_csum,metadata_csum_seed,^orphan_file

# Orphan list as Linux leaves it after a crash (released at a read-write
# mount): a deleted file, then a file whose truncate was interrupted.
cp "$out/ext4-1k.img" "$work/orphans.img"
ino()
{
	debugfs -R "stat $1" "$work/orphans.img" 2>/dev/null |
		sed -n 's/^Inode: \([0-9]*\).*/\1/p'
}
a=$(ino /a.txt)
b=$(ino /dir/b.bin)
debugfs -w -f - "$work/orphans.img" >/dev/null 2>&1 <<CMDS
unlink /a.txt
sif <$a> links_count 0
sif <$a> dtime $b
sif <$b> size 100
sif <$b> dtime 0
ssv last_orphan $a
CMDS
cp "$work/orphans.img" "$out/ext4-orphans-1k.img"

# Operation scripts for fuzz_rw: every operation once, and pseudo-random
# ones. Written as bytes by awk, then appended with the 2-byte length.
script()
{
	awk -v seed="$1" 'BEGIN {
		if (seed == 0) {
			for (op = 0; op < 16; op++) {
				printf "%c", op
				for (a = 0; a < 6; a++)
					printf "%c", (op * 37 + a * 11 + 1) % 256
			}
			exit
		}
		srand(seed)
		for (i = 0; i < 160; i++)
			printf "%c", int(rand() * 256)
	}'
}

for img in "$out"/*.img; do
	base=${img%.img}
	for s in 0 1 2; do
		script "$s" >"$work/s"
		n=$(wc -c <"$work/s")
		{
			cat "$img" "$work/s"
			printf "\\$(printf %03o $((n % 256)))\\$(printf %03o $((n / 256)))"
		} >"$base-rw$s.seed"
	done
done
ls "$out" | wc -l
