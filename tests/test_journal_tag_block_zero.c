/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A journal tag for filesystem block 0 (fork issue #230, found by fuzz_rw).
 * Replay took such a block for the one holding the superblock and copied
 * the superblock from offset 1024 of the logged block. With 1 KiB blocks the
 * superblock is in block 1 and offset 1024 is the end of the buffer: a heap
 * buffer overflow (ASan, which red-green builds use, reports it).
 *
 * No filesystem metadata lives in block 0 there, so such a journal is
 * damaged: recovery must fail with EIO, leave the boot block alone and leave
 * the journal to be replayed. With larger blocks block 0 does hold the
 * superblock, and replaying it must keep working. The images are made by
 * the .sh.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

#define MARKER "REPLAYED-INTO-BOOT-BLOCK"
#define LABEL "replayed"
#define SB_OFFSET 1024
#define SB_LABEL_OFFSET 120

static void read_image(const char *image, long offset, void *buf, size_t len)
{
	FILE *f = fopen(image, "rb");

	TEST_ASSERT(f != NULL);
	TEST_ASSERT_EQ(0, fseek(f, offset, SEEK_SET));
	TEST_ASSERT_EQ(len, fread(buf, 1, len, f));
	fclose(f);
}

static void check_1k_refused(const char *image)
{
	char boot[sizeof(MARKER) - 1];
	int i;

	/* Twice: a refused journal stays marked as needing replay */
	for (i = 0; i < 2; i++) {
		TEST_ASSERT_EQ(EOK, test_mount(image, false));
		TEST_ASSERT_EQ(EIO, ext4_recover(TEST_MP));
		test_umount();
	}

	read_image(image, 0, boot, sizeof(boot));
	TEST_ASSERT(memcmp(boot, MARKER, sizeof(boot)) != 0);
}

/* Not to over-correct: larger blocks still replay their superblock */
static void check_4k_replayed(const char *image)
{
	char label[sizeof(LABEL) - 1];

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	test_umount();

	read_image(image, SB_OFFSET + SB_LABEL_OFFSET, label, sizeof(label));
	TEST_ASSERT(memcmp(label, LABEL, sizeof(label)) == 0);

	/* The journal is empty now */
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char image_4k[512];

	snprintf(image_4k, sizeof(image_4k), "%s.4k", image);
	check_4k_replayed(image_4k);
	check_1k_refused(image);
	return 0;
}
