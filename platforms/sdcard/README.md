Boards with an SD card on SPI
=============================

Bare-metal code for real microcontroller boards with a micro SD card on
SPI, no vendor SDK or RTOS. Every board is one directory of `platforms/`
with ~150 lines of register level code; this directory is what they share.

| Board | MCU | Core |
|---|---|---|
| [nucleo_f401re](../nucleo_f401re) | STM32F401RE | Cortex-M4F |
| [nucleo_g071rb](../nucleo_g071rb) | STM32G071RB | Cortex-M0+ |
| [nucleo_l552ze_q](../nucleo_l552ze_q) | STM32L552ZE | Cortex-M33 |
| [nrf52840dk](../nrf52840dk) | nRF52840 | Cortex-M4F |

| File | What |
|---|---|
| [board.h](board.h) | what a board implements: clocks and pins (`board_init`), a polled console UART, the SPI bus to the card |
| [startup.c](startup.c) | vector table, `.data`/`.bss` set-up, a fault handler that prints the faulting PC, newlib system calls (`_sbrk`, `_write` to the UART), heap and stack high water marks ([sysmem.h](sysmem.h)) |
| [cortex-m.ld](cortex-m.ld) | linker script; the board's `memory.ld` has the chip's flash and RAM sizes and the stack reservation |
| [sd_spi.c](sd_spi.c) | the SD card in SPI mode: identification, CRC protected commands, single and multi block reads and writes |
| [sd_blockdev.c](sd_blockdev.c) | the card, and its MBR partitions, as lwext4 block devices |
| [platform.c](platform.c) | the platform API ([platform.h](../platform.h)) for the [example firmware](../../examples/firmware/README.md): `platform_disk()` is the whole card (with `cut=<n>` for power cuts in tests), `platform_cmdline()` prints `READY` and reads a line from the console, `platform_exit()` prints `EXIT <status>` |

Each board directory has `board.c`, `memory.ld`, `board.cmake` (compiler
flags, lwext4 options, Renode names) and `board.repl` (the board for
[Renode](https://renode.io), which CI runs the firmware on, SD card
included). [examples/baremetal-sdcard](../../examples/baremetal-sdcard/README.md)
builds and tests them, with the example applications (`apps/<app>.hex`).
