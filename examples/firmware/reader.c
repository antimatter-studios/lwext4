/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * reader: read files from a card made on a PC (mke2fs -d, or any Linux
 * machine), never writing to it. Mount read-only, read /config.txt
 * (key=value lines), and list /assets with the size and a checksum of
 * each file, as firmware loading its settings and resources does.
 */
#include <platform.h>

#include <ext4.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MP "/mp/"

static int fail(const char *what, int r)
{
	printf("reader: %s failed: %d\n", what, r);
	return 1;
}

static int read_config(void)
{
	char line[128];
	size_t len = 0, got;
	ext4_file f;
	char c;
	int r;

	r = ext4_fopen(&f, MP "config.txt", "rb");
	if (r != EOK)
		return fail("opening /config.txt", r);
	/* One character at a time: the file is small and lwext4 has its own
	 * block cache, so this costs no extra disk reads */
	while ((r = ext4_fread(&f, &c, 1, &got)) == EOK && got == 1) {
		if (c != '\n' && len < sizeof(line) - 1) {
			line[len++] = c;
			continue;
		}
		line[len] = 0;
		if (strchr(line, '='))
			printf("config: %s\n", line);
		len = 0;
	}
	ext4_fclose(&f);
	return r == EOK ? 0 : fail("reading /config.txt", r);
}

/* FNV-1a of a file's content */
static int checksum(const char *path, uint32_t *hash, uint64_t *size)
{
	uint8_t buf[256];
	ext4_file f;
	size_t got, i;
	int r;

	*hash = 2166136261u;
	*size = 0;
	r = ext4_fopen(&f, path, "rb");
	if (r != EOK)
		return r;
	while ((r = ext4_fread(&f, buf, sizeof(buf), &got)) == EOK && got) {
		for (i = 0; i < got; i++)
			*hash = (*hash ^ buf[i]) * 16777619u;
		*size += got;
	}
	ext4_fclose(&f);
	return r;
}

static int list_assets(void)
{
	const ext4_direntry *de;
	char path[64 + 255];
	ext4_dir d;
	int r;

	r = ext4_dir_open(&d, MP "assets");
	if (r != EOK)
		return fail("opening /assets", r);
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		uint32_t hash;
		uint64_t size;

		if (de->inode_type != EXT4_DE_REG_FILE)
			continue;
		snprintf(path, sizeof(path), MP "assets/%.*s",
			 (int)de->name_length, (const char *)de->name);
		r = checksum(path, &hash, &size);
		if (r != EOK)
			break;
		printf("asset: %.*s %lu %08lx\n", (int)de->name_length,
		       (const char *)de->name, (unsigned long)size,
		       (unsigned long)hash);
	}
	ext4_dir_close(&d);
	return r == EOK ? 0 : fail("reading /assets", r);
}

int main(void)
{
	int r;

	platform_init();
	r = ext4_device_register(platform_disk(), "disk");
	if (r != EOK)
		return fail("ext4_device_register", r);
	r = ext4_mount("disk", MP, true);
	if (r != EOK)
		return fail("ext4_mount", r);
	if (read_config() || list_assets())
		return 1;
	r = ext4_umount(MP);
	if (r != EOK)
		return fail("ext4_umount", r);
	ext4_device_unregister("disk");
	printf("reader: done\n");
	return 0;
}
