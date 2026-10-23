#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Release version checks (.github/workflows/release.yml).
#
#   release-version.sh <tag>              the tag is vMAJOR.MINOR.PATCH
#   release-version.sh <tag> <dist dir>   and every package in <dist dir>
#                                         is of that version: its name is
#                                         lwext4-<version>-*.tar.gz, and
#                                         the lwext4.pc it installs says
#                                         Version: <version>
set -eu

tag=$1
echo "$tag" | grep -Eqx 'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)' ||
	{ echo "release tag '$tag' is not vMAJOR.MINOR.PATCH" >&2; exit 1; }
v=${tag#v}
[ $# -ge 2 ] || exit 0

cd "$2"
n=0
pcs=0
for f in *.tar.gz; do
	[ -e "$f" ] || break
	n=$((n + 1))
	case "$f" in
	"lwext4-$v-"*) ;;
	*) echo "$f: not a package of version $v" >&2; exit 1 ;;
	esac
	pc=$(tar -tzf "$f" | grep '/lib/pkgconfig/lwext4\.pc$' || true)
	[ -n "$pc" ] || continue
	got=$(tar -xzOf "$f" "$pc" | sed -n 's/^Version: //p')
	[ "$got" = "$v" ] ||
		{ echo "$f: $pc says version '$got', not $v" >&2; exit 1; }
	pcs=$((pcs + 1))
done
[ "$n" -gt 0 ] || { echo "no packages in $2" >&2; exit 1; }
[ "$pcs" -gt 0 ] || { echo "no package installs lwext4.pc" >&2; exit 1; }
echo "$n packages of version $v, $pcs with lwext4.pc saying so"
