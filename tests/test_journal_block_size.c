/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A journal superblock that does not fit the filesystem (fork issue #157,
 * found by fuzz_rw). lwext4 copied journal->block_size bytes (from the
 * journal superblock) between block buffers of the filesystem block size:
 * with a journal block size of 64 KiB on a 1 KiB filesystem, a heap
 * overflow (ASan, which red-green builds use, reports it). Linux refuses
 * such a journal.
 *
 * $1's journal says its block size is 4096, $1.first's that its log starts
 * at block 0 (see the .sh): replay and the journal must be refused with
 * EIO, and writing must still work without the journal.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

static void run(const char *image)
{
	char buf[8];
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EIO, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EIO, ext4_journal_start(TEST_MP));

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "hello", 5, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ((size_t)5, n);
	TEST_ASSERT(memcmp(buf, "hello", 5) == 0);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char first[512];

	snprintf(first, sizeof(first), "%s.first", image);
	run(image);
	run(first);
	return 0;
}
