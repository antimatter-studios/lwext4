/* SPDX-License-Identifier: BSD-3-Clause */
/* libFuzzer target: ext4_mkfs with parameters from the input, on a zeroed
 * RAM disk, then mount the result read-write, write to it, and read it all
 * back read only, and ext4_mkfs_read_info.
 *
 * Input: byte 0 size (64 KiB << 0..7), 1 block size (1 KiB << 0..6),
 * 2 type (ext2/3/4), 3 flags (journal, dsc_size 64, explicit inode count),
 * 4-5 inode size, 6-7 inodes per group, 8-9 blocks per group, 10-11
 * journal blocks, 12-15 extra incompatible features, 16-19 read-only
 * compatible, 20-23 compatible, 24- the label. Short inputs read zeros. */
#include "fuzz_common.h"

#include <ext4_mkfs.h>

static struct ext4_fs fs;

static uint32_t get(const uint8_t *d, size_t size, size_t at, int bytes)
{
	uint32_t v = 0;

	for (int i = 0; i < bytes; i++)
		v |= (uint32_t)(at + i < size ? d[at + i] : 0) << (8 * i);
	return v;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static const int types[] = {F_SET_EXT2, F_SET_EXT3, F_SET_EXT4};
	struct ext4_mkfs_info info, back;
	char label[17];
	uint32_t flags = get(data, size, 3, 1);
	size_t n;

	memset(&info, 0, sizeof(info));
	img_len = (size_t)64 * 1024 << (get(data, size, 0, 1) % 8);
	img = calloc(1, img_len);
	if (!img)
		return 0;
	ram.bdif->ph_bcnt = img_len / BSIZE;
	ram.part_offset = 0;
	ram.part_size = img_len;

	info.len = img_len;
	info.block_size = 1024u << (get(data, size, 1, 1) % 7);
	info.journal = flags & 1;
	info.dsc_size = flags & 2 ? 64 : 0;
	info.inode_size = get(data, size, 4, 2);
	info.inodes_per_group = get(data, size, 6, 2);
	info.blocks_per_group = get(data, size, 8, 2);
	if (flags & 4)
		info.inodes = get(data, size, 6, 2) * 4;
	info.journal_blocks = get(data, size, 10, 2);
	info.feat_incompat = get(data, size, 12, 4);
	info.feat_ro_compat = get(data, size, 16, 4);
	info.feat_compat = get(data, size, 20, 4);
	n = size > 24 ? size - 24 : 0;
	if (n > 16)
		n = 16;
	if (n)
		memcpy(label, data + 24, n);
	label[n] = '\0';
	info.label = label;

	if (ext4_mkfs(&fs, &ram, &info,
		      types[get(data, size, 2, 1) % 3]) == EOK &&
	    ext4_device_register(&ram, "fz") == EOK) {
		if (ext4_mount("fz", "/fz/", false) == EOK) {
			ext4_file f;
			size_t w;

			ext4_recover("/fz/");
			ext4_journal_start("/fz/");
			ext4_dir_mk("/fz/d");
			if (ext4_fopen(&f, "/fz/d/f", "wb") == EOK) {
				ext4_fwrite(&f, img, 5000, &w);
				ext4_fclose(&f);
			}
			ext4_setxattr("/fz/d", "user.x", 6, "v", 1);
			ext4_journal_stop("/fz/");
			ext4_umount("/fz/");
		}
		if (ext4_mount("fz", "/fz/", true) == EOK) {
			budget = 100;
			walk("/fz/", 0);
			ext4_umount("/fz/");
		}
		ext4_device_unregister("fz");
		ext4_mkfs_read_info(&ram, &back);
	}
	ram_unload();
	return 0;
}
