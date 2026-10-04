# SPDX-License-Identifier: BSD-3-Clause
# The read-only mount changed nothing.
. "$(dirname "$0")/common/check.sh"
cmp -s "$1" "$1.orig" || check_fail "lwext4 changed the image"
