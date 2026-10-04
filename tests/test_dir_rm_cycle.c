/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_dir_rm on a damaged tree with a cycle (fork issue #150, found by
 * fuzz_rw). ext4_dir_rm removes a tree by descending into directories that
 * have children; an entry pointing back to an ancestor made it descend
 * forever.
 *
 * Removing /d (two step cycle) and /e (three step cycle, see the .sh) must
 * fail with EIO, quickly (an alarm ends the test otherwise), and the
 * filesystem stay usable: /ok is still removed.
 */

#include "test_util.h"

#include <signal.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);

	alarm(20);
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EIO, ext4_dir_rm(TEST_MP "d"));
	TEST_ASSERT_EQ(EIO, ext4_dir_rm(TEST_MP "e"));
	TEST_ASSERT_EQ(EOK, ext4_dir_rm(TEST_MP "ok"));
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "ok", EXT4_DE_DIR));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "lost+found",
					     EXT4_DE_DIR));
	test_umount();
	return 0;
}
