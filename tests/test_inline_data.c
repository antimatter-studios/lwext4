/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * inline_data (fork issue #130). lwext4 refused such filesystems with
 * ENOTSUP. It now reads them: files, symlinks and directories whose data
 * is in the i-node (i_block, continued in the system.data xattr). Writing
 * inline data is not supported yet, so the filesystem is mounted
 * read-only (as with an unsupported read-only feature).
 *
 * Read back every file of the image made in the .sh, list its inline
 * directories (also entries in system.data), look names up through them,
 * check that writes are refused; the .check.sh compares the image with
 * its copy.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

static char buf[8192];

static void check_file(const char *path, size_t n)
{
	ext4_file f;
	size_t got = 0, i;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ((uint64_t)n, ext4_fsize(&f));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &got));
	TEST_ASSERT_EQ(n, got);
	for (i = 0; i < n; i++)
		TEST_ASSERT_EQ((char)('a' + i % 26), buf[i]);
	/* From the middle, across i_block and system.data */
	if (n > 40) {
		TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 40, SEEK_SET));
		TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, 30, &got));
		TEST_ASSERT_EQ(n - 40 < 30 ? n - 40 : 30, got);
		TEST_ASSERT_EQ((char)('a' + 40 % 26), buf[0]);
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void check_content(const char *path, const char *text)
{
	ext4_file f;
	size_t got = 0;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &got));
	TEST_ASSERT_EQ(strlen(text), got);
	TEST_ASSERT(memcmp(buf, text, got) == 0);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

/* The names of a directory, in order, separated by spaces */
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

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	static const size_t sizes[] = {1, 59, 60, 61, 100, 120, 140, 5000};
	char path[64], link[128];
	size_t i, n = 0;
	ext4_file f;

	/* Asked read-write, mounted read-only */
	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		snprintf(path, sizeof(path), TEST_MP "f%u", (unsigned)sizes[i]);
		check_file(path, sizes[i]);
	}

	memset(link, 'L', 80);
	TEST_ASSERT_EQ(EOK, ext4_readlink(TEST_MP "long", buf, sizeof(buf),
					  &n));
	TEST_ASSERT_EQ((size_t)80, n);
	TEST_ASSERT(memcmp(buf, link, 80) == 0);

	check_list(TEST_MP "small", ". .. e1 e2 xa xb ");
	check_content(TEST_MP "small/e2", "s2\n");
	check_file(TEST_MP "small/xa", 1);  /* a link of /f1 */
	check_file(TEST_MP "small/xb", 59); /* in system.data */
	TEST_ASSERT_EQ(ENOENT, ext4_inode_exist(TEST_MP "small/xc",
						EXT4_DE_REG_FILE));
	check_list(TEST_MP "medium/deeper", ". .. x ");
	check_content(TEST_MP "medium/deeper/x", "deep\n");

	/* Writes are refused */
	TEST_ASSERT_EQ(EROFS, ext4_fopen(&f, TEST_MP "small/new", "wb"));
	TEST_ASSERT_EQ(EROFS, ext4_dir_mk(TEST_MP "small/d"));
	TEST_ASSERT_EQ(EROFS, ext4_fremove(TEST_MP "small/e1"));
	test_umount();
	return 0;
}
