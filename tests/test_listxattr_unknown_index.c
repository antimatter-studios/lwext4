/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Xattr entries with an unknown name index (fork issue #134, found by
 * fuzz_mount). ext4_get_xattr_name_prefix() has no prefix for such an
 * index, and ext4_listxattr() copied its NULL prefix with memcpy (undefined
 * behaviour, length 0) and listed the entry by its bare name, which no
 * other xattr call can use. Linux does not list entries it has no handler
 * for.
 *
 * The setup makes a file with user.ok and user.zq9; here the name index
 * of zq9 is changed on disk to 0x7f, in the inode body and in an xattr
 * block. Listing must return user.ok only, and the size query must agree.
 */

#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNKNOWN_INDEX 0x7f

/* Find the zq9 entry in the image and change its name index. The entry
 * header (16 bytes: name_len, name_index, value_offs, value_inum,
 * value_size, hash) is followed by the name without its prefix. */
static void damage_index(const char *image)
{
	FILE *f = fopen(image, "r+b");
	static unsigned char buf[8 << 20];
	size_t len, i, found = 0;

	TEST_ASSERT(f != NULL);
	len = fread(buf, 1, sizeof(buf), f);
	for (i = 16; i + 3 <= len; i++) {
		if (memcmp(buf + i, "zq9", 3) || buf[i - 16] != 3 ||
		    buf[i - 15] != 1 /* user. */)
			continue;
		buf[i - 15] = UNKNOWN_INDEX;
		TEST_ASSERT(fseek(f, (long)(i - 15), SEEK_SET) == 0);
		TEST_ASSERT(fputc(UNKNOWN_INDEX, f) == UNKNOWN_INDEX);
		found++;
	}
	TEST_ASSERT_EQ((size_t)1, found);
	TEST_ASSERT(fclose(f) == 0);
}

static void run(const char *image)
{
	char list[64];
	size_t len = 0;

	damage_index(image);
	TEST_ASSERT_EQ(EOK, test_mount(image, true));

	TEST_ASSERT_EQ(EOK, ext4_listxattr(TEST_MP "f", NULL, 0, &len));
	TEST_ASSERT_EQ(sizeof("user.ok"), len);

	memset(list, 0xff, sizeof(list));
	TEST_ASSERT_EQ(EOK, ext4_listxattr(TEST_MP "f", list, sizeof(list),
					   &len));
	TEST_ASSERT_EQ(sizeof("user.ok"), len);
	TEST_ASSERT(memcmp(list, "user.ok", sizeof("user.ok")) == 0);

	/* The valid attribute is still readable */
	TEST_ASSERT_EQ(EOK, ext4_getxattr(TEST_MP "f", "user.ok", 7, list,
					  sizeof(list), &len));
	TEST_ASSERT_EQ((size_t)1, len);
	TEST_ASSERT_EQ('1', list[0]);
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char ext2[512];

	snprintf(ext2, sizeof(ext2), "%s.ext2", image);
	run(image);
	run(ext2);
	return 0;
}
