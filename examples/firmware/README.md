Example firmware
================

Three applications to start an MCU project from, written once against
[platforms/platform.h](../../platforms/platform.h) and built for every
Cortex-M CPU. CI runs each in QEMU on a disk image and checks the image
with `e2fsck -fn` and `debugfs` ([check.sh](check.sh)).

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

or `ci/run.sh cortex-m cortex-m4`, in CI's container. The firmware is
`build-m4/examples/firmware/<app>.elf`; QEMU runs it on a disk image with
`-semihosting-config enable=on,target=native,arg=disk.img` (see
[check.sh](check.sh)).

On your board, [platforms/mps2](../../platforms/mps2/README.md) is what
you replace: its `disk.c` becomes your SD card or flash driver (the five
callbacks of [blockdev-template](../blockdev-template/my_blockdev.c)), its
`startup.c` and `mps2.ld` your vendor's start-up code and memory map.
