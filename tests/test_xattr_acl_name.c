/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * POSIX ACL attributes (fork issue #121). system.posix_acl_access and
 * system.posix_acl_default are stored as name index 2 and 3 with an empty
 * name. ext4_extract_xattr_name() returned NULL as that name, which
 * ext4_xattr_find_entry() passed to memcmp() and ext4_xattr_set_entry() to
 * memcpy() (length 0): undefined behaviour, which UBSan reports.
 *
 * Set, read, replace, list and remove both ACL attributes, in the inode
 * body (ext4, 256 byte inodes) and in the xattr block (ext2, 128 byte
 * inodes), and read them back after a remount.
 */

#include "test_util.h"

#include <string.h>

#define ACCESS "system.posix_acl_access"
#define DEFAULT "system.posix_acl_default"

/* Version 2 ACLs as Linux stores them: user::rw- group::r-- other::r--,
 * and the same with x for the directory default. */
static const char acl_file[] = "\x02\x00\x00\x00"
			       "\x01\x00\x06\x00\xff\xff\xff\xff"
			       "\x04\x00\x04\x00\xff\xff\xff\xff"
			       "\x20\x00\x04\x00\xff\xff\xff\xff";
static const char acl_dir[] = "\x02\x00\x00\x00"
			      "\x01\x00\x07\x00\xff\xff\xff\xff"
			      "\x04\x00\x05\x00\xff\xff\xff\xff"
			      "\x20\x00\x05\x00\xff\xff\xff\xff";
#define ACL_LEN (sizeof(acl_file) - 1)

static void set(const char *path, const char *name, const char *v)
{
	TEST_ASSERT_EQ(EOK, ext4_setxattr(path, name, strlen(name), v,
					  ACL_LEN));
}

static void expect(const char *path, const char *name, const char *v)
{
	char buf[64];
	size_t len = 0;

	TEST_ASSERT_EQ(EOK, ext4_getxattr(path, name, strlen(name), buf,
					  sizeof(buf), &len));
	TEST_ASSERT_EQ(ACL_LEN, len);
	TEST_ASSERT(memcmp(buf, v, len) == 0);
}

static void expect_list(const char *path, const char *names, size_t len)
{
	char buf[256];
	size_t got = 0;

	TEST_ASSERT_EQ(EOK, ext4_listxattr(path, buf, sizeof(buf), &got));
	TEST_ASSERT_EQ(len, got);
	TEST_ASSERT(memcmp(buf, names, len) == 0);
}

static void run(const char *image)
{
	char cmd[1024];
	size_t len;
	char buf[64];

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d"));
	{
		ext4_file f;
		TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "wb"));
		TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	}

	/* New entries, then a replacement of the same size */
	set(TEST_MP "f", ACCESS, acl_dir);
	set(TEST_MP "f", ACCESS, acl_file);
	set(TEST_MP "d", ACCESS, acl_dir);
	set(TEST_MP "d", DEFAULT, acl_dir);
	set(TEST_MP "d", "user.x", acl_file);
	expect(TEST_MP "f", ACCESS, acl_file);
	expect(TEST_MP "d", ACCESS, acl_dir);
	expect(TEST_MP "d", DEFAULT, acl_dir);
	expect_list(TEST_MP "f", ACCESS "", sizeof(ACCESS));
	/* Remove one of them */
	TEST_ASSERT_EQ(EOK, ext4_removexattr(TEST_MP "d", ACCESS,
					     strlen(ACCESS)));
	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(TEST_MP "d", ACCESS,
					      strlen(ACCESS), buf,
					      sizeof(buf), &len));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	expect(TEST_MP "f", ACCESS, acl_file);
	expect(TEST_MP "d", DEFAULT, acl_dir);
	TEST_ASSERT_EQ(ENODATA, ext4_getxattr(TEST_MP "d", ACCESS,
					      strlen(ACCESS), buf,
					      sizeof(buf), &len));
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
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
