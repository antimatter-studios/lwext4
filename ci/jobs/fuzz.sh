# SPDX-License-Identifier: BSD-3-Clause
# env: native
# libFuzzer targets of tests/fuzz, built with clang, ASan and UBSan.
#
#   ci/run.sh fuzz replay              run every committed input once (seed
#                                      corpus and crashes); fails on a crash
#   ci/run.sh fuzz repro <input>...   run each input through every target
#                                      and show the full report
#   ci/run.sh fuzz run [seconds] [target]
#                                      fuzz for a while (default 600 s per
#                                      target), new crash inputs in
#                                      build-ci/fuzz/art/
#
# Targets: one per tests/fuzz/fuzz_<name>.c. Inputs: the seed corpus that
# tests/fuzz/make-seeds.sh makes (reproducibly, with e2fsprogs) and
# tests/fuzz/crashes/ (every input that ever crashed a target, gzipped,
# kept as regression tests).
mode=${1:-replay}

. ci/scripts/common.sh
b=$(ci_build_dir fuzz)

mkdir -p "$b/bin"
ln -sf "$(command -v clang)" "$b/bin/cc"
export PATH="$PWD/$b/bin:$PATH"

cmake -S . -B "$b/lib" -DCMAKE_TOOLCHAIN_FILE=toolchain/generic.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DLWEXT4_SANITIZE="fuzzer-no-link,address,undefined" >/dev/null
cmake --build "$b/lib" -j"$(ci_jobs)" --target lwext4 >/dev/null

gen=$(find "$b/lib" -type d -name generated -path '*include*' | head -n 1)
targets=
for src in tests/fuzz/fuzz_*.c; do
	t=$(basename "$src" .c)
	clang -g -O1 -fsanitize=fuzzer,address,undefined \
		-fno-sanitize-recover=undefined -Iinclude -I"$(dirname "$gen")" \
		"$src" "$b/lib/src/liblwext4.a" -o "$b/$t"
	targets="$targets $t"
done

rm -rf "$b/seeds"
sh tests/fuzz/make-seeds.sh "$b/seeds" >/dev/null

export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

# The crash inputs are stored gzipped (mostly zeros of disk images)
rm -rf "$b/crashes"
mkdir -p "$b/crashes"
for f in tests/fuzz/crashes/*.gz; do
	[ -e "$f" ] || continue
	gzip -dc "$f" >"$b/crashes/$(basename "$f" .gz)"
done

inputs()
{
	find "$b/seeds" "$b/crashes" -type f | LC_ALL=C sort
}

case "$mode" in
replay)
	n=$(inputs | wc -l)
	[ "$n" -gt 0 ] || { echo "no fuzz inputs" >&2; exit 1; }
	for t in $targets; do
		echo "== $t: $n inputs"
		# shellcheck disable=SC2046
		"$b/$t" -runs=1 -timeout=60 -rss_limit_mb=2048 $(inputs) \
			>"$b/$t.log" 2>&1 || {
			tail -n 80 "$b/$t.log"
			exit 1
		}
		grep -c '^Executed ' "$b/$t.log"
	done
	;;
repro)
	shift
	rc=0
	for t in $targets; do
		echo "== $t"
		"$b/$t" -runs=1 "$@" 2>&1 | grep -v '^INFO:' || rc=1
	done
	exit $rc
	;;
run)
	secs=${2:-600}
	only=${3:-}
	mkdir -p "$b/art"
	for t in $targets; do
		[ -z "$only" ] || [ "$t" = "$only" ] || continue
		mkdir -p "$b/corpus/$t"
		echo "== $t: ${secs}s"
		"$b/$t" -max_total_time="$secs" -timeout=10 -rss_limit_mb=2048 \
			-max_len=2400000 -artifact_prefix="$b/art/$t-" \
			-dict=tests/fuzz/ext4.dict \
			-print_final_stats=1 "$b/corpus/$t" "$b/seeds" \
			"$b/crashes" 2>&1 | tail -n 40 || true
	done
	ls -l "$b/art"
	[ -z "$(ls -A "$b/art")" ]
	;;
*)
	echo "unknown mode '$mode'" >&2
	exit 2
	;;
esac
