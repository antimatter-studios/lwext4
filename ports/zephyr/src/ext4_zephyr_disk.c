/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * An lwext4 block device on Zephyr's disk access API
 * (<zephyr/storage/disk_access.h>): whatever disk driver provides the disk
 * (SD/MMC card over SPI or an SD host controller, eMMC, NVMe, RAM disk,
 * flash disk, USB mass storage), lwext4 reads and writes it in whole
 * sectors through disk_access_read()/disk_access_write().
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>

#include <ext4_errno.h>

#include <ext4_zephyr.h>

LOG_MODULE_REGISTER(lwext4, LOG_LEVEL_INF);

/* Zephyr functions return 0 or a negative errno value, lwext4 expects EOK
 * or a positive one. */
static int to_ext4_err(int ret)
{
	return ret < 0 ? -ret : (ret > 0 ? EIO : EOK);
}

static struct ext4_zephyr_disk *to_dev(struct ext4_blockdev *bdev)
{
	/* The interface is shared by every partition ext4_mbr_scan() finds,
	 * so the owner is reached through it, not through the bdev. */
	return bdev->bdif->p_user;
}

/* ext4_block_init() (ext4_device_register(), ext4_mkfs()) opens the device,
 * ext4_block_fini() closes it. The disk itself is initialised once by
 * ext4_zephyr_disk_init(). */
static int disk_open(struct ext4_blockdev *bdev)
{
	struct ext4_zephyr_disk *dev = to_dev(bdev);
	int status = disk_access_status(dev->disk_name);

	if (status != DISK_STATUS_OK) {
		LOG_ERR("%s: disk status 0x%x", dev->disk_name, status);
		return (status & DISK_STATUS_NOMEDIA) ? ENODEV : EIO;
	}
	return EOK;
}

static int disk_close(struct ext4_blockdev *bdev)
{
	struct ext4_zephyr_disk *dev = to_dev(bdev);

	/* Commit the disk's own write cache, if it has one */
	return to_ext4_err(disk_access_ioctl(dev->disk_name,
					     DISK_IOCTL_CTRL_SYNC, NULL));
}

static bool range_ok(struct ext4_blockdev *bdev, uint64_t blk_id,
		     uint32_t blk_cnt)
{
	/* disk_access_read()/write() take 32 bit sector numbers */
	return blk_id + blk_cnt <= bdev->bdif->ph_bcnt &&
	       blk_id + blk_cnt >= blk_id && blk_id + blk_cnt <= UINT32_MAX;
}

static int disk_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt)
{
	struct ext4_zephyr_disk *dev = to_dev(bdev);
	int ret;

	if (!range_ok(bdev, blk_id, blk_cnt)) {
		return EINVAL;
	}
	ret = disk_access_read(dev->disk_name, buf, (uint32_t)blk_id, blk_cnt);
	if (ret < 0) {
		LOG_ERR("%s: read of %u sectors at %u failed: %d",
			dev->disk_name, blk_cnt, (uint32_t)blk_id, ret);
	}
	return to_ext4_err(ret);
}

static int disk_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt)
{
	struct ext4_zephyr_disk *dev = to_dev(bdev);
	int ret;

	if (!range_ok(bdev, blk_id, blk_cnt)) {
		return EINVAL;
	}
	ret = disk_access_write(dev->disk_name, buf, (uint32_t)blk_id,
				blk_cnt);
	if (ret < 0) {
		LOG_ERR("%s: write of %u sectors at %u failed: %d",
			dev->disk_name, blk_cnt, (uint32_t)blk_id, ret);
	}
	return to_ext4_err(ret);
}

/* Serialise the partitions of one disk (see ext4_mbr_scan()) */
static int disk_lock(struct ext4_blockdev *bdev)
{
	return to_ext4_err(k_mutex_lock(&to_dev(bdev)->lock, K_FOREVER));
}

static int disk_unlock(struct ext4_blockdev *bdev)
{
	return to_ext4_err(k_mutex_unlock(&to_dev(bdev)->lock));
}

int ext4_zephyr_disk_init(struct ext4_zephyr_disk *dev, const char *disk_name)
{
	uint32_t sector_size = 0;
	uint32_t sector_count = 0;
	int ret;

	memset(dev, 0, sizeof(*dev));
	dev->disk_name = disk_name;
	k_mutex_init(&dev->lock);

	/* Brings the disk up (e.g. identifies the SD card); reference
	 * counted, balanced by ext4_zephyr_disk_deinit(). */
	ret = disk_access_ioctl(disk_name, DISK_IOCTL_CTRL_INIT, NULL);
	if (ret < 0) {
		LOG_ERR("%s: initialisation failed: %d", disk_name, ret);
		return to_ext4_err(ret);
	}
	ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_SIZE,
				&sector_size);
	if (ret == 0) {
		ret = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_COUNT,
					&sector_count);
	}
	if (ret < 0) {
		LOG_ERR("%s: no geometry: %d", disk_name, ret);
		goto fail;
	}
	if (sector_size == 0 || sector_size > sizeof(dev->ph_bbuf) ||
	    (sector_size & (sector_size - 1)) != 0 || sector_count == 0) {
		LOG_ERR("%s: unsupported geometry: %u sectors of %u bytes "
			"(CONFIG_LWEXT4_DISK_MAX_SECTOR_SIZE=%u)",
			disk_name, sector_count, sector_size,
			CONFIG_LWEXT4_DISK_MAX_SECTOR_SIZE);
		ret = -ENOTSUP;
		goto fail;
	}

	dev->iface = (struct ext4_blockdev_iface){
		.open = disk_open,
		.bread = disk_bread,
		.bwrite = disk_bwrite,
		.close = disk_close,
		.lock = disk_lock,
		.unlock = disk_unlock,
		.ph_bsize = sector_size,
		.ph_bcnt = sector_count,
		.ph_bbuf = dev->ph_bbuf,
		.p_user = dev,
	};
	dev->bdev.bdif = &dev->iface;
	dev->bdev.part_offset = 0;
	dev->bdev.part_size = (uint64_t)sector_count * sector_size;
	LOG_INF("%s: %u sectors of %u bytes", disk_name, sector_count,
		sector_size);
	return EOK;

fail:
	(void)disk_access_ioctl(disk_name, DISK_IOCTL_CTRL_DEINIT, NULL);
	return to_ext4_err(ret);
}

int ext4_zephyr_disk_deinit(struct ext4_zephyr_disk *dev)
{
	return to_ext4_err(disk_access_ioctl(dev->disk_name,
					     DISK_IOCTL_CTRL_DEINIT, NULL));
}
