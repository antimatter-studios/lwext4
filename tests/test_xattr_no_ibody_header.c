/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Extended attributes of files that have none. With 256 byte inodes the
 * extra space of an inode holds attributes once it starts with the xattr
 * header; until the first attribute is set it does not (every inode lwext4
 * or mke2fs creates). The lookup took the missing header for a corrupt
 * one, so ext4_getxattr() and ext4_removexattr() returned EIO instead of
 * ENODATA for every file without attributes. ext4_setxattr() wrote the
 * header first, so only files that had had an attribute behaved.
 *
 * Checked on a file made by mke2fs -d and one made by lwext4, then that
 * setting, reading, listing and removing an attribute still work.
 */

#include "test_util.h"

#include <string.h>

#define NONE "user.none"
#define NAME "user.a"

/* On a read only mount only the lookup: removing is EROFS there. */
static void check_no_attribute(const char *path, bool writable)
{
	char buf[64];
	size_t len = 0;

	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(path, NONE, strlen(NONE), buf,
					      sizeof(buf), &len));
	if (writable)
		TEST_ASSERT_EQ(ENODATA,
			       ext4_removexattr(path, NONE, strlen(NONE)));
}

static size_t list(const char *path, char *buf, size_t size)
{
	size_t len = 0;

	TEST_ASSERT_EQ(EOK, ext4_listxattr(path, buf, size, &len));
	return len;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	const char *file = TEST_MP "new.txt";
	char buf[64];
	size_t len = 0;
	ext4_file f;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	/* made by mke2fs -d */
	check_no_attribute(TEST_MP "hello.txt", true);

	/* made by lwext4 */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, file, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	check_no_attribute(file, true);
	TEST_ASSERT_EQ(0, list(file, buf, sizeof(buf)));

	/* One attribute: the header is written, the rest still works */
	TEST_ASSERT_EQ(EOK, ext4_setxattr(file, NAME, strlen(NAME), "1234", 4));
	TEST_ASSERT_EQ(EOK, ext4_getxattr(file, NAME, strlen(NAME), buf,
					  sizeof(buf), &len));
	TEST_ASSERT_EQ(4, len);
	TEST_ASSERT(memcmp(buf, "1234", 4) == 0);
	TEST_ASSERT_EQ(sizeof(NAME), list(file, buf, sizeof(buf)));
	TEST_ASSERT(strcmp(buf, NAME) == 0);
	check_no_attribute(file, true);

	TEST_ASSERT_EQ(EOK, ext4_removexattr(file, NAME, strlen(NAME)));
	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(file, NAME, strlen(NAME), buf,
					      sizeof(buf), &len));
	TEST_ASSERT_EQ(0, list(file, buf, sizeof(buf)));

	test_umount();

	/* and after a remount */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	check_no_attribute(TEST_MP "hello.txt", false);
	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(file, NAME, strlen(NAME), buf,
					      sizeof(buf), &len));
	test_umount();
	return 0;
}
