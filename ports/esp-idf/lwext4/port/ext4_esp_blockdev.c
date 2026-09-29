/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 block devices on top of the ESP-IDF storage drivers:
 *  - sdmmc_card_t (SDMMC host or SDSPI) through sdmmc_read/write_sectors()
 *  - SPI flash data partitions through esp_partition_read/erase/write()
 */
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#include <ext4_errno.h>

#include "ext4_esp.h"

static const char *TAG = "lwext4";

#define PARTITION_SECTOR_SIZE 4096u

static ext4_esp_blockdev_t *to_dev(struct ext4_blockdev *bdev)
{
	/* bdif is shared by all partitions found by ext4_mbr_scan(), so the
	 * owner is looked up through the interface, not the bdev. */
	return (ext4_esp_blockdev_t *)bdev->bdif->p_user;
}

static int esp_to_errno(esp_err_t err)
{
	switch (err) {
	case ESP_OK:
		return EOK;
	case ESP_ERR_NO_MEM:
		return ENOMEM;
	case ESP_ERR_INVALID_ARG:
	case ESP_ERR_INVALID_SIZE:
		return EINVAL;
	case ESP_ERR_TIMEOUT:
	default:
		return EIO;
	}
}

static int bd_open(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int bd_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int bd_lock(struct ext4_blockdev *bdev)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	return xSemaphoreTake(dev->mutex, portMAX_DELAY) == pdTRUE ? EOK : EIO;
}

static int bd_unlock(struct ext4_blockdev *bdev)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	return xSemaphoreGive(dev->mutex) == pdTRUE ? EOK : EIO;
}

static bool range_ok(struct ext4_blockdev *bdev, uint64_t blk_id,
		     uint32_t blk_cnt)
{
	return blk_id + blk_cnt <= bdev->bdif->ph_bcnt &&
	       blk_id + blk_cnt >= blk_id;
}

/* ---------------------------------------------------------------- SD/MMC */

static int sd_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		    uint32_t blk_cnt)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	if (!range_ok(bdev, blk_id, blk_cnt))
		return EINVAL;
	esp_err_t err = sdmmc_read_sectors(dev->u.card, buf, (size_t)blk_id,
					   blk_cnt);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "sdmmc read %llu+%u: %s",
			 (unsigned long long)blk_id, (unsigned)blk_cnt,
			 esp_err_to_name(err));
	return esp_to_errno(err);
}

static int sd_bwrite(struct ext4_blockdev *bdev, const void *buf,
		     uint64_t blk_id, uint32_t blk_cnt)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	if (!range_ok(bdev, blk_id, blk_cnt))
		return EINVAL;
	esp_err_t err = sdmmc_write_sectors(dev->u.card, buf, (size_t)blk_id,
					    blk_cnt);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "sdmmc write %llu+%u: %s",
			 (unsigned long long)blk_id, (unsigned)blk_cnt,
			 esp_err_to_name(err));
	return esp_to_errno(err);
}

/* ------------------------------------------------------- flash partition */

#if CONFIG_LWEXT4_PARTITION_MMAP_READS
/*
 * Reads go through the flash cache: a window of the partition is mapped
 * into the data address space (esp_partition_mmap) and copied from. This
 * avoids esp_partition_read(), which disables the caches (and stalls the
 * other core) for every chunk it moves through the SPI registers.
 * Flash writes and erases invalidate the cache for the affected range, so
 * the mapping never returns stale data.
 */
#define MMAP_WINDOW (64u * 1024u)

static esp_err_t part_read(ext4_esp_blockdev_t *dev, size_t off, void *buf,
			   size_t len)
{
	const esp_partition_t *part = dev->u.part;
	uint8_t *dst = buf;

	while (len) {
		if (!dev->map_ptr || off < dev->map_off ||
		    off >= dev->map_off + dev->map_len) {
			if (dev->map_ptr)
				esp_partition_munmap(dev->map_handle);
			dev->map_ptr = NULL;
			dev->map_off = off & ~(size_t)(MMAP_WINDOW - 1);
			dev->map_len = MMAP_WINDOW;
			if (dev->map_off + dev->map_len > part->size)
				dev->map_len = part->size - dev->map_off;
			esp_err_t err = esp_partition_mmap(
				part, dev->map_off, dev->map_len,
				ESP_PARTITION_MMAP_DATA, &dev->map_ptr,
				&dev->map_handle);
			if (err != ESP_OK) {
				dev->map_ptr = NULL;
				return err;
			}
		}
		size_t n = dev->map_off + dev->map_len - off;
		if (n > len)
			n = len;
		memcpy(dst, (const uint8_t *)dev->map_ptr + (off - dev->map_off),
		       n);
		dst += n;
		off += n;
		len -= n;
	}
	return ESP_OK;
}
#else
static esp_err_t part_read(ext4_esp_blockdev_t *dev, size_t off, void *buf,
			   size_t len)
{
	return esp_partition_read(dev->u.part, off, buf, len);
}
#endif

static int part_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	if (!range_ok(bdev, blk_id, blk_cnt))
		return EINVAL;
	esp_err_t err = part_read(dev, (size_t)blk_id * PARTITION_SECTOR_SIZE,
				  buf, (size_t)blk_cnt * PARTITION_SECTOR_SIZE);
	if (err != ESP_OK)
		ESP_LOGE(TAG, "partition read %llu+%u: %s",
			 (unsigned long long)blk_id, (unsigned)blk_cnt,
			 esp_err_to_name(err));
	return esp_to_errno(err);
}

