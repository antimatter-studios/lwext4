/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Copies of an i-node read past it on filesystems with 128 byte i-nodes
 * (fork issue #179, found by fuzz_mkfs). struct ext4_inode is 156 bytes
 * (the fields after the first 128 exist only in larger i-nodes), and
 * jbd_get_fs() and ext4_raw_inode_fill() copied all of it: from the
 * i-node table block, the 28 bytes after the i-node, which are the next
 * i-node, or for the last i-node of a block (the journal's, i-node 8, with
 * 1 KiB blocks) memory after the cache buffer (AddressSanitizer:
 * heap-buffer-overflow, in ext4_recover()).
 *
 * A copy takes the i-node size of the filesystem and zeroes the rest:
 * ext4_raw_inode_fill() of /a gives /a, not the start of /b after it, and
 * recovering and journalling on such a filesystem reads nothing past its
 * buffers (run with AddressSanitizer, as red-green does).
 */

#include "test_util.h"

#include <ext4_inode.h>
#include <ext4_super.h>

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_inode inode;
	struct ext4_sblock *sb;
	uint32_t a, b;
	size_t i;
	ext4_file f;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_get_sblock(TEST_MP, &sb));
	TEST_ASSERT_EQ(128, ext4_get16(sb, inode_size));

	/* The journal's i-node, the last of its block */
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "c", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));

	/* /a and /b are consecutive i-nodes of one block */
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "b", &b, &inode));
	memset(&inode, 0xa5, sizeof(inode));
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "a", &a, &inode));
	TEST_ASSERT_EQ(a + 1, b);
	TEST_ASSERT(a % 8 != 0);
	TEST_ASSERT(ext4_inode_is_type(sb, &inode, EXT4_INODE_MODE_FILE));
	for (i = 128; i < sizeof(inode); i++)
		TEST_ASSERT_EQ(0, ((uint8_t *)&inode)[i]);
	test_umount();
	return 0;
}
