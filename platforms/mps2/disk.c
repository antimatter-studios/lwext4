/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * platform_disk() on the MPS2 boards: a disk image file on the host,
 * reached through semihosting (newlib's rdimon stdio), so that what the
 * firmware writes can be checked with e2fsck and debugfs afterwards.
 *
 * On a real board this file is the storage driver: the same five
 * callbacks over an SD card, eMMC or USB drive (examples/blockdev-template
 * explains each). Writes are unbuffered, so a write that returned is on
 * the disk, as on real storage; cut=<n> stops the machine at the write
 * after the n-th, as a power loss would (see platform.h).
 */
#include "../platform.h"

#include <ext4_blockdev.h>
#include <ext4_errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512

static FILE *image;
static char image_name[64] = "disk.img";
static unsigned long writes, cut_after;
static int cut;

static int disk_open(struct ext4_blockdev *bdev);
static int disk_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt);
static int disk_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt);
static int disk_close(struct ext4_blockdev *bdev);
static int disk_lock(struct ext4_blockdev *bdev);
static int disk_unlock(struct ext4_blockdev *bdev);

EXT4_BLOCKDEV_STATIC_INSTANCE(disk, SECTOR, 0, disk_open, disk_bread,
			      disk_bwrite, disk_close, disk_lock, disk_unlock);

static int disk_open(struct ext4_blockdev *bdev)
{
	long size;

	if (!image) {
		image = fopen(image_name, "r+b");
		if (!image)
			return EIO;
		setvbuf(image, NULL, _IONBF, 0);
	}
	if (fseek(image, 0, SEEK_END) != 0 || (size = ftell(image)) < 0)
		return EIO;
	bdev->bdif->ph_bcnt = (uint64_t)size / SECTOR;
	bdev->part_offset = 0;
	bdev->part_size = bdev->bdif->ph_bcnt * SECTOR;
	return EOK;
}

static int seek(uint64_t blk_id)
{
	return fseek(image, (long)(blk_id * SECTOR), SEEK_SET) == 0 ? EOK : EIO;
}

static int disk_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt)
{
	(void)bdev;
	if (seek(blk_id) != EOK ||
	    fread(buf, SECTOR, blk_cnt, image) != blk_cnt)
		return EIO;
	return EOK;
}

static int disk_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt)
{
	(void)bdev;
	if (cut && writes == cut_after) {
		printf("POWER CUT before block write %lu\n", writes + 1);
		fflush(stdout);
		platform_exit(1);
	}
	writes++;
	if (seek(blk_id) != EOK ||
	    fwrite(buf, SECTOR, blk_cnt, image) != blk_cnt)
		return EIO;
	return EOK;
}

static int disk_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return fflush(image) == 0 ? EOK : EIO;
}

static int disk_lock(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int disk_unlock(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

struct ext4_blockdev *platform_disk(void)
{
	const char *p = platform_cmdline();
	int named = 0;

	/* Words: "cut=<n>", and the image file name (without '=') */
	while (*p) {
		size_t n;

		while (*p == ' ')
			p++;
		n = strcspn(p, " ");
		if (n > 4 && !strncmp(p, "cut=", 4)) {
			cut = 1;
			cut_after = strtoul(p + 4, NULL, 10);
		} else if (!named && n && n < sizeof(image_name) &&
			   !memchr(p, '=', n)) {
			named = 1;
			memcpy(image_name, p, n);
			image_name[n] = 0;
		}
		p += n;
	}
	return &disk;
}
