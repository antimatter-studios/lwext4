# env: arm-none-eabi
# SPDX-License-Identifier: BSD-3-Clause
# README.md "Build for a microcontroller": its cortex-m4 block word for
# word, and the same commands for cortex-m0 and cortex-m3 (the README says
# every toolchain builds that way); then the cortex-m4 memory footprint
# numbers, and tests/baremetal on QEMU.
set -e
sh tests/acceptance/readme-blocks.sh run build-for-a-microcontroller#1
for cpu in cortex-m0 cortex-m3; do
	make "$cpu"
	(cd "build_$cpu" && make lwext4)
done
sh tests/acceptance/test-cortex-m.sh
