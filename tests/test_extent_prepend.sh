# SPDX-License-Identifier: BSD-3-Clause
# An empty ext4 with 1 KiB blocks: the test lays out its blocks itself.
lwext4_mke2fs -t ext4 -b 1024 "$1" 16M
