/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Freeing an extent that spans two block groups smaller than their bitmap
 * (mke2fs -g 1024 with 1 KiB blocks, see the .sh), from the orphan list at
 * mount.
 *
 * ext4_balloc_free_blocks() capped a group's share of the range at the
 * bits of a bitmap block (8192) instead of the blocks in the group (1024).
 * The whole extent was cleared in group 1's bitmap: past its end, into
 * the padding, and group 2's blocks stayed allocated (e2fsck: wrong free
 * counts, bitmap padding not set).
 *
 * When ext4_fs_put_block_group_ref() failed (the descriptor block is
 * written through while the orphan list is released), the loop stopped
 * with the next group's blocks still to free, the revoke loop replaced
 * the error with EOK and ext4_assert(count == 0) aborted. The failed
 * write leaves the change in the dirty descriptor block, so the release
 * must go on: the orphan is gone, and after a remount without faults
 * e2fsck finds every count right (no group freed twice).
 */

#include "fault_dev.h"

#include <ext4_super.h>

#include <stdio.h>
#include <string.h>

/* Group descriptors (1 KiB blocks): block 2. */
#define GDT_OFF 2048

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
	char fault[512];

	TEST_ASSERT(strlen(image) + sizeof(".fault") <= sizeof(fault));
	strcpy(fault, image);
	strcat(fault, ".fault");

	/* The orphan is released, every block of it in its own group. */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ((uint32_t)0, last_orphan());
	test_umount();
	fsck(image);

	/* The extents are freed in order. The first write of the descriptor
	 * block is group 0's (first extent), the second group 1's, in the
	 * middle of the extent across groups 1 and 2: fail that one. */
	file_dev_name_set(fault);
	TEST_ASSERT_EQ(EOK, ext4_device_register(fault_dev_wrap(file_dev_get()),
						 TEST_DEV));
	fault_dev_fail_range(FAULT_DEV_WRITE, GDT_OFF, 1024);
	fault_dev_state.nth = 2;
	fault_dev_state.count = 1;
	TEST_ASSERT_EQ(EOK, ext4_mount(TEST_DEV, TEST_MP, false));
	TEST_ASSERT_EQ(1, fault_dev_failed());
	fault_dev_disarm();
	TEST_ASSERT_EQ((uint32_t)0, last_orphan());
	test_umount();

	TEST_ASSERT_EQ(EOK, fault_dev_mount(fault, false));
	test_umount();
	fsck(fault);
	return 0;
}
