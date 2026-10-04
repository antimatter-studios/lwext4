/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Truncating a block mapped file with a damaged huge size (fork issue
 * #162). The truncate steps started at what a block map can address (16.8
 * million blocks with 1 KiB blocks), each block of the hole a lookup. They
 * start after the highest mapped block now.
 *
 * /a and /b (see the .sh) have 300 blocks and a size of about 16 GiB:
 * truncating /a to 0 and removing /b must finish (an alarm ends the test
 * otherwise), free their blocks, and leave e2fsck clean.
 *
 * red-green: guard. On main the block map steps take 1.5 s here, not
 * minutes (the hang of #162 was inline data, test_truncate_inline_huge):
 * this guards that starting after the highest mapped block still releases
 * everything.
 */

#include "test_util.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_mount_stats before, after;
	char cmd[1024];
	ext4_file f;

	alarm(20);
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &before));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "a", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 0));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "b"));

	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &after));
	/* 2 x 300 data blocks, plus their indirect blocks */
	TEST_ASSERT(after.free_blocks_count >= before.free_blocks_count + 600);
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
	return 0;
}
