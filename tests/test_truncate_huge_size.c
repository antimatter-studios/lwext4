/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Truncating an i-node with a damaged huge size (fork issue #152, found by
 * fuzz_rw). ext4_trunc_inode released a file in steps of
 * CONFIG_MAX_TRUNCATE_SIZE from its size down: about 2^62 / step steps for
 * these files, which never ended. Nothing is mapped above where the data
 * ends, so the steps start there now.
 *
 * The orphan of the .sh is released by the mount, /big is removed; both
 * must finish (an alarm ends the test otherwise), and e2fsck must find the
 * filesystem clean.
 */

#include "test_util.h"

#include <ext4_misc.h>

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_mount_stats before, after;
	struct ext4_sblock *sb = NULL;
	char cmd[1024];

	alarm(20);
	/* The mount releases the orphan */
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_get_sblock(TEST_MP, &sb));
	TEST_ASSERT_EQ(0u, ext4_get32(sb, last_orphan));
	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &before));

	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "big"));
	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &after));
	/* Its three blocks are free again */
	TEST_ASSERT(after.free_blocks_count >= before.free_blocks_count + 3);
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
	return 0;
}
