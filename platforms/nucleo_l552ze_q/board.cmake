# SPDX-License-Identifier: BSD-3-Clause
set(BOARD_CPU_FLAGS -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16)
set(BOARD_DEFS CONFIG_UNALIGNED_ACCESS=1)
set(BOARD_RENODE_UART sysbus.lpuart1)
set(BOARD_RENODE_SPI sysbus.spi1)
