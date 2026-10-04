/* SPDX-License-Identifier: BSD-3-Clause */
/* Benchmark of the operations of lwext4 (fork issue #165): for each, the
 * CPU cost (bench_counter: instructions on an emulator), the block reads
 * and writes of the device, the peak heap and the peak stack. One line per
 * operation:
 *
 *   BENCH <platform> <op> <counter> <unit> reads <n> writes <n> heap <bytes>
 *         stack <bytes>
 *
 * The workloads run on a RAM disk of BENCH_DISK bytes with 1 KiB blocks,
 * ext4 with a journal, as an MCU with an SD card would use it. */
#include "bench.h"

#include <ext4.h>
#include <ext4_blockdev.h>
#include <ext4_mkfs.h>

#include <stdio.h>
#include <string.h>

#ifndef BENCH_DISK
#define BENCH_DISK (3ul * 1024ul * 1024ul)
#endif
#define BENCH_BSIZE 512u
#define MP "/b/"

static uint8_t disk[BENCH_DISK] __attribute__((aligned(8)));
static uint32_t reads, writes;

static int dev_open(struct ext4_blockdev *b) { (void)b; return EOK; }
static int dev_close(struct ext4_blockdev *b) { (void)b; return EOK; }

static int dev_bread(struct ext4_blockdev *b, void *buf, uint64_t id,
		     uint32_t cnt)
{
	(void)b;
	if ((id + cnt) * BENCH_BSIZE > BENCH_DISK)
		return EIO;
	reads++;
	memcpy(buf, disk + id * BENCH_BSIZE, cnt * BENCH_BSIZE);
	return EOK;
}

static int dev_bwrite(struct ext4_blockdev *b, const void *buf, uint64_t id,
		      uint32_t cnt)
{
	(void)b;
	if ((id + cnt) * BENCH_BSIZE > BENCH_DISK)
		return EIO;
	writes++;
	memcpy(disk + id * BENCH_BSIZE, buf, cnt * BENCH_BSIZE);
	return EOK;
}

EXT4_BLOCKDEV_STATIC_INSTANCE(bench_dev, BENCH_BSIZE, BENCH_DISK / BENCH_BSIZE,
			      dev_open, dev_bread, dev_bwrite, dev_close, 0, 0);

static struct ext4_fs fs;
static uint8_t buf[4096];
static int failed;

/* Measuring one operation */
static uint64_t t0;
static uint32_t r0, w0;
static const char *op_name;

static void start(const char *name)
{
	op_name = name;
	bench_heap_reset();
	bench_stack_paint();
	r0 = reads;
	w0 = writes;
	t0 = bench_counter();
}

static void stop(int r)
{
	uint64_t t = bench_counter() - t0;
	char line[200];

	if (r != EOK) {
		snprintf(line, sizeof(line), "FAIL: %s returned %d", op_name, r);
		bench_print(line);
		failed = 1;
		return;
	}
	snprintf(line, sizeof(line),
		 "BENCH %s %s %llu %s reads %u writes %u heap %u stack %u",
		 bench_platform, op_name, (unsigned long long)t,
		 bench_counter_unit, (unsigned)(reads - r0),
		 (unsigned)(writes - w0), (unsigned)bench_heap_peak(),
		 (unsigned)bench_stack_used());
	bench_print(line);
}

static int write_file(const char *path, size_t len)
{
	ext4_file f;
	size_t n, done = 0;
	int r = ext4_fopen(&f, path, "wb");

	while (r == EOK && done < len) {
		size_t chunk = len - done < sizeof(buf) ? len - done
							 : sizeof(buf);
		r = ext4_fwrite(&f, buf, chunk, &n);
		done += n;
	}
	if (r == EOK)
		r = ext4_fclose(&f);
	return r;
}

static int read_file(const char *path)
{
	ext4_file f;
	size_t n;
	int r = ext4_fopen(&f, path, "rb");

	while (r == EOK) {
		r = ext4_fread(&f, buf, sizeof(buf), &n);
		if (!n)
			break;
	}
	if (r == EOK)
		r = ext4_fclose(&f);
	return r;
}

int bench_main(void)
{
	/* The smallest journal (1 MiB), leaving room for the 1 MiB file */
	struct ext4_mkfs_info info = {
		.block_size = 1024, .journal = true,
		.journal_blocks = EXT4_MKFS_MIN_JOURNAL_BLOCKS};
	char path[64];
	ext4_file f;
	int r, i;

	memset(buf, 'b', sizeof(buf));
	bench_print("lwext4 benchmark: ext4 with a journal, 1 KiB blocks");

	start("mkfs");
	stop(ext4_mkfs(&fs, &bench_dev, &info, F_SET_EXT4));

	r = ext4_device_register(&bench_dev, "bench");
	if (r != EOK) {
		bench_print("FAIL: ext4_device_register");
		return 1;
	}

	start("mount");
	r = ext4_mount("bench", MP, false);
	if (r == EOK)
		r = ext4_recover(MP);
	if (r == EOK)
		r = ext4_journal_start(MP);
	stop(r);
	if (r != EOK)
		return 1;

	start("create");
	r = ext4_fopen(&f, MP "empty", "wb");
	if (r == EOK)
		r = ext4_fclose(&f);
	stop(r);

	start("write-4k");
	stop(write_file(MP "small", 4096));

	start("write-1m");
	stop(write_file(MP "big", 1024 * 1024));

	start("read-1m");
	stop(read_file(MP "big"));

	/* A directory of 200 entries, then lookups in it */
	r = ext4_dir_mk(MP "dir");
	for (i = 0; r == EOK && i < 200; i++) {
		snprintf(path, sizeof(path), MP "dir/entry_%03d", i);
		r = ext4_fopen(&f, path, "wb");
		if (r == EOK)
			r = ext4_fclose(&f);
	}
	start("lookup-x100");
	for (i = 0; r == EOK && i < 100; i++) {
		snprintf(path, sizeof(path), MP "dir/entry_%03d", i * 2);
		r = ext4_inode_exist(path, EXT4_DE_REG_FILE);
	}
	stop(r);

	start("truncate-1m");
	r = ext4_fopen(&f, MP "big", "r+b");
	if (r == EOK)
		r = ext4_ftruncate(&f, 0);
	if (r == EOK)
		r = ext4_fclose(&f);
	stop(r);

	start("unlink");
	stop(ext4_fremove(MP "small"));

	start("umount");
	r = ext4_journal_stop(MP);
	if (r == EOK)
		r = ext4_umount(MP);
	stop(r);
	ext4_device_unregister("bench");

	bench_print(failed ? "FAIL" : "PASS");
	return failed;
}
