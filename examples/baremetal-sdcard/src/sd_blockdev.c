/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 block device on top of the SD card driver, see sd_blockdev.h.
 *
 * lwext4 talks to storage through struct ext4_blockdev: open/close and
 * bread/bwrite of whole physical blocks (here the card's 512 byte
 * sectors). lwext4 itself translates its file system blocks (1-4 KiB) into
 * runs of physical blocks and adds the partition offset, so a multi-block
 * read of one 4 KiB file system block arrives here as blk_cnt = 8 and goes
 * to the card as a single CMD18.
 */
#include "sd_blockdev.h"

#include <stddef.h>

#include <ext4_errno.h>
#include <ext4_mbr.h>

#include "sd_spi.h"

static int sd_bd_open(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK; /* the card was initialised by sd_init() */
}

static int sd_bd_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		       uint32_t blk_cnt)
{
	(void)bdev;
	if (blk_id + blk_cnt > sd.sectors)
		return EIO;
	return sd_read(buf, (uint32_t)blk_id, blk_cnt) == 0 ? EOK : EIO;
}

static int sd_bd_bwrite(struct ext4_blockdev *bdev, const void *buf,
			uint64_t blk_id, uint32_t blk_cnt)
{
	(void)bdev;
	if (blk_id + blk_cnt > sd.sectors)
		return EIO;
	return sd_write(buf, (uint32_t)blk_id, blk_cnt) == 0 ? EOK : EIO;
}

static int sd_bd_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

/*
 * Static instance: interface, one sector of scratch buffer (ph_bbuf, used
 * by lwext4 for unaligned byte accesses) and the device. The block count
 * is filled in at run time. No lock/unlock: single threaded.
 */
EXT4_BLOCKDEV_STATIC_INSTANCE(sd_card_bd, SD_SECTOR_SIZE, 0, sd_bd_open,
			      sd_bd_bread, sd_bd_bwrite, sd_bd_close, NULL,
			      NULL);

static struct ext4_mbr_bdevs mbr;

struct ext4_blockdev *sd_blockdev_card(void)
{
	sd_card_bd.bdif->ph_bcnt = sd.sectors;
	sd_card_bd.part_size = (uint64_t)sd.sectors * SD_SECTOR_SIZE;
	return &sd_card_bd;
}

struct ext4_blockdev *sd_blockdev_partition(int n)
{
	if (n < 0 || n > 3)
		return NULL;
	if (ext4_mbr_scan(sd_blockdev_card(), &mbr) != EOK)
		return NULL;
	return mbr.partitions[n].bdif ? &mbr.partitions[n] : NULL;
}
