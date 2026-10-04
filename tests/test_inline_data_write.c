/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Writing inline_data filesystems (fork issue #130). An inline i-node
 * keeps its data in i_block and the system.data xattr; before it changes,
 * lwext4 moves it to a block (as Linux does when inline data no longer
 * fits): files when written or grown, directories when an entry is added
 * or removed or their ".." changes. Shrinking keeps the data inline, and
 * deleting an inline file releases no blocks.
 *
 * Every write path on the image of the .sh, then a remount reads it all
 * back; the .check.sh runs e2fsck and checks with debugfs what stayed
 * inline.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

static char buf[8192];

static size_t read_all(const char *path)
{
	ext4_file f;
	size_t n = 0;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	return n;
}

static void check_pattern(size_t from, size_t to)
{
	for (size_t i = from; i < to; i++)
		TEST_ASSERT_EQ((char)('a' + i % 26), buf[i]);
}

static void check_list(const char *path, const char *names)
{
	char got[512] = "";
	const ext4_direntry *de;
	ext4_dir d;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, path));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		strncat(got, (const char *)de->name, de->name_length);
		strcat(got, " ");
	}
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	if (strcmp(got, names)) {
		fprintf(stderr, "%s: got '%s', expected '%s'\n", path, got,
			names);
		TEST_ASSERT(0);
	}
}

static void verify(void)
{
	size_t i;

	TEST_ASSERT_EQ((size_t)105, read_all(TEST_MP "f100"));
	check_pattern(0, 100);
	TEST_ASSERT(memcmp(buf + 100, "APPND", 5) == 0);

	TEST_ASSERT_EQ((size_t)1, read_all(TEST_MP "f1"));
	TEST_ASSERT_EQ('Z', buf[0]);

	TEST_ASSERT_EQ((size_t)70, read_all(TEST_MP "f120"));
	check_pattern(0, 70);
	TEST_ASSERT_EQ((size_t)0, read_all(TEST_MP "f59"));

	TEST_ASSERT_EQ((size_t)3000, read_all(TEST_MP "f61"));
	check_pattern(0, 61);
	for (i = 61; i < 3000; i++)
		TEST_ASSERT_EQ(0, buf[i]);

	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "f60",
						EXT4_DE_REG_FILE));

	/* The new entry, and the old ones, also those that were in
	 * system.data */
	check_list(TEST_MP "small", ". .. e1 e2 xa xb new ");
	TEST_ASSERT_EQ((size_t)4, read_all(TEST_MP "small/new"));
	TEST_ASSERT_EQ((size_t)3, read_all(TEST_MP "small/e2"));
	TEST_ASSERT_EQ((size_t)1, read_all(TEST_MP "small/xa"));

	check_list(TEST_MP "moved", ". .. ");
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "medium/deeper",
						EXT4_DE_DIR));
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "gone", EXT4_DE_DIR));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	/* Files: append, overwrite, shrink (stays inline), grow, delete */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f100", "ab"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "APPND", 5, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f1", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "Z", 1, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f120", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 70));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f59", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 0));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f61", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 3000));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "f60"));

	/* Directories: add, remove, move (".."), remove a tree */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "small/new", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "new\n", 4, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "medium/deeper/x"));
	TEST_ASSERT_EQ(EOK, ext4_dir_mv(TEST_MP "medium/deeper",
					TEST_MP "moved"));
	TEST_ASSERT_EQ(EOK, ext4_dir_rm(TEST_MP "gone"));

	verify();
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	verify();
	test_umount();
	return 0;
}
