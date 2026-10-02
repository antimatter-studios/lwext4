# SPDX-License-Identifier: BSD-3-Clause
# The attribute lwext4 stored turned the ext_attr feature on, so e2fsck
# accepts it and debugfs reads it.
check_fsck "$1"
check_eq yes "$(dumpe2fs -h "$1" 2>/dev/null |
	grep -q '^Filesystem features:.*ext_attr' && echo yes)" "ext_attr feature"
check_eq "kept by e2fsprogs" \
	"$(check_debugfs "$1" 'ea_get -V /a.txt user.lwext4')" "/a.txt user.lwext4"
