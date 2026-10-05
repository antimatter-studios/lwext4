#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Report what a scheduled fuzzing run (.github/workflows/fuzz.yml) found in
# one target: open an issue (label `fuzzing`), or comment on the one still
# open for that target, with the sanitizer report and how to reproduce it.
#
#   ci/scripts/fuzz-report.sh <target> <repository> <run id> <artifact>
#
# Reads the inputs ci/run.sh fuzz run left in build-ci/fuzz/art/ and its
# output in build-ci/fuzz/<target>.run.log. Does nothing if the target left
# no input (the run failed for another reason: the failed job shows why).
# With DRY_RUN=1 it prints the issue instead of filing it.
set -eu

target=$1
repo=$2
run=$3
artifact=$4
b=build-ci/fuzz

inputs=$(cd "$b/art" 2>/dev/null && ls "$target"-* 2>/dev/null) || true
if [ -z "$inputs" ]; then
	echo "fuzz-report: $target left no input, nothing to report"
	exit 0
fi

# From the first error line of libFuzzer or a sanitizer, with the stack
report=$(sed -n '/==ERROR\|runtime error:\|ERROR: libFuzzer\|^SUMMARY:/,$p' \
	"$b/$target.run.log" | grep -v '^INFO:\|^MS: \|^base64: ' |
	head -n 60)
[ -n "$report" ] || report=$(tail -n 40 "$b/$target.run.log")

title="Scheduled fuzzing: $target fails"
url="https://github.com/$repo/actions/runs/$run"
body=$(cat <<EOF
The [scheduled fuzzing run]($url) found inputs that make \`$target\` fail:

\`\`\`
$report
\`\`\`

The inputs and the full output of the target are in the run's artifact
\`$artifact\`. To reproduce, from a checkout of ${GITHUB_SHA:-the commit the run tested}:

\`\`\`sh
gh run download $run -R $repo -n $artifact -D tmp/$artifact
$(for i in $inputs; do echo "ci/run.sh fuzz repro tmp/$artifact/art/$i"; done)
\`\`\`

Fix it with a red/green test, and add the input, minimised and gzipped, to
\`tests/fuzz/crashes\` ([tests/fuzz/README.md](https://github.com/$repo/blob/main/tests/fuzz/README.md)).
EOF
)

if [ "${DRY_RUN:-}" = 1 ]; then
	printf 'title: %s\n\n%s\n' "$title" "$body"
	exit 0
fi

open=$(gh issue list -R "$repo" -l fuzzing -s open -L 100 \
	--json number,title -q ".[] | select(.title == \"$title\") | .number" |
	head -n 1)
if [ -n "$open" ]; then
	gh issue comment "$open" -R "$repo" -b "$body"
else
	gh issue create -R "$repo" -l fuzzing -l bug -t "$title" -b "$body"
fi
