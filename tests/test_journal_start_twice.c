/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_journal_start() with a journal session already open (fork issue
 * #177, found by fuzz_rwx). A session stays open after ext4_journal_stop()
 * failed to write the journalled blocks (it keeps the session so that it
 * can be called again), and an application may simply call start twice.
 * The second start called jbd_get_fs() on the mount point's jbd_fs, which
 * the open session uses: it zeroed it and, with jbd_journal_start(),
 * initialised the journal again, dropping (leaking) the transactions
 * waiting for their checkpoint. When reading the journal i-node then
 * failed, the session was left with a zeroed jbd_fs and the next
 * ext4_journal_stop() dereferenced its NULL fs.
 *
 * A second start continues the open session: it succeeds without any
 * I/O (here: with every read failing), and what both "sessions" wrote
 * reaches the disk (e2fsck -fn, files read back). Run with AddressSanitizer
 * (red-green does), the leak of the old behaviour fails the test too.
 */

#include "fault_dev.h"

#include <stdlib.h>
#include <string.h>

static void fsck(const char *image)
{
	char cmd[1024];

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null",
		 image);
	TEST_ASSERT_EQ(0, system(cmd));
}

static void put(const char *path, char fill)
{
	char buf[3000];
	ext4_file f;
	size_t n;

	memset(buf, fill, sizeof(buf));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void check(const char *path, char fill)
{
	char buf[4000];
	ext4_file f;
	size_t n, i;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(3000, n);
	for (i = 0; i < n; i++)
		TEST_ASSERT_EQ(fill, buf[i]);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	/* Write-back: the transaction of "a" waits for its checkpoint */
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, true));
	put(TEST_MP "a", 'a');

	fault_dev_fail_nth(FAULT_DEV_READ | FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(0, fault_dev_failed());
	fault_dev_disarm();

	put(TEST_MP "b", 'b');
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();
	fsck(image);

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
	check(TEST_MP "a", 'a');
	check(TEST_MP "b", 'b');
	test_umount();
	return 0;
}
