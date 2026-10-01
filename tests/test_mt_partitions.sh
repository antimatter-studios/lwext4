# SPDX-License-Identifier: BSD-3-Clause
# Two 8 MiB ext4 partitions with a journal, one after the other (the
# test's PART_SIZE), 1 KiB blocks on 512 byte sectors.
lwext4_mke2fs -t ext4 -b 1024 "$1.0" 8M
lwext4_mke2fs -t ext4 -b 1024 "$1.1" 8M
cat "$1.0" "$1.1" > "$1"
