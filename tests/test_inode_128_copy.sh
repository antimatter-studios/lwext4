# SPDX-License-Identifier: BSD-3-Clause
# ext3 with 1 KiB blocks and 128 byte i-nodes (mke2fs -I 128): eight
# i-nodes per table block, the journal (i-node 8) the last of its block;
# /a and /b take two consecutive i-nodes of one block.
lwext4_mke2fs -t ext3 -b 1024 -I 128 "$1" 8M
debugfs -w -f - "$1" >/dev/null 2>&1 <<'CMDS'
write /dev/null a
write /dev/null b
CMDS
