/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Metadata blocks marked free by a damaged block bitmap (fork issue #142,
 * found by fuzz_rw). The allocator took whatever the bitmap said: it
 * handed out block 0, the superblock (ext4_fs_append_inode_dblk aborted on
 * an assert), or the group descriptors, which file data then overwrote.
 *
 * The bitmap of the image marks blocks 0 to 3 free (see the .sh). Filling
 * the filesystem must end with ENOSPC, without ever allocating them: the
 * allocator sets their bits again instead. Afterwards the filesystem
 * mounts, reads back, and e2fsck finds it clean, bitmap included.
 */

#include "test_util.h"

#include <stdio.h>
#include <string.h>

static char buf[16384];

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char cmd[1024], path[64];
	size_t n, total = 0;
	ext4_file f;
	int i, r = EOK;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d"));
	memset(buf, 'z', sizeof(buf));

	/* Fill the filesystem: files of 16 KiB until it is full */
	for (i = 0; r == EOK && i < 1000; i++) {
		snprintf(path, sizeof(path), TEST_MP "d/f%d", i);
		r = ext4_fopen(&f, path, "wb");
		if (r != EOK)
			break;
		n = 0;
		r = ext4_fwrite(&f, buf, sizeof(buf), &n);
		total += n;
		TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	}
	TEST_ASSERT_EQ(ENOSPC, r);
	TEST_ASSERT(total > 0);
	test_umount();

	/* The superblock and descriptors are intact */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "d/f0", "rb"));
	TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, sizeof(buf), &n));
	TEST_ASSERT_EQ(sizeof(buf), n);
	TEST_ASSERT_EQ('z', buf[0]);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
	return 0;
}
