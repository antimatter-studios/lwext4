/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * The feature tables of README.md ("Supported ext2/3/4 features") are
 * claims; this test proves each row. test_features.sh makes one image per
 * row with e2fsprogs and lists them with the outcome the README gives:
 *
 *  rw       mounts read-write; a workload of creating, writing, linking,
 *           renaming, truncating and deleting files and directories (a
 *           300 entry directory, xattrs, a symlink) through the journal
 *           leaves a filesystem that e2fsck -fn finds clean, and it all
 *           reads back after a remount;
 *  ro       mounts, reads, refuses writes with EROFS and leaves the image
 *           as it was;
 *  refused  ext4_mount() fails with ENOTSUP.
 *
 * Every row is checked; the test lists the rows that do not hold and
 * fails if there is one.
 */

#include "test_util.h"

#include <ext4_super.h>

#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
	do {                                                                   \
		if (!(cond)) {                                                 \
			printf("  %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			return 1;                                              \
		}                                                              \
	} while (0)

#define CHECK_EQ(expected, actual)                                             \
	do {                                                                   \
		long long e_ = (long long)(expected), a_ = (long long)(actual);\
		if (e_ != a_) {                                                \
			printf("  %s:%d: %s is %lld, not %lld\n", __FILE__,     \
			       __LINE__, #actual, a_, e_);                     \
			return 1;                                              \
		}                                                              \
	} while (0)

#define BIG 200000
#define BIG_CUT 70000
#define MANY 300

static uint8_t pattern(size_t i)
{
	return (uint8_t)(i * 7 + i / 251);
}

static uint64_t image_hash(const char *path)
{
	static uint8_t buf[1 << 16];
	uint64_t h = 1469598103934665603ull;
	FILE *f = fopen(path, "rb");
	size_t n, i;

	if (!f)
		return 0;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		for (i = 0; i < n; i++)
			h = (h ^ buf[i]) * 1099511628211ull;
	fclose(f);
	return h;
}

static int write_file(const char *path, const void *data, size_t len)
{
	ext4_file f;
	size_t n;

	CHECK(ext4_fopen(&f, path, "wb") == EOK);
	CHECK(ext4_fwrite(&f, data, len, &n) == EOK && n == len);
	CHECK(ext4_fclose(&f) == EOK);
	return 0;
}

static int read_file(const char *path, void *data, size_t cap, size_t *len)
{
	ext4_file f;

	CHECK(ext4_fopen(&f, path, "rb") == EOK);
	CHECK(ext4_fread(&f, data, cap, len) == EOK);
	CHECK(ext4_fclose(&f) == EOK);
	return 0;
}

static int count_entries(const char *path)
{
	const ext4_direntry *de;
	ext4_dir d;
	int n = 0;

	int r = ext4_dir_open(&d, path);

	if (r != EOK) {
		printf("  ext4_dir_open(%s): %d\n", path, r);
		return -1;
	}
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		n++;
	ext4_dir_close(&d);
	return n - 2; /* . and .. */
}

static int fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null 2>&1",
		 image);
	return system(cmd);
}

static int workload(void)
{
	static uint8_t big[BIG];
	char path[128];
	size_t i;
	ext4_file f;

	for (i = 0; i < BIG; i++)
		big[i] = pattern(i);

	struct ext4_sblock *sb;

	CHECK(ext4_get_sblock(TEST_MP, &sb) == EOK);
	/* ENOTSUP: no journal to replay (ext2) */
	CHECK_EQ(ext4_sb_feature_com(sb, EXT4_FCOM_HAS_JOURNAL) ? EOK : ENOTSUP,
		 ext4_recover(TEST_MP));
	CHECK(ext4_journal_start(TEST_MP) == EOK);
	CHECK(ext4_dir_mk(TEST_MP "t") == EOK);
	CHECK(write_file(TEST_MP "t/big", big, BIG) == 0);
	CHECK(write_file(TEST_MP "t/small", "lwext4", 6) == 0);
	CHECK(ext4_setxattr(TEST_MP "t/small", "user.k", 6, "value", 5) ==
	      EOK);
	CHECK(ext4_fsymlink("small2", TEST_MP "t/link") == EOK);
	CHECK(ext4_flink(TEST_MP "t/small", TEST_MP "t/hard") == EOK);
	CHECK(ext4_frename(TEST_MP "t/small", TEST_MP "t/small2") == EOK);
	CHECK(ext4_dir_mk(TEST_MP "t/many") == EOK);
	for (i = 0; i < MANY; i++) {
		snprintf(path, sizeof(path),
			 TEST_MP "t/many/a_rather_long_file_name_%04zu", i);
		CHECK(write_file(path, path, strlen(path)) == 0);
	}
	for (i = 0; i < MANY; i += 2) {
		snprintf(path, sizeof(path),
			 TEST_MP "t/many/a_rather_long_file_name_%04zu", i);
		CHECK(ext4_fremove(path) == EOK);
	}
	CHECK(ext4_dir_mk(TEST_MP "t/gone") == EOK);
	CHECK(write_file(TEST_MP "t/gone/f", big, 5000) == 0);
	CHECK(ext4_dir_rm(TEST_MP "t/gone") == EOK);
	CHECK(ext4_fopen(&f, TEST_MP "t/big", "r+b") == EOK);
	CHECK(ext4_ftruncate(&f, BIG_CUT) == EOK);
	CHECK(ext4_fclose(&f) == EOK);
	CHECK(ext4_journal_stop(TEST_MP) == EOK);
	return 0;
}

static int verify(void)
{
	static uint8_t buf[BIG];
	char link[32];
	size_t n, i;

	CHECK(read_file(TEST_MP "t/big", buf, sizeof(buf), &n) == 0);
	CHECK(n == BIG_CUT);
	for (i = 0; i < n; i++)
		CHECK(buf[i] == pattern(i));
	CHECK(read_file(TEST_MP "t/small2", buf, sizeof(buf), &n) == 0);
	CHECK(n == 6 && !memcmp(buf, "lwext4", 6));
	CHECK(read_file(TEST_MP "t/hard", buf, sizeof(buf), &n) == 0);
	CHECK(n == 6 && !memcmp(buf, "lwext4", 6));
	CHECK(ext4_getxattr(TEST_MP "t/small2", "user.k", 6, buf, sizeof(buf),
			    &n) == EOK);
	CHECK(n == 5 && !memcmp(buf, "value", 5));
	CHECK(ext4_readlink(TEST_MP "t/link", link, sizeof(link), &n) == EOK);
	CHECK(n == 6 && !memcmp(link, "small2", 6));
	CHECK(count_entries(TEST_MP "t/many") == MANY / 2);
	CHECK(ext4_inode_exist(TEST_MP "t/gone", EXT4_DE_DIR) != EOK);
	return 0;
}

static int read_write(const char *image)
{
	int r = test_mount(image, false);

	CHECK(r == EOK);
	r = workload();
	test_umount();
	CHECK(r == 0);
	CHECK(fsck(image) == 0);
	CHECK(test_mount(image, true) == EOK);
	r = verify();
	test_umount();
	return r;
}

static int read_only(const char *image)
{
	uint64_t before = image_hash(image);
	ext4_file f;
	int r;

	CHECK(test_mount(image, false) == EOK);
	r = count_entries(TEST_MP);
	if (r < 1)
		printf("  root directory: %d entries\n", r);
	r = r >= 1;
	if (r && (r = ext4_fopen(&f, TEST_MP "new", "wb")) != EROFS)
		printf("  ext4_fopen: %d\n", r);
	r = r == EROFS;
	if (r && (r = ext4_dir_mk(TEST_MP "newdir")) != EROFS)
		printf("  ext4_dir_mk: %d\n", r);
	r = r == EROFS;
	ext4_recover(TEST_MP);
	ext4_journal_start(TEST_MP);
	ext4_journal_stop(TEST_MP);
	test_umount();
	CHECK(r);
	CHECK(image_hash(image) == before);
	return 0;
}

static int refused(const char *image)
{
	CHECK(test_mount(image, false) == ENOTSUP);
	test_umount();
	CHECK(test_mount(image, true) == ENOTSUP);
	test_umount();
	return 0;
}

int main(int argc, char **argv)
{
	FILE *list = fopen(test_image_arg(argc, argv), "r");
	char line[1024], image[900], expect[16], name[64];
	int rows = 0, bad = 0;

	TEST_ASSERT(list != NULL);
	while (fgets(line, sizeof(line), list)) {
		int r;

		if (sscanf(line, "%899s %15s %63s", image, expect, name) != 3)
			continue;
		rows++;
		printf("%-24s %-8s ", name, expect);
		fflush(stdout);
		if (!strcmp(expect, "rw"))
			r = read_write(image);
		else if (!strcmp(expect, "ro"))
			r = read_only(image);
		else
			r = refused(image);
		printf("%s\n", r ? "FAILS" : "ok");
		bad += r != 0;
	}
	fclose(list);
	printf("%d rows, %d do not hold\n", rows, bad);
	TEST_ASSERT(rows > 40);
	fflush(stdout);
	TEST_ASSERT_EQ(0, bad);
	return 0;
}
