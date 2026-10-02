/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * A read error while ext4_dir_mk() checks whether the directory exists
 * (fork issue #107).
 *
 * The concern of the issue: a lookup that took a failed read for "name not
 * found" would let ext4_dir_mk() add a second entry with a name that
 * already exists. It does not: lookups return read errors, and
 * ext4_dir_mk() then looks the name up a second time to create it, which
 * reads again. With a single failed read that second lookup succeeds, so
 * ext4_dir_mk() returns EOK and the result on disk is correct.
 *
 * Only the Nth read fails, for every N, for a new name and for an existing
 * directory: the name must be in the directory at most once, exactly once
 * if it existed or EOK was returned, and the filesystem must stay
 * consistent.
 *
 * red-green: guard (the base already behaves this way).
 */

#include "fault_dev.h"

#include <string.h>

#define DIR TEST_MP "many"

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

static unsigned count_name(const char *name)
{
	const ext4_direntry *de;
	unsigned found = 0;
	ext4_dir d;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, DIR));
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		if (de->name_length == strlen(name) &&
		    !memcmp(de->name, name, de->name_length))
			found++;
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	return found;
}

static void run(const char *image, const char *pristine, const char *name,
		bool exists)
{
	char path[128], cmd[1024];
	uint64_t n;

	snprintf(path, sizeof(path), DIR "/%s", name);
	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s' >/dev/null",
		 image);

	for (n = 1; n < 1000; n++) {
		uint64_t failed;
		unsigned found;
		int r;

		copy(pristine, image);
		TEST_ASSERT_EQ(EOK, fault_dev_mount(image, false));
		fault_dev_fail_nth(FAULT_DEV_READ, n, 1);
		r = ext4_dir_mk(path);
		fault_dev_disarm();
		failed = fault_dev_failed();
		test_umount();

		TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
		found = count_name(name);
		test_umount();
		if (found > 1 || ((exists || r == EOK) && found != 1)) {
			fprintf(stderr, "ext4_dir_mk(%s), read %llu failed, "
				"r=%d: %u entries\n", path,
				(unsigned long long)n, r, found);
			exit(1);
		}
		TEST_ASSERT_EQ(0, system(cmd));

		if (!failed) {
			TEST_ASSERT_EQ(EOK, r);
			break;
		}
	}
	/* The lookup read several directory blocks */
	TEST_ASSERT(n > 10 && n < 1000);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char pristine[512];

	snprintf(pristine, sizeof(pristine), "%s.pristine", image);
	run(image, pristine, "new_dir", false);
	run(image, pristine, "zz_existing", true);
	return 0;
}
