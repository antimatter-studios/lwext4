#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
#
# Consistency of the documentation with the tests and the repository:
#  - every README.md code block is covered by the inventory in
#    tests/acceptance/README.md (and the inventory has no stale entries),
#  - the Debian dependency commands in README.md are the ones the
#    acceptance-debian container installs,
#  - the files and directories README.md's "Project tree" lists exist,
#  - every Makefile target README.md mentions exists,
#  - every example in examples/ is linked from README.md's "Getting
#    started" and from examples/README.md,
#  - every job README.md's "How it is tested" names exists,
#  - this job depends on the documented CI suites.
set -eu
. "$(dirname "$0")/lib.sh"

step "README.md code blocks vs. inventory"
sh "$ACC_DIR/readme-blocks.sh" check
pass "every README.md code block is in the inventory"

step "README.md Debian dependencies vs. ci/envs/acceptance-debian"
dockerfile="$TOP_DIR/ci/envs/acceptance-debian/Dockerfile"
readme_deps=$(
	sh "$ACC_DIR/readme-blocks.sh" show dependencies#2 &&
	sh "$ACC_DIR/readme-blocks.sh" show dependencies#3
)
docker_deps=$(sed -n 's/^RUN \(apt-get install\) -y /\1 /p' "$dockerfile")
[ "$readme_deps" = "$docker_deps" ] ||
	die "README: '$readme_deps', Dockerfile: '$docker_deps'"
pass "README.md Debian build and regression test packages match acceptance-debian"

step "README.md project tree"
sed -n '/^Project tree/,/^Compile/p' "$TOP_DIR/README.md" |
	sed -n 's/^\* *\([^ ]*\) .*/\1/p' >"$WORK/tree.txt"
[ -s "$WORK/tree.txt" ] || die "could not parse the project tree"
while read -r entry; do
	[ -e "$TOP_DIR/$entry" ] || die "README.md lists '$entry', which does not exist"
	pass "project tree entry exists: $entry"
done <"$WORK/tree.txt"

step "Makefile targets named in README.md"
# The make database lists every target; nothing is run.
make -C "$TOP_DIR" -pRrq : 2>/dev/null |
	sed -n 's/^\([a-zA-Z0-9_+.-][^:=# \t]*\):.*/\1/p' | sort -u >"$WORK/targets.txt"
# "make <target>" in code blocks before any "cd" (after "cd build_*" it is
# the CMake generated Makefile's target), and in the prose unless a block
# runs it as such a CMake target (e.g. "make install")
for key in $(sh "$ACC_DIR/readme-blocks.sh" list | cut -f1); do
	sh "$ACC_DIR/readme-blocks.sh" show "$key" |
		awk '/^cd / { cd = 1 } /^(sudo )?make [a-z0-9_+-]*$/ { print cd + 0, $NF }'
done >"$WORK/block-targets.txt"
{
	awk '$1 == 0 { print $2 }' "$WORK/block-targets.txt"
	grep -o '`make [a-z0-9_+-]*`' "$TOP_DIR/README.md" | tr -d '`' |
		awk '{ print $2 }' | while read -r target; do
		grep -Fqx "1 $target" "$WORK/block-targets.txt" || echo "$target"
	done
} | sort -u >"$WORK/readme-targets.txt"
for target in $(cat "$WORK/readme-targets.txt"); do
	grep -Fqx -- "$target" "$WORK/targets.txt" ||
		die "README.md mentions 'make $target', which the Makefile does not have"
	pass "Makefile has target $target"
done

step "examples linked from README.md and examples/README.md"
# Link targets ([text](target)) of the "Getting started" section of
# README.md (up to the next underlined heading) and of examples/README.md.
# The examples job checks that relative targets exist.
awk '/^Getting started$/ { on = 1; next }
	on && /^=+$/ { if (seen) exit; seen = 1; next }
	on { print prev } { prev = $0 }' "$TOP_DIR/README.md" |
	grep -o '](\([^)]*\))' | sed 's/^](//; s/)$//' >"$WORK/started-links.txt"
grep -o '](\([^)]*\))' "$TOP_DIR/examples/README.md" |
	sed 's/^](//; s/)$//' >"$WORK/examples-links.txt"
[ -s "$WORK/started-links.txt" ] || die "no links in README.md's Getting started"
for dir in "$TOP_DIR"/examples/*/; do
	name=$(basename "$dir")
	grep -Eq "(^|/)examples/$name(/|\$)" "$WORK/started-links.txt" ||
		die "README.md's Getting started does not link examples/$name"
	grep -Eq "^$name(/|\$)|/examples/$name(/|\$)" "$WORK/examples-links.txt" ||
		die "examples/README.md does not link $name"
	pass "examples/$name is linked from README.md's Getting started and examples/README.md"
done

step "jobs of README.md's \"How it is tested\""
# The last column of its table names the jobs (`job [arg]`) that test each
# row; each has to be a job of ci/jobs.
sed -n '/^How it is tested$/,/^Project tree$/p' "$TOP_DIR/README.md" |
	sed -n 's/^|.*| \([^|]*\) |$/\1/p' | grep -o '`[^`]*`' | tr -d '`' |
	awk '{ print $1 }' | sort -u >"$WORK/tested-jobs.txt"
[ -s "$WORK/tested-jobs.txt" ] || die "no jobs found in README.md's How it is tested"
while read -r job; do
	[ -f "$TOP_DIR/ci/jobs/$job.sh" ] ||
		die "README.md's How it is tested names '$job', there is no ci/jobs/$job.sh"
	pass "How it is tested: job $job exists"
done <"$WORK/tested-jobs.txt"

step "CI jobs and suites named in README.md"
for key in $(sh "$ACC_DIR/readme-blocks.sh" list | cut -f1); do
	sh "$ACC_DIR/readme-blocks.sh" show "$key"
done | sed -n 's|^ci/run\.sh \([a-z0-9].*\)$|\1|p' >"$WORK/jobs.txt"
python3 "$ACC_DIR/check-ci.py" "$WORK/jobs.txt"
pass "CI runs the documented suites before this job"

finish
