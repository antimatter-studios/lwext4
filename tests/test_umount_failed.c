/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_umount() that fails, and ext4_umount() called again (fork issue
 * #181, found by fuzz_rwx).
 *
 * When writing the superblock failed, ext4_umount() returned the error
 * and the mount point stayed mounted, but it had already cleared the block
 * device's fs pointer: the next change on the mount point dereferenced it
 * (UBSan in ext4_trans_set_block_dirty, a crash without it). And
 * ext4_umount() looked the mount point up by name whether it was mounted
 * or not: a second call ran ext4_fs_fini() again, writing the old
 * superblock to whatever the device held by then, and released the block
 * cache and the device a second time.
 *
 *  1. A failed ext4_umount() leaves the mount point as it was: files can
 *     still be created, and the next ext4_umount() succeeds; e2fsck -fn
 *     finds the filesystem clean and the file is there.
 *  2. ext4_umount() of a mount point that is not mounted fails with
 *     ENODEV and does no I/O.
 */

#include "fault_dev.h"

#include <stdlib.h>
#include <string.h>

static void fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null",
		 image);
	TEST_ASSERT_EQ(0, system(cmd));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	ext4_file f;

	/* 1. A failed ext4_umount() */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	fault_dev_fail_nth(FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT(ext4_umount(TEST_MP) != EOK);
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "after", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));

	/* 2. ...and once more: not mounted any more */
	fault_dev_fail_nth(FAULT_DEV_READ | FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT_EQ(ENODEV, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(0, fault_dev_failed());
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));
	fsck(image);

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_inode_exist(TEST_MP "after", EXT4_DE_REG_FILE));
	test_umount();
	return 0;
}
