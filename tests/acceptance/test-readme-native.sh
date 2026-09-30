#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
#
# Runs README.md's own commands, in README order, on the build host, and
# checks their results with e2fsprogs:
#   Getting started, Compile & install tools, Using the installed library,
#   lwext4-generic demo application, Run automatic tests (make test, make
#   test_all), Using lwext4-mkfs tool, Run regression tests (native part).
set -eu
. "$(dirname "$0")/lib.sh"

cd "$TOP_DIR"
blocks="sh $ACC_DIR/readme-blocks.sh"
README_DESTDIR="$WORK/install"
export README_DESTDIR

# lwext4-generic always writes /test1 (rw_count chunks of rw_size bytes,
# chunk i filled with '0' + i % 10) and /hello.txt.
check_generic_output()
{
	img=$1
	: >"$WORK/expect.bin"
	for i in 0 1 2 3 4 5 6 7 8 9; do
		head -c 1048576 /dev/zero | tr '\0' "$i" >>"$WORK/expect.bin"
	done
	dbg "$img" "dump /test1 $WORK/test1.bin" >/dev/null
	cmp "$WORK/test1.bin" "$WORK/expect.bin" ||
		die "$img: /test1 is not what lwext4-generic wrote"
	[ "$(dbg "$img" "cat /hello.txt")" = "Hello World !" ] ||
		die "$img: /hello.txt differs"
	pass "debugfs reads back what lwext4-generic wrote to $img"
}

step "Getting started"
$blocks run getting-started#1
# Step 2 runs in build_generic, where step 1 left the reader.
rm -f build_generic/disk.img
README_BLOCK_DIR="$TOP_DIR/build_generic" $blocks run getting-started#2
fsck_clean build_generic/disk.img "the basic example's image"
[ -n "$(dir_names build_generic/disk.img /docs)" ] ||
	die "the basic example wrote nothing to /docs"
pass "the basic example writes /docs: $(dir_names build_generic/disk.img /docs | tr '\n' ' ')"

step "Compile & install tools"
$blocks run compile-install-tools#1
for tool in lwext4-generic lwext4-mkfs lwext4-mbr lwext4-server lwext4-client; do
	[ -x "$README_DESTDIR/usr/local/bin/$tool" ] ||
		die "make install did not install $tool"
	"$README_DESTDIR/usr/local/bin/$tool" --version >/dev/null ||
		die "installed $tool does not run"
done
pass "make install installs lwext4-generic, -mkfs, -mbr, -server and -client"

step "Using the installed library"
prefix="$README_DESTDIR/usr/local"
# The layout block: "<dir>/  <file>, <file>, ..." or "<file>  <description>";
# every file name in it (parenthesised alternatives aside) must be installed.
sh "$ACC_DIR/readme-blocks.sh" show using-the-installed-library#1 |
	sed 's/([^)]*)//g; s/,/ /g' | while read -r first rest; do
	case "$first" in
	*/) for name in $rest; do
		case "$name" in
		*.*[a-z]) [ -e "$prefix/$first$name" ] ||
			die "make install did not install $first$name" ;;
		esac
	    done ;;
	*) [ -e "$prefix/$first" ] || die "make install did not install $first" ;;
	esac
done
pass "make install installs the layout README.md shows"
# The build commands, each on its own, with app.c a program using the
# installed headers and libraries (tests/package/consumer.c).
app="$WORK/app"
rm -rf "$app"
mkdir -p "$app"
cp tests/package/consumer.c "$app/app.c"
sh "$ACC_DIR/readme-blocks.sh" show using-the-installed-library#2 >"$WORK/cc.txt"
while read -r cmd; do
	rm -f "$app/a.out" "$app/app.img"
	(cd "$app" && PREFIX="$prefix" PKG_CONFIG_PATH="$prefix/lib/pkgconfig" \
		bash -exc "$cmd") || die "does not build: $cmd"
	"$app/a.out" "$app/app.img" || die "the program built with '$cmd' fails"
	fsck_clean "$app/app.img" "image written by the program built with '$cmd'"
