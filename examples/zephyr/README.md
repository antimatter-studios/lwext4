# lwext4 on Zephyr

This example uses lwext4 as a [Zephyr](https://zephyrproject.org) module:
an ordinary Zephyr application formats a disk of Zephyr's disk access API
with ext4, mounts it, starts the journal, writes, reads, lists, renames and
removes files and directories, unmounts, mounts again and checks that
everything is still there.

CI builds it with `west` for the `mps2/an385` board (Arm MPS2, Cortex-M3,
4 MiB SRAM) and runs it in QEMU. The disk is a 2 MiB RAM disk; after the
run the test copies it out of the emulated memory and checks it on the PC
with `e2fsck -fn` and `debugfs`. Nothing needs to be installed on your
machine except docker: Zephyr, its SDK and QEMU live in the `zephyr`
container (`ci/envs/zephyr`, native on x86_64 and arm64, e.g. a Raspberry
Pi).

- [The pieces](#the-pieces)
- [Build the firmware](#build-the-firmware)
- [Run it in QEMU](#run-it-in-qemu)
- [Using lwext4 in your own Zephyr application](#using-lwext4-in-your-own-zephyr-application)
- [Other disks: SD cards](#other-disks-sd-cards)
- [What the emulator does and does not prove](#what-the-emulator-does-and-does-not-prove)
- [Licensing](#licensing)

## The pieces

```
zephyr/module.yml                makes this repository a Zephyr module
ports/zephyr/                    the module's build glue (see its README)
  CMakeLists.txt                 builds ../../src/*.c as the library "lwext4"
  Kconfig                        CONFIG_LWEXT4_* (menuconfig: Modules -> lwext4)
  include/generated/ext4_config.h  maps those options onto lwext4's
                                 CONFIG_* switches
  include/ext4_zephyr.h          the API below
  src/ext4_zephyr_disk.c         an lwext4 block device on any disk of the
                                 disk access API
  src/ext4_zephyr_lock.c         a k_mutex for ext4_mount_setup_locks()

examples/zephyr/                 this example (an ordinary west project)
  CMakeLists.txt                 adds the repository to ZEPHYR_EXTRA_MODULES
  prj.conf                       configuration for every board
  boards/mps2_an385.overlay      the RAM disk (devicetree)
  boards/mps2_an385.conf         a QEMU monitor socket for the test
  src/main.c                     the application, commented step by step
  host/run_qemu_test.py          runs it in QEMU and checks the disk
```

**The block device.** lwext4 talks to storage through a
`struct ext4_blockdev` (read/write whole blocks).
`ext4_zephyr_disk_init(&disk, "RAM")` makes one out of a disk of Zephyr's
disk access API (`disk_access_read()`, `disk_access_write()`,
`DISK_IOCTL_*`), named by the `disk-name` property of its devicetree node.
The same code serves an SD card (`zephyr,sdmmc-disk`), eMMC, NVMe, a flash
disk or USB mass storage: only the name changes.

**Memory.** lwext4 allocates its block cache and journal records with
`malloc()`; on microcontrollers Zephyr gives the C library's heap all RAM
the image does not use. The firmware prints what it used at the end
(`MEM: heap peak ... main stack used ...`). 1 KiB filesystem blocks keep
the block cache small. The RAM disk itself (2 MiB) is a static array: the
smallest ext4 journal lwext4 makes is 1024 blocks (1 MiB here), so a
filesystem with a journal needs well over 1 MiB.

## Build the firmware

From the repository root:

```sh
ci/run.sh zephyr-build mps2/an385
```

The first run builds the container. The images end up in
`examples/zephyr/dist/mps2_an385/`:

| File | What |
|------|------|
| `lwext4-example-mps2_an385.elf` | with symbols: QEMU `-kernel`, gdb |
| `lwext4-example-mps2_an385.bin` | raw image, written at the start of the flash |
| `lwext4-example-mps2_an385.hex` | Intel HEX, for flashing tools |
| `config` | the complete Kconfig configuration |

CI uploads the same directory as a workflow artifact.

To change options, open a shell in the container, where every `west`
command works as documented by Zephyr:

```sh
ci/run.sh --shell zephyr
west build -b mps2/an385 -d build-m3 examples/zephyr
west build -d build-m3 -t menuconfig    # Modules -> lwext4
west build -d build-m3 -t run           # QEMU; Ctrl-a x quits
```

## Run it in QEMU

```sh
ci/run.sh zephyr-qemu mps2/an385
```

This builds the firmware and boots it with Zephyr's own run target
(`west build -t run`, QEMU's `mps2-an385` machine). The firmware prints
`LWEXT4-TEST: PASS` or `LWEXT4-TEST: FAIL: <reason>` and idles. After a
PASS the test runner looks up the RAM disk's buffer (`disk_buf_0` of
Zephyr's RAM disk driver) in `zephyr.elf` and asks QEMU's monitor to save
it (`pmemsave`), then stops QEMU. The saved disk must be a clean ext4
filesystem (`e2fsck -fn`) with the label and journal `ext4_mkfs` gave it,
and hold exactly the directories and files the firmware wrote, byte for
byte (`debugfs`). The console log and the disk image are left in
`examples/zephyr/build-mps2_an385/qemu-test/`; open the image on Linux
with `debugfs disk.img` or `sudo mount -o loop,ro disk.img /mnt`.

The job fails on a FAIL verdict, a crash (a failed assertion or a fault),
no verdict within the timeout, or any problem the host finds in the disk.

## Using lwext4 in your own Zephyr application

Add this repository to your west manifest:

```yaml
manifest:
  projects:
    - name: lwext4
      url: https://github.com/antimatter-studios/lwext4
      revision: <commit or tag>
      path: modules/lib/lwext4
```

or pass its path in `ZEPHYR_EXTRA_MODULES`, as this example's
`CMakeLists.txt` does. Then enable it in `prj.conf`:

```
CONFIG_LWEXT4=y
CONFIG_DISK_ACCESS=y
```

and use the lwext4 API through `ext4_zephyr.h`:

```c
#include <ext4_zephyr.h>

static struct ext4_zephyr_disk disk;

ext4_zephyr_disk_init(&disk, "SD");    /* disk-name of the devicetree node */
ext4_device_register(ext4_zephyr_disk_bdev(&disk), "sd");
ext4_mount("sd", "/sd/", false);
ext4_mount_setup_locks("/sd/", ext4_zephyr_mount_locks());
ext4_recover("/sd/");
ext4_journal_start("/sd/");
/* ext4_fopen("/sd/..."), ext4_dir_mk(), ... */
ext4_journal_stop("/sd/");
ext4_umount("/sd/");
```

`src/main.c` shows every step with comments, including `ext4_mkfs()` and
the write-back cache; [ports/zephyr/README.md](../../ports/zephyr/README.md)
lists the Kconfig options.

## Other disks: SD cards

On a real board the disk is usually an SD card. Describe it in the board's
devicetree overlay and change `DISK` in `src/main.c` to its disk name, for
example for a card on a SPI bus:

```dts
&spi1 {
	status = "okay";
	cs-gpios = <&gpio0 4 GPIO_ACTIVE_LOW>;

	sdhc0: sdhc@0 {
		compatible = "zephyr,sdhc-spi-slot";
		reg = <0>;
		spi-max-frequency = <24000000>;
		status = "okay";

		mmc {
			compatible = "zephyr,sdmmc-disk";
			disk-name = "SD";
			status = "okay";
		};
	};
};
```

with `CONFIG_SPI=y` and `CONFIG_GPIO=y` in the board's `.conf`. For a
card with a partition table, look the partitions up with `ext4_mbr_scan()`
(`include/ext4_mbr.h`) and register one of them instead of the whole disk.

Why CI does not use an emulated SD card: QEMU's `lm3s6965evb` machine
(Zephyr's `qemu_cortex_m3` board) has a microSD socket on a SPI bus, and
with it Zephyr v4.4.2 identifies the card and reads it, but every write
fails with `sd: Card did not return to ready state`. The SD specification
asks the host for at least one byte of clocks (Nwr) between the card's R1
response to a write command and the start token of the data block; Zephyr's
`sdhc_spi` driver sends the token right after the response. QEMU's
`ssi-sd` model uses that byte to return to its command state, so it
swallows the token and never sees the data. With one extra byte clocked in
`sdhc_spi_write_data()` the whole example passes on that board, and
`e2fsck` finds its card clean. The example should run on unmodified
Zephyr, so CI uses a RAM disk until one of the two is changed. (QEMU 10.0
also refuses to send an SD card's CSD and CID in SPI mode; 10.1 fixed that.)

## What the emulator does and does not prove

Emulated: the Cortex-M3 (Thumb-2 instruction set, NVIC, SysTick), the MPS2
memory map and UART, with the real Zephyr kernel, drivers and build, and
the image built for the board. So the lwext4 code, its Zephyr glue, the
Kconfig/devicetree integration and the memory limits of a 4 MiB
microcontroller are what they would be on the board.

Not emulated: timing, caches and real storage. A RAM disk never fails, is
never slow and survives no reset; the SD card path (and its error handling)
is only exercised through the disk access API it shares with the RAM disk.
Power cut behaviour is tested elsewhere (the bare-metal SD card example and
the host tests).

## Licensing

Everything in `examples/zephyr`, `ports/zephyr` and `zephyr/` is
BSD-3-Clause, like the lwext4 core. lwext4's `ext4_extent.c` and
`ext4_xattr.c` are GPL-2.0; they are built only with
`CONFIG_LWEXT4_EXTENTS` / `CONFIG_LWEXT4_XATTR` (on by default, see
[ports/zephyr/Kconfig](../../ports/zephyr/Kconfig)). Zephyr itself is
Apache-2.0.
