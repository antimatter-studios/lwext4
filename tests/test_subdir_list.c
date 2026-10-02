/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Listing nested directories (upstream issue 53): subdirectories created by
 * mke2fs -d and by lwext4 itself must be listable through ext4_dir_open()/
 * ext4_dir_entry_next(), with and without a trailing slash, before and
 * after a remount. The 1000-entry directory is larger than the block cache
 * (CONFIG_BLOCK_DEV_CACHE_SIZE), so listing it also evicts cached blocks.
 *
 * red-green: guard (the report does not reproduce: listing nested
 * directories already works on the base).
 */

#include "test_util.h"

#include <string.h>

/* Count entries of a directory, excluding "." and "..", and check that
 * "expect" (if not NULL) is among them. */
static int count_entries(const char *path, const char *expect)
{
	ext4_dir d;
	const ext4_direntry *de;
	int n = 0;
	bool found = expect == NULL;

	int r = ext4_dir_open(&d, path);
	if (r != EOK) {
		fprintf(stderr, "ext4_dir_open(%s) = %d\n", path, r);
		exit(1);
	}

	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		char name[256];
		memcpy(name, de->name, de->name_length);
		name[de->name_length] = 0;
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		if (expect && !strcmp(name, expect))
			found = true;
		n++;
	}
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));

	if (!found) {
		fprintf(stderr, "%s: entry %s not found\n", path, expect);
		exit(1);
	}
	return n;
}

static void check_tree(void)
{
	TEST_ASSERT_EQ(4, count_entries(TEST_MP, "dir1"));

	/* Created by mke2fs -d */
	TEST_ASSERT_EQ(2, count_entries(TEST_MP "dir1", "dir2"));
	TEST_ASSERT_EQ(2, count_entries(TEST_MP "dir1/", "dir2"));
	TEST_ASSERT_EQ(1, count_entries(TEST_MP "dir1/dir2", "deep.txt"));
	TEST_ASSERT_EQ(1, count_entries(TEST_MP "dir1/dir2/", "deep.txt"));
	/* Multi-block linear directory */
	TEST_ASSERT_EQ(1000, count_entries(TEST_MP "big/", "file_0999"));

	/* Created by lwext4 (hash-indexed once it grows past one block) */
	TEST_ASSERT_EQ(1, count_entries(TEST_MP "new1", "new2"));
	TEST_ASSERT_EQ(1, count_entries(TEST_MP "new1/", "new2"));
	TEST_ASSERT_EQ(200, count_entries(TEST_MP "new1/new2", "f199"));
	TEST_ASSERT_EQ(200, count_entries(TEST_MP "new1/new2/", "f0"));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	ext4_file f;
	char path[64];
	int i;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "new1"));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "new1/new2/"));
	for (i = 0; i < 200; i++) {
		snprintf(path, sizeof(path), TEST_MP "new1/new2/f%d", i);
		TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
		TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	}

	check_tree();
	test_umount();

	/* Again from a fresh mount with a cold cache, read only. */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	check_tree();
	test_umount();
	return 0;
}
