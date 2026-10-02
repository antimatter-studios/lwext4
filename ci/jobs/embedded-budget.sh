# SPDX-License-Identifier: BSD-3-Clause
# env: arm-none-eabi
# What the library needs on a microcontroller, checked against
# ci/embedded-budget.txt (see there): the external symbols the Cortex-M0
# build of liblwext4.a calls (C library functions, compiler helpers such as
# 64-bit division) and the stack frame of each of its functions
# (-fstack-usage), with debug output and assertions off as firmware ships
# it (with them on, the library also calls printf and fflush). A new external symbol, a variable sized frame (VLA,
# alloca) or a frame larger than the ceiling fails the job.
#
#   ci/run.sh embedded-budget
. ci/scripts/common.sh
build=$(ci_build_dir embedded-budget)
arm-none-eabi-gcc --version | head -n 1

cmake -S . -B "$build" \
	-DCMAKE_TOOLCHAIN_FILE=toolchain/cortex-m0.cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DLWEXT4_BUILD_TESTS=OFF \
	-DLWEXT4_EXTRA_C_FLAGS="-fstack-usage -DCONFIG_DEBUG_PRINTF=0 -DCONFIG_DEBUG_ASSERT=0"
cmake --build "$build" -j"$(ci_jobs)" --target lwext4

lib=$(find "$build" -name liblwext4.a | head -n 1)
[ -n "$lib" ] || { echo "no liblwext4.a in $build" >&2; exit 1; }
arm-none-eabi-nm -u "$lib" > "$build/undefined.txt"
arm-none-eabi-nm --defined-only "$lib" > "$build/defined.txt"
find "$build" -name '*.su' -exec cat {} + > "$build/stack.txt"
python3 ci/scripts/check-embedded-budget.py ci/embedded-budget.txt \
	"$build/undefined.txt" "$build/defined.txt" "$build/stack.txt" \
	"$build/summary.md"
