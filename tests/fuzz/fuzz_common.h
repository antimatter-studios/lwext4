/* SPDX-License-Identifier: BSD-3-Clause */
/* Shared parts of the libFuzzer targets: a RAM block device over a copy of
 * the input image, and a bounded walk that reads everything below a
 * directory. */
#ifndef FUZZ_COMMON_H_
#define FUZZ_COMMON_H_

#include <ext4.h>
#include <ext4_blockdev.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BSIZE 512

static uint8_t *img;
static size_t img_len;
static int budget;

/* Injected I/O errors (fuzz_rwx): the read or write number fault_nth
 * fails with EIO, and with fault_sticky every one after it too, as a
 * device that broke. fault_nth 0: no errors. */
#define FAULT_READ 1
#define FAULT_WRITE 2
static unsigned fault_ops, fault_nth, fault_sticky;
static unsigned fault_reads, fault_writes;

static int fault_hit(unsigned op, unsigned *count)
{
	if (!(fault_ops & op) || !fault_nth)
		return 0;
	++*count;
	return *count == fault_nth || (fault_sticky && *count > fault_nth);
}

static int ram_open(struct ext4_blockdev *b) { (void)b; return EOK; }
static int ram_close(struct ext4_blockdev *b) { (void)b; return EOK; }
static int ram_bread(struct ext4_blockdev *b, void *buf, uint64_t id,
		     uint32_t cnt)
{
	uint64_t off = id * BSIZE, len = (uint64_t)cnt * BSIZE;
	(void)b;
	if (off > img_len || len > img_len - off)
		return EIO;
	if (fault_hit(FAULT_READ, &fault_reads))
		return EIO;
	memcpy(buf, img + off, len);
	return EOK;
}
static int ram_bwrite(struct ext4_blockdev *b, const void *buf, uint64_t id,
		      uint32_t cnt)
{
	uint64_t off = id * BSIZE, len = (uint64_t)cnt * BSIZE;
	(void)b;
	if (off > img_len || len > img_len - off)
		return EIO;
	if (fault_hit(FAULT_WRITE, &fault_writes))
		return EIO;
	memcpy(img + off, buf, len);
	return EOK;
}

EXT4_BLOCKDEV_STATIC_INSTANCE(ram, BSIZE, 0, ram_open, ram_bread, ram_bwrite,
			      ram_close, 0, 0);

/* Point the block device at a private copy of <data>, whole sectors only.
 * Returns 0 if the image is too small to be worth mounting. */
static int ram_load(const uint8_t *data, size_t size)
{
	if (size < 4 * BSIZE)
		return 0;
	img_len = size / BSIZE * BSIZE;
	img = malloc(img_len);
	if (!img)
		return 0;
	memcpy(img, data, img_len);
	ram.bdif->ph_bcnt = img_len / BSIZE;
	ram.part_offset = 0;
	ram.part_size = img_len;
	return 1;
}

/* At the end of an input nothing may stay mounted: a mount point left
 * behind (an ext4_umount() that failed while the device failed) would
 * carry this input's state into the next one, whose crash would then not
 * reproduce alone. The device works again here, so unmounting must
 * succeed; if it does not, stop on this input. */
static void unmount_all(void)
{
	static const char *const mps[] = {"/fz/", "/fzp/"};
	struct ext4_mount_stats st;

	for (size_t i = 0; i < sizeof(mps) / sizeof(mps[0]); i++)
		if (ext4_mount_point_stats(mps[i], &st) == EOK &&
		    ext4_umount(mps[i]) != EOK)
			__builtin_trap();
	ext4_device_unregister_all();
}

static void ram_unload(void)
{
	unmount_all();
	free(img);
	img = NULL;
	img_len = 0;
}

/* Read every directory, file, symlink and xattr list below <dir>, at most
 * <budget> entries in total. */
static void walk(const char *dir, int depth)
{
	static char buf[4096];
	ext4_dir d;
	const ext4_direntry *de;

	if (depth > 6 || ext4_dir_open(&d, dir) != EOK)
		return;
	while ((de = ext4_dir_entry_next(&d)) != NULL && budget-- > 0) {
		char path[512];
		size_t n;

		if (!de->name_length ||
		    (de->name_length == 1 && de->name[0] == '.') ||
		    (de->name_length == 2 && !memcmp(de->name, "..", 2)))
			continue;
		snprintf(path, sizeof(path), "%s%.*s", dir, de->name_length,
			 de->name);
		if (de->inode_type == EXT4_DE_DIR) {
			strncat(path, "/", sizeof(path) - strlen(path) - 1);
			walk(path, depth + 1);
		} else if (de->inode_type == EXT4_DE_REG_FILE) {
			ext4_file f;
			if (ext4_fopen(&f, path, "rb") == EOK) {
				for (int i = 0; i < 16; i++)
					if (ext4_fread(&f, buf, sizeof(buf),
						       &n) != EOK || !n)
						break;
				ext4_fclose(&f);
			}
		} else if (de->inode_type == EXT4_DE_SYMLINK) {
			ext4_readlink(path, buf, sizeof(buf), &n);
		}
		ext4_listxattr(path, buf, sizeof(buf), &n);
	}
	ext4_dir_close(&d);
}

#endif
