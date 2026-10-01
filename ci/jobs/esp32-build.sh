# env: esp-idf
# SPDX-License-Identifier: BSD-3-Clause
# Build the lwext4 ESP-IDF example for one chip with idf.py, exactly as a
# user would, and collect the flashable images.
#
#   ci/run.sh esp32-build <esp32|esp32c3|esp32s3> [sdspi]
#
# Output: examples/esp-idf/dist/<variant>/
#   bootloader/bootloader.bin, partition_table/partition-table.bin,
#   lwext4_example.bin, ext4host.img   the individual images
#   flash_args                         their offsets (esptool @flash_args)
#   lwext4-example-<variant>.bin       everything merged, write at 0x0
target=${1:?usage: esp32-build.sh <esp32|esp32c3|esp32s3> [sdspi]}
variant=$target
defaults="sdkconfig.defaults"
if [ "${2:-}" = sdspi ]; then
	# SD card on a SPI bus (the usual hobby "micro SD module"). QEMU does
	# not emulate the general purpose SPI controllers, so this variant is
	# built but only runs on real hardware.
	variant=$target-sdspi
	defaults="sdkconfig.defaults;sdkconfig.sdspi"
fi

proj=examples/esp-idf
build=build-$variant
dist=dist/$variant

cd "$proj"
# IDF also applies sdkconfig.defaults.<target> when it exists.
idf.py -B "$build" -D SDKCONFIG="$build/sdkconfig" \
	-D SDKCONFIG_DEFAULTS="$defaults" -D IDF_TARGET="$target" build

rm -rf "$dist"
mkdir -p "$dist"
# flash_args lists "<offset> <file>" pairs relative to the build directory;
# keep that layout so "esptool.py write_flash @flash_args" works in dist too.
(cd "$build" && sed -n 's/^0x[0-9a-fA-F]* //p' flash_args) | while read -r f; do
	mkdir -p "$dist/$(dirname "$f")"
	cp "$build/$f" "$dist/$f"
done
cp "$build/flash_args" "$build/flasher_args.json" "$dist/"
(cd "$build" && python -m esptool --chip "$target" merge_bin \
	-o "../$dist/lwext4-example-$variant.bin" @flash_args)
echo "Flashable images in $proj/$dist:"
ls -l "$dist"
