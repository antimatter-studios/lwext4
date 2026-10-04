/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Directories with large holes (fork issue #147, found by fuzz_mount). The
 * directory iterator stepped through a hole one block at a time, an extent
 * lookup each: a directory whose (damaged) size claims gigabytes beyond its
 * one block took seconds, minutes on a microcontroller. With extents the
 * hole is skipped at once.
 *
 * /huge and /normal (see the .sh) have the same entries; /huge claims
 * 4 GiB - 1 KiB. Listing /huge must take about as long as listing /normal
 * (compared, not timed: CI machines differ), and return the same entries.
 */

#include "test_util.h"

#include <stdio.h>
#include <time.h>

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Seconds to list a directory 20 times; its entries in *n */
static double list(const char *path, int *n)
{
	double t = now();
	int i;

	for (i = 0; i < 20; i++) {
		ext4_dir d;

		*n = 0;
		TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, path));
		while (ext4_dir_entry_next(&d))
			(*n)++;
		TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	}
	return now() - t;
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	double normal, huge;
	int n_normal, n_huge;

	TEST_ASSERT_EQ(EOK, test_mount(image, true));
	normal = list(TEST_MP "normal", &n_normal);
	huge = list(TEST_MP "huge", &n_huge);
	test_umount();

	printf("normal %.6f s, huge %.6f s\n", normal, huge);
	TEST_ASSERT_EQ(2, n_normal); /* "." and ".." */
	TEST_ASSERT_EQ(n_normal, n_huge);
	/* Stepping through the hole costs four million lookups: thousands
	 * of times the time of /normal. Allow a factor of 50, and 50 ms for
	 * noise on a slow machine. */
	TEST_ASSERT(huge < normal * 50 + 0.05);
	return 0;
}
