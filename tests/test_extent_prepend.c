/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Merging a new extent in front of an existing one (fork issue #112).
 *
 * When a new extent ends exactly where an existing one starts, logically
 * and physically, ext4_ext_insert_leaf() merges it into the existing one.
 * It moved the logical start but not the physical one, so every block of
 * the merged extent mapped one extent length too far: the new data went to
 * a block the file no longer mapped, and the old blocks read their
 * neighbours.
 *
 * A takes a block, B gets logical block 1 on the block after it, A is
 * removed, and B's hole at logical block 0 is written: the allocator's
 * goal for it is the block before B's extent, A's, which is free again,
 * so the new extent is merged in front. Both blocks of B must read back
 * what was written (the check script checks that the merge happened).
 */

#include "test_util.h"

#include <string.h>

static void fill(ext4_file *f, uint64_t off, char c)
{
	char buf[1024];
	size_t n;

	memset(buf, c, sizeof(buf));
	TEST_ASSERT_EQ(EOK, ext4_fseek(f, (int64_t)off, SEEK_SET));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(sizeof(buf), n);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char got[2048];
	ext4_file f;
	size_t n, k;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "A", "wb"));
	fill(&f, 0, 'a');
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "B", "wb"));
	fill(&f, 1024, 'b');
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "A"));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "B", "r+b"));
	fill(&f, 0, 'z');
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "B", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, got, sizeof(got), &n));
	TEST_ASSERT_EQ(sizeof(got), n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();
	for (k = 0; k < sizeof(got); k++)
		if (got[k] != (k < 1024 ? 'z' : 'b')) {
			fprintf(stderr, "/B byte %u is 0x%02x\n", (unsigned)k,
				(uint8_t)got[k]);
			return 1;
		}
	return 0;
}
