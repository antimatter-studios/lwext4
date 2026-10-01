# SPDX-License-Identifier: BSD-3-Clause
# Two 4 MiB partitions, one after the other (the test's PART_SIZE).
lwext4_mke2fs -t ext4 -b 1024 "$1.0" 4M
lwext4_mke2fs -t ext4 -b 1024 "$1.1" 4M
cat "$1.0" "$1.1" > "$1"
