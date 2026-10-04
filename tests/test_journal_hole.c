/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A journal with a hole (fork issue #146, found by fuzz_rw). jbd_inode_bmap
 * mapped an unmapped journal block to block 0; the commit "loaded" block 0
 * and releasing it aborted on the block cache's assertion. A hole in the
 * journal is damage (Linux reports it as an I/O error).
 *
 * Blocks 5 to 20 of the journal are a hole (see the .sh). Create files, each
 * in its own transaction, until a commit reaches the hole: it must fail with
 * an error, not abort the program, and the filesystem must mount again.
 */

#include "test_util.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char path[64];
	ext4_file f;
	int i, r = EOK;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));

	for (i = 0; i < 200 && r == EOK; i++) {
		snprintf(path, sizeof(path), TEST_MP "f%d", i);
		r = ext4_fopen(&f, path, "wb");
		if (r == EOK)
			r = ext4_fclose(&f);
	}
	/* The journal reached its hole */
	TEST_ASSERT(r != EOK);
	TEST_ASSERT(i < 200);

	(void)ext4_journal_stop(TEST_MP);
	test_umount();

	/* Still a filesystem lwext4 mounts and lists */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "lost+found",
					     EXT4_DE_DIR));
	test_umount();
	return 0;
}
