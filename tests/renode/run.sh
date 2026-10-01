#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Runs the Renode robot suite against a firmware build directory.
#
#   tests/renode/run.sh <build-dir> [renode-test args, e.g. --include/-t]
#
# Needs renode-test (Renode portable package) and e2fsprogs on PATH.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$1" && pwd)
shift
[ -f "$build/board.env" ] || { echo "no board.env in $build" >&2; exit 1; }
firmware=${FIRMWARE:-}
. "$build/board.env"
# FIRMWARE in the environment overrides the build's image
[ -z "$firmware" ] || FIRMWARE=$firmware

export BIG_FILE_KIB
# tests a board cannot run (e.g. not enough RAM), see its board.cmake
excludes=
for tag in ${BOARD_TEST_EXCLUDE:-}; do
	excludes="$excludes --exclude $tag"
done
out="$build/renode-results"
mkdir -p "$out/images"
# renode-test drops snapshots/ and logs/ of failed tests in the cwd
cd "$out"
exec renode-test "$here/lwext4.robot" \
	--results-dir "$out" \
	--variable "BOARD:$BOARD" \
	--variable "BOARD_REPL:$BOARD_REPL" \
	--variable "BOARD_UART:$BOARD_UART" \
	--variable "BOARD_SPI:$BOARD_SPI" \
	--variable "FIRMWARE:$FIRMWARE" \
	--variable "ELF:$ELF" \
	--variable "WORKDIR:$out/images" \
	$excludes "$@"
