# SPDX-License-Identifier: BSD-3-Clause
# ext4 with metadata_csum_seed (fork issue #127), the default of e2fsprogs
# 1.47 and later. The UUID is changed afterwards: with csum_seed tune2fs
# keeps the stored seed, so from then on the checksums only verify against
# checksum_seed, not against crc32c(uuid).
mke2fs -q -F -t ext4 -b 1024 -I 256 -O metadata_csum,metadata_csum_seed "$1" 8M
tune2fs -U 6c77e7c4-1111-4000-8000-0000000000aa "$1" >/dev/null
