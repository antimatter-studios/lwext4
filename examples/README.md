# lwext4 examples

Small, complete programs that show how lwext4 is used. The host examples
are built with the generic (PC) target and run by CI, which checks the
filesystems they write with `e2fsck -fn` and `debugfs` (`ci/run.sh
examples`). The microcontroller examples are built and run in emulators by
their own workflows.

| Example | What it shows |
|---|---|
| [basic](basic/main.c) | The whole life cycle on a PC: register a file-backed block device, `ext4_mkfs`, mount, journal recovery and start, write-back cache, create directories, write, read, list, rename and remove files, unmount. Commented step by step. |
| [blockdev-template](blockdev-template/my_blockdev.c) | An annotated skeleton of a custom `struct ext4_blockdev`: the part you write to port lwext4 to new storage hardware. Runs on a RAM disk ([ram_storage.c](blockdev-template/ram_storage.c)) so it is tested on a PC; replace the `storage_*()` functions of [storage.h](blockdev-template/storage.h) with your driver. |

On microcontrollers:

| Example | What it shows |
|---|---|
| [baremetal-sdcard](baremetal-sdcard/README.md) | A complete bare-metal firmware for ST NUCLEO-F401RE, NUCLEO-G071RB, NUCLEO-L552ZE-Q and Nordic nRF52840 DK boards with a micro SD card on SPI, without a vendor SDK or RTOS: MBR partitioning, `ext4_mkfs`, the journal, power cut recovery. Built with `ci/run.sh baremetal-sdcard-build <board>`; CI flashes the same image onto the boards emulated in Renode (`ci/run.sh renode-baremetal-sdcard <board>`). |
| [zephyr](zephyr/README.md) | Zephyr RTOS: lwext4 as a Zephyr module (`zephyr/module.yml`, `ports/zephyr`) with a block device on any disk of Zephyr's disk access API, and an ordinary west application: `ext4_mkfs`, the journal, files and directories, remount and verify. Built with `ci/run.sh zephyr-build mps2/an385`; CI runs it in QEMU on the Cortex-M3 `mps2/an385` board with a RAM disk and checks the disk with `e2fsck -fn` and `debugfs` (`ci/run.sh zephyr-qemu mps2/an385`). |
| [esp-idf in antimatter-studios/lwext4](https://github.com/antimatter-studios/lwext4/tree/main/examples/esp-idf) | ESP32, ESP32-C3 and ESP32-S3 (ESP-IDF): an lwext4 component with block devices for SPI flash partitions and SD cards, and an example firmware that CI runs in Espressif's QEMU. |

## Build and run the host examples

```bash
 make generic
 cd build_generic
 make lwext4-example-basic lwext4-example-blockdev-template
 ./examples/lwext4-example-basic disk.img
 ./examples/lwext4-example-blockdev-template ram.img
 ```

Both leave an ordinary ext4 image behind. With e2fsprogs installed:

```bash
 e2fsck -fn disk.img                      # consistency check, changes nothing
 debugfs -R 'ls -l /docs' disk.img        # list a directory
 debugfs -R 'cat /docs/hello.txt' disk.img
 ```

On Linux the image can also be mounted: `sudo mount -o loop disk.img /mnt`.

## From here

- The API is declared, with comments, in [include/ext4.h](../include/ext4.h);
  formatting in [include/ext4_mkfs.h](../include/ext4_mkfs.h), partition
  tables in [include/ext4_mbr.h](../include/ext4_mbr.h).
- Features and buffer sizes are chosen at compile time with `CONFIG_*`
  options: see [include/ext4_config.h](../include/ext4_config.h) for the
  list and defaults, and [CMakeLists.txt](../CMakeLists.txt) for the values
  each target uses.
- For microcontrollers, build the library with a toolchain file from
  [toolchain/](../toolchain) (e.g. `make cortex-m4`) and link it with your
  block device.
