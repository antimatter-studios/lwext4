# SPDX-License-Identifier: BSD-3-Clause
set(BOARD_CPU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)
set(BOARD_DEFS "")
set(BOARD_RENODE_UART sysbus.usart2)
set(BOARD_RENODE_SPI sysbus.spi1)
# 36 KiB of RAM: with 4 KiB file system blocks lwext4 needs ~66 KiB of heap
# (block cache plus journal transactions), see README.md. This board only
# runs the tests with 1 KiB blocks.
set(BOARD_TEST_EXCLUDE mkfs4k)
