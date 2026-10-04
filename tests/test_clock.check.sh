# SPDX-License-Identifier: BSD-3-Clause
# What lwext4 stamped, read by e2fsprogs: e2fsck is clean, the creation
# time, and a time past 2038 (epoch bit of the extended field).
. "$(dirname "$0")/common/check.sh"
check_fsck "$1"
stat=$(check_debugfs "$1" "stat /d/f")
echo "$stat" | grep -q 'crtime: 0x6553f100' ||
	check_fail "crtime of /d/f is not 1700000000: $stat"
# 2046 as Linux encodes it (ext4_encode_extra_time): the low 32 bits of
# the seconds, 0x90000000, and epoch 1 in the extended field, which Linux
# reads as (int32_t)0x90000000 + (1 << 32). (debugfs of e2fsprogs 1.47 adds
# the epoch to the unsigned seconds and prints 2182; it is not checked.)
late=$(check_debugfs "$1" "stat /late")
echo "$late" | grep -q 'mtime: 0x90000000:00000001' ||
	check_fail "mtime of /late is not 0x90000000 with epoch 1: $late"
