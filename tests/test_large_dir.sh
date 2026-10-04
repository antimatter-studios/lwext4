# SPDX-License-Identifier: BSD-3-Clause
# Directories with htrees of three levels (large_dir, fork issue #131),
# 1 KiB blocks, entries of 248 characters (three per block), all hard
# links of /target:
#   $1          large_dir, /big with 50000 entries: three levels
#   $1.full     large_dir, /big with 46400 entries: two levels, the root
#               and the index nodes full, so lwext4 adds the third
#   $1.nolarge  the same without large_dir: the tree cannot grow
make_dir()
{
	img=$1 count=$2
	shift 2
	mke2fs -q -F -t ext4 -b 1024 -N 256 "$@" "$img" 40M
	echo x >"$img.t"
	debugfs -w -R "write $img.t target" "$img" >/dev/null 2>&1
	python3 "$(dirname "$0")/common/mkdir_blocks.py" "$img.big" "$count" 12 13
	debugfs -w -R "write $img.big big" "$img" >/dev/null 2>&1
	[ "$(debugfs -R 'stat /target' "$img" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')" = 12 ]
	[ "$(debugfs -R 'stat /big' "$img" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p')" = 13 ]
	printf 'sif /big mode 040755\nsif /big links_count 2\n' |
		debugfs -w -f - "$img" >/dev/null 2>&1
	rm -f "$img.t" "$img.big"
	# Link counts, then index /big
	e2fsck -fyD "$img" >/dev/null 2>&1 || [ $? -le 1 ]
	e2fsck -fn "$img" >/dev/null
}
make_dir "$1" 50000 -O large_dir,^metadata_csum_seed,^orphan_file
debugfs -R 'htree /big' "$1" 2>/dev/null | grep -q 'Indirect levels: 2'
make_dir "$1.full" 46400 -O large_dir,^metadata_csum_seed,^orphan_file
debugfs -R 'htree /big' "$1.full" 2>/dev/null | grep -q 'Indirect levels: 1'
make_dir "$1.nolarge" 46400 -O ^metadata_csum_seed,^orphan_file
