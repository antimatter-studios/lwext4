# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a 1 MiB journal in one extent.
lwext4_mke2fs -t ext4 -b 1024 -J size=1 "$1" 32M
debugfs -R 'stat <8>' "$1" 2>/dev/null | grep -q '^(0-1023):' ||
	{ echo "journal: not one extent" >&2; exit 1; }
