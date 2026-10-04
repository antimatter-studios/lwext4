# SPDX-License-Identifier: BSD-3-Clause
# ext4 whose journal inode has a hole: blocks 5 to 20 of the journal are
# not mapped (fork issue #146).
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
debugfs -w -R 'punch <8> 5 20' "$1" >/dev/null 2>&1
debugfs -R 'stat <8>' "$1" 2>/dev/null | grep -q '(21-1023)'
