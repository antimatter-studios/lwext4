/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 example/test firmware for microcontroller boards with an SD card on
 * SPI. The same image runs on the real board and in Renode.
 *
 * The firmware talks to an SD card through the MCU's SPI controller (see
 * sd_spi.c) and to the test harness through the board's console UART. After
 * reset it identifies the card, prints "READY" and waits for one command
 * line:
 *
 *   hostimg        mount the ext4 partition of a card prepared on the host
 *                  (mke2fs -d), check the files the host put there, run the
 *                  workload, remount and verify.
 *   mkfs <bsize>   partition the card (ext4_mbr_write), ext4_mkfs the first
 *                  partition, run the workload, remount and verify.
 *   torture        mount and create/rename/remove files forever; the harness
 *                  cuts the power at an arbitrary point.
 *   recover        after a power cut: mount, replay the journal, check the
 *                  file system is usable, umount.
 *
 * The result is printed as "LWEXT4-TEST: PASS" or "LWEXT4-TEST: FAIL: ...",
 * followed by RAM usage figures. The host then checks the card image with
 * e2fsck and debugfs (tests/renode/scripts/sdimage.py) and must see the
 * same files the firmware wrote.
 */
#include <ext4.h>
#include <ext4_mbr.h>
#include <ext4_mkfs.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "sd_blockdev.h"
#include "sd_spi.h"
#include "sysmem.h"
#include "workload.h"

/* ------------------------------------------------------------------------ */
/* Result reporting                                                          */

void test_fail(const char *file, int line, const char *what, int rc)
{
	printf("LWEXT4-TEST: FAIL: %s:%d: %s (rc=%d)\n", file, line, what, rc);
	sysmem_report();
	for (;;)
		;
}

