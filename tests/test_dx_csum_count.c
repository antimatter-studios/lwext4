/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * An htree index node whose count exceeds its limit (fork issue #155,
 * found by fuzz_mount). The metadata checksum of the node was computed over
 * count entries, past the end of the block: AddressSanitizer reports a
 * heap-buffer-overflow, which red-green builds catch. Such a node is now
 * reported as damaged without computing over it, and lookups search the
 * directory linearly.
 *
 * The root of /dir/sub (see the .sh) claims 0xFFFF entries. Look names up
 * and list the directory: everything is still found.
 */

#include "test_util.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	const ext4_direntry *de;
	char path[96];
	ext4_dir d;
	int i, n = 0;

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	for (i = 1; i <= 60; i += 7) {
		snprintf(path, sizeof(path),
			 TEST_MP "dir/sub/entry_with_a_long_name_%d", i);
		TEST_ASSERT_EQ(EOK, ext4_inode_exist(path, EXT4_DE_REG_FILE));
	}
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "dir/sub/missing",
						EXT4_DE_REG_FILE));

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, TEST_MP "dir/sub"));
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		n++;
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	TEST_ASSERT_EQ(62, n);
	test_umount();
	return 0;
}
