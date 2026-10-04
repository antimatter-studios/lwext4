# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks.
lwext4_mke2fs -t ext4 -b 1024 "$1" 8M
