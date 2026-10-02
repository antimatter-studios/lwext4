/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * ext4_readlink_at() and the length query of ext4_readlink() (upstream
 * issue 60): a link target must be readable in pieces into a buffer smaller
 * than the target, and its length must be available before reading it.
 * Fast (inline) and slow (data block) symlinks, made by mke2fs and by
 * lwext4, before and after a remount.
 */

#include "test_util.h"

#include <string.h>

#define SLOW_LEN 1004 /* 1000 'd' + "/end" */

static char long_target[1001];

static void check_link(const char *path, const char *expect)
{
	size_t len = strlen(expect);
	char got[1100];
	char buf[7];
	size_t rcnt, off;

	/* Length of the target */
	rcnt = 12345;
	TEST_ASSERT_EQ(EOK, ext4_readlink(path, NULL, 0, &rcnt));
	TEST_ASSERT_EQ(len, rcnt);
	rcnt = 12345;
	TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, 3, NULL, 0, &rcnt));
	TEST_ASSERT_EQ(len, rcnt);

	/* Read into a small buffer: truncated unless the target fits */
	TEST_ASSERT_EQ(EOK, ext4_readlink(path, buf, sizeof(buf), &rcnt));
	TEST_ASSERT_EQ(len < sizeof(buf) ? len : sizeof(buf), rcnt);
	TEST_ASSERT(memcmp(buf, expect, rcnt) == 0);

	/* The whole target, in pieces of sizeof(buf) bytes */
	off = 0;
	for (;;) {
		TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, off, buf,
						     sizeof(buf), &rcnt));
		if (rcnt == 0)
			break;
		TEST_ASSERT(rcnt <= sizeof(buf));
		TEST_ASSERT(off + rcnt <= len);
		memcpy(got + off, buf, rcnt);
		off += rcnt;
	}
	TEST_ASSERT_EQ(len, off);
	TEST_ASSERT(memcmp(got, expect, len) == 0);

	/* One byte from the middle and from the last position */
	TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, len / 2, buf, 1, &rcnt));
	TEST_ASSERT_EQ(1, rcnt);
	TEST_ASSERT_EQ(expect[len / 2], buf[0]);
	TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, len - 1, buf, sizeof(buf),
					     &rcnt));
	TEST_ASSERT_EQ(1, rcnt);
	TEST_ASSERT_EQ(expect[len - 1], buf[0]);

	/* At and past the end */
	rcnt = 12345;
	TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, len, buf, sizeof(buf),
					     &rcnt));
	TEST_ASSERT_EQ(0, rcnt);
	rcnt = 12345;
	TEST_ASSERT_EQ(EOK, ext4_readlink_at(path, len + 5000, buf,
					     sizeof(buf), &rcnt));
	TEST_ASSERT_EQ(0, rcnt);

	/* A NULL buffer with a size, or a length query without rcnt */
	TEST_ASSERT_EQ(EINVAL, ext4_readlink(path, NULL, 5, &rcnt));
	TEST_ASSERT_EQ(EINVAL, ext4_readlink(path, NULL, 0, NULL));
}

static void check_all(void)
{
	char slow[SLOW_LEN + 1];

	memset(slow, 'd', 1000);
	strcpy(slow + 1000, "/end");

	check_link(TEST_MP "fast", "short/target.txt");
	check_link(TEST_MP "slow", slow);
	check_link(TEST_MP "new_fast", "a/b/c");
	check_link(TEST_MP "new_slow", long_target);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	size_t rcnt;
	int i;

	for (i = 0; i < 1000; i++)
		long_target[i] = 'a' + i % 26;
	long_target[1000] = 0;

	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_fsymlink("a/b/c", TEST_MP "new_fast"));
	TEST_ASSERT_EQ(EOK, ext4_fsymlink(long_target, TEST_MP "new_slow"));
	check_all();

	/* Not a symlink */
	TEST_ASSERT(ext4_readlink(TEST_MP "lost+found", NULL, 0, &rcnt) != EOK);
	test_umount();

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	check_all();
	test_umount();
	return 0;
}
