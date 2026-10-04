# SPDX-License-Identifier: BSD-3-Clause
# Two directories with the same entries (fork issue #147): /normal, and
# /huge, whose size claims 4 GiB - 1 KiB although only its first block is
# mapped (damage: the rest is one hole of four million blocks).
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
debugfs -w -f - "$1" >/dev/null 2>&1 <<'CMDS'
mkdir /normal
mkdir /huge
sif /huge size 4294966272
CMDS
debugfs -R 'stat /huge' "$1" 2>/dev/null | grep -q 'Size: 4294966272'