done <"$WORK/cc.txt"
# The CMake block, in a project that adds the target "app".
mkdir -p "$app/cmake"
{
	echo 'cmake_minimum_required(VERSION 3.10)'
	echo 'project(app C)'
	echo 'add_executable(app ../app.c)'
	sh "$ACC_DIR/readme-blocks.sh" show using-the-installed-library#3
} >"$app/cmake/CMakeLists.txt"
cmake -S "$app/cmake" -B "$app/cmake/build" -DCMAKE_PREFIX_PATH="$prefix" \
	>"$WORK/cmake-app.log" 2>&1 &&
	cmake --build "$app/cmake/build" >>"$WORK/cmake-app.log" 2>&1 ||
	{ cat "$WORK/cmake-app.log"; die "the CMake block does not build"; }
rm -f "$app/app.img"
"$app/cmake/build/app" "$app/app.img" || die "the program built with CMake fails"
fsck_clean "$app/app.img" "image written by the program built with CMake"
pass "programs built with pkg-config, -I/-L and find_package(lwext4) work"

step "Compile & install tools: another prefix"
home="$WORK/home"
rm -rf "$home"
mkdir -p "$home"
HOME="$home" $blocks run compile-install-tools#2
for f in bin/lwext4-mkfs bin/lwext4-generic lib/liblwext4.a \
	 include/lwext4/ext4.h lib/pkgconfig/lwext4.pc; do
	[ -e "$home/.local/$f" ] || die "cmake --install --prefix did not install $f"
done
pass "cmake --install --prefix \$HOME/.local installs below \$HOME/.local"

step "Run automatic tests: make test"
$blocks run run-automatic-tests#1
pass "make test (server/client suite t0..t20 on ext2, ext3 and ext4)"
for fs in ext2 ext3 ext4; do
	img=ext_images/$fs
	fsck_clean "$img" "$fs after make test"
	[ "$(dir_names "$img" / | tr '\n' ' ')" = "lost+found " ] ||
		die "$fs: make test left files behind"
	pass "$fs: make test removed everything it created"
done

step "lwext4-generic demo application"
$blocks run lwext4-generic-demo-application#1
for fs in ext2 ext3 ext4; do
	fsck_clean "ext_images/$fs" "$fs after lwext4-generic"
	check_generic_output "ext_images/$fs"
done
$blocks run lwext4-generic-demo-application#2 >"$WORK/generic-help.txt"
grep -q -- '--sbstat' "$WORK/generic-help.txt" ||
	die "lwext4-generic --help does not show the option set"
pass "lwext4-generic --help shows the full option set"

step "Using lwext4-mkfs tool"
$blocks run using-lwext4-mkfs-tool#1
[ "$(wc -c <ext_image)" -eq 1073741824 ] || die "ext_image is not 1 GiB"
for e in 2 3 4; do
	$blocks run "using-lwext4-mkfs-tool#$e"
	fsck_clean ext_image "lwext4-mkfs -i ext_image -e $e"
	case $e in
	2) ! has_feature ext_image has_journal && ! has_feature ext_image extent ;;
	3) has_feature ext_image has_journal && ! has_feature ext_image extent ;;
	4) has_feature ext_image has_journal && has_feature ext_image extent ;;
	esac || die "-e $e created: $(features ext_image)"
	pass "lwext4-mkfs -e $e created ext$e ($(features ext_image))"
	"$README_DESTDIR/usr/local/bin/lwext4-generic" -i ext_image >/dev/null
	fsck_clean ext_image "lwext4-generic on the ext$e lwext4-mkfs created"
	check_generic_output ext_image
done
$blocks run using-lwext4-mkfs-tool#5 >"$WORK/mkfs-help.txt"
grep -q -- '--block' "$WORK/mkfs-help.txt" ||
	die "lwext4-mkfs --help does not show the option set"
pass "lwext4-mkfs --help shows the full option set"
rm -f ext_image

step "Run automatic tests: make test_all"
$blocks run run-automatic-tests#2
pass "make test_all (t0..t26 on 1 GiB ext2, ext3, ext4 images, then e2fsck)"
for fs in ext2 ext3 ext4; do
	[ "$(dir_names "ext_images/$fs" / | tr '\n' ' ')" = "lost+found " ] ||
		die "$fs: make test_all left files behind"
done
pass "make test_all removed everything it created"
rm -rf ext_images

step "Run regression tests"
$blocks run run-regression-tests#1
pass "ctest passes"

finish
