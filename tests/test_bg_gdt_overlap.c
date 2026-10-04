/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A block bitmap on the group descriptors (fork issue #160, found by
 * fuzz_mount). The location check of a group descriptor only made sure
 * its bitmaps and inode table lie after the superblock; initialising an
 * uninitialised block bitmap at the descriptor block then zeroed the
 * descriptors in the cache, the inode table address read 0, and releasing
 * the root i-node "loaded" from block 0 aborted. Linux rejects such a
 * descriptor.
 *
 * Group 0 of the image (see the .sh) is such a group: reading the root
 * directory must fail with an error, not abort, and the image stays as it
 * was (.check.sh).
 */

#include "test_util.h"

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	ext4_dir d;
	int r;

	r = test_mount(image, true);
	if (r == EOK) {
		r = ext4_dir_open(&d, TEST_MP);
		if (r == EOK)
			ext4_dir_close(&d);
		TEST_ASSERT(r != EOK);
		TEST_ASSERT(ext4_inode_exist(TEST_MP "lost+found",
					     EXT4_DE_DIR) != EOK);
		test_umount();
	} else {
		ext4_device_unregister(TEST_DEV);
	}
	return 0;
}
