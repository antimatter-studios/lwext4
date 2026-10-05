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
#  - every job README.md's "How it is tested" names exists.
set -eu
. "$(dirname "$0")/lib.sh"

step "README.md code blocks vs. inventory"
sh "$ACC_DIR/readme-blocks.sh" check
pass "every README.md code block is in the inventory"

step "README.md Debian dependencies vs. ci/envs/acceptance-debian"
dockerfile="$TOP_DIR/ci/envs/acceptance-debian/Dockerfile"
readme_deps=$(sh "$ACC_DIR/readme-blocks.sh" show dependencies#2)
docker_deps=$(sed -n 's/^RUN \(apt-get install\) -y /\1 /p' "$dockerfile" | head -n 1)
[ "$readme_deps" = "$docker_deps" ] ||
	die "README: '$readme_deps', Dockerfile: '$docker_deps'"
pass "README.md Debian package list is what acceptance-debian installs"
# The tools README.md's "Run regression tests" says the tests need, and the
# Debian package of each: exactly these are acceptance-debian's second
# install command.
tests_text=$(sed -n '/^Run regression tests/,/^```/p' "$TOP_DIR/README.md" | tr '\n' ' ')
tests_pkgs=
for tool_pkg in mke2fs:e2fsprogs sfdisk:fdisk python3:python3 pkg-config:pkgconf; do
	tool=${tool_pkg%%:*}
	case "$tests_text" in
	*"\`$tool\`"*) ;;
	*) die "README.md no longer says the regression tests need $tool" ;;
	esac
	tests_pkgs="$tests_pkgs${tests_pkgs:+ }${tool_pkg#*:}"
done
docker_tests_pkgs=$(sed -n 's/^RUN apt-get install -y //p' "$dockerfile" | sed -n 2p)
[ "$docker_tests_pkgs" = "$tests_pkgs" ] ||
	die "acceptance-debian installs '$docker_tests_pkgs' for the tests, README.md: '$tests_pkgs'"
pass "README.md regression test requirements are what acceptance-debian installs ($tests_pkgs)"

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
# check-docs.py (Examples workflow) checks that relative targets exist.
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

step "ci/run.sh jobs named in README.md"
# README.md shows "ci/run.sh <job> [arg]"; the job has to exist and be one
# of the jobs .github/workflows/ci.yml runs (with that matrix value).
for key in $(sh "$ACC_DIR/readme-blocks.sh" list | cut -f1); do
	sh "$ACC_DIR/readme-blocks.sh" show "$key"
done | sed -n 's/^ci\/run\.sh \([a-z0-9][a-z0-9_-]*\) *\([a-z0-9_-]*\)$/\1 \2/p' >"$WORK/jobs.txt"
[ -s "$WORK/jobs.txt" ] || die "no ci/run.sh jobs found in README.md"
ci_yml="$TOP_DIR/.github/workflows/ci.yml"
while read -r job arg; do
	[ -f "$TOP_DIR/ci/jobs/$job.sh" ] ||
		die "README.md runs 'ci/run.sh $job', there is no ci/jobs/$job.sh"
	grep -Eq "ci/run.sh $job( |\$)|job: \[.*\b$job\b" "$ci_yml" ||
		die "ci.yml does not run the job '$job' README.md shows"
	[ -z "$arg" ] || grep -Eq "^ *(- |(arch|toolchain): )$arg\$" "$ci_yml" ||
		die "ci.yml does not run '$job $arg'"
	pass "README.md's 'ci/run.sh $job $arg' is a job ci.yml runs"
done <"$WORK/jobs.txt"

finish
