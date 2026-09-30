# SPDX-License-Identifier: BSD-3-Clause
#
# lwext4 on Zephyr: an ordinary Zephyr application.
#
#   west build -b qemu_cortex_m3 examples/zephyr
#
# or, with nothing but docker installed (see README.md):
#
#   ci/run.sh zephyr-build qemu_cortex_m3
#   ci/run.sh zephyr-qemu qemu_cortex_m3
cmake_minimum_required(VERSION 3.20.0)

# lwext4 is a Zephyr module (zephyr/module.yml at the top of this
# repository). A west workspace would list it in its manifest; this example
# points Zephyr at the repository it lives in instead.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lwext4_zephyr_example C)

target_sources(app PRIVATE src/main.c)

# Board support the upstream board lacks (see drivers/fixed_clock_cell.c)
target_sources(app PRIVATE drivers/fixed_clock_cell.c)
