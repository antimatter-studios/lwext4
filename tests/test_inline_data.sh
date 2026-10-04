# SPDX-License-Identifier: BSD-3-Clause
# ext4 with inline_data (fork issue #130), 256 byte inodes:
#   /f<N>       files of N bytes: up to 60 in i_block, up to 120 continued
#               in the system.data xattr, 140 and more in blocks
#   /long       an 80 byte symlink, inline
#   /small      an inline directory whose last two entries (xa, xb) are in
#               system.data (written by hand: e2fsprogs moves a directory to
#               a block rather than extending it there, Linux does not)
#   /medium     a directory in a block, with an inline subdirectory
# $1.orig is a copy: the check compares the image with it.
t="$1.tree"
mkdir -p "$t/small" "$t/medium/deeper"
for n in 1 59 60 61 100 120 140 5000; do
	awk -v n=$n 'BEGIN { for (i = 0; i < n; i++) printf "%c", 97 + i % 26 }' >"$t/f$n"
done
echo s1 >"$t/small/e1"
echo s2 >"$t/small/e2"
for i in 1 2 3 4 5 6; do echo m$i >"$t/medium/entry_$i"; done
echo deep >"$t/medium/deeper/x"
ln -s "$(printf 'L%.0s' $(seq 1 80))" "$t/long"
# No inherited ACLs: they would take the room of system.data
if command -v setfacl >/dev/null 2>&1; then setfacl -R -b "$t"; fi
mke2fs -q -F -t ext4 -b 1024 -I 256 -O inline_data,^metadata_csum_seed,^orphan_file -d "$t" "$1" 4M
rm -rf "$t"

ino()
{
	debugfs -R "stat $1" "$2" 2>/dev/null | sed -n 's/^Inode: \([0-9]*\).*/\1/p'
}
a=$(ino /f1 "$1")
b=$(ino /f59 "$1")
python3 - "$1.xv" "$a" "$b" <<'PY'
import struct, sys
a, b = int(sys.argv[2]), int(sys.argv[3])
with open(sys.argv[1], 'wb') as f:
    f.write(struct.pack('<IHBB', a, 12, 2, 1) + b'xa' + b'\0\0')
    f.write(struct.pack('<IHBB', b, 20, 2, 1) + b'xb' + b'\0' * 10)
PY
debugfs -w -f - "$1" >/dev/null 2>&1 <<CMDS
ea_set -f $1.xv /small system.data
sif /small size 92
sif <$a> links_count 2
sif <$b> links_count 2
CMDS
rm -f "$1.xv"
debugfs -R "stat /small" "$1" 2>/dev/null | grep -q 'Size of inline data: 92'
debugfs -R "stat /f100" "$1" 2>/dev/null | grep -q 'Size of inline data: 100'
e2fsck -fn "$1" >/dev/null
cp "$1" "$1.orig"
