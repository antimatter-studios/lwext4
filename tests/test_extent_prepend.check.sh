# SPDX-License-Identifier: BSD-3-Clause
# /B is one extent, logical blocks 0-1 on two consecutive physical blocks:
# the new block was merged in front of the existing extent, which is the
# case the test is about; e2fsck accepts the result.
check_fsck "$1"
ex=$(check_debugfs "$1" "ex /B" | sed -n 's/^ *[0-9]*\/ *[0-9]* *[0-9]*\/ *[0-9]* *\([0-9]*\) *- *\([0-9]*\) *\([0-9]*\) *- *\([0-9]*\).*/\1 \2 \3 \4/p')
set -- $ex
[ "$#" = 4 ] && [ "$1" = 0 ] && [ "$2" = 1 ] && [ $(($4 - $3)) = 1 ] ||
	{ echo "check: /B is not one extent 0-1 on consecutive blocks: $ex" >&2; exit 1; }
