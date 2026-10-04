# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks whose group 0 descriptor puts its block bitmap on
# the group descriptor block (block 2) and marks it uninitialised (fork
# issue #160). $1.orig is a copy: the check compares the image with it.
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
debugfs -w -f - "$1" >/dev/null 2>&1 <<'CMDS'
set_bg 0 block_bitmap 2
set_bg 0 flags 0x2
set_bg 0 checksum calc
CMDS
debugfs -R 'stats' "$1" 2>/dev/null | grep -q 'Group  0: block bitmap at 2,'
cp "$1" "$1.orig"
