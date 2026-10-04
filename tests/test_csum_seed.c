/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * metadata_csum_seed (fork issue #127). e2fsprogs 1.47 enables it by
 * default: the metadata checksums start from checksum_seed in the
 * superblock instead of crc32c(uuid), so the UUID can change without
 * rewriting them. lwext4 refused such filesystems with ENOTSUP.
 *
 * The image's UUID was changed after mke2fs (see the .sh), so every
 * checksum lwext4 verifies or writes must use the stored seed. Mount it
 * read-write and exercise each kind of checksummed metadata: inodes, the
 * block and inode bitmaps and group descriptors, linear and htree
 * directory blocks, extent tree blocks, in-inode and block xattrs. Then
 * e2fsck must be clean, and a read-only remount must read everything back.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

#define FILES 200
#define BIG_BLOCKS 300

static void write_file(const char *path, const void *data, size_t len)
{
	ext4_file f;
	size_t n = 0;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, data, len, &n));
	TEST_ASSERT_EQ(len, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void check_file(const char *path, const void *data, size_t len)
{
	char buf[64];
	ext4_file f;
	size_t n = 0;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(len, n);
	TEST_ASSERT(memcmp(buf, data, len) == 0);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	static char big_xattr[700];
	char path[128], buf[1024], cmd[1024];
	ext4_file f;
	size_t n;
	int i;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	/* Inodes, bitmaps, descriptors, linear then htree directory blocks */
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "dir"));
	for (i = 0; i < FILES; i++) {
		snprintf(path, sizeof(path),
			 TEST_MP "dir/a_file_with_a_long_name_%03d", i);
		snprintf(buf, sizeof(buf), "%d", i);
		write_file(path, buf, strlen(buf));
	}

	/* Extent tree with an index level: one block in every other one, so
	 * every block is its own extent and they cannot fit in the inode */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "big", "wb"));
	memset(buf, 'b', sizeof(buf));
	for (i = 0; i < BIG_BLOCKS; i++) {
		TEST_ASSERT_EQ(EOK, ext4_fseek(&f, (int64_t)i * 2 * 1024,
					       SEEK_SET));
		TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, buf, 1024, &n));
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	/* Xattrs: a small one in the inode, a big one in an xattr block */
	TEST_ASSERT_EQ(EOK, ext4_setxattr(TEST_MP "dir", "user.small", 10,
					  "v", 1));
	memset(big_xattr, 'x', sizeof(big_xattr));
	TEST_ASSERT_EQ(EOK, ext4_setxattr(TEST_MP "big", "user.big", 8,
					  big_xattr, sizeof(big_xattr)));
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));

	/* Read back what was written: the checksums verify on reading */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	for (i = 0; i < FILES; i += 37) {
		snprintf(path, sizeof(path),
			 TEST_MP "dir/a_file_with_a_long_name_%03d", i);
		snprintf(buf, sizeof(buf), "%d", i);
		check_file(path, buf, strlen(buf));
	}
	TEST_ASSERT_EQ(EOK, ext4_getxattr(TEST_MP "big", "user.big", 8, buf,
					  sizeof(buf), &n));
	TEST_ASSERT_EQ(sizeof(big_xattr), n);
	TEST_ASSERT(memcmp(buf, big_xattr, n) == 0);
	TEST_ASSERT_EQ(EOK, ext4_getxattr(TEST_MP "dir", "user.small", 10,
					  buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(1, n);
	test_umount();
	return 0;
}
