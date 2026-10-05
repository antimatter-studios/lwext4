Example firmware
================

Three applications to start an MCU project from, written once against
[platforms/platform.h](../../platforms/platform.h) and built for every
Cortex-M CPU, and for the boards with an SD card of
[platforms/sdcard](../../platforms/sdcard/README.md). CI runs each in QEMU
on a disk image ([check.sh](check.sh)), and on the emulated boards in
Renode with an SD card image ([apps.robot](../../tests/renode/apps.robot)),
and checks the image with `e2fsck -fn` and `debugfs`.

| Application | What it does | Checked |
|---|---|---|
| [hello.c](hello.c) | format, mount, write a file through the journal, read it back, unmount: the minimum | `/hello.txt` reads back with debugfs, e2fsck clean |
| [datalogger.c](datalogger.c) | append records to log files, one transaction each; a new file every 50 records, the oldest removed beyond 4; on start, replay the journal and check every record | the power is cut during start-up and at every block write of a stretch of several records (63 cuts): after each, every record reported written is there, at most the one being written is lost, e2fsck clean |
| [reader.c](reader.c) | mount a card made on a PC read-only, read `/config.txt`, list `/assets` with sizes and checksums | output matches what the host computes; the image is unchanged, byte for byte |

Build and run, here for the Cortex-M4 (`toolchain/` has the others):

```sh
cmake -S . -B build-m4 -DCMAKE_TOOLCHAIN_FILE=toolchain/cortex-m4.cmake
cmake --build build-m4
ctest --test-dir build-m4 -R firmware -V
```

or `ci/run.sh cortex-m cortex-m4`, in CI's container. Every release has
the firmware of each CPU, with these sources and platforms/, in
`lwext4-<version>-cortex-m.tar.gz` (`<cpu>/firmware/*.elf`); the
[project site](https://antimatter-studios.github.io/lwext4/firmware/) has
a page per CPU. The firmware is
`build-m4/examples/firmware/<app>.elf`; QEMU runs it on a disk image with
`-semihosting-config enable=on,target=native,arg=disk.img` (see
[check.sh](check.sh)).

On the SD card boards (NUCLEO-F401RE, NUCLEO-G071RB, NUCLEO-L552ZE-Q,
nRF52840 DK) the applications are built with the board's firmware:

```sh
ci/run.sh baremetal-sdcard-build nucleo_f401re     # dist/nucleo_f401re/apps/<app>.hex
ci/run.sh renode-baremetal-sdcard nucleo_f401re --include apps
```

Flash `apps/<app>.hex`; it prints `READY` on the board's console (the
debugger's USB serial port) and starts when you press Enter. The disk is
the whole SD card. Each release has them in
`lwext4-<version>-baremetal-sdcard-<board>.tar.gz`.

On your own board, [platforms/mps2](../../platforms/mps2/README.md) or
[platforms/sdcard](../../platforms/sdcard/README.md) is what you replace: its `disk.c` becomes your SD card or flash driver (the five
callbacks of [blockdev-template](../blockdev-template/my_blockdev.c)), its
`startup.c` and `mps2.ld` your vendor's start-up code and memory map.
