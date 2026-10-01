/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 glue for Zephyr: a block device on a disk of the disk access API,
 * and mount point locks on a k_mutex.
 *
 * Typical use with the SD card of a "zephyr,sdmmc-disk" node whose
 * disk-name is "SD":
 *
 *     static struct ext4_zephyr_disk sd;
 *
 *     ext4_zephyr_disk_init(&sd, "SD");        // DISK_IOCTL_CTRL_INIT
 *     ext4_device_register(ext4_zephyr_disk_bdev(&sd), "sd");
 *     ext4_mount("sd", "/sd/", false);
 *     ext4_mount_setup_locks("/sd/", ext4_zephyr_mount_locks());
 *     ...
 *     ext4_umount("/sd/");
 *     ext4_device_unregister("sd");
 *     ext4_zephyr_disk_deinit(&sd);            // DISK_IOCTL_CTRL_DEINIT
 *
 * For a card with a partition table, pass ext4_zephyr_disk_bdev(&sd) to
 * ext4_mbr_scan() and register the partition block devices it returns.
 *
 * This header includes the lwext4 API (ext4.h, ext4_mkfs.h, ext4_mbr.h);
 * include it instead of those. All functions return EOK (0) or a positive
 * errno value, like the rest of the lwext4 API.
 */
#ifndef EXT4_ZEPHYR_H_
#define EXT4_ZEPHYR_H_

#include <stdint.h>

#include <zephyr/kernel.h>

/*
 * lwext4's public headers define IN_RANGE(b, first, len) for lwext4's own
 * use, a different macro from Zephyr's IN_RANGE(val, min, max). Include
 * the lwext4 API through this header, which keeps Zephyr's definition for
 * the code that includes it.
 */
#pragma push_macro("IN_RANGE")
#undef IN_RANGE
#include <ext4.h>
#include <ext4_blockdev.h>
#include <ext4_mbr.h>
#include <ext4_mkfs.h>
#undef IN_RANGE
#pragma pop_macro("IN_RANGE")

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_LWEXT4_DISK_ACCESS

/** Block device state. Treat as opaque; it must stay valid (static or
 *  heap, not a stack frame that returns) while it is registered. */
struct ext4_zephyr_disk {
	struct ext4_blockdev bdev;
	struct ext4_blockdev_iface iface;
	const char *disk_name;
	struct k_mutex lock;
	/* one disk sector, for lwext4's partial block reads and writes */
	uint8_t ph_bbuf[CONFIG_LWEXT4_DISK_MAX_SECTOR_SIZE] __aligned(4);
};

/** Initialise the disk access disk @p disk_name (the disk-name of its
 *  devicetree node), read its sector size and count, and set up @p dev as
 *  an lwext4 block device on it. */
int ext4_zephyr_disk_init(struct ext4_zephyr_disk *dev, const char *disk_name);

/** Flush the disk's write cache and release it (DISK_IOCTL_CTRL_DEINIT),
 *  after the block device has been unregistered. */
int ext4_zephyr_disk_deinit(struct ext4_zephyr_disk *dev);

static inline struct ext4_blockdev *
ext4_zephyr_disk_bdev(struct ext4_zephyr_disk *dev)
{
	return &dev->bdev;
}

#endif /* CONFIG_LWEXT4_DISK_ACCESS */

/** Mount point lock for ext4_mount_setup_locks() when several threads use
 *  the file system: a k_mutex shared by all mount points. */
const struct ext4_lock *ext4_zephyr_mount_locks(void);

#ifdef __cplusplus
}
#endif

#endif /* EXT4_ZEPHYR_H_ */
