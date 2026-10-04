/* SPDX-License-Identifier: BSD-3-Clause */
/* libFuzzer target: mount a (damaged) image read-write, run a short script
 * of file system operations on it, unmount, then mount the result read only
 * and read everything back.
 *
 * Input layout, so that mutating the script does not shift the image:
 *
 *   [ image ... ][ script: n bytes ][ n: 2 bytes, little endian ]
 *
 * The script is a sequence of operations, each an opcode byte followed by
 * argument bytes (see run_op). Running past its end reads zeros. */
#include "fuzz_ops.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t slen;

	if (size < 2)
		return 0;
	slen = data[size - 2] | data[size - 1] << 8;
	if (slen > MAX_SCRIPT || slen > size - 2)
		return 0;
	if (!ram_load(data, size - 2 - slen))
		return 0;

	if (ext4_device_register(&ram, "fz") == EOK) {
		if (ext4_mount("fz", "/fz/", false) == EOK) {
			ext4_recover("/fz/");
			ext4_journal_start("/fz/");
			sp = data + size - 2 - slen;
			se = sp + slen;
			for (int i = 0; i < MAX_OPS && sp < se; i++)
				run_op();
			ext4_cache_write_back("/fz/", false);
			ext4_journal_stop("/fz/");
			ext4_umount("/fz/");
		}
		/* What was written must be readable again. */
		if (ext4_mount("fz", "/fz/", true) == EOK) {
			budget = 3000;
			walk("/fz/", 0);
			ext4_umount("/fz/");
		}
		ext4_device_unregister("fz");
	}
	ram_unload();
	return 0;
}
