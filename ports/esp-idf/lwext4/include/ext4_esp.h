/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 glue for ESP-IDF: block devices on top of the ESP-IDF storage
 * drivers, and FreeRTOS based mount point locks.
 *
 * Typical use with an SD card (SDMMC host or SDSPI):
 *
 *     sdmmc_card_t card;                    // from sdmmc_card_init()
 *     ext4_esp_blockdev_t sd;
 *     ESP_ERROR_CHECK(ext4_esp_blockdev_init_sdmmc(&sd, &card));
 *     ext4_device_register(ext4_esp_blockdev(&sd), "sd");
 *     ext4_mount("sd", "/mp/", false);
 *     ext4_mount_setup_locks("/mp/", ext4_esp_mount_locks());
 *
 * or with a data partition in SPI flash (type data, any subtype):
 *
 *     const esp_partition_t *p = esp_partition_find_first(
 *             ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ext4");
 *     ext4_esp_blockdev_t fl;
 *     ESP_ERROR_CHECK(ext4_esp_blockdev_init_partition(&fl, p));
 *
 * Flash partitions are exposed with 4 KiB physical blocks (the erase sector
 * size), so filesystems on them must use a 4 KiB block size
 * (mke2fs -b 4096, or ext4_mkfs_info.block_size = 4096). Writes erase and
 * program whole sectors; there is no wear levelling.
 */
#ifndef EXT4_ESP_H_
#define EXT4_ESP_H_

#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"
#include "sdmmc_cmd.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <ext4.h>
#include <ext4_blockdev.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	EXT4_ESP_BACKEND_SDMMC,
	EXT4_ESP_BACKEND_PARTITION,
} ext4_esp_backend_t;

/** Block device state. Treat as opaque; must stay valid while registered. */
typedef struct {
	struct ext4_blockdev bdev;
	struct ext4_blockdev_iface iface;
	ext4_esp_backend_t backend;
	union {
		sdmmc_card_t *card;
		const esp_partition_t *part;
	} u;
	uint8_t *ph_bbuf;     /* one physical block, used by lwext4 */
	uint8_t *sector_buf;  /* partition backend: read-back buffer */
	const void *map_ptr;  /* partition backend: mapped read window */
	esp_partition_mmap_handle_t map_handle;
	size_t map_off, map_len;
	SemaphoreHandle_t mutex;
	StaticSemaphore_t mutex_buf;
} ext4_esp_blockdev_t;

/** Wrap an initialised SD/MMC card (sdmmc_card_init() done, SDMMC or
 *  SDSPI host). The card must outlive the block device. */
esp_err_t ext4_esp_blockdev_init_sdmmc(ext4_esp_blockdev_t *dev,
				       sdmmc_card_t *card);

/** Wrap a flash data partition. Its size must be a multiple of 4 KiB. */
esp_err_t ext4_esp_blockdev_init_partition(ext4_esp_blockdev_t *dev,
					   const esp_partition_t *part);

/** Free the buffers allocated by ext4_esp_blockdev_init_*(). */
void ext4_esp_blockdev_deinit(ext4_esp_blockdev_t *dev);

static inline struct ext4_blockdev *ext4_esp_blockdev(ext4_esp_blockdev_t *dev)
{
	return &dev->bdev;
}

/** Recursive FreeRTOS mutex usable with ext4_mount_setup_locks(). One
 *  lock is shared by all mount points. */
const struct ext4_lock *ext4_esp_mount_locks(void);

#ifdef __cplusplus
}
#endif

#endif /* EXT4_ESP_H_ */
