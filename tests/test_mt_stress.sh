# SPDX-License-Identifier: BSD-3-Clause
# ext4 with a journal, 1 KiB blocks: file data spans many blocks and the
# directories of the threads become indexed.
lwext4_mke2fs -t ext4 -b 1024 "$1" 32M
