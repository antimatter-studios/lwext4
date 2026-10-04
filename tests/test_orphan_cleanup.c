/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Orphan inodes (fork issue #128). When Linux stops while a deleted file
 * is still open, or in the middle of a truncate, the inode stays on the
 * orphan list (s_last_orphan, chained through i_dtime) and Linux finishes
 * the job at the next mount. lwext4 ignored the list: the space was never
 * released, and e2fsck found the filesystem inconsistent.
 *
 * A read-write mount must release the list's inodes (see the .sh): the
 * deleted file is freed, the truncated file loses its blocks past i_size
 * and keeps the first KiB, the list is empty and e2fsck is clean. A list
 * that loops must not hang or free an inode twice.
 */

#include "test_util.h"

#include <ext4_misc.h>

#include <stdio.h>
#include <string.h>

static void fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
}

static uint32_t last_orphan(void)
{
	struct ext4_sblock *sb = NULL;

	TEST_ASSERT_EQ(EOK, ext4_get_sblock(TEST_MP, &sb));
	return ext4_get32(sb, last_orphan);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char loop[512], buf[2048];
	struct ext4_mount_stats before, after;
	ext4_file f;
	size_t n = 0;

	/* Read only: nothing is released, the image stays as it is */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT(last_orphan() != 0);
	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &before));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ((uint32_t)0, last_orphan());
	TEST_ASSERT_EQ(EOK, ext4_mount_point_stats(TEST_MP, &after));
	/* 1 MiB of the deleted file and 63 KiB past the truncated size,
	 * plus their extent and other metadata blocks */
	TEST_ASSERT(after.free_blocks_count >=
		    before.free_blocks_count + 1024 + 63);
	TEST_ASSERT_EQ(before.free_inodes_count + 1, after.free_inodes_count);

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "b", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ((size_t)1024, n);
	TEST_ASSERT(buf[0] == 'a' && buf[1023] == 'a');
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();
	fsck(image);

	snprintf(loop, sizeof(loop), "%s.loop", image);
	TEST_ASSERT_EQ(EOK, test_mount(loop, false));
	TEST_ASSERT_EQ((uint32_t)0, last_orphan());
	test_umount();
	fsck(loop);
	return 0;
}
