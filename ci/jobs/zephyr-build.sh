# env: zephyr
# SPDX-License-Identifier: BSD-3-Clause
# Build the examples/zephyr application for one board with west, exactly as
# a user would, and collect the images.
#
#   ci/run.sh zephyr-build <board> [extra CMake -D options]
#
# Boards: those with a boards/<name>.overlay in examples/zephyr, where
# <name> is the board with '/' replaced by '_' (mps2/an385: mps2_an385).
# Output: examples/zephyr/dist/<name>/
#   lwext4-example-<name>.elf   with symbols: QEMU -kernel, gdb
#   lwext4-example-<name>.bin   raw image, write at the start of the flash
#   lwext4-example-<name>.hex   Intel HEX, for flashing tools
#   config                       the complete Kconfig configuration
board=${1:?usage: zephyr-build.sh <board> [cmake options]}
shift
. ci/scripts/common.sh

proj=examples/zephyr
name=$(echo "$board" | tr / _)
build=$proj/build-$name
dist=$proj/dist/$name

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
	cp "$build/zephyr/zephyr.$ext" "$dist/lwext4-example-$name.$ext"
done
cp "$build/zephyr/.config" "$dist/config"
echo "Images in $dist:"
ls -l "$dist"
