# env: zephyr
# SPDX-License-Identifier: BSD-3-Clause
# Build the lwext4 Zephyr example for one board and run it in QEMU with a
# blank SD card image: the firmware formats the card, works on it and must
# print "LWEXT4-TEST: PASS"; then e2fsck -fn and debugfs check the card
# image on the host.
#
#   ci/run.sh zephyr-qemu <board>
#
# Console log and card image: examples/zephyr/build-<board>/qemu-test/
board=${1:?usage: zephyr-qemu.sh <board>}

bash ci/jobs/zephyr-build.sh "$board"
qemu-system-arm --version | head -n 1
python3 examples/zephyr/host/run_qemu_test.py --build-dir "examples/zephyr/build-$board"
