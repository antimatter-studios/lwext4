# env: arm-none-eabi
# SPDX-License-Identifier: BSD-3-Clause
# Build the examples/baremetal-sdcard firmware for one board, exactly as a
# user would, and collect the flashable images.
#
#   ci/run.sh baremetal-sdcard-build <board> [extra cmake -D options]
#
# Boards: the directories in examples/baremetal-sdcard/boards.
# Output: examples/baremetal-sdcard/dist/<board>/
#   lwext4-example-<board>.hex   Intel HEX, for st-flash/nrfjprog/OpenOCD or
#                                drag and drop onto the board's USB drive
#   lwext4-example-<board>.bin   raw image, write at the start of the flash
#   lwext4-example-<board>.elf   with symbols, for gdb
#   footprint.txt                flash/RAM use per component
board=${1:?usage: baremetal-sdcard-build.sh <board> [cmake options]}
shift
. ci/scripts/common.sh

proj=examples/baremetal-sdcard
build=$proj/build-$board
dist=$proj/dist/$board

rm -rf "$build"
cmake -S "$proj" -B "$build" -DBOARD="$board" "$@"
cmake --build "$build" -j"$(ci_jobs)"

rm -rf "$dist"
mkdir -p "$dist"
for ext in hex bin elf; do
	cp "$build/firmware.$ext" "$dist/lwext4-example-$board.$ext"
done
cp "$build/footprint.txt" "$dist/"

echo
arm-none-eabi-gcc --version | head -n 1
arm-none-eabi-size "$build/firmware.elf"
echo
cat "$dist/footprint.txt"
echo "Flashable images in $dist:"
ls -l "$dist"
