# SPDX-License-Identifier: BSD-3-Clause
# env: arm-none-eabi
# Release package for bare-metal Cortex-M: liblwext4.a and the headers
# (including the generated ext4_config.h of that build) for every Cortex-M
# toolchain file, the example firmware of each (examples/firmware, built
# for its QEMU MPS2 board) with their sources and platforms/, plus the
# CTest results of each build (the test firmware and the examples, run in
# QEMU, see cortex-m.sh).
. ci/scripts/common.sh
. ci/scripts/package-common.sh
version=$(pkg_version)
name=lwext4-$version-cortex-m
stage=$PWD/build-ci/stage/$name
rm -rf "$stage"
mkdir -p "$stage/test-results"

for cpu in cortex-m0 cortex-m0+ cortex-m3 cortex-m4 cortex-m4f cortex-m7; do
	build=$(ci_build_dir "package-$cpu")
	cmake -S . -B "$build" -DCMAKE_TOOLCHAIN_FILE="toolchain/$cpu.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DVERSION="$version"
	cmake --build "$build" -j"$(ci_jobs)"
	ctest --test-dir "$build" \
		--output-junit "$stage/test-results/ctest-$cpu.xml"
	mkdir -p "$stage/$cpu/lib" "$stage/$cpu/include/lwext4"
	cp "$build/src/liblwext4.a" "$stage/$cpu/lib/"
	cp -R include/. "$stage/$cpu/include/lwext4/"
	cp -R "$build/include/." "$stage/$cpu/include/lwext4/"
	mkdir -p "$stage/$cpu/firmware"
	cp "$build"/examples/firmware/*.elf "$stage/$cpu/firmware/"
done

# The sources of the example firmware, and the board code they build on
mkdir -p "$stage/examples/firmware" "$stage/platforms"
cp examples/firmware/*.c examples/firmware/README.md "$stage/examples/firmware/"
cp -R platforms/. "$stage/platforms/"

cp "$stage"/test-results/*.xml "$(pkg_dist)/"
pkg_tar "$stage" "$name"
