# lwext4 on a microcontroller with an SD card (bare metal)

A complete firmware that puts lwext4 on hobby boards and talks to a micro SD
card over SPI: formats it, mounts it, writes and reads files through the
journal, survives power cuts. The same image is flashed onto the real board
and tested in [Renode](https://renode.io), which emulates the chip, its SPI
controller and an SD card.

| Board | MCU | Core | Flash / RAM | SD card SPI | Console |
|---|---|---|---|---|---|
| `nucleo_f401re` ST NUCLEO-F401RE | STM32F401RE | Cortex-M4F | 512 KiB / 96 KiB | SPI1, polled | USART2 (ST-LINK VCP) |
| `nucleo_g071rb` ST NUCLEO-G071RB | STM32G071RB | Cortex-M0+ | 128 KiB / 36 KiB | SPI1, polled | USART2 (ST-LINK VCP) |
| `nucleo_l552ze_q` ST NUCLEO-L552ZE-Q | STM32L552ZE | Cortex-M33 | 512 KiB / 256 KiB | SPI1, polled | LPUART1 (ST-LINK VCP) |
| `nrf52840dk` Nordic nRF52840 DK | nRF52840 | Cortex-M4F | 1 MiB / 256 KiB | SPIM2, EasyDMA | UARTE0 (J-Link VCP) |

No vendor SDK or RTOS is used: each board is ~150 lines of register level
code (`platforms/<board>/board.c`, see [platforms/](../../platforms/README.md)), so the example shows everything lwext4
needs and nothing else. Porting to another chip means writing that one file.

## Build it (only docker needed)

```sh
ci/run.sh baremetal-sdcard-build nucleo_f401re
```

runs CMake in the [arm-none-eabi](../../ci/envs/arm-none-eabi) container and
leaves in `examples/baremetal-sdcard/dist/nucleo_f401re/`:

| File | Use |
|---|---|
| `lwext4-example-<board>.hex` | Intel HEX: drag and drop, `st-flash`, `nrfjprog`, OpenOCD |
| `lwext4-example-<board>.bin` | raw image, starts at the beginning of the flash |
| `lwext4-example-<board>.elf` | the same with symbols, for gdb |
| `footprint.txt` | flash and RAM per component, lwext4 per source file |
| `apps/<app>.hex`, `.elf` | the [example applications](../firmware/README.md) (hello, datalogger, reader) for the board |

CI builds every board and publishes these as workflow artifacts
(`lwext4-example-<board>`). Without docker: `cmake -S examples/baremetal-sdcard
-B build -DBOARD=<board> && cmake --build build` with `arm-none-eabi-gcc` and
newlib on the PATH.

## Test it in the emulator

```sh
ci/run.sh renode-baremetal-sdcard nucleo_f401re
```

boots `dist/<board>/lwext4-example-<board>.hex` - the file you would flash -
in the [renode](../../ci/envs/renode) container and runs
[tests/renode/lwext4.robot](../../tests/renode/lwext4.robot):

1. **Mount Card Formatted On Host** - a 32 MiB card image with an MBR and an
   ext4 partition made by `mke2fs -d` (files, deep paths, a 300 entry htree
   directory). The firmware checks the host's files, runs the workload
   (directories, 48 small files, a 256 KiB file written in 1000 byte
   chunks, seeks, truncate, rename, remove, symlink, xattr), modifies host
   files, unmounts, remounts and verifies everything.
2. **Format Card On Device 1k/4k Blocks** - blank card: `ext4_mbr_write`
   partitions it, `ext4_mkfs` formats it, then the same workload.
3. **Power Cut During Writes** - the firmware creates, renames and removes
   files in a loop; the harness stops the emulation at six different points
   of virtual time (mid SPI transfer, mid journal commit, ...), boots a new
   machine with the same card and lets lwext4 replay its journal.
4. **The example applications** ([tests/renode/apps.robot](../../tests/renode/apps.robot)) -
   hello, reader and the datalogger of [examples/firmware](../firmware/README.md)
   on the board; the datalogger loses power at 13 different block writes
   and must keep every record it reported written.

After every run the card image is checked on the host by `e2fsck -fn` and
`debugfs` ([tests/renode/scripts/sdimage.py](../../tests/renode/scripts/sdimage.py)):
the files must be exactly what the firmware wrote. For power cut images
`e2fsck -fy` must also be able to replay lwext4's journal on its own.

## The pieces

The board code is in [platforms/](../../platforms/README.md), shared with
the other firmware of this repository:

```
platforms/sdcard/startup.c      vector table, .data/.bss init, fault handler
                                that prints the faulting PC, newlib system
                                calls (_sbrk, _write), heap and stack high
                                water marks
platforms/sdcard/cortex-m.ld    linker script; <board>/memory.ld has the
                                chip's real flash/RAM sizes and the stack
                                reservation
platforms/sdcard/sd_spi.c       SD card SPI mode driver: CMD0/CMD8/ACMD41/
                                CMD58 identification, CRC on (CMD59), CSD
                                capacity, single and multi block
                                reads/writes (CMD17/18/24/25)
platforms/sdcard/sd_blockdev.c  the lwext4 block device on top of it, and
                                the MBR partitions (ext4_mbr_scan)
platforms/<board>/              board.c (clocks, pins, UART, SPI),
                                memory.ld, board.cmake (CPU flags),
                                board.repl (Renode description)
```

and here:

```
src/main.c          mount/umount, the test commands
src/workload.c      the file system workload
```

### lwext4 integration

lwext4 sees storage as a `struct ext4_blockdev` - open/close and
bread/bwrite of whole physical blocks. `platforms/sdcard/sd_blockdev.c` is the whole port:

```c
EXT4_BLOCKDEV_STATIC_INSTANCE(sd_card_bd, 512, 0, sd_bd_open,
                              sd_bd_bread, sd_bd_bwrite, sd_bd_close,
                              NULL, NULL);          /* no locks: one thread */

struct ext4_blockdev *part = sd_blockdev_partition(0);   /* MBR partition 1 */
ext4_device_register(part, "sd0p1");
ext4_mount("sd0p1", "/mp/", false);
ext4_recover("/mp/");               /* replay the journal after a crash */
ext4_journal_start("/mp/");
/* ext4_fopen/ext4_fwrite/ext4_dir_mk/... on "/mp/..." */
ext4_journal_stop("/mp/");
ext4_umount("/mp/");
```

### Configuration

lwext4 is configured with `CONFIG_*` defines ([include/ext4_config.h](../../include/ext4_config.h));
[CMakeLists.txt](CMakeLists.txt) sets them for a product-like build:

| Define | Here | Effect |
|---|---|---|
| `CONFIG_DEBUG_PRINTF` | 0 | no debug output, no format strings in flash |
| `CONFIG_DEBUG_ASSERT`, `CONFIG_HAVE_OWN_ASSERT` | 1, 0 | keep internal checks, reported through newlib `assert()` |
| `CONFIG_BLOCK_DEV_CACHE_SIZE` | 8 (default) | blocks kept in the block cache |
| `CONFIG_UNALIGNED_ACCESS` | 1 on M4/M33, 0 on M0+ | faster bitmap scans where the core allows unaligned loads |
| `CONFIG_EXTENTS_ENABLE`, `CONFIG_XATTR_ENABLE` | 1 (default) | ext4 extents and extended attributes |
| `WRITE_BACK_CACHE` (CMake option) | OFF | `ext4_cache_write_back()`: fewer card writes, but far more RAM (see below) |

Licensing: lwext4 is BSD-3-Clause except `src/ext4_extent.c` and
`src/ext4_xattr.c`, which are GPLv2. With extents or xattr support enabled
(the defaults, needed for ext4 file systems made by `mke2fs -t ext4`) those
two files are compiled in, and the firmware is subject to the GPLv2. For a
BSD-only build use ext2/ext3 file systems and set `CONFIG_EXTENTS_ENABLE=0`
and `CONFIG_XATTR_ENABLE=0`. This example's own files are BSD-3-Clause.

### Footprint

`-Os`, newlib-nano, GCC 14 (Debian `gcc-arm-none-eabi`). Flash is text +
rodata + data, measured from the link map (`footprint.txt`); heap and stack
are the high water marks the firmware measured while running the tests in
Renode (the `MEM:` lines of the log).

| Board (core) | lwext4 flash | whole firmware flash | static RAM | heap peak 1 KiB blocks | heap peak 4 KiB blocks | stack peak |
|---|---|---|---|---|---|---|
| nucleo_f401re (M4F) | 51,446 | 78,956 | 8,464 | 15,520 | 67,456 | 1,600 |
| nucleo_g071rb (M0+) | 63,660 | 90,848 | 8,464 | 15,520 | does not fit | 1,688 |
| nucleo_l552ze_q (M33) | 51,436 | 78,408 | 8,464 | 15,520 | 67,456 | 1,600 |
| nrf52840dk (M4F) | 51,446 | 79,016 | 8,464 | 15,520 | 67,456 | 1,600 |

All figures in bytes. Static RAM is the whole firmware's .data + .bss, of
which lwext4 itself uses 4,688 bytes (mount point and block device tables);
the rest is the example's I/O buffers and newlib. The Thumb-1 instruction
set of the M0+ makes lwext4 about 24 % larger. Measured with
`WRITE_BACK_CACHE=OFF`; the 4 KiB block figure is the peak over mkfs plus
the workload.

What drives the RAM use:

- **the block cache**: `CONFIG_BLOCK_DEV_CACHE_SIZE` buffers of one file
  system block each. With 4 KiB blocks (what `mke2fs` picks for cards of
  512 MiB and more) that is 32 KiB. Buffers referenced by the journal are
  not evicted, so the cache can exceed the configured size temporarily.
- **the journal**: every transaction that has not been written back to its
  home location yet keeps a record in RAM; with `WRITE_BACK_CACHE` this
  grows to tens of KiB.
- The Cortex-M0+ board (36 KiB RAM) runs only the 1 KiB block tests: with
  4 KiB blocks and the journal lwext4 needs about 66 KiB of heap. On such
  parts, format cards with `mke2fs -b 1024` (or `mkfs 1024`).

### Wiring an SD card module

Any micro SD breakout with an SPI interface (3.3 V; modules with a level
shifter/regulator also work from 5 V). Pins are the Arduino header names
printed on the boards:

| SD module | Nucleo F401RE / G071RB / L552ZE-Q | nRF52840 DK |
|---|---|---|
| CS | D10 (PB6 / PB0 / PD14) | D10 (P1.12) |
| MOSI (DI) | D11 (PA7) | D11 (P1.13) |
| MISO (DO) | D12 (PA6) | D12 (P1.14) |
| SCK | D13 (PA5) | D13 (P1.15) |
| VCC / GND | 3V3 / GND | VDD / GND |

### Flashing a real board

- **Nucleo boards**: plug in the ST-LINK USB port, a `NODE_F401RE` (etc.)
  drive appears: copy `lwext4-example-<board>.bin` onto it. Or
  `st-flash --format ihex write lwext4-example-<board>.hex`, or
  `openocd -f board/st_nucleo_f4.cfg -c "program lwext4-example-nucleo_f401re.elf verify reset exit"`.
- **nRF52840 DK**: copy `lwext4-example-nrf52840dk.hex` onto the `JLINK`
  drive, or `nrfjprog --program lwext4-example-nrf52840dk.hex --chiperase --verify --reset`.
  The image runs without a SoftDevice.

Open the board's USB serial port at 115200 8N1 (`picocom -b 115200
/dev/ttyACM0`). After reset the firmware identifies the card and prints
`READY`; type a command and Enter:

| Command | Card |
|---|---|
| `mkfs 4096` (or `mkfs 1024`) | **erases the card**: new MBR, one ext4 partition, then the workload |
| `hostimg` | a card written from `tests/renode/scripts/sdimage.py create card.img 32` with `dd if=card.img of=/dev/sdX` |
| `torture` / `recover` | pull the power while `torture` runs, then `recover` checks the journal replay |

The result ends with `LWEXT4-TEST: PASS` and the RAM figures. Put the card
into a Linux PC afterwards: `e2fsck -fn /dev/sdX1` should be clean and the
files under `/fw` readable.

## What the emulator does and does not model

Emulated faithfully enough to run the unmodified image: the core
(Cortex-M0+/M4F/M33 instruction sets, FPU, exceptions), the real memory map
and RAM/flash sizes (an overflow does not fit silently), the SPI controller
register interfaces (STM32 SPI incl. 8 bit data register access on the FIFO
parts; nRF52 SPIM with EasyDMA moving data to and from RAM), UARTs, GPIO
registers, and an SD card answering the SPI mode protocol (R1/R3/R7
responses, data tokens, CRC16 on data it sends, data response tokens).

Not modeled, so these bugs would only show on hardware:

- **timing**: SPI clock rates, card busy times, flash wait states; every
  transfer completes instantly.
- **unaligned access faults**: a real Cortex-M0+ faults on an unaligned
  32 bit load, Renode 1.17 performs it. The host build with UBSan
  (`ci/run.sh native asan-ubsan`) covers misaligned accesses instead.
- **chip select**: the card model ignores CS, a driver that forgets to
  toggle it would still work.
- **CRC**: the card model does not check command or data CRCs it receives.
- **clock tree**: RCC/CLOCK registers accept writes, nothing depends on them.
- **SDHC over SPI**: Renode 1.17 sends the OCR register (CMD58) least
  significant byte first, so a spec conformant driver misreads the CCS bit
  of cards above 2 GiB. The tests therefore use a 32 MiB (SDSC, byte
  addressed) card; the driver's SDHC path is only exercised on hardware.
