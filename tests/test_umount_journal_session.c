/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Unmounting with the journal session still open (fork issue #167, found
 * by fuzz_rwx).
 * ext4_umount() did not end the session: the mount point kept its
 * jbd_journal and jbd_fs, ext4_fs_init() of the next mount on it did not
 * clear them, and that mount (with no ext4_journal_start()) ran its
 * transactions in the old session, whose journal i-node and transactions
 * pointed into the freed block cache of the old mount. A session is left
 * open by an application that does not call ext4_journal_stop(), and by
 * ext4_journal_stop() itself when it cannot write the journalled blocks
 * (it keeps the session so that it can be called again).
 *
 * ext4_umount() now ends an open session as ext4_journal_stop() does; when
 * that cannot write the blocks, it ends it without them and the journal
 * stays marked for replay. Three ways, each followed by a mount that
 * writes without a journal session, then e2fsck -fn and the files read
 * back:
 *
 *  1. ext4_journal_stop() not called;
 *  2. it failed (writes failing), then the device works again;
 *  3. it failed and the device still fails when unmounting: ext4_umount()
 *     fails and the mount point stays mounted, until a second call.
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

static void put(const char *name, char fill)
{
	char path[64], buf[3000];
	ext4_file f;
	size_t n;

	snprintf(path, sizeof(path), TEST_MP "%s", name);
	memset(buf, fill, sizeof(buf));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(sizeof(buf), n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void check(const char *name, char fill)
{
	char path[64], buf[4000];
	ext4_file f;
	size_t n, i;

	snprintf(path, sizeof(path), TEST_MP "%s", name);
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(3000, n);
	for (i = 0; i < n; i++)
		TEST_ASSERT_EQ(fill, buf[i]);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

/* Mount, write NAME without a journal session, unmount; the image must
 * be consistent and have every file of FILES (name, fill pairs). */
static void after(const char *image, const char *name, const char *files)
{
	/* No session survived: a read only mount writes nothing, not even
	 * when an old session is stopped */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
	fault_dev_fail_nth(FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	TEST_ASSERT_EQ(0, fault_dev_failed());
	fault_dev_disarm();
	test_umount();

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	put(name, 'z');
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));
	fsck(image);

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
	check(name, 'z');
	for (; *files; files += 2)
		check((char[]){files[0], 0}, files[1]);
	test_umount();
}

/* Mount with a journal session in write-back mode and write NAME: its
 * blocks are journalled but not yet in place */
static void begin(const char *image, const char *name, char fill)
{
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, true));
	put(name, fill);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);

	/* 1. No ext4_journal_stop() */
	begin(image, "a", 'a');
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));
	after(image, "x", "aa");

	/* 2. ext4_journal_stop() fails, the device works again */
	begin(image, "b", 'b');
	fault_dev_fail_nth(FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT(ext4_journal_stop(TEST_MP) != EOK);
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));
	after(image, "y", "aabb");

	/* 3. It fails, and so does the first ext4_umount() */
	begin(image, "c", 'c');
	fault_dev_fail_nth(FAULT_DEV_WRITE, 1, 0);
	TEST_ASSERT(ext4_journal_stop(TEST_MP) != EOK);
	TEST_ASSERT(ext4_umount(TEST_MP) != EOK);
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));
	after(image, "w", "aabbcc");
	return 0;
}
