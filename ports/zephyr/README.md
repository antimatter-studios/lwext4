# lwext4 for Zephyr

This repository is a Zephyr module ([zephyr/module.yml](../../zephyr/module.yml)
points here). Add it to your west manifest

```yaml
manifest:
  projects:
    - name: lwext4
      url: https://github.com/antimatter-studios/lwext4
      revision: <commit or tag>
      path: modules/lib/lwext4
```

or pass its path in `ZEPHYR_EXTRA_MODULES` (what
[examples/zephyr](../../examples/zephyr/CMakeLists.txt) does), then enable
it in `prj.conf`:

```
CONFIG_LWEXT4=y
CONFIG_DISK_ACCESS=y          # for the disk access block device
```

| File | What |
|---|---|
| `CMakeLists.txt` | builds `src/*.c` of this repository as the Zephyr library `lwext4` (`ext4_extent.c` and `ext4_xattr.c`, GPLv2, only when enabled) |
| `Kconfig` | `CONFIG_LWEXT4_*` options (menuconfig: Modules -> lwext4): feature set, journal, extents, xattr, cache size, device/mount point counts, debug output |
| `include/generated/ext4_config.h` | maps those options onto lwext4's own `CONFIG_*` switches |
| `include/ext4_zephyr.h` | the API below; include it instead of `ext4.h` |
| `src/ext4_zephyr_disk.c` | `ext4_zephyr_disk_init()`: an lwext4 block device on any disk of Zephyr's disk access API (SD/MMC, eMMC, NVMe, RAM disk, flash disk, USB mass storage) |
| `src/ext4_zephyr_lock.c` | `ext4_zephyr_mount_locks()`: a `k_mutex` for `ext4_mount_setup_locks()` |

```c
static struct ext4_zephyr_disk sd;

ext4_zephyr_disk_init(&sd, "SD");   /* disk-name of the devicetree node */
ext4_device_register(ext4_zephyr_disk_bdev(&sd), "sd");
ext4_mount("sd", "/sd/", false);
ext4_recover("/sd/");
ext4_journal_start("/sd/");
/* ext4_fopen("/sd/...") ... */
```

lwext4 allocates its block cache and journal records with `malloc()`, so
the C library needs a heap (on microcontrollers Zephyr gives it all RAM
left over by default).

A complete application, run in QEMU by CI on an emulated Cortex-M3 board
with a RAM disk, is in [examples/zephyr](../../examples/zephyr/README.md).
