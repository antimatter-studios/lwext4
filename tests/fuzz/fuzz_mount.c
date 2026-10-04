/* SPDX-License-Identifier: BSD-3-Clause */
/* libFuzzer target: the input is a disk image; mount it read only and read
 * everything (directories, files, symlinks, xattr lists). */
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (!ram_load(data, size))
		return 0;
	budget = 3000;

	if (ext4_device_register(&ram, "fz") == EOK) {
		if (ext4_mount("fz", "/fz/", true) == EOK) {
			walk("/fz/", 0);
			ext4_umount("/fz/");
		}
		ext4_device_unregister("fz");
	}
	ram_unload();
	return 0;
}
