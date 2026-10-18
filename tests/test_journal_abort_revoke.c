/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A transaction frees a block that the transaction before it journalled,
 * and is then aborted (fork issue #217, found by fuzz_rw).
 *
 * In write-back mode a committed block stays dirty in the cache until it
 * is written to its place. Freeing it revokes it and drops it from the
 * cache, with the callback that tells the earlier transaction its copy
 * reached the disk. The abort put back the blocks the transaction had
 * changed, but not that one: the block was in use again, was read back
 * from the disk without the committed change, and the earlier transaction
 * still waited for it to be written. ext4_journal_stop() found it clean
 * and up to date, "wrote" it without a callback, and looped forever.
 *
 * Here (see the .sh) the file f has the block of the directory d as its
 * block. Moving f into d journals that block. ext4_fremove() of d/f frees
 * it, reads d again from the disk, where f is not in it yet, and fails:
 * the abort must put the committed block of d back into the cache. Then d
 * still has f, and ext4_journal_stop() writes the block and returns.
 */

#include "test_util.h"

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

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	/* Committed blocks stay in the cache, not yet at their places */
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, true));

	/* The transaction journals the block of d */
	TEST_ASSERT_EQ(EOK, ext4_frename(TEST_MP "f", TEST_MP "d/f"));

	/* The next one frees it (the block of d/f), reads d from the disk
	 * and does not find f there */
	TEST_ASSERT(ext4_fremove(TEST_MP "d/f") != EOK);

	/* d is the one the rename made */
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "d/f", EXT4_DE_REG_FILE));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "d/x", EXT4_DE_REG_FILE));

	signal(SIGALRM, hang);
	alarm(20);
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	alarm(0);

	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
	test_umount();

	/* The disk has it too, without a journal to replay */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "d/f", EXT4_DE_REG_FILE));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "d/x", EXT4_DE_REG_FILE));
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "f", EXT4_DE_REG_FILE));
	test_umount();
	return 0;
}
