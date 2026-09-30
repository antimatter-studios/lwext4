/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A block group descriptor whose block bitmap, inode bitmap and inode table
 * are all 0 (found by fuzzing mount on damaged images). Nothing checked the
 * locations: lwext4 read the root inode from "inode table" block 0 and then
 * released block 0, which aborted the program in ext4_bcache_free()
 * (Assertion `b->lb_id' failed; block 0 means "no block" there). For a
 * group with uninitialised bitmaps it would even have written them over
 * block 0. Locations outside the filesystem, or on or before the block
 * holding the superblock, must be refused with EIO.
 */

#include "test_util.h"

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	ext4_dir d;
	int r;

	r = test_mount(image, true);
	if (r == EOK) {
		TEST_ASSERT_EQ(EIO, ext4_dir_open(&d, TEST_MP));
		TEST_ASSERT_EQ(EIO, ext4_inode_exist(TEST_MP "a.txt",
						     EXT4_DE_REG_FILE));
		test_umount();
	} else {
		TEST_ASSERT_EQ(EIO, r);
		ext4_device_unregister(TEST_DEV);
	}
	return 0;
}
