# SPDX-License-Identifier: BSD-3-Clause
# What lwext4 wrote to inline data, read by e2fsprogs: e2fsck is clean,
# written inodes moved to blocks, a shrunk one stayed inline.
. "$(dirname "$0")/common/check.sh"
check_fsck "$1"
check_debugfs "$1" "stat /f100" | grep -q 'Size of inline data' &&
	check_fail "/f100 was appended to but is still inline"
check_debugfs "$1" "stat /f120" | grep -q 'Size: 70$' ||
	check_fail "/f120 is not 70 bytes"
check_debugfs "$1" "stat /f120" | grep -q 'Size of inline data' ||
	check_fail "/f120 was shrunk, it should stay inline"
check_debugfs "$1" "ls /small" | grep -q 'new' ||
	check_fail "/small has no entry 'new'"
