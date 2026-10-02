/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * The ext_attr feature (fork issue #98). lwext4 stored extended attributes
 * on filesystems without it, and its mkfs never set it, so e2fsprogs
 * ignored every attribute lwext4 wrote on a filesystem lwext4 formatted
 * (and e2fsck's repair dropped xattr blocks). Like Linux, storing an
 * attribute must turn the feature on (the check script reads the
 * attribute with debugfs), and ext4_mkfs must set it, as mke2fs does.
 */

#include <ext4_mkfs.h>
#include <ext4_super.h>

#include "test_util.h"

#include <stdint.h>
#include <string.h>

#define RD_BSIZE 512u
#define RD_SIZE (4u * 1024u * 1024u)

static uint8_t rd[RD_SIZE];

static int rd_open(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int rd_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		    uint32_t blk_cnt)
{
	(void)bdev;
	memcpy(buf, rd + blk_id * RD_BSIZE, blk_cnt * RD_BSIZE);
	return EOK;
}

static int rd_bwrite(struct ext4_blockdev *bdev, const void *buf,
		     uint64_t blk_id, uint32_t blk_cnt)
{
	(void)bdev;
	memcpy(rd + blk_id * RD_BSIZE, buf, blk_cnt * RD_BSIZE);
	return EOK;
}

EXT4_BLOCKDEV_STATIC_INSTANCE(rd_dev, RD_BSIZE, RD_SIZE / RD_BSIZE, rd_open,
			      rd_bread, rd_bwrite, rd_open, 0, 0);

static int mkfs_has_ext_attr(int fs_type)
{
	static struct ext4_fs fs;
	struct ext4_mkfs_info info = {.block_size = 1024};
	struct ext4_sblock sb;

	memset(rd, 0, sizeof(rd));
	TEST_ASSERT_EQ(EOK, ext4_mkfs(&fs, &rd_dev, &info, fs_type));
	memcpy(&sb, rd + 1024, sizeof(sb));
	return ext4_sb_feature_com(&sb, EXT4_FCOM_EXT_ATTR);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	static const char value[] = "kept by e2fsprogs";

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_setxattr(TEST_MP "a.txt", "user.lwext4", 11,
					  value, sizeof(value) - 1));
	test_umount();

	TEST_ASSERT(mkfs_has_ext_attr(F_SET_EXT2));
	TEST_ASSERT(mkfs_has_ext_attr(F_SET_EXT3));
	TEST_ASSERT(mkfs_has_ext_attr(F_SET_EXT4));
	return 0;
}
