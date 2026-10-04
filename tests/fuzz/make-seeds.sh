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
# Small files, symlinks and directories inline in the i-node
image ext4-inline-1k.img 256K -t ext4 -b 1024 -I 256 -O ^has_journal,inline_data,^metadata_csum_seed,^orphan_file

# More layouts: 4 KiB blocks, meta_bg, large_dir, 1 KiB i-nodes (much
# room for xattrs in the i-node)
image ext4-4k.img 512K -t ext4 -b 4096 -O ^has_journal,^metadata_csum_seed,^orphan_file
image ext4-meta-bg-1k.img 256K -t ext4 -b 1024 -O ^has_journal,^resize_inode,meta_bg,^metadata_csum_seed,^orphan_file
image ext4-large-dir-1k.img 256K -t ext4 -b 1024 -O ^has_journal,large_dir,^metadata_csum_seed,^orphan_file
image ext4-1k-inode-1k.img 256K -t ext4 -b 1024 -I 1024 -O ^has_journal,^metadata_csum_seed,^orphan_file

# A file whose extent tree has an index level: one block in every other
cp "$out/ext4-1k.img" "$work/extents.img"
i=0
: >"$work/sparse"
while [ $i -lt 120 ]; do
	printf 'e' | dd of="$work/sparse" bs=1024 seek=$((i * 2)) count=1 conv=notrunc 2>/dev/null
	i=$((i + 1))
done
debugfs -w -R "write $work/sparse sparse" "$work/extents.img" >/dev/null 2>&1
debugfs -R "stat /sparse" "$work/extents.img" 2>/dev/null | grep -q 'ETB0' ||
	{ echo "make-seeds.sh: no extent tree level" >&2; exit 1; }
cp "$work/extents.img" "$out/ext4-extent-tree-1k.img"

# Journals that need replaying, as debugfs writes them: a copy of a real
# i-node table block and of a data block, and a revoke record
journalled()
{
	src=$1 dst=$2
	cp "$src" "$work/j.img"
	itab=$(debugfs -R "imap <2>" "$work/j.img" 2>/dev/null |
		sed -n 's/.*located at block \([0-9]*\).*/\1/p')
	data=$(debugfs -R "bmap /dir/b.bin 0" "$work/j.img" 2>/dev/null)
	dd if="$work/j.img" of="$work/jblk" bs=1024 skip="$itab" count=1 2>/dev/null
	debugfs -w -f - "$work/j.img" >/dev/null 2>&1 <<CMDS
journal_open
journal_write -b $itab $work/jblk
journal_write -b $data $work/jblk
journal_write -r $((data + 1))
journal_close
CMDS
	debugfs -R "logdump" "$work/j.img" 2>/dev/null | grep -q 'revoke table' ||
		{ echo "make-seeds.sh: no journal written" >&2; exit 1; }
	# debugfs stamps the commit blocks with the time (h_commit_sec at
	# 48, h_commit_nsec at 56): zero it (these journals have no
	# checksums over it)
	for jb in $(debugfs -R "logdump" "$work/j.img" 2>/dev/null |
		    sed -n 's/.*(commit block) at block \([0-9]*\).*/\1/p'); do
		fb=$(debugfs -R "bmap <8> $jb" "$work/j.img" 2>/dev/null)
		dd if=/dev/zero of="$work/j.img" bs=1 seek=$((fb * 1024 + 48)) \
			count=12 conv=notrunc 2>/dev/null
	done
	cp "$work/j.img" "$out/$dst"
}
image ext4-journal-nocsum-1k.img 2304K -t ext4 -b 1024 -I 256 -O ^metadata_csum,^metadata_csum_seed,^orphan_file
journalled "$out/ext3-1k.img" ext3-replay-1k.img
journalled "$out/ext4-journal-nocsum-1k.img" ext4-replay-1k.img

# Partitioned disks (fuzz_partition): an MBR with a primary and an
# extended partition holding two logical ones, and a GPT, each with the
# ext2 image in its first partition
partitioned()
{
	label=$1 dst=$2
	rm -f "$work/disk"
	truncate -s 1M "$work/disk"
	if [ "$label" = dos ]; then
		printf 'label: dos\nlabel-id: 0x6c77e7c4\nstart=64, size=400, type=83\nstart=470, size=500, type=5\nstart=480, size=200, type=83\nstart=690, size=200, type=83\n'
	else
		printf 'label: gpt\nlabel-id: 6c77e7c4-0000-4000-8000-000000000010\nstart=64, size=400, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, uuid=6c77e7c4-0000-4000-8000-000000000011\nstart=470, size=500, uuid=6c77e7c4-0000-4000-8000-000000000012\n'
	fi | sfdisk -q "$work/disk" >/dev/null 2>&1 ||
		{ printf '%s\n' "make-seeds.sh: sfdisk failed" >&2; exit 1; }
	dd if="$out/ext2-1k.img" of="$work/disk" bs=512 seek=64 count=400 conv=notrunc 2>/dev/null
	cp "$work/disk" "$out/$dst"
}
partitioned dos part-mbr.img
partitioned gpt part-gpt.img

# fuzz_mkfs parameters (see fuzz_mkfs.c): ext4 1 MiB, 1 KiB blocks,
# journal and 64 byte descriptors; ext2 512 KiB, 4 KiB blocks; ext3 2 MiB,
# 2 KiB blocks, 256 byte i-nodes, a label
printf '\004\000\002\003\000\001' >"$out/mkfs-ext4.seed"
printf '\003\002\000\000\200\000' >"$out/mkfs-ext2.seed"
printf '\005\001\001\001\000\001\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000lwext4-fuzz' \
	>"$out/mkfs-ext3.seed"

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
	# fuzz_rwx: the fault bytes before the length (see fuzz_rwx.c): no
	# errors, and every write failing from the 40th on
	script 2 >"$work/s"
	n=$(wc -c <"$work/s")
	for f in nofault:'\000\000\000\000' wfail40:'\006\050\000\000'; do
		{
			cat "$img" "$work/s"
			printf "${f#*:}"
			printf "\\$(printf %03o $((n % 256)))\\$(printf %03o $((n / 256)))"
		} >"$base-rwx-${f%%:*}.seed"
	done
done
ls "$out" | wc -l
