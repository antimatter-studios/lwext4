# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 256 byte inodes (creation time and the extended time fields),
# made at a known time: mke2fs sets mkfs_time and write_time to it.
E2FSPROGS_FAKE_TIME=1000000000 lwext4_mke2fs -t ext4 -b 1024 -I 256 "$1" 8M
