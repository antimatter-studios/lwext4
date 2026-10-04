/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_journal_stop() after a failed ext4_journal_start() (fork issue
 * #135, found by fuzz_rw). When the journal cannot be started, start
 * returns the error and leaves the mount read-write without a journal.
 * Stop only checked the has_journal feature and called jbd_journal_stop()
 * on the journal that was never started: a NULL jbd_fs, a segfault.
 * Calling stop unconditionally after start is what the README example
 * does.
 *
 * The journal superblock of the image is damaged (see the .sh): start
 * must fail, then writing works without the journal, stop returns EOK,
 * and a remount reads the file back.
 */

#include "test_util.h"

#include <string.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char buf[16];
	ext4_file f;
	size_t n = 0;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT(ext4_journal_start(TEST_MP) != EOK);

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "hello", 5, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));

	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	/* Stopping twice is as harmless */
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ((size_t)5, n);
	TEST_ASSERT(memcmp(buf, "hello", 5) == 0);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();
	return 0;
}
