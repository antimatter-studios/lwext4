# SPDX-License-Identifier: BSD-3-Clause
# The block appended to /full holds the 10 bytes written and zeros up to
# its end, not the 0xaa bytes the device held (they would show once the
# file grows).
for img in "$1" "$1.ext2"; do
	check_fsck "$img"
	blk=$(check_debugfs "$img" "bmap /full 2")
	[ -n "$blk" ] && [ "$blk" != 0 ] || { echo "check: $img: no block 2 in /full" >&2; exit 1; }
	stale=$(dd if="$img" bs=1024 skip="$blk" count=1 2>/dev/null |
		tail -c 1014 | tr -d '\000' | wc -c | tr -d ' ')
	check_eq 0 "$stale" "$img: nonzero bytes after the end of /full"
done
