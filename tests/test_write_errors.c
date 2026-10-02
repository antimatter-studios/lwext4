/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Write errors without a journal (fork issue #61): every operation must
 * report that a block could not be written.
 *
 * Without a journal blocks are written when they are released, or when an
 * operation leaves write-back mode, and those writes failed silently:
 * ext4_bcache_free() dropped the error of writing the block, the API calls
 * ignored the result of ext4_block_cache_write_back(), and several of them
 * the result of releasing a block or an inode. The operation returned EOK
 * although its changes never reached the disk.
 *
 * For each operation, on ext4 without a journal and on ext2, every write
 * from the Nth on fails, for every N until the operation completes before
 * the Nth write. Since no later write succeeds, a failed write must make
 * the operation fail.
 */

#include "fault_dev.h"

#include <string.h>

static ext4_file F;
static char buf[70000];

static int prep_none(void) { return EOK; }
static int prep_big(void) { return ext4_fopen(&F, TEST_MP "big", "r+"); }
static int prep_new(void) { return ext4_fopen(&F, TEST_MP "new", "wb"); }

static int op_truncate(void) { return ext4_ftruncate(&F, 1000); }
static int op_truncate0(void) { return ext4_ftruncate(&F, 0); }
static int op_write_new(void)
{
	size_t n;

	return ext4_fwrite(&F, buf, sizeof(buf), &n);
}
static int op_write_append(void)
{
	size_t n;

	TEST_ASSERT_EQ(EOK, ext4_fseek(&F, 0, SEEK_END));
	return ext4_fwrite(&F, buf, sizeof(buf), &n);
}
static int op_remove(void) { return ext4_fremove(TEST_MP "big"); }
static int op_remove_small(void) { return ext4_fremove(TEST_MP "small"); }
static int op_symlink(void) { return ext4_fsymlink("small", TEST_MP "s1"); }
static int op_symlink_slow(void)
{
	return ext4_fsymlink("a/very/long/target/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
			     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", TEST_MP "s2");
}
static int op_mkdir(void) { return ext4_dir_mk(TEST_MP "nd"); }
static int op_mkdir_many(void) { return ext4_dir_mk(TEST_MP "many/nd"); }
static int op_rmdir(void) { return ext4_dir_rm(TEST_MP "d"); }
static int op_rmdir_many(void) { return ext4_dir_rm(TEST_MP "many"); }
static int op_dir_mv(void) { return ext4_dir_mv(TEST_MP "d", TEST_MP "many/d"); }
static int op_rename(void)
{
	return ext4_frename(TEST_MP "small", TEST_MP "many/small2");
}
static int op_link(void) { return ext4_flink(TEST_MP "small", TEST_MP "many/l"); }
static int op_mknod(void)
{
	return ext4_mknod(TEST_MP "nod", EXT4_DE_CHRDEV, 0x0101);
}
static int op_setxattr(void)
{
	return ext4_setxattr(TEST_MP "small", "user.a", 6, buf, 100);
}
static int op_setxattr_block(void)
{
	return ext4_setxattr(TEST_MP "small", "user.b", 6, buf, 600);
}
static int op_create(void)
{
	ext4_file f;
	int r = ext4_fopen(&f, TEST_MP "many/created", "wb");

	if (r == EOK)
		ext4_fclose(&f);
	return r;
}
static int op_open_truncate(void)
{
	ext4_file f;
	int r = ext4_fopen(&f, TEST_MP "big", "wb");

	if (r == EOK)
		ext4_fclose(&f);
	return r;
}
static int op_mode(void) { return ext4_mode_set(TEST_MP "small", 0600); }

static const struct {
	const char *name;
	int (*prep)(void);
	int (*op)(void);
} ops[] = {
	{"ftruncate", prep_big, op_truncate},
	{"ftruncate to 0", prep_big, op_truncate0},
	{"fwrite new file", prep_new, op_write_new},
	{"fwrite append", prep_big, op_write_append},
	{"fremove", prep_none, op_remove},
	{"fremove small", prep_none, op_remove_small},
	{"fsymlink fast", prep_none, op_symlink},
	{"fsymlink slow", prep_none, op_symlink_slow},
	{"dir_mk", prep_none, op_mkdir},
	{"dir_mk in large dir", prep_none, op_mkdir_many},
	{"dir_rm", prep_none, op_rmdir},
	{"dir_rm large dir", prep_none, op_rmdir_many},
	{"dir_mv", prep_none, op_dir_mv},
	{"frename", prep_none, op_rename},
	{"flink", prep_none, op_link},
	{"mknod", prep_none, op_mknod},
	{"setxattr in inode", prep_none, op_setxattr},
	{"setxattr block", prep_none, op_setxattr_block},
	{"fopen create", prep_none, op_create},
	{"fopen truncate", prep_none, op_open_truncate},
	{"mode_set", prep_none, op_mode},
};

static void copy(const char *src, const char *dst)
{
	static char tmp[1 << 16];
	FILE *a = fopen(src, "rb"), *b = fopen(dst, "wb");
	size_t n;

	TEST_ASSERT(a && b);
	while ((n = fread(tmp, 1, sizeof(tmp), a)) > 0)
		TEST_ASSERT_EQ(n, fwrite(tmp, 1, n, b));
	fclose(a);
	fclose(b);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	static const char *const suffixes[] = {"", ".ext2"};
	char img[512], pristine[512];
	int lost = 0;
	size_t s, i;

	memset(buf, 'q', sizeof(buf));
	for (s = 0; s < 2; s++) {
		snprintf(img, sizeof(img), "%s%s", image, suffixes[s]);
		snprintf(pristine, sizeof(pristine), "%s.pristine", img);
		for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
			uint64_t n;

			for (n = 1; n < 1000; n++) {
				int r;

				copy(pristine, img);
				TEST_ASSERT_EQ(EOK, fault_dev_mount(img, false));
				TEST_ASSERT_EQ(EOK, ops[i].prep());
				fault_dev_fail_nth(FAULT_DEV_WRITE, n, 0);
				r = ops[i].op();
				fault_dev_disarm();
				if (!fault_dev_failed()) {
					TEST_ASSERT_EQ(EOK, r);
					test_umount();
					break;
				}
				if (r == EOK) {
					fprintf(stderr, "%s: %s returned EOK "
						"with every write from the "
						"%lluth on failing\n",
						suffixes[s][0] ? "ext2" : "ext4",
						ops[i].name,
						(unsigned long long)n);
					lost++;
				}
				/* the unmount writes what is left, which may
				 * fail as well: only the operation counts */
				ext4_umount(TEST_MP);
				ext4_device_unregister(TEST_DEV);
			}
			TEST_ASSERT(n < 1000);
		}
	}
	TEST_ASSERT_EQ(0, lost);
	return 0;
}
