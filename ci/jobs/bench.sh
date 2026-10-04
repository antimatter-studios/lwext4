# SPDX-License-Identifier: BSD-3-Clause
# env: arm-none-eabi
# Benchmark of lwext4 (tests/bench, fork issue #165) on the emulated MPS2
# boards: per operation the instructions (QEMU -icount), block reads and
# writes, peak heap and peak stack, and the code size of the library.
#
#   ci/run.sh bench [cortex-m0|cortex-m3|cortex-m4|cortex-m7 ...]
#       measure (default: all four), write build-ci/bench/<toolchain>.txt
#       and compare with the dataset in docs/performance/data: fail if a
#       figure got worse by more than the margin, or if the pages are not
#       the ones the dataset makes
#   ci/run.sh bench update
#       measure all four and make the result the new dataset: rewrites
#       docs/performance/data and the generated pages, to commit
. ci/scripts/common.sh
out=build-ci/bench
data=docs/performance/data
mkdir -p "$out"
mode=check
if [ "${1:-}" = update ]; then
	mode=update
	shift
fi
[ $# -gt 0 ] || set -- cortex-m0 cortex-m3 cortex-m4 cortex-m7

for toolchain in "$@"; do
	case "$toolchain" in
	cortex-m0|cortex-m0+|cortex-m3) machine=mps2-an385 ;;
	cortex-m4|cortex-m4f)           machine=mps2-an386 ;;
	cortex-m7)                      machine=mps2-an500 ;;
	*) echo "unknown toolchain '$toolchain'" >&2; exit 2 ;;
	esac
	build=$(ci_build_dir "bench-$toolchain")
	cmake -S . -B "$build" \
		-DCMAKE_TOOLCHAIN_FILE="toolchain/$toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=Release \
		-DLWEXT4_CONFIG="CONFIG_DEBUG_PRINTF=0;CONFIG_DEBUG_ASSERT=0" \
		-DLWEXT4_QEMU_MACHINE="$machine" >/dev/null
	cmake --build "$build" -j"$(ci_jobs)" --target bench.elf >/dev/null
	ctest --test-dir "$build" -R '^bench$' -V >"$build/bench.log" 2>&1 || {
		tail -n 40 "$build/bench.log"
		exit 1
	}
	{
		sed -n 's/^[0-9]*: \(BENCH .*\)/\1/p' "$build/bench.log"
		# Code and static data of the library (text, data, bss)
		arm-none-eabi-size -t "$build/src/liblwext4.a" | tail -n 1 |
			awk -v t="$toolchain" '{ print "SIZE " t " text " $1 " data " $2 " bss " $3 }'
	} >"$out/$toolchain.txt"
	cat "$out/$toolchain.txt"
	# Every measurement is of this platform (the pages are made by name)
	if awk -v t="$toolchain" '$2 != t { bad = 1 } END { exit !bad }' 		"$out/$toolchain.txt" || ! grep -q '^BENCH ' "$out/$toolchain.txt"
	then
		echo "bench: $toolchain: no measurements, or of another platform" >&2
		exit 1
	fi
done

if [ "$mode" = update ]; then
	mkdir -p "$data"
	for toolchain in "$@"; do
		cp "$out/$toolchain.txt" "$data/$toolchain.txt"
	done
	python3 tests/bench/table.py docs "$data" docs/performance README.md
	echo "New dataset in $data and docs/performance: review and commit it"
	exit 0
fi

rc=0
python3 tests/bench/table.py check "$data"/*.txt -- "$out"/*.txt || rc=1
python3 tests/bench/table.py verify "$data" docs/performance README.md || rc=1
exit $rc
