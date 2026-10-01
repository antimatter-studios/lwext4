/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Checkpointing journalled blocks when the block device fails.
 *
 * - Writing a block to its place on the disk fails (write through cache):
 *   the journal counted the block as written anyway. It moved its start
 *   past the transaction and ext4_journal_stop() reported success and
 *   marked the filesystem clean, although the block had never reached the
 *   disk; after a power cut nothing replays it (here: the new file's inode
 *   is all zeros). The transaction must stay in the journal until the
 *   block is written: ext4_journal_stop() fails and can be called again
 *   once the device works, and a copy of the image taken in between (a
 *   power cut) replays the transaction.
 * - The same while the journal fills up: making room for a new
 *   transaction checkpoints the oldest one, and must not drop it from the
 *   journal while its block is not written.
 * - Reading the journal copy of a block fails while an older transaction
 *   is checkpointed whose block a newer transaction changed again (write
 *   back cache): ext4_assert(r == EOK) in jbd_journal_flush_trans()
 *   aborted the program. ext4_journal_stop() must fail and succeed once
 *   the device works again.
 */

#include "fault_dev.h"

#include <string.h>

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

/* After replaying its journal, image has regular files at the paths. */
static void expect_files(const char *image, const char *const *paths)
{
	uint32_t mode;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	for (; *paths; paths++) {
		mode = 0;
		TEST_ASSERT_EQ(EOK, ext4_mode_get(*paths, &mode));
		TEST_ASSERT_EQ(0100000, mode & 0170000);
	}
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	/* 1 KiB blocks: the superblock at 1024, the descriptors in block 2. */
	uint64_t itable = le_at(image, 2048 + 8, 4) * 1024ULL;
	uint64_t isize = le_at(image, 1024 + 88, 2);
	/* The journal (inode 8): one extent in the inode. */
	uint64_t jinode = itable + 7 * isize;
	uint64_t jstart = le_at(image, jinode + 40 + 20, 4) * 1024ULL;
	uint64_t jlen = le_at(image, jinode + 40 + 16, 2) * 1024ULL;
	static char files[1000][32];
	static const char *first[1002] = {TEST_MP "new"};
	char path[32];
	ext4_file f;
	uint64_t failed;
	int n, i;
	const char *const all[] = {TEST_MP "new", TEST_MP "a", TEST_MP "b",
				   NULL};
	char cut[512];

	snprintf(cut, sizeof(cut), "%s.cut", image);
	TEST_ASSERT_EQ(0xf30a, le_at(image, jinode + 40, 2));
	TEST_ASSERT_EQ(0, le_at(image, jinode + 40 + 6, 2));
	TEST_ASSERT(jlen >= 1024 * 1024);

	/* The inode of the new file (12, the first free one: no later file
	 * shares its inode table block) cannot be written: its transaction is
	 * committed to the journal, its blocks not checkpointed. */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	/* A block cache large enough for every block used here (ports
	 * configure larger caches than the default of 8 blocks): the block
	 * that cannot be written is not evicted before the journal is full. */
	fault_dev.bc->cnt = 4096;
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	fault_dev_fail_range(FAULT_DEV_WRITE, itable + 11 * isize, isize);
	create(TEST_MP "new");
	TEST_ASSERT(fault_dev_failed() > 0);
	TEST_ASSERT_EQ(EIO, ext4_journal_stop(TEST_MP));
	/* Fill the journal: making room for a transaction checkpoints the
	 * oldest one, whose inode still cannot be written. */
	failed = fault_dev_failed();
	for (n = 0; n < 1000 && fault_dev_failed() == failed; n++) {
		snprintf(path, sizeof(path), TEST_MP "f%d", n);
		if (ext4_fopen(&f, path, "wb") == EOK)
			TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	}
	TEST_ASSERT(fault_dev_failed() > failed);
	TEST_ASSERT_EQ(EIO, ext4_journal_stop(TEST_MP));
	/* Power cut: the journal still has the transactions. */
	copy_file(image, cut);
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();
	/* The last file's transaction found no room: all but that one. */
	for (i = 0; i + 1 < n; i++) {
		snprintf(files[i], sizeof(files[i]), TEST_MP "f%d", i);
		first[i + 1] = files[i];
	}
	first[i + 1] = NULL;
	expect_files(cut, first);
	expect_files(image, first);

	/* Write back cache: the blocks "a" changes (directory, bitmaps,
	 * inode table) are changed again by "b" before they are written.
	 * Checkpointing the transaction of "a" then writes their journal
	 * copies, which cannot be read. */
	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, 1));
	create(TEST_MP "a");
	create(TEST_MP "b");
	fault_dev_fail_range(FAULT_DEV_READ, jstart, jlen);
	TEST_ASSERT_EQ(EIO, ext4_journal_stop(TEST_MP));
	TEST_ASSERT(fault_dev_failed() > 0);
	copy_file(image, cut);
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, 0));
	test_umount();
	expect_files(cut, all);
	expect_files(image, all);
	return 0;
}
