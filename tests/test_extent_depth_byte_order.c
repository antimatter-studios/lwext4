/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * eh_depth of an extent tree block is a little endian 16 bit field.
 * ext4_ext_init_header() stored it in host byte order, so on big endian
 * hosts every index block made by ext4_ext_split_node() (depth 1 or more)
 * had a wrong depth: depth 1 was written as 256, the next read of the
 * block failed the depth check of ext4_ext_check() and ext4_fwrite()
 * returned EIO. Leaf blocks (depth 0) and index blocks made by
 * ext4_ext_grow_indepth() (copied from the inode) were not affected.
 * Only big endian hosts are affected; run this test under qemu-user for
 * those (ci qemu-user matrix: s390x, powerpc, mips).
 *
 * red-green: guard on little endian hosts, where the base stores the
 * same bytes (the test is red against the base on big endian hosts).
 *
 * Two files are written block by block in turn, so each block is an
 * extent of its own. With 1 KiB blocks a tree block holds 84 entries and
 * the inode 4: the tree gets two levels below the inode and, after 84
 * leaves, the first index block is split. The index blocks named by the
 * inode are then read from the device and their headers checked byte by
 * byte, independently of the extent code, and both files are read back.
 */

#include "test_util.h"

#include <ext4_fs.h>
#include <ext4_super.h>

#include <stddef.h>
#include <string.h>

#define BS 1024
#define BLOCKS 7200 /* per file: 86 full leaves of 84 extents */

static uint8_t buf[BS];

static uint16_t get_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}

static void fill(uint32_t file, uint32_t blk)
{
	for (uint32_t i = 0; i < BS; i += 4) {
		uint32_t v = file << 28 | blk << 8 | i / 4;

		memcpy(buf + i, &v, 4);
	}
}

static void verify(const char *path, uint32_t file)
{
	uint8_t rd[BS];
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ((uint64_t)BLOCKS * BS, ext4_fsize(&f));
	for (uint32_t b = 0; b < BLOCKS; b++) {
		fill(file, b);
		TEST_ASSERT_EQ(EOK, ext4_fread(&f, rd, BS, &n));
		TEST_ASSERT_EQ(BS, n);
		TEST_ASSERT(!memcmp(rd, buf, BS));
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_inode inode;
	const uint8_t *root = (const uint8_t *)inode.blocks;
	struct ext4_sblock *sb;
	struct ext4_block b;
	struct ext4_fs *fs;
	ext4_file fa, fb;
	uint32_t ino;
	uint16_t entries;
	size_t n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&fa, TEST_MP "a", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&fb, TEST_MP "b", "wb"));
	for (uint32_t blk = 0; blk < BLOCKS; blk++) {
		fill(1, blk);
		TEST_ASSERT_EQ(EOK, ext4_fwrite(&fa, buf, BS, &n));
		TEST_ASSERT_EQ(BS, n);
		fill(2, blk);
		TEST_ASSERT_EQ(EOK, ext4_fwrite(&fb, buf, BS, &n));
		TEST_ASSERT_EQ(BS, n);
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&fa));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&fb));
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(EOK, ext4_get_sblock(TEST_MP, &sb));
	fs = (struct ext4_fs *)((char *)sb - offsetof(struct ext4_fs, sb));
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "a", &ino, &inode));

	/* struct ext4_extent_header: eh_magic, eh_entries, eh_max, eh_depth */
	TEST_ASSERT_EQ(0xF30A, get_le16(root));
	TEST_ASSERT_EQ(2, get_le16(root + 6));
	entries = get_le16(root + 2);
	/* the first index block was split */
	TEST_ASSERT(entries >= 2);
	for (uint16_t i = 0; i < entries; i++) {
		/* struct ext4_extent_index: ei_block, ei_leaf_lo, ei_leaf_hi */
		const uint8_t *ix = root + 12 + 12 * i;
		uint64_t blk = get_le32(ix + 4) | (uint64_t)get_le16(ix + 8) << 32;

		TEST_ASSERT_EQ(EOK, ext4_block_get(fs->bdev, &b, blk));
		TEST_ASSERT_EQ(0xF30A, get_le16(b.data));
		TEST_ASSERT_EQ(84, get_le16(b.data + 4));
		TEST_ASSERT_EQ(1, get_le16(b.data + 6));
		TEST_ASSERT_EQ(EOK, ext4_block_set(fs->bdev, &b));
	}

	verify(TEST_MP "a", 1);
	verify(TEST_MP "b", 2);
	test_umount();
	return 0;
}
