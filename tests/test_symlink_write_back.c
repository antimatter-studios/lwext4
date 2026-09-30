/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Reading back a slow symlink (target in a data block) made in write-back
 * mode. ext4_fsymlink() put the zero padded target block into the block
 * cache as a dirty block, but ext4_readlink() reads the target through
 * ext4_fread(), which reads file data from the device and does not look
 * into the cache. While write-back mode kept the block unwritten (here
 * ext4_cache_write_back(), in a threaded program also another thread's)
 * readlink returned whatever the block held before: the right length,
 * wrong bytes. The block has to reach the device when the symlink is
 * made, like file data does.
 *
 * red-green: guard (passes on the foundation, which wrote the target to
 * the device; it fails with the zero padding of test_symlink_slow_zero
 * alone, which is what this branch builds on)
 */

#include "test_util.h"

#include <string.h>

#define TARGET                                                                 \
	"/a/symlink/target/that/is/long/enough/to/need/a/data/block/of/its/"   \
	"own/instead/of/the/inode"

static void check_link(const char *path)
{
	char buf[256];
	size_t rcnt = 0;

	memset(buf, 0, sizeof(buf));
	TEST_ASSERT_EQ(EOK, ext4_readlink(path, buf, sizeof(buf), &rcnt));
	TEST_ASSERT_EQ(strlen(TARGET), rcnt);
	TEST_ASSERT(memcmp(buf, TARGET, rcnt) == 0);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	static unsigned char junk[65536];
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));

	/* Leave non-zero data in free blocks */
	memset(junk, 0xaa, sizeof(junk));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "junk", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, junk, sizeof(junk), &n));
	TEST_ASSERT_EQ(sizeof(junk), n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "junk"));

	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, true));
	TEST_ASSERT_EQ(EOK, ext4_fsymlink(TARGET, TEST_MP "link"));
	check_link(TEST_MP "link");
	TEST_ASSERT_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
	check_link(TEST_MP "link");
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	check_link(TEST_MP "link");
	test_umount();
	return 0;
}
