# SPDX-License-Identifier: BSD-3-Clause
# The filesystem was mounted read-only: not a byte changed.
. "$(dirname "$0")/common/check.sh"
check_fsck "$1"
cmp -s "$1" "$1.orig" || check_fail "lwext4 changed the inline_data image"