static int part_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt)
{
	ext4_esp_blockdev_t *dev = to_dev(bdev);
	const uint8_t *src = buf;
	esp_err_t err = ESP_OK;

	if (!range_ok(bdev, blk_id, blk_cnt))
		return EINVAL;

	for (uint32_t i = 0; i < blk_cnt; i++) {
		size_t off = (size_t)(blk_id + i) * PARTITION_SECTOR_SIZE;
		const uint8_t *data = src + (size_t)i * PARTITION_SECTOR_SIZE;

#if CONFIG_LWEXT4_PARTITION_SKIP_IDENTICAL
		err = part_read(dev, off, dev->sector_buf,
				PARTITION_SECTOR_SIZE);
		if (err != ESP_OK)
			break;
		if (memcmp(dev->sector_buf, data, PARTITION_SECTOR_SIZE) == 0)
			continue;
#endif
		err = esp_partition_erase_range(dev->u.part, off,
						PARTITION_SECTOR_SIZE);
		if (err != ESP_OK)
			break;
		err = esp_partition_write(dev->u.part, off, data,
					  PARTITION_SECTOR_SIZE);
		if (err != ESP_OK)
			break;
	}
	if (err != ESP_OK)
		ESP_LOGE(TAG, "partition write %llu+%u: %s",
			 (unsigned long long)blk_id, (unsigned)blk_cnt,
			 esp_err_to_name(err));
	return esp_to_errno(err);
}

/* ---------------------------------------------------------------- common */

static esp_err_t common_init(ext4_esp_blockdev_t *dev, uint32_t bsize,
			     uint64_t bcnt)
{
	dev->ph_bbuf = heap_caps_malloc(bsize, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
	if (!dev->ph_bbuf)
		return ESP_ERR_NO_MEM;

	dev->mutex = xSemaphoreCreateMutexStatic(&dev->mutex_buf);

	dev->iface.open = bd_open;
	dev->iface.close = bd_close;
	dev->iface.lock = bd_lock;
	dev->iface.unlock = bd_unlock;
	dev->iface.ph_bsize = bsize;
	dev->iface.ph_bcnt = bcnt;
	dev->iface.ph_bbuf = dev->ph_bbuf;
	dev->iface.p_user = dev;

	dev->bdev.bdif = &dev->iface;
	dev->bdev.part_offset = 0;
	dev->bdev.part_size = bcnt * bsize;
	return ESP_OK;
}

esp_err_t ext4_esp_blockdev_init_sdmmc(ext4_esp_blockdev_t *dev,
				       sdmmc_card_t *card)
{
	if (!dev || !card || card->csd.sector_size <= 0 ||
	    card->csd.capacity <= 0)
		return ESP_ERR_INVALID_ARG;

	memset(dev, 0, sizeof(*dev));
	dev->backend = EXT4_ESP_BACKEND_SDMMC;
	dev->u.card = card;

	esp_err_t err = common_init(dev, (uint32_t)card->csd.sector_size,
				    (uint64_t)card->csd.capacity);
	if (err != ESP_OK)
		return err;
	dev->iface.bread = sd_bread;
	dev->iface.bwrite = sd_bwrite;
	return ESP_OK;
}

esp_err_t ext4_esp_blockdev_init_partition(ext4_esp_blockdev_t *dev,
					   const esp_partition_t *part)
{
	if (!dev || !part || part->type != ESP_PARTITION_TYPE_DATA ||
	    part->size < PARTITION_SECTOR_SIZE ||
	    part->size % PARTITION_SECTOR_SIZE)
		return ESP_ERR_INVALID_ARG;
	if (part->erase_size != PARTITION_SECTOR_SIZE) {
		ESP_LOGE(TAG, "partition %s: unsupported erase size %u",
			 part->label, (unsigned)part->erase_size);
		return ESP_ERR_NOT_SUPPORTED;
	}

	memset(dev, 0, sizeof(*dev));
	dev->backend = EXT4_ESP_BACKEND_PARTITION;
	dev->u.part = part;

	esp_err_t err = common_init(dev, PARTITION_SECTOR_SIZE,
				    part->size / PARTITION_SECTOR_SIZE);
	if (err != ESP_OK)
		return err;
#if CONFIG_LWEXT4_PARTITION_SKIP_IDENTICAL
	dev->sector_buf = malloc(PARTITION_SECTOR_SIZE);
	if (!dev->sector_buf) {
		ext4_esp_blockdev_deinit(dev);
		return ESP_ERR_NO_MEM;
	}
#endif
	dev->iface.bread = part_bread;
	dev->iface.bwrite = part_bwrite;
	return ESP_OK;
}

void ext4_esp_blockdev_deinit(ext4_esp_blockdev_t *dev)
{
	if (!dev)
		return;
	if (dev->mutex)
		vSemaphoreDelete(dev->mutex);
#if CONFIG_LWEXT4_PARTITION_MMAP_READS
	if (dev->map_ptr)
		esp_partition_munmap(dev->map_handle);
	dev->map_ptr = NULL;
#endif
	free(dev->ph_bbuf);
	free(dev->sector_buf);
	dev->mutex = NULL;
	dev->ph_bbuf = NULL;
	dev->sector_buf = NULL;
}
