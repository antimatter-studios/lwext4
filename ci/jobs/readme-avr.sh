# env: avr
# SPDX-License-Identifier: BSD-3-Clause
# README.md "Build for a microcontroller" for the avrxmega7 toolchain: the
# commands of its block with that toolchain's name.
set -e
make avrxmega7
(cd build_avrxmega7 && make lwext4)
avr-objdump -f build_avrxmega7/src/liblwext4.a | grep -q 'file format elf32-avr'
avr-size -t build_avrxmega7/src/liblwext4.a | tail -n 1
echo "README build-for-a-microcontroller with avrxmega7: AVR library built"
