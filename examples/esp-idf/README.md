# lwext4 on the ESP32 family (ESP-IDF)

This example turns lwext4 into firmware for Espressif's ESP32, ESP32-C3 and
ESP32-S3: a normal ESP-IDF application that mounts ext4 filesystems stored
in the SPI flash and on an SD card, reads files a PC put there, formats a
filesystem of its own, and writes files a PC can read back.

The same firmware image is what CI runs in Espressif's QEMU and what you
flash to a board. Nothing needs to be installed on your machine except
docker: the whole toolchain lives in the `esp-idf` container
(`ci/envs/esp-idf`, the official `espressif/idf` image plus e2fsprogs; it
runs natively on x86_64 and arm64, e.g. a Raspberry Pi).

- [The pieces](#the-pieces)
- [Build the firmware](#build-the-firmware)
- [Run it in QEMU](#run-it-in-qemu)
- [Flash a real board](#flash-a-real-board)
- [Adding an SD card](#adding-an-sd-card)
- [Using lwext4 in your own project](#using-lwext4-in-your-own-project)
- [What the emulator does and does not prove](#what-the-emulator-does-and-does-not-prove)
- [Licensing](#licensing)

## The pieces

```
ports/esp-idf/lwext4/            the lwext4 ESP-IDF component
  CMakeLists.txt                 builds ../../../src/*.c as a component
  Kconfig                        "idf.py menuconfig" -> Component config -> lwext4
  include/generated/ext4_config.h  maps those Kconfig options onto lwext4's
                                 CONFIG_* switches (journal, extents, xattr,
                                 cache size, debug output, ...)
  include/ext4_esp.h             the API below
  port/ext4_esp_blockdev.c       lwext4 block devices on ESP-IDF drivers
  port/ext4_esp_lock.c           FreeRTOS mutex for ext4_mount_setup_locks()

examples/esp-idf/                this example (an ordinary idf.py project)
  CMakeLists.txt                 adds ports/esp-idf to EXTRA_COMPONENT_DIRS,
                                 builds host/ext4host.img and flashes it
                                 into the "ext4host" partition
  partitions.csv                 flash layout: app + two ext4 partitions
  sdkconfig.defaults[.esp32]     configuration (SD card on SDMMC for esp32)
  sdkconfig.sdspi                variant: SD card module on a SPI bus
  main/lwext4_example_main.c     the application
  main/Kconfig.projbuild         example options (SD interface, pins)
  host/mkimage.sh                builds the ext4 images on the PC (mke2fs -d)
  host/run_qemu_test.py          runs the firmware in QEMU and checks the
                                 result with e2fsck/debugfs
```

**Block devices.** lwext4 talks to storage through a `struct ext4_blockdev`
(read/write whole blocks). `ext4_esp.h` provides two:

- `ext4_esp_blockdev_init_sdmmc(dev, card)` - any `sdmmc_card_t`, i.e. an
  SD card on the SDMMC host controller *or* on a SPI bus (SDSPI): both
  drivers end in `sdmmc_read_sectors()` / `sdmmc_write_sectors()`. 512 byte
  blocks; use `ext4_mbr_scan()` to find the partitions on the card.
- `ext4_esp_blockdev_init_partition(dev, partition)` - a data partition in
  the SPI flash. Flash is erased in 4 KiB sectors, so this device has
  4 KiB blocks and filesystems on it must use 4 KiB blocks
  (`mke2fs -b 4096`, `ext4_mkfs_info.block_size = 4096`). A write erases and
  programs whole sectors (skipped when the content is unchanged); reads go
  through the flash cache (`esp_partition_mmap`). There is no wear
  levelling: fine for data that changes occasionally, not for logging
  every second.

**Partition table** (`partitions.csv`, 16 MB flash): the app, `ext4host`
(9.5 MB, an ext4 image made on the PC by `mke2fs -d` from a directory tree
and flashed together with the app, just like a SPIFFS/FAT image) and
`ext4dev` (5.4 MB, formatted by the firmware with `ext4_mkfs`). ext4 needs a
journal of at least 1024 blocks, which is why these partitions are several
megabytes.

**What the application does.** Every reset advances one step, remembered
by marker files on the filesystem:

1. verify the files the PC put on the host-made filesystems, write
   directories and a large pattern file, rename and delete files, remount,
   verify; `ext4_mkfs` the other partition (and SD partition 2), fill it
   (including a directory big enough to need an htree index), remount,
   verify
2. verify everything survived the reset, read `fromhost.bin` if the PC
   added it in between, truncate a file, remove a directory
3. and later: check the final state (no file changes; mounting still
   updates the superblock)

It prints `LWEXT4-TEST: PASS (boot N)` or `LWEXT4-TEST: FAIL: <reason>` on
the console UART and idles. Flash it again to start over.

## Build the firmware

From the repository root:

```sh
ci/run.sh esp32-build esp32        # or esp32c3, esp32s3
ci/run.sh esp32-build esp32c3 sdspi  # variant with an SD card module on SPI
```

The first run builds the container (a few GB download). The images end up in
`examples/esp-idf/dist/<chip>/`:

| File | Flash offset |
|------|--------------|
| `bootloader/bootloader.bin` | 0x1000 (esp32), 0x0 (esp32c3, esp32s3) |
| `partition_table/partition-table.bin` | 0x8000 |
| `lwext4_example.bin` | 0x10000 |
| `ext4host.img` | 0x110000 |
| `flash_args` | the list above, for `esptool.py write_flash @flash_args` |
| `lwext4-example-<chip>.bin` | everything merged, write at 0x0 |

CI uploads the same directory as a workflow artifact for every chip.

To change options (`idf.py menuconfig`, cache size, journaling, SD pins)
open a shell in the container:

```sh
ci/run.sh --shell esp-idf
cd examples/esp-idf
idf.py -B build-esp32 menuconfig
idf.py -B build-esp32 build
```

## Run it in QEMU

```sh
ci/run.sh esp32-qemu esp32         # or esp32c3, esp32s3
```

This builds the firmware, then boots the merged image
(`dist/<chip>/lwext4-example-<chip>.bin`, padded to the 16 MB flash size)
three times with `idf.py qemu --flash-file ...`; for the esp32 it also
attaches a 64 MB SD card image (MBR, partition 1 made by `mke2fs`,
partition 2 empty) with `-drive if=sd`. Between the boots it extracts every
ext4 filesystem from the flash and SD images and checks it on the PC with
`e2fsck -fn` and `debugfs` (directory listings, file contents, journal
present), and adds `fromhost.bin` with `debugfs -w` for the firmware to
read. Logs and images are left in `examples/esp-idf/build-<chip>/qemu-test/`.

To watch the firmware interactively instead:

```sh
ci/run.sh --shell esp-idf
cd examples/esp-idf
idf.py -B build-esp32 qemu monitor   # Ctrl-] to quit
```

## Flash a real board

Any esptool works with the files from `dist/<chip>/` (or the CI artifact),
for example on a PC with Python:

```sh
pip install esptool
cd examples/esp-idf/dist/esp32
esptool.py --chip esp32 -p /dev/ttyUSB0 -b 460800 write_flash @flash_args
# or: esptool.py --chip esp32 -p /dev/ttyUSB0 write_flash 0x0 lwext4-example-esp32.bin
```

Browser alternative: Espressif's [ESP Launchpad](https://espressif.github.io/esp-launchpad/)
or the [esptool-js flasher](https://espressif.github.io/esptool-js/) can
write `lwext4-example-<chip>.bin` at 0x0 over WebSerial.

Or from the container, passing the serial port through (the image is
`lwext4-ci-esp-idf:<tag>`, see `docker images`):

```sh
docker run --rm -it --device /dev/ttyUSB0 --group-add "$(stat -c %g /dev/ttyUSB0)" \
    -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src/examples/esp-idf \
    lwext4-ci-esp-idf:<tag> idf.py -B build-esp32 -p /dev/ttyUSB0 flash monitor
```

The board needs 16 MB of flash for this partition table (most ESP32-S3
boards, many ESP32 modules such as the WROVER-E/16MB, ESP32-C3 boards with
4 MB need smaller partitions: shrink `ext4host`/`ext4dev` in
`partitions.csv` and `CONFIG_ESPTOOLPY_FLASHSIZE`, keeping at least 2048
blocks of 4 KiB for a filesystem made by `mke2fs` with a journal).

Open a serial terminal at 115200 baud (`idf.py monitor`, `screen`,
`minicom`, ...) and reset the board: you should see `LWEXT4-TEST: PASS
(boot 1)`; press reset again for boot 2 and 3.

## Adding an SD card

Prepare the card on a Linux PC. This writes an MBR, an ext4 partition 1
with the example files, and leaves partition 2 (32 MB) for the firmware to
format; **it overwrites the card**:

```sh
examples/esp-idf/host/mkimage.sh sd sd.img     # inside ci/run.sh --shell esp-idf
sudo dd if=sd.img of=/dev/sdX bs=1M conv=fsync  # outside, X = your card
```

**SDMMC controller** (ESP32, ESP32-S3; the default for the ESP32 build).
ESP32 slot 1 uses fixed pins; every line needs a 10 kOhm pull-up to 3.3 V
(the internal pull-ups are enabled but are too weak for reliable use):

| SD card | ESP32 GPIO |
|---------|------------|
| CLK | 14 |
| CMD | 15 |
| D0 | 2 |
| D1 | 4 |
| D2 | 12 |
| D3 / CD | 13 |
| VDD / VSS | 3.3 V / GND |

GPIO12 is a strapping pin (flash voltage). If a pull-up on it stops the
board from booting, disable "Use 4-bit SDMMC bus" in menuconfig (1-bit mode
uses only CLK, CMD and D0) or burn the flash voltage eFuse as described in
the ESP-IDF SD card documentation.

**SPI bus / micro SD module** (all chips; `ci/run.sh esp32-build <chip>
sdspi`). The common breakout modules have a level shifter and pull-ups on
board; power them from 3.3 V or 5 V as the module requires:

| Module pin | ESP32 | ESP32-S3 | ESP32-C3 |
|------------|-------|----------|----------|
| MOSI / DI | 23 | 11 | 7 |
| MISO / DO | 19 | 13 | 2 |
| SCK / CLK | 18 | 12 | 6 |
| CS | 5 | 10 | 10 |

The pins are Kconfig options (`lwext4 example` menu).

## Using lwext4 in your own project

1. Put lwext4 somewhere in (or next to) your project, e.g. as a git
   submodule, and add its component directory in your top level
   `CMakeLists.txt`, before `project()`:

   ```cmake
   set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/lwext4/ports/esp-idf")
   ```

2. Add `lwext4` to `PRIV_REQUIRES` of your component.
3. Mount:

   ```c
   #include "ext4_esp.h"

   static ext4_esp_blockdev_t bd;
   const esp_partition_t *p = esp_partition_find_first(
           ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ext4");
   ESP_ERROR_CHECK(ext4_esp_blockdev_init_partition(&bd, p));
   ext4_device_register(ext4_esp_blockdev(&bd), "flash");
   ext4_mount("flash", "/fs/", false);
   ext4_mount_setup_locks("/fs/", ext4_esp_mount_locks());
   ext4_recover("/fs/");              /* replay the journal after power loss */
   ext4_journal_start("/fs/");

   ext4_file f;
   ext4_fopen(&f, "/fs/hello.txt", "wb");
   ext4_fwrite(&f, "hi\n", 3, NULL);
   ext4_fclose(&f);

   ext4_journal_stop("/fs/");
   ext4_umount("/fs/");
   ```

   For an SD card, initialise it the usual ESP-IDF way (`sdmmc_host_init()`
   / `sdspi_host_init_device()` + `sdmmc_card_init()`) and pass the
   `sdmmc_card_t` to `ext4_esp_blockdev_init_sdmmc()`; see `sd_card_init()`
   in `main/lwext4_example_main.c`.

4. Run lwext4 calls from a task with a large enough stack: the example
   uses 16 KiB and prints the high water mark at the end.

lwext4 has its own API (`ext4_fopen`, `ext4_dir_open`, ...); it is not
registered with the ESP-IDF VFS, so `fopen("/fs/...")` does not work.

## What the emulator does and does not prove

Emulated faithfully enough that the real ESP-IDF code runs unmodified: the
CPU cores (Xtensa LX6 dual core, Xtensa LX7 dual core, RISC-V), the ROM
bootloader and second stage bootloader, the partition table, the flash
MMU/cache and the SPI flash controller (the real `spi_flash`/`esp_partition`
driver erases and programs an emulated NOR flash chip), and on the ESP32 the
SDMMC host controller with its IDMAC DMA descriptors (the real `sdmmc`
driver initialises an emulated SD card, reads its CSD/SCR, switches to
4-bit mode and transfers sectors). eFuses and the UART are emulated too.

Not covered: timing (emulated flash erase and SD transfers are instant or
arbitrarily slow, not like real parts), power loss in the middle of a
write, real SD card quirks (busy signalling, CRC errors, slow cards, card
removal), flash wear and read disturb, the GPIO matrix / pin
configuration, and SD on SPI (QEMU has no general purpose SPI controller
model; that variant is only built). ESP32-C3 and ESP32-S3 have no SD
emulation, so on those chips only the flash path runs in QEMU.

Known emulator bug: with the two cores of the ESP32/ESP32-S3 running in
parallel host threads, Espressif QEMU 9.2.2 occasionally panics the firmware
with a `LoadStorePIFAddrError` on an ordinary peripheral register access
([espressif/qemu#174](https://github.com/espressif/qemu/issues/174)); we
saw it on the ESP32-S3 in about one run in four. Scheduling both cores on
one host thread (`-accel tcg,thread=single`) avoids it but makes the run
about 20 times slower, so instead `host/run_qemu_test.py` recognises that
exact signature (EXCCAUSE 15 on a peripheral address - the firmware never
accesses peripherals directly), restores the flash/SD images from before
that boot and reruns it, at most twice, with a warning in the log. Any
other crash fails the test.

## Licensing

The component glue, the example and the scripts are BSD-3-Clause, like
lwext4's core. Two lwext4 source files are GPLv2: `src/ext4_extent.c` and
`src/ext4_xattr.c`. They are compiled in when extents
(`CONFIG_LWEXT4_EXTENTS`, needed for ext4 images made by `mke2fs -t ext4`)
or extended attributes (`CONFIG_LWEXT4_XATTR`) are enabled, which is the
default; your firmware then contains GPLv2 code. Disabling both in
menuconfig leaves the two files out of the build (you then need ext2/ext3,
or ext4 without extents); that configuration is not exercised by CI yet.
