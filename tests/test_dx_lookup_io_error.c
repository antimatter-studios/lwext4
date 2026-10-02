/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Read errors while looking a name up in an indexed directory (fork issue
 * #107).
 *
 * ext4_dir_dx_find_entry() and ext4_dir_dx_add_entry() turned every error
 * of walking the index, also a failed read of an index node, into
 * EXT4_ERR_BAD_DX_DIR: the caller then took the index for damaged, cleared
 * the directory's index flag for good and searched it linearly. And an
 * error loading the next leaf block (ext4_dir_dx_next_block()) was only
 * taken for one if it was negative, which lwext4 errors are not: the
 * lookup reported ENOENT, so opening an existing name for writing could
 * create a second entry with that name.
 *
 * One read fails, the Nth, for every N: looking up an existing name in a
 * two level htree must find it or fail with an error, never report
 * ENOENT; opening it with "wb" must not create a second entry; and the
 * directory must keep its index.
 */

#include "fault_dev.h"

#include <ext4_inode.h>

#include <string.h>

#define DIR TEST_MP "many"
#define NAME "file_with_a_long_name_04321"

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

static void check_dir(uint64_t n)
{
	struct ext4_inode inode;
	const ext4_direntry *de;
	uint32_t ino;
	ext4_dir d;
	unsigned found = 0, entries = 0;

	TEST_ASSERT_EQ(EOK, ext4_raw_inode_fill(DIR, &ino, &inode));
	if (!ext4_inode_has_flag(&inode, EXT4_INODE_FLAG_INDEX)) {
		fprintf(stderr, "N=%llu: the directory lost its index\n",
			(unsigned long long)n);
		exit(1);
	}
	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, DIR));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		entries++;
		if (de->name_length == sizeof(NAME) - 1 &&
		    !memcmp(de->name, NAME, sizeof(NAME) - 1))
			found++;
	}
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	if (found != 1 || entries != 8000 + 2) {
		fprintf(stderr, "N=%llu: %u entries named " NAME ", %u in "
			"all\n", (unsigned long long)n, found, entries);
		exit(1);
	}
}

static void run(const char *image, const char *pristine, const char *mode)
{
	uint64_t n;

	for (n = 1; n < 100; n++) {
		ext4_file f;
		int r;

		copy(pristine, image);
		TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
		fault_dev_fail_nth(FAULT_DEV_READ, n, 1);
		r = ext4_fopen(&f, DIR "/" NAME, mode);
		fault_dev_disarm();
		if (r == EOK)
			ext4_fclose(&f);
		if (r == ENOENT) {
			fprintf(stderr, "N=%llu: fopen(\"%s\") of an existing "
				"name: ENOENT\n", (unsigned long long)n, mode);
			exit(1);
		}
		check_dir(n);
		test_umount();
		if (!fault_dev_failed()) {
			TEST_ASSERT_EQ(EOK, r);
			break;
		}
	}
	TEST_ASSERT(n > 1 && n < 100);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char pristine[512];

	snprintf(pristine, sizeof(pristine), "%s.pristine", image);
	run(image, pristine, "rb");
	run(image, pristine, "wb");
	return 0;
}
