/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Read errors while listing a directory (fork issue #107).
 *
 * ext4_dir_entry_next() returns NULL both at the end of a directory and
 * when reading a directory block fails, so a failed read silently ended the
 * listing early. ext4_dir_entry_get() reports the error.
 *
 * Every read from the Nth on fails, for every N: listing a directory of
 * many blocks must either list every entry or return an error, and the
 * entries returned before the error must be the start of the listing.
 * Only the Nth read fails: calling ext4_dir_entry_get() again after the
 * error must continue the listing, which must end up complete, without
 * duplicates.
 */

#include "fault_dev.h"

#include <string.h>

#define DIR TEST_MP "many"
#define MAX_ENTRIES 400

static char names[MAX_ENTRIES][256];
static unsigned n_names;

static void name_of(const ext4_direntry *de, char *out)
{
	memcpy(out, de->name, de->name_length);
	out[de->name_length] = 0;
}

/* The listing without faults */
static void list_clean(void)
{
	const ext4_direntry *de;
	ext4_dir d;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, DIR));
	for (;;) {
		TEST_ASSERT_EQ(EOK, ext4_dir_entry_get(&d, &de));
		if (!de)
			break;
		TEST_ASSERT(n_names < MAX_ENTRIES);
		TEST_ASSERT(de->inode != 0);
		name_of(de, names[n_names++]);
	}
	/* At the end it stays at the end */
	TEST_ASSERT_EQ(EOK, ext4_dir_entry_get(&d, &de));
	TEST_ASSERT(de == NULL);
	TEST_ASSERT(ext4_dir_entry_next(&d) == NULL);
	TEST_ASSERT_EQ(EINVAL, ext4_dir_entry_get(&d, NULL));
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));

	/* ".", "..", and 300 names of which 41 were removed */
	TEST_ASSERT_EQ(2 + 300 - 41, n_names);
}

/* List with reads failing from the nth on (count 0) or only the nth
 * (count 1). Returns false once no read failed at all. */
static bool list_failing(uint64_t n, uint64_t count)
{
	const ext4_direntry *de;
	char name[256];
	unsigned got = 0, errors = 0;
	ext4_dir d;
	int r;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, DIR));
	fault_dev_fail_nth(FAULT_DEV_READ, n, count);
	for (;;) {
		r = ext4_dir_entry_get(&d, &de);
		if (r != EOK) {
			TEST_ASSERT(de == NULL);
			errors++;
			if (count == 0)
				break;
			/* A single failed read: try again */
			TEST_ASSERT(errors < 10);
			continue;
		}
		if (!de)
			break;
		name_of(de, name);
		if (got >= n_names || strcmp(name, names[got])) {
			fprintf(stderr, "N=%llu count=%llu: entry %u is %s, "
				"expected %s\n", (unsigned long long)n,
				(unsigned long long)count, got, name,
				got < n_names ? names[got] : "the end");
			exit(1);
		}
		got++;
	}
	fault_dev_disarm();
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));

	if (!fault_dev_failed()) {
		TEST_ASSERT_EQ(0, errors);
		TEST_ASSERT_EQ(n_names, got);
		return false;
	}
	if (count == 0 && errors == 0) {
		fprintf(stderr, "N=%llu: a read failed, but the listing "
			"ended without an error after %u of %u entries\n",
			(unsigned long long)n, got, n_names);
		exit(1);
	}
	if (count == 1 && got != n_names) {
		fprintf(stderr, "N=%llu: one failed read: listed %u of %u "
			"entries\n", (unsigned long long)n, got, n_names);
		exit(1);
	}
	return true;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	uint64_t count, n;

	TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
	list_clean();
	test_umount();

	for (count = 0; count < 2; count++) {
		for (n = 1; n < 1000; n++) {
			bool failed;

			/* A cold cache for every run */
			TEST_ASSERT_EQ(EOK, fault_dev_mount(image, true));
			failed = list_failing(n, count);
			test_umount();
			if (!failed)
				break;
		}
		/* Several blocks were read, and the last run read them all */
		TEST_ASSERT(n > 10 && n < 1000);
	}
	return 0;
}
