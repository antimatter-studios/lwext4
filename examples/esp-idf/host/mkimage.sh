#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Build the host-made ext4 images the ESP32 test firmware mounts.
#
#   mkimage.sh flash <out> <size>  ext4 (4 KiB blocks) for a flash partition
#   mkimage.sh sd <out>            64 MiB SD card: MBR, p1 = host-made ext4
#                                  (1 KiB blocks), p2 = empty (the firmware
#                                  formats it with ext4_mkfs)
#
# The file tree written here is what main/lwext4_test.c expects to find
# (host_tree_verify()); keep the two in sync.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
PATH="$PATH:/sbin:/usr/sbin"

HOST_PATTERN_SEED=2
HOST_PATTERN_SIZE=300000

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

populate()
{
	root="$tmp/root"
	mkdir -p "$root/host/a/b/c" "$root/host/many"
	printf 'Hello from mke2fs on the build host\n' >"$root/hello.txt"
	printf 'deep\n' >"$root/host/a/b/c/deep.txt"
	python3 "$here/pattern.py" gen $HOST_PATTERN_SEED $HOST_PATTERN_SIZE \
		"$root/host/pattern.bin"
	i=0
	while [ $i -lt 40 ]; do
		n=$(printf '%03d' $i)
		printf 'file %s\n' "$n" >"$root/host/many/f$n"
		i=$((i + 1))
	done
	ln -s ../hello.txt "$root/host/link"
}

# mke2fs <image> <blocks> <options...>
mkfs()
{
	img=$1
	blocks=$2
	shift 2
	# Newer e2fsprogs enable features lwext4 does not support.
	mke2fs -q -F -O ^orphan_file,^metadata_csum_seed "$@" -d "$tmp/root" \
		"$img" "$blocks" 2>/dev/null ||
		mke2fs -q -F "$@" -d "$tmp/root" "$img" "$blocks"
	# mke2fs silently drops the journal when it does not fit.
	dumpe2fs -h "$img" 2>/dev/null | grep -q '^Filesystem features:.*has_journal' || {
		echo "mkimage.sh: $img has no journal" >&2
		exit 1
	}
}

# Write a DOS partition table: mbr <image> <start sector> <sectors> ...
mbr()
{
	python3 - "$@" <<'EOF'
import struct, sys
img = sys.argv[1]
parts = [int(x) for x in sys.argv[2:]]
mbr = bytearray(512)
struct.pack_into("<I", mbr, 440, 0x4c784534)  # disk id
for i in range(0, len(parts), 2):
    start, count = parts[i], parts[i + 1]
    # status, CHS (unused: LBA only), type 0x83 (Linux), CHS, LBA, count
    struct.pack_into("<B3sB3sII", mbr, 446 + 16 * (i // 2), 0,
                     b"\xfe\xff\xff", 0x83, b"\xfe\xff\xff", start, count)
mbr[510:512] = b"\x55\xaa"
with open(img, "r+b") as f:
    f.write(mbr)
EOF
}

case "${1:-}" in
flash)
	out=$2
	size=$3
	populate
	rm -f "$out"
	# 4 KiB blocks = the flash erase sector: the block device exposes
	# 4 KiB physical blocks. Smallest journal mke2fs allows (1024 blocks,
	# which needs a filesystem of at least 2048 blocks).
	mkfs "$out" $((size / 4096)) -t ext4 -b 4096 -L ext4host -J size=4
	;;
sd)
	out=$2
	populate
	rm -f "$out"
	truncate -s 64M "$out"
	# p1: sectors 2048..65535 (1 MiB .. 32 MiB), p2: 32 MiB .. 64 MiB.
	mbr "$out" 2048 63488 65536 65536
	mkfs "$tmp/p1.img" $((63488 / 2)) -t ext4 -b 1024 -L sdhost
	dd if="$tmp/p1.img" of="$out" bs=512 seek=2048 conv=notrunc \
		status=none
	;;
*)
	echo "usage: $0 flash <out> <size> | sd <out>" >&2
	exit 2
	;;
esac
