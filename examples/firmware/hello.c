/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * hello: the smallest complete use of lwext4 on a board. Format the disk
 * as ext4, mount it, write /hello.txt through the journal, read it back,
 * unmount. Start here.
 */
#include <platform.h>

#include <ext4.h>
#include <ext4_mkfs.h>

#include <stdio.h>
#include <string.h>

static const char text[] = "Hello from lwext4\n";

static int fail(const char *what, int r)
{
	printf("hello: %s failed: %d\n", what, r);
	return 1;
}

int main(void)
{
	/* ext4_mkfs needs a struct ext4_fs: large, so not on the stack */
	static struct ext4_fs fs;
	struct ext4_mkfs_info info = {.block_size = 1024, .journal = true};
	struct ext4_blockdev *bd;
	char buf[sizeof(text)];
	ext4_file f;
	size_t n;
	int r;

	platform_init();
	bd = platform_disk();
	if (!bd)
		return fail("platform_disk", -1);

	r = ext4_mkfs(&fs, bd, &info, F_SET_EXT4);
	if (r != EOK)
		return fail("ext4_mkfs", r);
	r = ext4_device_register(bd, "disk");
	if (r != EOK)
		return fail("ext4_device_register", r);
	r = ext4_mount("disk", "/mp/", false);
	if (r != EOK)
		return fail("ext4_mount", r);
	ext4_recover("/mp/");
	ext4_journal_start("/mp/");

	r = ext4_fopen(&f, "/mp/hello.txt", "wb");
	if (r == EOK) {
		r = ext4_fwrite(&f, text, strlen(text), &n);
		ext4_fclose(&f);
	}
	if (r != EOK)
		return fail("writing /hello.txt", r);

	r = ext4_fopen(&f, "/mp/hello.txt", "rb");
	if (r == EOK) {
		r = ext4_fread(&f, buf, sizeof(buf), &n);
		ext4_fclose(&f);
	}
	if (r != EOK || n != strlen(text) || memcmp(buf, text, n))
		return fail("reading /hello.txt back", r);

	ext4_journal_stop("/mp/");
	r = ext4_umount("/mp/");
	if (r != EOK)
		return fail("ext4_umount", r);
	ext4_device_unregister("disk");
	printf("hello: wrote and read back /hello.txt\n");
	return 0;
}
