/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Replaying the journal (ext4_recover()) when the block device fails.
 *
 * jbd_replay_block_tags() ignored the errors of reading a block from the
 * journal and of writing it to its place, and jbd_recover() then marked
 * the journal empty and cleared the recovery flag: ext4_recover()
 * reported success, the journal was gone and the block had never been
 * replayed (after a power cut: a new file's inode is all zeros). The
 * recovery flag was cleared before the journal was marked empty, and the
 * error of writing the journal superblock was ignored: ext4_recover()
 * reported success with the old journal start on the disk, so a later
 * recovery replays the transactions again over newer changes. A read
 * error while collecting the revoked blocks leaked the revoke records.
 *
 * Every read and every write of the recovery fails in turn, once. The
 * recovery must either fail, and then succeed when called again, or
 * succeed with the journal replayed and marked empty on the disk; a
 * journal that is not empty must never be left without the recovery
 * flag. A copy of the image taken right after the failing call (a power
 * cut) must recover to the same files.
 */

#include "fault_dev.h"

#include <string.h>

#define EXT4_FINCOM_RECOVER_BIT 0x4

static uint32_t le_at(const char *image, long off, int len)
{
	uint8_t b[4] = {0};
	FILE *f = fopen(image, "rb");

	TEST_ASSERT(f);
	TEST_ASSERT(fseek(f, off, SEEK_SET) == 0);
	TEST_ASSERT(fread(b, 1, len, f) == (size_t)len);
	fclose(f);
	return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint32_t be32_at(const char *image, long off)
{
	uint32_t v = le_at(image, off, 4);

	return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24;
}

static void copy_file(const char *from, const char *to)
{
	static char buf[65536];
	size_t n;
	FILE *in = fopen(from, "rb");
	FILE *out = fopen(to, "wb");

	TEST_ASSERT(in && out);
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		TEST_ASSERT_EQ(n, fwrite(buf, 1, n, out));
	TEST_ASSERT_EQ(0, fclose(out));
	fclose(in);
}

static void create(const char *path)
{
	ext4_file f;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

/* On the disk: the journal (superblock at jsb) is not empty. */
static bool journal_used(const char *image, long jsb)
{
	return be32_at(image, jsb + 28) != 0;
}

/* On the disk: the filesystem is marked for recovery. */
static bool needs_recovery(const char *image)
{
	return le_at(image, 1024 + 96, 4) & EXT4_FINCOM_RECOVER_BIT;
}

/* After replaying its journal, image has the files of the transactions. */
static void expect_files(const char *image)
{
	uint32_t mode;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	mode = 0;
	TEST_ASSERT_EQ(EOK, ext4_mode_get(TEST_MP "new", &mode));
	TEST_ASSERT_EQ(0100000, mode & 0170000);
	mode = 0;
	TEST_ASSERT_EQ(EOK, ext4_mode_get(TEST_MP "last", &mode));
	TEST_ASSERT_EQ(0100000, mode & 0170000);
	TEST_ASSERT_EQ(ENOENT, ext4_mode_get(TEST_MP "d", &mode));
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	/* 1 KiB blocks: the superblock at 1024, the descriptors in block 2. */
	uint64_t itable = le_at(image, 2048 + 8, 4) * 1024ULL;
	uint64_t isize = le_at(image, 1024 + 88, 2);
	/* The journal (inode 8): its superblock is the first block of the
	 * first extent in the inode. */
	uint64_t jinode = itable + 7 * isize;
	long jsb = le_at(image, jinode + 40 + 20, 4) * 1024L;
	static const int ops[] = {FAULT_DEV_READ, FAULT_DEV_WRITE};
	char need[512], work[512], cut[512];
	uint64_t nth, failed;
	uint32_t mode;
	size_t i;
	int r;

	snprintf(need, sizeof(need), "%s.need", image);
	snprintf(work, sizeof(work), "%s.work", image);
	snprintf(cut, sizeof(cut), "%s.cut", image);
	TEST_ASSERT_EQ(0xf30a, le_at(image, jinode + 40, 2));
	TEST_ASSERT_EQ(0, le_at(image, jinode + 40 + 6, 2));
	TEST_ASSERT_EQ(0, le_at(image, jinode + 40 + 12, 4));

	/* Transactions in the journal, not written to their places (write
	 * back cache): a directory created and removed again (its block is
	 * revoked) and two files. A copy of the image then needs recovery. */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, 1));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d"));
	create(TEST_MP "new");
	TEST_ASSERT_EQ(EOK, ext4_dir_rm(TEST_MP "d"));
	create(TEST_MP "last");
	copy_file(image, need);
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, 0));
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();
	TEST_ASSERT(journal_used(need, jsb) && needs_recovery(need));
	/* Without the replay the new file is not there. */
	copy_file(need, work);
	TEST_ASSERT_EQ(EOK, test_mount(work, false));
	mode = 0;
	r = ext4_mode_get(TEST_MP "new", &mode);
	TEST_ASSERT(r != EOK || (mode & 0170000) != 0100000);
	test_umount();

	for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		for (nth = 1;; nth++) {
			TEST_ASSERT(nth < 1000);
			copy_file(need, work);
			TEST_ASSERT_EQ(EOK, fault_dev_mount(work, false));
			fault_dev_fail_nth(ops[i], nth, 1);
			r = ext4_recover(TEST_MP);
			failed = fault_dev_failed();
			fault_dev_disarm();
			copy_file(work, cut);
			/* A journal that is not empty is never left without
			 * the recovery flag. */
			TEST_ASSERT(!journal_used(cut, jsb) ||
				    needs_recovery(cut));
			if (r == EOK)
				TEST_ASSERT(!journal_used(cut, jsb) &&
					    !needs_recovery(cut));
			else
				TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
			test_umount();
			expect_files(cut);
			expect_files(work);
			if (!failed)
				break;
		}
		printf("%s: %llu failing ops\n",
		       ops[i] == FAULT_DEV_READ ? "reads" : "writes",
		       (unsigned long long)nth - 1);
	}
	return 0;
}
