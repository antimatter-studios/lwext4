# SPDX-License-Identifier: BSD-3-Clause
# e2fsck accepts the attributes lwext4 wrote and debugfs reads every value,
# including those that fill the inode exactly.
check_fsck "$1"
for size in 40 60 64 65 66 67 68 69 70 71 72 73 76 80 100; do
	want=$(awk -v n="$size" 'BEGIN { for (k = 0; k < n; k++)
		printf "%c", 97 + (k + n) % 26 }')
	check_eq "$want" \
		"$(check_debugfs "$1" "ea_get -V /f$size user.a0")" \
		"/f$size user.a0"
done
