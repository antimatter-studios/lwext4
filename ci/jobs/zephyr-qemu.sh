# env: zephyr
# SPDX-License-Identifier: BSD-3-Clause
# Build the lwext4 Zephyr example for one board and run it in QEMU: the
# firmware formats its RAM disk, works on it and must print
# "LWEXT4-TEST: PASS"; then the disk is copied out of QEMU's memory and
# e2fsck -fn and debugfs check it on the host.
#
#   ci/run.sh zephyr-qemu <board>
#
# Console log and disk image: examples/zephyr/build-<name>/qemu-test/
# (<name>: the board with '/' replaced by '_')
board=${1:?usage: zephyr-qemu.sh <board>}

bash ci/jobs/zephyr-build.sh "$board"
qemu-system-arm --version | head -n 1
python3 examples/zephyr/host/run_qemu_test.py \
	--build-dir "examples/zephyr/build-$(echo "$board" | tr / _)"
