/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Growing files (fork issue #103).
 *
 * ext4_ftruncate() to a larger size returned EOK and left the file as it
 * was, and ext4_fseek() past the end of a file failed: there was no way
 * to grow a file or to leave a hole. Like POSIX ftruncate() and lseek(),
 * growing must make the file larger with the new bytes reading as zeros,
 * also where the old last block held other data (shrinking to an
 * unaligned size zeroes the rest of the block); a write past the end must
 * leave a hole; reading past the end returns nothing; truncating a file
 * whose data follows a hole must release that data (fork issue #113); and
 * a file of 2 GiB or more turns the large_file feature on.
 */

#include "test_util.h"

#include <string.h>

static void expect(const char *path, uint64_t size, const char *pattern)
{
	static char got[16384];
	ext4_file f;
	size_t n, k;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(size, ext4_fsize(&f));
	TEST_ASSERT(size <= sizeof(got));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, got, sizeof(got), &n));
	TEST_ASSERT_EQ(size, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	for (k = 0; k < size; k++)
		if (got[k] != pattern[k]) {
			fprintf(stderr, "%s byte %u: 0x%02x, expected 0x%02x\n",
				path, (unsigned)k, (uint8_t)got[k],
				(uint8_t)pattern[k]);
			exit(1);
		}
}

static void run(const char *image)
{
	static char want[16384];
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	/* 3000 'x' -> 1500 -> 6000: zeros from 1500 on */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 1500));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 6000));
	TEST_ASSERT_EQ(6000, ext4_fsize(&f));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	memset(want, 0, sizeof(want));
	memset(want, 'x', 1500);
	expect(TEST_MP "f", 6000, want);

	/* A write past the end leaves a hole; reading past the end returns
	 * nothing */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "g", "w+b"));
	TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 5000, SEEK_SET));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "hole", 4, &n));
	TEST_ASSERT_EQ(4, n);
	TEST_ASSERT_EQ(5004, ext4_fsize(&f));
	TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 7000, SEEK_SET));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, want, 10, &n));
	TEST_ASSERT_EQ(0, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	memset(want, 0, sizeof(want));
	memcpy(want + 5000, "hole", 4);
	expect(TEST_MP "g", 5004, want);

	/* Data after a hole, truncated away: the blocks after the new end
	 * must be released (the extents after a hole were left in place, so
	 * growing again showed the old data, and e2fsck the extents after
	 * the end) */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "h", "w+b"));
	TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 14248, SEEK_SET));
	memset(want, 'D', 1281);
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, want, 1281, &n));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 12193));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 15946));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	memset(want, 0, sizeof(want));
	expect(TEST_MP "h", 15946, want);

	/* 3 GiB, sparse */
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "big", "w+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 3221225472ULL));
	TEST_ASSERT_EQ(3221225472ULL, ext4_fsize(&f));
	TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 3221225472ULL - 10, SEEK_SET));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, want, sizeof(want), &n));
	TEST_ASSERT_EQ(10, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
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
