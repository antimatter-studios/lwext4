/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * The clock (fork issue #129). lwext4 had no notion of time: everything it
 * created was dated 1970 and no operation updated a time. Now it stamps
 * like Linux, from whatever time the board has:
 *
 *  1. no clock at all: the newest time stored in the filesystem (here the
 *     mke2fs time, 1000000000), never 1970
 *  2. a start date advanced by an uptime counter
 *  3. a clock callback: the times of each operation
 *  4. a clock that goes back: stamps stay at the newest time
 *  5. a time past 2038: the epoch bit (checked with debugfs in the
 *     .check.sh, with the creation time)
 *
 * and keeps the superblock's mount and write times, which carry the time
 * to the next mount.
 */

#include "test_util.h"

#include <ext4_inode.h>
#include <ext4_misc.h>

#include <string.h>

#define MKFS_TIME 1000000000u
#define START 1100000000u
#define T 1700000000u
#define LATE 0x90000000u /* 2046 */

static uint32_t fake_now;
static uint32_t fake_uptime;

static uint32_t now_cb(void) { return fake_now; }
static uint32_t uptime_cb(void) { return fake_uptime; }

static void create(const char *path)
{
	ext4_file f;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void append(const char *path)
{
	ext4_file f;
	size_t n;

	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "ab"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, "x", 1, &n));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void times(const char *path, uint32_t a, uint32_t m, uint32_t c)
{
	uint32_t v;

	TEST_ASSERT_EQ(EOK, ext4_atime_get(path, &v));
	TEST_ASSERT_EQ(a, v);
	TEST_ASSERT_EQ(EOK, ext4_mtime_get(path, &v));
	TEST_ASSERT_EQ(m, v);
	TEST_ASSERT_EQ(EOK, ext4_ctime_get(path, &v));
	TEST_ASSERT_EQ(c, v);
}

static uint32_t mtime(const char *path)
{
	uint32_t v = 0;

	TEST_ASSERT_EQ(EOK, ext4_mtime_get(path, &v));
	return v;
}

static struct ext4_sblock *sb(void)
{
	struct ext4_sblock *s = NULL;

	TEST_ASSERT_EQ(EOK, ext4_get_sblock(TEST_MP, &s));
	return s;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct ext4_inode raw;
	uint32_t ino, root_atime;
	ext4_file f;

	/* 1. No clock: the time of the filesystem */
	TEST_ASSERT_EQ(0u, ext4_clock_get());
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	create(TEST_MP "seeded");
	times(TEST_MP "seeded", MKFS_TIME, MKFS_TIME, MKFS_TIME);
	TEST_ASSERT_EQ(MKFS_TIME, ext4_get32(sb(), mount_time));
	test_umount();

	/* 2. A start date, advanced by an uptime counter */
	fake_uptime = 5;
	ext4_clock_uptime_setup(uptime_cb);
	ext4_clock_set(START);
	TEST_ASSERT_EQ(START, ext4_clock_get());
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	create(TEST_MP "u");
	TEST_ASSERT_EQ(START, mtime(TEST_MP "u"));
	fake_uptime = 105;
	append(TEST_MP "u");
	times(TEST_MP "u", START, START + 100, START + 100);
	test_umount();
	ext4_clock_uptime_setup(NULL);

	/* 3. A clock: each operation */
	ext4_clock_setup(now_cb);
	fake_now = T;
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_atime_get(TEST_MP, &root_atime));
	create(TEST_MP "f");
	times(TEST_MP "f", T, T, T);
	/* the parent's contents changed, it was not read */
	times(TEST_MP, root_atime, T, T);
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "f", &ino, &raw));
	TEST_ASSERT_EQ(T, to_le32(raw.crtime));

	fake_now = T + 10;
	append(TEST_MP "f");
	times(TEST_MP "f", T, T + 10, T + 10);

	fake_now = T + 20;
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "f", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 0));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	times(TEST_MP "f", T, T + 20, T + 20);

	fake_now = T + 30;
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(TEST_MP "d"));
	times(TEST_MP "d", T + 30, T + 30, T + 30);
	fake_now = T + 40;
	TEST_ASSERT_EQ(EOK, ext4_frename(TEST_MP "f", TEST_MP "d/f"));
	times(TEST_MP "d/f", T, T + 20, T + 40);
	times(TEST_MP, root_atime, T + 40, T + 40);
	times(TEST_MP "d", T + 30, T + 40, T + 40);

	fake_now = T + 50;
	TEST_ASSERT_EQ(EOK, ext4_flink(TEST_MP "d/f", TEST_MP "g"));
	times(TEST_MP "d/f", T, T + 20, T + 50);
	times(TEST_MP, root_atime, T + 50, T + 50);

	fake_now = T + 60;
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "g"));
	times(TEST_MP "d/f", T, T + 20, T + 60);
	times(TEST_MP, root_atime, T + 60, T + 60);

	fake_now = T + 70;
	TEST_ASSERT_EQ(EOK, ext4_mode_set(TEST_MP "d/f", 0600));
	times(TEST_MP "d/f", T, T + 20, T + 70);
	fake_now = T + 80;
	TEST_ASSERT_EQ(EOK, ext4_setxattr(TEST_MP "d/f", "user.a", 6, "v", 1));
	times(TEST_MP "d/f", T, T + 20, T + 80);

	/* 4. The clock goes back: never below the newest stamp */
	fake_now = T - 1000;
	create(TEST_MP "old");
	times(TEST_MP "old", T + 80, T + 80, T + 80);
	test_umount();

	/* The unmount saved the time, a read-only mount does not change it */
	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	TEST_ASSERT_EQ(T + 80, ext4_get32(sb(), write_time));
	TEST_ASSERT_EQ(T, ext4_get32(sb(), mount_time));
	test_umount();

	/* 5. After 2038 */
	fake_now = LATE;
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	create(TEST_MP "late");
	TEST_ASSERT_EQ(LATE, mtime(TEST_MP "late"));
	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(TEST_MP "late", &ino, &raw));
	TEST_ASSERT_EQ(1u, to_le32(raw.mtime_extra) & 3);
	test_umount();
	ext4_clock_setup(NULL);
	return 0;
}
