# env: esp-idf
# SPDX-License-Identifier: BSD-3-Clause
# Build the lwext4 ESP-IDF example for one chip and run the very image a
# user would flash (dist/<target>/lwext4-example-<target>.bin) in
# Espressif's QEMU, then check the filesystems it wrote with e2fsck/debugfs.
#
#   ci/run.sh esp32-qemu <esp32|esp32c3|esp32s3>
target=${1:?usage: esp32-qemu.sh <esp32|esp32c3|esp32s3>}

bash ci/jobs/esp32-build.sh "$target"
python3 examples/esp-idf/host/run_qemu_test.py \
	--build-dir "examples/esp-idf/build-$target" \
	--flash-image "examples/esp-idf/dist/$target/lwext4-example-$target.bin"
