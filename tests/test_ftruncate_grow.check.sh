# SPDX-License-Identifier: BSD-3-Clause
# e2fsck accepts the grown files and the 3 GiB sparse one, and the ext2
# image got the large_file feature for it.
for img in "$1" "$1.ext2"; do
	check_fsck "$img"
	check_eq 3221225472 "$(check_debugfs "$img" 'stat /big' |
		sed -n 's/.*Size: \([0-9]*\).*/\1/p' | head -n 1)" "$img: /big size"
done
check_eq yes "$(dumpe2fs -h "$1.ext2" 2>/dev/null |
	grep -q '^Filesystem features:.*large_file' && echo yes)" "ext2 large_file"
