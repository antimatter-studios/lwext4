/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Partial writes into new blocks (fork issue #109).
 *
 * ext4_fwrite() allocated a block for the unaligned head or tail of a
 * write into a hole and wrote only the requested bytes there: the rest of
 * the block kept what the device held, and reading the file returned it
 * (old data, e.g. of deleted files). A block appended at the end of a file
 * for a partial tail likewise kept stale bytes after the end of the file.
 *
 * On ext4 and ext2 images whose free blocks hold 0xaa bytes: short writes
 * into a sparse file, one inside a block and one spanning a partial block,
 * whole blocks and a partial block, must leave zeros around the written
 * bytes; the file is compared with what it must hold. The check script
 * checks the tail of a block appended to a file.
 */

#include "test_util.h"

#include <string.h>

#define SPARSE_SIZE 100000u

static uint8_t want[SPARSE_SIZE], got[SPARSE_SIZE];

static void write_at(ext4_file *f, uint64_t off, const char *fill, size_t len)
{
	static char data[8192];
	size_t n;

	TEST_ASSERT(len <= sizeof(data));
	memset(data, fill[0], len);
	TEST_ASSERT_EQ(EOK, ext4_fseek(f, (int64_t)off, SEEK_SET));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(f, data, len, &n));
	TEST_ASSERT_EQ(len, n);
	memset(want + off, fill[0], len);
}

static void run(const char *image)
{
	ext4_file f;
	size_t n, k;

	memset(want, 0, sizeof(want));
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "sparse", "r+b"));
	write_at(&f, 50000, "A", 10);          /* inside one block */
	write_at(&f, 70000, "B", 3000);        /* head, whole blocks, tail */
	write_at(&f, 99990, "C", 10);          /* the last, partial block */
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "full", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_fseek(&f, 0, SEEK_END));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "0123456789", 10, &n));
	TEST_ASSERT_EQ(10, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();

	/* Read back from a fresh mount */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "sparse", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, got, sizeof(got), &n));
	TEST_ASSERT_EQ(SPARSE_SIZE, n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();
	for (k = 0; k < SPARSE_SIZE; k++)
		if (got[k] != want[k]) {
			fprintf(stderr, "%s: /sparse byte %u is 0x%02x, "
				"expected 0x%02x\n", image, (unsigned)k,
				got[k], want[k]);
			exit(1);
		}
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
