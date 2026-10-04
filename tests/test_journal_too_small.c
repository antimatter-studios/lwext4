/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A journal too small for a transaction (fork issue #174, found by
 * fuzz_mkfs). When even an empty journal could not hold the transaction
 * being committed, jbd_journal_alloc_block() hit
 * ext4_assert(journal->last != journal->start) and aborted the program. ext4_mkfs() made such journals
 * on request: it accepted any journal_blocks, e.g. 9, although the jbd2
 * minimum is 1024 blocks (mke2fs refuses less, Linux does not load such a
 * journal).
 *
 *  1. ext4_mkfs() refuses journal_blocks below 1024 with EINVAL (0 still
 *     means "choose": at least 1024).
 *  2. On an image whose journal (see the .sh) has room for 4 blocks, a
 *     ext4_dir_mk() that needs more fails with ENOSPC; the transaction is
 *     dropped, nothing of it reaches the disk, and the filesystem can be
 *     unmounted, mounted again and read.
 */

#include "test_util.h"

#include "../../blockdev/linux/file_dev.h"

#include <ext4_mkfs.h>

#include <string.h>

static struct ext4_fs fs;

static int mkfs(const char *image, uint32_t journal_blocks)
{
	struct ext4_mkfs_info info;
	int r;

	memset(&info, 0, sizeof(info));
	info.len = 4 * 1024 * 1024;
	info.block_size = 1024;
	info.journal = true;
	info.journal_blocks = journal_blocks;
	file_dev_name_set(image);
	r = ext4_mkfs(&fs, file_dev_get(), &info, F_SET_EXT4);
	return r;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char path[512];
	const ext4_direntry *de;
	ext4_dir d;
	int r;

	/* 1. ext4_mkfs */
	snprintf(path, sizeof(path), "%s.mkfs", image);
	TEST_ASSERT_EQ(EINVAL, mkfs(path, 9));
	TEST_ASSERT_EQ(EINVAL, mkfs(path, 1023));
	TEST_ASSERT_EQ(EOK, mkfs(path, 1024));

	/* 2. a journal with room for 4 blocks */
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	r = ext4_dir_mk(TEST_MP "d");
	TEST_ASSERT_EQ(ENOSPC, r);
	(void)ext4_journal_stop(TEST_MP);
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, TEST_MP));
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		TEST_ASSERT(!(de->name_length == 1 && de->name[0] == 'd'));
	ext4_dir_close(&d);
	test_umount();
	return 0;
}