#define CHECK(expr)                                                            \
	do {                                                                   \
		int r_ = (expr);                                               \
		if (r_ != EOK)                                                 \
			test_fail(__FILE__, __LINE__, #expr, r_);              \
	} while (0)

#define ASSERT(cond)                                                           \
	do {                                                                   \
		if (!(cond))                                                   \
			test_fail(__FILE__, __LINE__, #cond, 0);               \
	} while (0)

/* newlib assert(), used by lwext4 with CONFIG_HAVE_OWN_ASSERT=0 */
void __assert_func(const char *file, int line, const char *func,
		   const char *expr)
{
	(void)func;
	test_fail(file, line, expr, -1);
	for (;;)
		;
}

static struct ext4_blockdev *part;

#ifndef WRITE_BACK_CACHE
#define WRITE_BACK_CACHE 0
#endif

#define DEV "sd0p1"
#define MP "/mp/"

static void find_partition(void)
{
	part = sd_blockdev_partition(0);
	ASSERT(part != NULL);
	printf("partition 1: offset %lu KiB, size %lu KiB\n",
	       (unsigned long)(part->part_offset / 1024),
	       (unsigned long)(part->part_size / 1024));
}

static void mount_fs(void)
{
	int r;

	CHECK(ext4_device_register(part, DEV));
	CHECK(ext4_mount(DEV, MP, false));
	sysmem_phase("mount");
	r = ext4_recover(MP);
	ASSERT(r == EOK || r == ENOTSUP);
	CHECK(ext4_journal_start(MP));
	sysmem_phase("journal start");
#if WRITE_BACK_CACHE
	/*
	 * Delay writing metadata blocks home until the cache is full. Fewer
	 * card writes, but the journal keeps a record per dirty block (and one
	 * per transaction) until then: more RAM, bounded by the journal size.
	 */
	CHECK(ext4_cache_write_back(MP, true));
#endif
}

static void umount_fs(void)
{
#if WRITE_BACK_CACHE
	CHECK(ext4_cache_write_back(MP, false));
#endif
	CHECK(ext4_journal_stop(MP));
	CHECK(ext4_umount(MP));
	CHECK(ext4_device_unregister(DEV));
}

static void print_fs_stats(void)
{
	struct ext4_mount_stats st;

	CHECK(ext4_mount_point_stats(MP, &st));
	printf("fs: block size %lu, %lu/%lu blocks free, %lu/%lu inodes free\n",
	       (unsigned long)st.block_size,
	       (unsigned long)st.free_blocks_count,
	       (unsigned long)st.blocks_count,
	       (unsigned long)st.free_inodes_count,
	       (unsigned long)st.inodes_count);
}

/* ------------------------------------------------------------------------ */
/* Commands                                                                  */

static void cmd_hostimg(void)
{
	find_partition();
	mount_fs();
	print_fs_stats();
	printf("checking files created by mke2fs -d\n");
	workload_check_host_files(MP);
	printf("running workload\n");
	workload_run(MP);
	workload_modify_host_files(MP);
	workload_verify(MP);
	umount_fs();

	printf("remount\n");
	mount_fs();
	workload_verify(MP);
	workload_verify_host_files_modified(MP);
	umount_fs();
}

static void cmd_mkfs(uint32_t block_size)
{
	struct ext4_mbr_parts parts = {.division = {100, 0, 0, 0}};
	struct ext4_mkfs_info info = {
		.block_size = block_size,
		.journal = true,
		.label = "lwext4-renode",
	};
	static struct ext4_fs fs;

	printf("writing MBR\n");
	CHECK(ext4_mbr_write(sd_blockdev_card(), &parts, 0x4c574558));
	find_partition();
	printf("ext4_mkfs, block size %lu\n", (unsigned long)block_size);
	CHECK(ext4_mkfs(&fs, part, &info, F_SET_EXT4));
	sysmem_phase("mkfs");

	mount_fs();
	print_fs_stats();
	printf("running workload\n");
	workload_run(MP);
	workload_verify(MP);
	umount_fs();

	printf("remount\n");
	mount_fs();
	workload_verify(MP);
	umount_fs();
}

static void cmd_torture(void)
{
	find_partition();
	mount_fs();
	/* The power cut must not lose data that was synced before it. */
	workload_torture_prepare(MP);
	CHECK(ext4_cache_flush(MP));
	printf("TORTURE: running\n");
	for (uint32_t i = 0;; i++) {
		workload_torture_step(MP, i);
		printf("TORTURE: iteration %lu\n", (unsigned long)i);
	}
}

static void cmd_recover(void)
{
	find_partition();
	printf("mount + journal recovery\n");
	CHECK(ext4_device_register(part, DEV));
	CHECK(ext4_mount(DEV, MP, false));
	CHECK(ext4_recover(MP));
	CHECK(ext4_journal_start(MP));
	print_fs_stats();
	workload_torture_check(MP);
	umount_fs();

	printf("remount\n");
	mount_fs();
	workload_torture_check(MP);
	umount_fs();
}

/* Raw card throughput: read 1 MiB with single and multi block reads. */
static void cmd_bench(void)
{
	uint8_t *buf = malloc(8 * SD_SECTOR_SIZE);

	ASSERT(buf != NULL);

	for (uint32_t s = 0; s < 1024; s++)
		ASSERT(sd_read(buf, s, 1) == 0);
	printf("BENCH: single block reads done\n");
	for (uint32_t s = 0; s < 1024; s += 8)
		ASSERT(sd_read(buf, s, 8) == 0);
	printf("BENCH: multi block reads done\n");
	free(buf);
}

static void read_line(char *buf, size_t size)
{
	size_t n = 0;

	for (;;) {
		int c = board_uart_getc();
		if (c == '\r' || c == '\n') {
			if (n == 0)
				continue;
			break;
		}
		if (n + 1 < size)
			buf[n++] = (char)c;
	}
	buf[n] = '\0';
}

int main(void)
{
	char cmd[32];
	int r;

	board_init();
	sysmem_init();
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("\nlwext4 test firmware: %s (%s), RAM %lu KiB\n", board_name,
	       board_mcu, (unsigned long)(sysmem_ram_size() / 1024));

	r = sd_init();
	if (r != 0) {
		printf("sd_init step %d failed\n", -r);
		test_fail(__FILE__, __LINE__, "sd_init", r);
	}
	printf("SD card: %s, %s addressed, %lu sectors (%lu MiB), "
	       "OCR %02x%02x%02x%02x\n",
	       sd.v2 ? "v2" : "v1", sd.block_addr ? "block" : "byte",
	       (unsigned long)sd.sectors, (unsigned long)(sd.sectors / 2048),
	       sd.ocr[0], sd.ocr[1], sd.ocr[2], sd.ocr[3]);

	printf("READY\n");
	read_line(cmd, sizeof(cmd));
	printf("command: %s\n", cmd);

	if (!strcmp(cmd, "hostimg"))
		cmd_hostimg();
	else if (!strncmp(cmd, "mkfs", 4))
		cmd_mkfs(cmd[4] ? (uint32_t)strtoul(cmd + 5, NULL, 10) : 1024);
	else if (!strcmp(cmd, "torture"))
		cmd_torture();
	else if (!strcmp(cmd, "recover"))
		cmd_recover();
	else if (!strcmp(cmd, "bench"))
		cmd_bench();
	else
		test_fail(__FILE__, __LINE__, "unknown command", 0);

	printf("SD: %lu commands, %lu blocks read, %lu blocks written\n",
	       (unsigned long)sd.cmds, (unsigned long)sd.rd_blocks,
	       (unsigned long)sd.wr_blocks);
	printf("LWEXT4-TEST: PASS\n");
	sysmem_report();
	for (;;)
		;
}
