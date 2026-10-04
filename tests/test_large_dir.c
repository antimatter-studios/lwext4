/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * large_dir (fork issue #131): htree directories of three index levels.
 * lwext4 refused such filesystems with ENOTSUP, and its htree code only
 * knew a root and one level of index nodes.
 *
 * The setup (.sh) makes, with e2fsprogs:
 *  - a directory of 50000 entries with three levels: lwext4 looks names
 *    up, adds 600, removes 300 and lists them all
 *  - a two level directory whose root and index nodes are full: lwext4
 *    adds entries, which takes a third level (checked by the .check.sh
 *    with debugfs)
 *  - the same without large_dir: the tree cannot grow, adding fails with
 *    ENOSPC
 * e2fsck must find every image clean afterwards.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

static void name(char *buf, size_t size, unsigned k)
{
	static char n240[241];

	memset(n240, 'n', 240);
	snprintf(buf, size, TEST_MP "big/%s_%07u", n240, k);
}

static int exists(unsigned k)
{
	char path[300];

	name(path, sizeof(path), k);
	return ext4_inode_exist(path, EXT4_DE_REG_FILE);
}

static int add(unsigned k)
{
	char path[300];

	name(path, sizeof(path), k);
	return ext4_flink(TEST_MP "target", path);
}

static size_t count_entries(void)
{
	ext4_dir d;
	const ext4_direntry *de;
	size_t n = 0;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, TEST_MP "big"));
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		n++;
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	return n;
}

static void three_levels(const char *image)
{
	char path[300];
	unsigned k;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, exists(0));
	TEST_ASSERT_EQ(EOK, exists(24999));
	TEST_ASSERT_EQ(EOK, exists(49999));
	TEST_ASSERT_EQ(ENOENT, exists(50000));

	for (k = 50000; k < 50600; k++)
		TEST_ASSERT_EQ(EOK, add(k));
	for (k = 100; k < 400; k++) {
		name(path, sizeof(path), k);
		TEST_ASSERT_EQ(EOK, ext4_fremove(path));
	}
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, exists(0));
	TEST_ASSERT_EQ(ENOENT, exists(250));
	TEST_ASSERT_EQ(EOK, exists(50599));
	/* ".", ".." and the entries */
	TEST_ASSERT_EQ((size_t)2 + 50000 + 600 - 300, count_entries());
	test_umount();
}

static void grow(const char *image)
{
	unsigned k;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	for (k = 46400; k < 46800; k++)
		TEST_ASSERT_EQ(EOK, add(k));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	for (k = 0; k < 46800; k += 997)
		TEST_ASSERT_EQ(EOK, exists(k));
	TEST_ASSERT_EQ(EOK, exists(46799));
	TEST_ASSERT_EQ((size_t)2 + 46800, count_entries());
	test_umount();
}

static void cannot_grow(const char *image)
{
	unsigned k;
	int r = EOK;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	for (k = 46400; k < 46800 && r == EOK; k++)
		r = add(k);
	TEST_ASSERT_EQ(ENOSPC, r);
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char path[512];

	three_levels(image);
	snprintf(path, sizeof(path), "%s.full", image);
	grow(path);
	snprintf(path, sizeof(path), "%s.nolarge", image);
	cannot_grow(path);
	return 0;
}
