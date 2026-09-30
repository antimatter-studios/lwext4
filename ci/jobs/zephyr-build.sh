# env: zephyr
# SPDX-License-Identifier: BSD-3-Clause
# Build the examples/zephyr application for one board with west, exactly as
# a user would, and collect the images.
#
#   ci/run.sh zephyr-build <board> [extra CMake -D options]
#
# Boards: those with a boards/<board>.overlay in examples/zephyr.
# Output: examples/zephyr/dist/<board>/
#   lwext4-example-<board>.elf   with symbols: QEMU -kernel, gdb
#   lwext4-example-<board>.bin   raw image, write at the start of the flash
#   lwext4-example-<board>.hex   Intel HEX, for flashing tools
#   config                       the complete Kconfig configuration
board=${1:?usage: zephyr-build.sh <board> [cmake options]}
shift
. ci/scripts/common.sh

proj=examples/zephyr
build=$proj/build-$board
dist=$proj/dist/$board

echo "Zephyr $(cat "$ZEPHYR_BASE/VERSION" | sed -n 's/^VERSION_\(MAJOR\|MINOR\) = //p;s/^PATCHLEVEL = //p' | paste -sd.), SDK $(cat "$ZEPHYR_SDK_INSTALL_DIR/sdk_version")"
# Zephyr caches compiler checks below $HOME/.cache when it exists, else in
# the (read only) Zephyr tree
mkdir -p "$HOME/.cache"
rm -rf "$build"
west build --pristine always --board "$board" --build-dir "$build" \
	--build-opt=-j"$(ci_jobs)" "$proj" -- "$@"

rm -rf "$dist"
mkdir -p "$dist"
for ext in elf bin hex; do
	cp "$build/zephyr/zephyr.$ext" "$dist/lwext4-example-$board.$ext"
done
cp "$build/zephyr/.config" "$dist/config"
echo "Images in $dist:"
ls -l "$dist"
