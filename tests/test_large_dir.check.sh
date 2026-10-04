# SPDX-License-Identifier: BSD-3-Clause
# The directory lwext4 grew to three index levels, as e2fsprogs reads it.
. "$(dirname "$0")/common/check.sh"
check_fsck "$1"
check_fsck "$1.full"
check_fsck "$1.nolarge"
check_debugfs "$1.full" "htree /big" | grep -q 'Indirect levels: 2' ||
	check_fail "lwext4 did not add a third index level to /big of $1.full"
