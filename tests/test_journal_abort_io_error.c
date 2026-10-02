/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Aborting a transaction when the block device fails, with the journal.
 *
 * ext4_dir_rm() of a large directory runs many transactions in write-back
 * mode: a block that an earlier, committed transaction changed (and that
 * is not yet written to its place) is changed again by the next one. When
 * reading fails in the middle of that next transaction, it is aborted, and
 * the abort puts the committed version of the block back into the cache
 * by reading its copy from the journal. That read failing (or getting the
 * cache buffer for it) hit ext4_assert(r == EOK) in
 * jbd_trans_finish_callback() and aborted the program.
 *
 * Every read from the Nth on fails, for each N until the operation no
 * longer fails. The call must return an error instead of aborting; once
 * the device works again, ext4_journal_stop() may fail (the journal then
 * stays marked for replay) and the filesystem must be consistent after the
 * next mount replays the journal (e2fsck -fn).
 */

#include "fault_dev.h"

#include <stdlib.h>
#include <string.h>

static void copy(const char *src, const char *dst)
{
	static char buf[1 << 16];
	FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
	size_t n;

	TEST_ASSERT(a && b);
	while ((n = fread(buf, 1, sizeof(buf), a)) > 0)
		TEST_ASSERT_EQ(n, fwrite(buf, 1, n, b));
	fclose(a);
	fclose(b);
}

static void fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null",
		 image);
	TEST_ASSERT_EQ(0, system(cmd));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char pristine[512];
	uint64_t n;
	int failed = 0;

	snprintf(pristine, sizeof(pristine), "%s.pristine", image);
	for (n = 1; n < 1000; n++) {
		int r;

		copy(pristine, image);
		TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
		TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
		TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));

		fault_dev_fail_nth(FAULT_DEV_READ, n, 0);
		r = ext4_dir_rm(TEST_MP "many");
		fault_dev_disarm();
		if (!fault_dev_failed()) {
			/* N is past the last read: done */
			TEST_ASSERT_EQ(EOK, r);
			TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
			test_umount();
			fsck(image);
			break;
		}
		TEST_ASSERT(r != EOK);
		failed++;

		/* The device works again: stopping the journal writes what it
		 * can; if the journal cannot be emptied it stays marked for
		 * replay. */
		(void)ext4_journal_stop(TEST_MP);
		test_umount();

		/* The next mount replays what was committed. */
		TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
		TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
		test_umount();
		fsck(image);
	}
	TEST_ASSERT(n < 1000);
	TEST_ASSERT(failed > 0);
	printf("%d failing reads handled\n", failed);
	return 0;
}
