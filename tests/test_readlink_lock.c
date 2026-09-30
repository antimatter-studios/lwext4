/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_readlink() takes the mount point lock and then reads the target with
 * the public ext4_fread(), which takes the same lock again. With the lock
 * callbacks of ext4_mount_setup_locks() backed by a mutex, as they are
 * meant to be, readlink deadlocks on its own lock (a recursive mutex would
 * hide it, an error checking one fails the second lock).
 *
 * The lock callbacks here count: no call may lock the mount point while it
 * already holds it, and every call must leave it unlocked. Fast (target in
 * the inode) and slow (target in a data block) symlinks are read back.
 */

#include "test_util.h"

#include <string.h>

static int depth, max_depth;

static void lock(void)
{
	depth++;
	if (depth > max_depth)
		max_depth = depth;
	if (depth > 1)
		fprintf(stderr, "mount point locked while already locked\n");
}

static void unlock(void)
{
	if (depth <= 0)
		fprintf(stderr, "mount point unlocked while not locked\n");
	depth--;
}

static const struct ext4_lock locks = {
	.lock = lock,
	.unlock = unlock,
};

static void check_readlink(const char *path, const char *target)
{
	char buf[256];
	size_t rcnt = 0;

	memset(buf, 0, sizeof(buf));
	TEST_ASSERT_EQ(EOK, ext4_readlink(path, buf, sizeof(buf), &rcnt));
	TEST_ASSERT_EQ(1, max_depth);
	TEST_ASSERT_EQ(0, depth);
	TEST_ASSERT_EQ(strlen(target), rcnt);
	TEST_ASSERT(memcmp(buf, target, rcnt) == 0);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char slow[200];

	memset(slow, 's', sizeof(slow) - 1);
	slow[sizeof(slow) - 1] = 0;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_mount_setup_locks(TEST_MP, &locks));

	TEST_ASSERT_EQ(EOK, ext4_fsymlink("fast/target", TEST_MP "fast"));
	TEST_ASSERT_EQ(EOK, ext4_fsymlink(slow, TEST_MP "slow"));
	TEST_ASSERT_EQ(1, max_depth);
	TEST_ASSERT_EQ(0, depth);

	check_readlink(TEST_MP "fast", "fast/target");
	check_readlink(TEST_MP "slow", slow);

	test_umount();
	TEST_ASSERT_EQ(0, depth);
	return 0;
}
