# SPDX-License-Identifier: BSD-3-Clause
# ext3 (block maps and a journal), 1 KiB blocks, damaged: the file f has the
# block of the directory d as its block (fork issue #217).
dir="$1.d"
mkdir -p "$dir/d"
echo x >"$dir/d/x"
awk 'BEGIN { for (i = 0; i < 1024; i++) printf "f" }' >"$dir/f"
lwext4_mke2fs -t ext3 -b 1024 -d "$dir" "$1" 4M
b=$(debugfs -R "bmap /d 0" "$1" 2>/dev/null)
old=$(debugfs -R "bmap /f 0" "$1" 2>/dev/null)
debugfs -w -R "sif /f block[0] $b" "$1" >/dev/null 2>&1
debugfs -w -R "freeb $old" "$1" >/dev/null 2>&1
[ "$(debugfs -R "bmap /f 0" "$1" 2>/dev/null)" = "$b" ] ||
	{ echo "f: not on the block of d" >&2; exit 1; }
