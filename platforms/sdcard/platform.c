/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * The platform API (platforms/platform.h) on the SD card boards: the
 * console is the board's UART (stdout through startup.c's _write), the
 * disk is the whole SD card, and the command line is a line typed on the
 * console when firmware asks for it.
 *
 * There is nothing to stop on a board: platform_exit() prints
 * "EXIT <status>" and waits for an interrupt that never comes, which is
 * also how the Renode tests see the end of a run.
 */
#include "../platform.h"

#include "board.h"
#include "sd_spi.h"

#include <ext4_blockdev.h>
#include <ext4_errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const platform_counter_unit = "none";

static unsigned long writes, cut_after;
static int cut;

void platform_init(void)
{
	board_init();
}

void platform_exit(int status)
{
	printf("EXIT %d\n", status);
	fflush(stdout);
	for (;;)
		__asm volatile("wfi");
}

/* No cycle counter common to every core here (the Cortex-M0+ has none) */
uint64_t platform_counter(void)
{
	return 0;
}

/* Prints READY and reads one line from the console, echoing it: options
 * for tests (cut=<n>, records=<n>, ...), or just Enter on a real board */
const char *platform_cmdline(void)
{
	static char line[128];
	static int done;
	size_t n = 0;

	if (done)
		return line;
	done = 1;
	printf("READY\n");
	fflush(stdout);
	for (;;) {
		int c = board_uart_getc();

		if (c == '\r' || c == '\n')
			break;
		if (c >= ' ' && n < sizeof(line) - 1) {
			line[n++] = (char)c;
			board_uart_putc((char)c); /* echo, as a terminal expects */
		}
	}
	line[n] = 0;
	printf("\n");
	return line;
}

static int disk_open(struct ext4_blockdev *bdev);
static int disk_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt);
static int disk_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt);
static int disk_close(struct ext4_blockdev *bdev);

EXT4_BLOCKDEV_STATIC_INSTANCE(disk, SD_SECTOR_SIZE, 0, disk_open, disk_bread,
			      disk_bwrite, disk_close, NULL, NULL);

static int disk_open(struct ext4_blockdev *bdev)
{
	bdev->bdif->ph_bcnt = sd.sectors;
	bdev->part_offset = 0;
	bdev->part_size = (uint64_t)sd.sectors * SD_SECTOR_SIZE;
	return EOK;
}

static int disk_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		      uint32_t blk_cnt)
{
	(void)bdev;
	return sd_read(buf, (uint32_t)blk_id, blk_cnt) == 0 ? EOK : EIO;
}

static int disk_bwrite(struct ext4_blockdev *bdev, const void *buf,
		       uint64_t blk_id, uint32_t blk_cnt)
{
	(void)bdev;
	if (cut && writes == cut_after) {
		printf("POWER CUT before block write %lu\n", writes + 1);
		fflush(stdout);
		for (;;)
			__asm volatile("wfi");
	}
	writes++;
	return sd_write(buf, (uint32_t)blk_id, blk_cnt) == 0 ? EOK : EIO;
}

static int disk_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

/* The whole card. cut=<n> on the command line cuts the power at the block
 * write after the n-th (see platform.h). */
struct ext4_blockdev *platform_disk(void)
{
	const char *opt = strstr(platform_cmdline(), "cut=");
	int r = sd_init();

	if (r != 0) {
		printf("sd_init failed at step %d\n", -r);
		return NULL;
	}
	if (opt) {
		cut = 1;
		cut_after = strtoul(opt + 4, NULL, 10);
	}
	return &disk;
}
