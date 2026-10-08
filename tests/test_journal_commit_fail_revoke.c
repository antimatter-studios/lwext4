/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A transaction frees a block that the transaction before it journalled,
 * and then cannot be committed (fork issue #207, found by fuzz_rwx).
 *
 * Freeing the block revokes it and drops it from the cache, with the
 * callback that tells the earlier transaction its copy reached the disk.
 * The commit failed in jbd_journal_prepare() or later (here: the journal
 * inode has a hole), after the block had been handed back to the earlier
 * transaction, and the journal did not go into its error state. When the
 * block was read again, ext4_journal_stop() found it clean and up to date,
 * "wrote" it without a callback, and looped forever waiting for the
 * earlier transaction to complete.
 *
 * A failed commit must leave the journal in its error state, like a failed
 * write: ext4_journal_stop() returns the error without writing anything,
 * and the journal keeps the earlier transactions for replay.
 */

#include "test_util.h"

#include "../../blockdev/linux/file_dev.h"

#include <ext4_blockdev.h>
#include <ext4_misc.h>

#include <signal.h>
#include <unistd.h>

static void hang(int sig)
{
	(void)sig;
	fprintf(stderr, "ext4_journal_stop() does not return\n");
	_exit(1);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_blockdev *bdev = file_dev_get();
	struct ext4_inode inode;
	struct ext4_block b;
	uint32_t ino, blk;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	/* Committed blocks stay in the cache, not yet at their places */
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, true));

	/* The last transaction journals the subdirectory's block */
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d"));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d/e"));
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "d/e", &ino, &inode));
	/* i_block: extent header, then the first extent's start (low) */
	blk = to_le32(inode.blocks[5]);
	TEST_ASSERT(blk != 0);

	/* The next one writes it (a fresh index root, ext4_trunc_dir())
	 * and frees it, and its commit reaches the hole */
	TEST_ASSERT(ext4_dir_rm(TEST_MP "d") != EOK);

	/* The block is read again (from the disk: clean, up to date) */
	TEST_ASSERT_EQ(EOK, ext4_block_get(bdev, &b, blk));
	TEST_ASSERT_EQ(EOK, ext4_block_set(bdev, &b));

	signal(SIGALRM, hang);
	alarm(20);
	TEST_ASSERT(ext4_journal_stop(TEST_MP) != EOK);
	alarm(0);

	ext4_cache_write_back(TEST_MP, false);
	test_umount();

	/* The replay restores the committed transactions: the directories
	 * are there, the failed removal is not */
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "d/e", &ino, &inode));
	test_umount();
	return 0;
}
