/* SPDX-License-Identifier: BSD-3-Clause */
/* libFuzzer target: the input is a disk; scan its partition tables (MBR
 * with logical partitions, GPT, and the old 4 partition MBR scan), then
 * mount the first partition with a size read only and read it a little,
 * so that filesystems inside partitions are fuzzed too. */
#include "fuzz_common.h"

#include <ext4_mbr.h>
#include <ext4_partition.h>

static struct ext4_part_bdevs parts;
static struct ext4_mbr_bdevs mbr;

static void try_mount(struct ext4_blockdev *part)
{
	if (!part->part_size || !part->bdif)
		return;
	if (ext4_device_register(part, "fzp") != EOK)
		return;
	if (ext4_mount("fzp", "/fzp/", true) == EOK) {
		budget = 200;
		walk("/fzp/", 0);
		ext4_umount("/fzp/");
	}
	ext4_device_unregister("fzp");
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (!ram_load(data, size))
		return 0;

	if (ext4_partition_scan(&ram, &parts) == EOK && parts.count)
		try_mount(&parts.partitions[0]);
	ext4_mbr_scan_all(&ram, &parts);
	ext4_gpt_scan(&ram, &parts);
	ext4_mbr_scan(&ram, &mbr);

	ram_unload();
	return 0;
}
