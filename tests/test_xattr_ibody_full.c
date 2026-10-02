/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * An in-inode attribute that fills the inode's attribute space exactly
 * (fork issue #97). ext4_xattr_set_entry() stores it, with value offsets
 * counted from the first entry, but ext4_xattr_is_ibody_valid() counted
 * them from the header: the values seemed to overlap the entry list by 4
 * bytes and the inode was refused as corrupt, so the attribute that was
 * just set could not be read (EIO). One attribute of every size from 40 to
 * 100 bytes, each on its own file, covers the exact fit for any
 * i_extra_isize; each must read back with its size and contents, and the
 * check script reads them with debugfs.
 */

#include "test_util.h"

#include <string.h>

static void value_of(char *buf, size_t size)
{
	size_t k;

	for (k = 0; k < size; k++)
		buf[k] = (char)('a' + (k + size) % 26);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char path[32], want[128], got[128];
	ext4_file f;
	size_t size, n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	for (size = 40; size <= 100; size++) {
		snprintf(path, sizeof(path), TEST_MP "f%u", (unsigned)size);
		TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
		TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
		value_of(want, size);
		TEST_ASSERT_EQ(EOK, ext4_setxattr(path, "user.a0", 7, want,
						  size));
	}
	for (size = 40; size <= 100; size++) {
		snprintf(path, sizeof(path), TEST_MP "f%u", (unsigned)size);
		value_of(want, size);
		memset(got, 0, sizeof(got));
		n = 0;
		if (ext4_getxattr(path, "user.a0", 7, got, sizeof(got), &n) !=
		    EOK) {
			fprintf(stderr, "%s: user.a0 of %u bytes unreadable\n",
				path, (unsigned)size);
			return 1;
		}
		TEST_ASSERT_EQ(size, n);
		TEST_ASSERT(!memcmp(want, got, size));
	}
	test_umount();
	return 0;
}
