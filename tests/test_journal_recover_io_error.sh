# SPDX-License-Identifier: BSD-3-Clause
# ext4 with 1 KiB blocks and a 1 MiB journal.
lwext4_mke2fs -t ext4 -b 1024 -J size=1 "$1" 8M
