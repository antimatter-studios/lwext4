/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Stopping the journal after writes failed and work again (fork issue
 * #123).
 *
 * When a write fails, the journal goes into its error state, and
 * ext4_journal_stop() then discards the committed transactions without
 * writing them (jbd_journal_discard_trans()): they stay in the journal for
 * replay. Discarding a transaction releases the cache buffers of its
 * blocks. A released dirty buffer is written, and its end_write callback,
 * if it still pointed into the journal, freed the jbd_bufs of that block,
 * also the one being discarded: a heap-use-after-free (ASan), when the
 * callback belonged to another jbd_buf than the one being discarded.
 *
 * When only writes after the last commit fail, the journal has no error,
 * and ext4_journal_stop() writes the committed blocks to their places
 * (jbd_journal_flush_trans()). Reading a block's journal copy could make
 * room in the cache by writing another dirty buffer, whose end_write
 * callback freed the next jbd_buf of the transaction being flushed: also a
 * heap-use-after-free.
 *
 * Every write from the Nth on fails during ext4_dir_rm() of a large
 * directory, then the device works again: for every N with 100 files (the
 * first case), and for the last 256 writes with 300 files (the second
 * one). ext4_journal_stop() must not touch freed memory; after the next
 * mount replays the journal, an ext4_dir_rm() that returned EOK must have
 * removed the directory, and the filesystem must be consistent (e2fsck
 * -fn).
 */

#include "fault_dev.h"

#include <stdlib.h>
#include <string.h>

static void copy(const char *src, const char *dst)
{
	static char buf[1 << 16];
	FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
	size_t n;

	TEST_ASSERT(a && b);
	while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
		TEST_ASSERT_EQ(n, fwrite(buf, 1, n, b));
	fclose(a);
	fclose(b);
}

static void fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null",
		 image);
	TEST_ASSERT_EQ(0, system(cmd));
}

/* ext4_dir_rm() with every write from the nth on failing. Returns false
 * once no write failed. */
static bool run(const char *image, const char *pristine, uint64_t n)
{
	struct ext4_inode inode;
	uint32_t ino;
	int r, r2;

	copy(pristine, image);
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));

	fault_dev_fail_nth(FAULT_DEV_WRITE, n, 0);
	r = ext4_dir_rm(TEST_MP "many");
	fault_dev_disarm();
	if (!fault_dev_failed()) {
		/* N is past the last write */
		TEST_ASSERT_EQ(EOK, r);
		TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
		test_umount();
		fsck(image);
		return false;
	}

	/* The device works again. Stopping the journal writes what it can,
	 * or keeps it marked for replay. */
	(void)ext4_journal_stop(TEST_MP);
	test_umount();

	/* The next mount replays what was committed. A write that failed
	 * after the last commit (to a block's place on the disk) leaves EOK
	 * correct: the replay writes the block. */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	r2 = ext4_raw_inode_fill(TEST_MP "many", &ino, &inode);
	test_umount();
	if (r == EOK && r2 != ENOENT) {
		fprintf(stderr, "%s N=%llu: ext4_dir_rm() returned EOK, but "
			"after the replay the directory is still there (%d)\n",
			image, (unsigned long long)n, r2);
		exit(1);
	}
	/* e2fsck takes most of the time: check every 8th run */
	if (n % 8 == 0)
		fsck(image);
	return true;
}

/* Writes of ext4_dir_rm() without faults */
static uint64_t count_writes(const char *image, const char *pristine)
{
	uint64_t writes;

	copy(pristine, image);
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	writes = fault_dev_state.writes;
	TEST_ASSERT_EQ(EOK, ext4_dir_rm(TEST_MP "many"));
	writes = fault_dev_state.writes - writes;
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();
	return writes;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char pristine[512], big[512], big_pristine[512];
	uint64_t n, writes;

	snprintf(pristine, sizeof(pristine), "%s.pristine", image);
	snprintf(big, sizeof(big), "%s.big", image);
	snprintf(big_pristine, sizeof(big_pristine), "%s.big.pristine", image);

	for (n = 1; run(image, pristine, n); n++)
		TEST_ASSERT(n < 5000);
	TEST_ASSERT(n > 100);
	printf("100 files: %llu failing writes handled\n",
	       (unsigned long long)n - 1);

	writes = count_writes(big, big_pristine);
	TEST_ASSERT(writes > 256);
	for (n = writes - 255; run(big, big_pristine, n); n++)
		TEST_ASSERT(n <= writes);
	TEST_ASSERT_EQ(writes + 1, n);
	printf("300 files: the last 256 of %llu writes failing handled\n",
	       (unsigned long long)writes);
	return 0;
}
