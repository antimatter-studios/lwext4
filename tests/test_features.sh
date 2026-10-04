# SPDX-License-Identifier: BSD-3-Clause
# The images of the feature tables of README.md ("Supported ext2/3/4
# features"): one per row, made with e2fsprogs. $1 becomes the list the
# test reads, one line per image: path, expected outcome (rw, ro or
# refused), name.
readme="$(dirname "$0")/../README.md"
: > "$1"
n=0

# Fail the setup (and so the test) when e2fsprogs did not make what the row
# claims to test
listed()
{
	grep '^Filesystem features:' | tr ' ' '\n' | grep -qx "$1"
}

has()
{
	debugfs -R features "$1" 2>/dev/null | listed "$2" || {
		echo "test_features.sh: $1 has no $2" >&2
		exit 1
	}
}

outcome()
{
	case "$1" in
	*read-write*) echo rw ;;
	*read-only*) echo ro ;;
	*refused*) echo refused ;;
	*) echo "test_features.sh: no outcome in '$1'" >&2; exit 1 ;;
	esac
}

rows=$(sed -n '/<!-- features: begin/,/<!-- features: end/p' "$readme" |
	grep '^| `')
[ -n "$rows" ] || { echo "test_features.sh: no tables in $readme" >&2; exit 1; }

echo "$rows" | while IFS='|' read -r _ name col2 col3 _; do
	name=$(echo "$name" | tr -d ' `')
	n=$((n + 1))
	img="$1.$n"
	case "$col2" in
	*KiB*)	# filesystem type and block size
		bs=$(($(echo "$col2" | tr -dc 0-9) * 1024))
		# (with 64 KiB blocks, mke2fs adds a journal from 256 MiB on;
		# the image is sparse)
		size=16M
		[ "$bs" -lt 65536 ] || size=256M
		mke2fs -q -F -t "$name" -b "$bs" "$img" "$size" 2>/dev/null
		has "$img" filetype
		echo "$img $(outcome "$col3") $name-$bs" >> "$1"
		continue
		;;
	esac
	case "$name" in
	meta_bg) opts=meta_bg,^resize_inode ;;
	uninit_bg) opts=uninit_bg,^metadata_csum ;;
	journal_dev) opts=journal_dev ;;
	*) opts=$name ;;
	esac
	if mke2fs -q -F -t ext4 -b 1024 -O "$opts" "$img" 16M 2>/dev/null
	then
		[ "$name" = journal_dev ] || has "$img" "$name"
	else
		# A flag mke2fs does not set: set on a default ext4. debugfs
		# lists the features it wrote; it cannot open the result again
		# when libext2fs does not support the feature either
		mke2fs -q -F -t ext4 -b 1024 "$img" 16M
		debugfs -w -R "feature $name" "$img" 2>/dev/null |
			listed "$name" || {
			echo "test_features.sh: debugfs did not set $name" >&2
			exit 1
		}
	fi
	echo "$img $(outcome "$col3") $name" >> "$1"
done
