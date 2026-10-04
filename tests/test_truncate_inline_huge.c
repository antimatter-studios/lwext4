/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Truncating an inline file with a damaged huge size (fork issue #162,
 * found by fuzz_rw). The truncate steps of CONFIG_MAX_TRUNCATE_SIZE ran
 * from the size down, about 2^64 / 16 MiB of them, although inline data
 * fits in the i-node. The size is bounded by the i-node first now.
 *
 * /a and /b (see the .sh): truncating /a to 0 and removing /b must finish
 * (an alarm ends the test otherwise), and e2fsck must find the filesystem
 * clean.
 */

#include "test_util.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	char cmd[1024];
	ext4_file f;

	alarm(20);
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "a", "r+b"));
	TEST_ASSERT_EQ(EOK, ext4_ftruncate(&f, 0));
	TEST_ASSERT_EQ((uint64_t)0, ext4_fsize(&f));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_fremove(TEST_MP "b"));
	test_umount();

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	TEST_ASSERT_EQ(0, system(cmd));
	return 0;
}
