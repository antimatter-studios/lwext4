/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Extended attributes of an inode with a damaged i_extra_isize (found by
 * fuzzing mount on damaged images). The in-inode xattr header starts after
 * the first 128 bytes plus i_extra_isize; nothing checked that it lies
 * inside the inode, so a large value made the xattr code read past the end
 * of the inode table block (heap-buffer-overflow in
 * ext4_xattr_is_ibody_valid()), and setting the first attribute cleared
 * "inode_size - 128 - i_extra_isize" bytes, a negative (huge) size.
 *
 * Such an inode has no room for attributes in the inode: they must go to
 * the xattr block. Here i_extra_isize is 60000 (debugfs).
 */

#include "test_util.h"

#include <string.h>

#define F TEST_MP "a.txt"

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char buf[64];
	size_t len = 0;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(F, "user.x", 6, buf, sizeof(buf),
					      &len));
	TEST_ASSERT_EQ(EOK, ext4_listxattr(F, buf, sizeof(buf), &len));
	TEST_ASSERT_EQ(0, len);

	TEST_ASSERT_EQ(EOK, ext4_setxattr(F, "user.x", 6, "value", 5));
	TEST_ASSERT_EQ(EOK, ext4_getxattr(F, "user.x", 6, buf, sizeof(buf),
					  &len));
	TEST_ASSERT_EQ(5, len);
	TEST_ASSERT(memcmp(buf, "value", 5) == 0);

	test_umount();
	return 0;
}
