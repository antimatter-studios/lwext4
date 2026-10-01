# SPDX-License-Identifier: BSD-3-Clause
# env: mingw
# platform: linux/amd64
# Release package for Windows x86_64 (MinGW-w64): static library and DLL,
# headers and the fs_test tools, plus the CTest results of the packaged
# build run under Wine.
. ci/scripts/common.sh
. ci/scripts/package-common.sh
export WINEPREFIX="$HOME/wine-prefix"
mkdir -p "$WINEPREFIX"
wineboot -i >/dev/null 2>&1 || echo "wineboot exited with $?"

version=$(pkg_version)
name=lwext4-$version-windows-x86_64
stage=$PWD/build-ci/stage/$name
rm -rf "$stage"

configure()
{
	cmake -S . -B "$1" -DCMAKE_TOOLCHAIN_FILE=toolchain/mingw.cmake \
		-DCMAKE_CROSSCOMPILING_EMULATOR=wine \
		-DCMAKE_BUILD_TYPE=Release -DLWEXT4_BUILD_SHARED_LIB="$2" \
		-DVERSION="$version"
}

# Static library, headers, tools.
build=$(ci_build_dir package-mingw-OFF)
configure "$build" OFF
cmake --build "$build" -j"$(ci_jobs)"
cmake --install "$build" --prefix "$stage"

# DLL and import library only (see package-linux.sh).
build=$(ci_build_dir package-mingw-ON)
configure "$build" ON
cmake --build "$build" --target lwext4 -j"$(ci_jobs)"
mkdir -p "$stage/bin"
cp "$build"/src/*.dll "$stage/bin/"
cp "$build"/src/*.dll.a "$stage/lib/"

mkdir -p "$stage/test-results"
ctest --test-dir build-ci/package-mingw-OFF \
	--output-junit "$stage/test-results/ctest-windows-x86_64.xml"

cp "$stage"/test-results/*.xml "$(pkg_dist)/"
pkg_tar "$stage" "$name"
