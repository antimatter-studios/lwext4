# SPDX-License-Identifier: BSD-3-Clause
# e2fsck accepts every image test_fsx wrote, and debugfs reads each file
# exactly as the test's model says (<image>.model.<name>).
for img in "$1" "$1.4k" "$1.nojournal" "$1.ext2"; do
	check_fsck "$img"
	n=0
	for model in "$img".model.*; do
		name=${model##*.model.}
		debugfs -R "dump /fsx/$name $model.dump" "$img" >/dev/null 2>&1
		cmp -s "$model" "$model.dump" ||
			{ echo "check: $img: /fsx/$name differs from the model" >&2; exit 1; }
		n=$((n + 1))
	done
	check_eq 8 "$n" "$img: files compared"
done
