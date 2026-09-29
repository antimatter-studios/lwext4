# SPDX-License-Identifier: BSD-3-Clause
set(BOARD_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16)
set(BOARD_DEFS CONFIG_UNALIGNED_ACCESS=1)
set(BOARD_RENODE_UART sysbus.usart2)
set(BOARD_RENODE_SPI sysbus.spi1)
