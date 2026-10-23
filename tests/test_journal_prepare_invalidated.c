/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A damaged filesystem can share a data block with its journal (fork
 * issue #219, found by fuzz_rw). A transaction changes that block and
 * then frees it, invalidating its buffer and clearing its write callback.
 * Preparing the transaction must discard the invalidated buffer before
 * writing any journal blocks: writing the journal block that shares it
 * makes it dirty again, so the transaction journalled it without a
 * callback to count it as written. The checkpoint wrote it uncounted;
 * once the block was read again (another directory of the image mapped
 * it), ext4_journal_stop() found a clean buffer to flush, which counts
 * nothing, and waited forever.
 *
 * Put an invalidated buffer between two dirty ones in a transaction, at
 * the journal's descriptor block and then at its first data block, and
 * read the block again after the commit. Stopping the journal must
 * return, with the two dirty blocks written and kept across a remount.
 */

#include "test_util.h"

#include "../../blockdev/linux/file_dev.h"

#include <ext4_bcache.h>
#include <ext4_blockdev.h>
#include <ext4_fs.h>
#include <ext4_journal.h>
#include <ext4_misc.h>

#include <signal.h>
#include <string.h>
#include <unistd.h>

static void hang(int sig)
{
	(void)sig;
	fprintf(stderr, "ext4_journal_stop() does not return\n");
	_exit(1);
}

static ext4_fsblk_t file_block(const char *path)
{
	struct ext4_inode inode;
	uint32_t ino;

	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(path, &ino, &inode));
	return to_le32(inode.blocks[0]);
}

static void add_block(struct jbd_trans *trans, ext4_fsblk_t lba, int value,
		      bool invalidate)
{
	struct ext4_blockdev *bdev = trans->journal->jbd_fs->bdev;
	struct ext4_block block;

	TEST_ASSERT_EQ(EOK, ext4_block_get(bdev, &block, lba));
	memset(block.data, value, trans->journal->block_size);
	TEST_ASSERT_EQ(EOK, jbd_trans_set_block_dirty(trans, &block));
	if (invalidate)
		ext4_bcache_invalidate_buf(bdev->bc, block.buf);
	TEST_ASSERT_EQ(EOK, ext4_block_set(bdev, &block));
}

static void expect_file(const char *path, int value)
{
	char data[1024];
	size_t n;
	ext4_file f;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, data, sizeof(data), &n));
	TEST_ASSERT_EQ(sizeof(data), n);
	for (size_t i = 0; i < n; i++)
		TEST_ASSERT_EQ(value, data[i]);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_blockdev *bdev = file_dev_get();

	signal(SIGALRM, hang);
	for (unsigned slot = 0; slot < 2; slot++) {
		struct jbd_journal *journal;
		struct jbd_trans *trans;
		struct ext4_block block;
		ext4_fsblk_t a, b, alias;

		TEST_ASSERT_EQ(EOK, test_mount(image, false));
		TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
		TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
		journal = bdev->fs->jbd_journal;
		TEST_ASSERT(journal != NULL);
		TEST_ASSERT_EQ(1024, journal->block_size);
		a = file_block(TEST_MP "a");
		b = file_block(TEST_MP "b");
		TEST_ASSERT_EQ(EOK, jbd_inode_bmap(journal->jbd_fs,
						 journal->first + slot, &alias));
		TEST_ASSERT(a != b && a != alias && b != alias);

		trans = jbd_journal_new_trans(journal);
		TEST_ASSERT(trans != NULL);
		/* Buffers are inserted at the head: b, alias, a. */
		add_block(trans, a, 'A' + slot, false);
		add_block(trans, alias, 'X', true);
		add_block(trans, b, 'B' + slot, false);
		TEST_ASSERT_EQ(EOK, jbd_journal_commit_trans(journal, trans));

		/* Read the block again, as a directory of the damaged
		 * filesystem that maps it would. */
		TEST_ASSERT_EQ(EOK, ext4_block_get(bdev, &block, alias));
		TEST_ASSERT_EQ(EOK, ext4_block_set(bdev, &block));

		alarm(20);
		TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
		alarm(0);
		expect_file(TEST_MP "a", 'A' + slot);
		expect_file(TEST_MP "b", 'B' + slot);
		test_umount();

		TEST_ASSERT_EQ(EOK, test_mount(image, true));
		expect_file(TEST_MP "a", 'A' + slot);
		expect_file(TEST_MP "b", 'B' + slot);
		test_umount();
	}
	return 0;
}
