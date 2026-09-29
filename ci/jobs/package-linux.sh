# SPDX-License-Identifier: BSD-3-Clause
# env: cross-linux
# Release package for one Linux architecture: static and shared library,
# headers and the fs_test tools, plus the CTest results (JUnit XML) of the
# packaged build, run under qemu-user where the architecture is foreign.
#
#   ci/run.sh package-linux <x86_64|aarch64|armhf|i686|riscv64|ppc64le|s390x|powerpc|mips>
arch=${1:?usage: package-linux.sh <arch>}
case "$arch" in
x86_64)  triple=x86_64-linux-gnu;      qemu=qemu-x86_64 ;;
aarch64) triple=aarch64-linux-gnu;     qemu=qemu-aarch64 ;;
armhf)   triple=arm-linux-gnueabihf;   qemu=qemu-arm ;;
i686)    triple=i686-linux-gnu;        qemu=qemu-i386 ;;
riscv64) triple=riscv64-linux-gnu;     qemu=qemu-riscv64 ;;
ppc64le) triple=powerpc64le-linux-gnu; qemu=qemu-ppc64le ;;
s390x)   triple=s390x-linux-gnu;       qemu=qemu-s390x ;;
powerpc) triple=powerpc-linux-gnu;     qemu=qemu-ppc ;;
mips)    triple=mips-linux-gnu;        qemu=qemu-mips ;;
*) echo "unknown arch '$arch'" >&2; exit 2 ;;
esac
# The build machine's own architecture runs natively.
case "$(uname -m)" in
x86_64)  [ "$arch" = x86_64 ] && qemu= ;;
aarch64) [ "$arch" = aarch64 ] && qemu= ;;
esac
ldflags=
if [ "$qemu" = qemu-arm ] && [ "$(getconf PAGESIZE)" -gt 4096 ]; then
	ldflags=-static # see qemu-user.sh
fi

. ci/scripts/common.sh
. ci/scripts/package-common.sh
version=$(pkg_version)
name=lwext4-$version-linux-$arch
stage=$PWD/build-ci/stage/$name
rm -rf "$stage"

configure()
{
	cmake -S . -B "$1" \
		-DCMAKE_TOOLCHAIN_FILE=toolchain/linux-cross.cmake \
		-DCROSS_TRIPLE="$triple" -DCROSS_EMULATOR="$qemu" \
		-DCROSS_SYSROOT="/usr/$triple" -DCROSS_LDFLAGS="$ldflags" \
		-DCMAKE_BUILD_TYPE=Release -DLWEXT4_BUILD_SHARED_LIB="$2" \
		-DVERSION="$version"
}

# Static library, headers, tools (linked statically against lwext4).
build=$(ci_build_dir "package-linux-$arch-OFF")
configure "$build" OFF
cmake --build "$build" -j"$(ci_jobs)"
cmake --install "$build" --prefix "$stage"

# Shared library only: the lib_size target and the tools assume a static
# lwext4 (src/CMakeLists.txt runs size on liblwext4.a).
build=$(ci_build_dir "package-linux-$arch-ON")
configure "$build" ON
cmake --build "$build" --target lwext4 -j"$(ci_jobs)"
cp -a "$build"/src/liblwext4.so* "$stage/lib/"

# Test the packaged (static) configuration and ship the results.
build=build-ci/package-linux-$arch-OFF
mkdir -p "$stage/test-results"
ctest --test-dir "$build" --output-junit "$stage/test-results/ctest-linux-$arch.xml"

cp "$stage"/test-results/*.xml "$(pkg_dist)/"
pkg_tar "$stage" "$name"
