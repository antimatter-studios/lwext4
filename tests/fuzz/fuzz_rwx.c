/* SPDX-License-Identifier: BSD-3-Clause */
/* libFuzzer target: fuzz_rw with more operations and injected I/O errors.
 *
 * Input layout:
 *
 *   [ image ... ][ script: n bytes ][ fault: 4 bytes ][ n: 2 bytes, LE ]
 *
 * fault: byte 0 selects the operations that fail (bit 0 reads, bit 1
 * writes, bit 2 sticky: every one from then on), bytes 1-3 the number of
 * the first failing one (little endian, 0: no errors). The script is
 * decoded like fuzz_rw's, with operations 16 to 31 added: remounting in
 * the middle, renames across types, readlink at offsets, xattr reads,
 * large writes, links to directories, growing truncates, cache flushes and
 * listings with a rewind. */
#include "fuzz_ops.h"

static char big[65536];

static void run_op_ext(void)
{
	unsigned op = arg8() % 32;
	char buf[512];
	size_t n;

	if (op < 16) {
		run_op_code(op);
		return;
	}

	switch (op) {
	case 16: /* remount, also replaying the journal */
		ext4_cache_write_back("/fz/", false);
		ext4_journal_stop("/fz/");
		ext4_umount("/fz/");
		if (ext4_mount("fz", "/fz/", false) == EOK) {
			ext4_recover("/fz/");
			ext4_journal_start("/fz/");
		}
		break;
	case 17: { /* rename across types: file to a directory's name */
		const char *a = file();
		ext4_frename(a, dir());
		break;
	}
	case 18: { /* readlink at an offset, and the length query */
		const char *p = file();
		uint64_t off = arg8();
		ext4_readlink_at(p, off, buf, arg8() % sizeof(buf), &n);
		ext4_readlink(p, NULL, 0, &n);
		break;
	}
	case 19: { /* xattr reads */
		const char *p = arg8() & 1 ? file() : dir();
		const char *x = xnames[arg8() % N(xnames)];
		ext4_getxattr(p, x, strlen(x), buf, arg8() * 2 % sizeof(buf),
			      &n);
		ext4_listxattr(p, buf, arg8() * 2 % sizeof(buf), &n);
		break;
	}
	case 20: { /* a large write anywhere: many blocks, extent splits */
		const char *p = file();
		uint32_t off = arg16() * 1024u;
		size_t len = (arg16() % 64 + 1) * 1024u;
		ext4_file f;
		if (ext4_fopen(&f, p, arg8() & 1 ? "r+b" : "wb") == EOK) {
			ext4_fseek(&f, off, SEEK_SET);
			ext4_fwrite(&f, big, len, &n);
			ext4_fclose(&f);
		}
		break;
	}
	case 21: { /* hard link to a directory (refused) or across dirs */
		const char *a = arg8() & 1 ? dir() : file();
		ext4_flink(a, file());
		break;
	}
	case 22: { /* grow by truncate, then shrink */
		ext4_file f;
		if (ext4_fopen(&f, file(), "r+b") == EOK) {
			ext4_ftruncate(&f, (uint64_t)arg16() * 4096u);
			ext4_ftruncate(&f, (uint64_t)arg16() * 16u);
			ext4_fclose(&f);
		}
		break;
	}
	case 23: { /* flush, stats, list with a rewind */
		struct ext4_mount_stats st;
		const ext4_direntry *de;
		ext4_dir d;
		ext4_cache_flush("/fz/");
		ext4_mount_point_stats("/fz/", &st);
		if (ext4_dir_open(&d, dir()) == EOK) {
			int k = arg8() % 8;
			while (k-- > 0 && ext4_dir_entry_get(&d, &de) == EOK &&
			       de)
				;
			ext4_dir_entry_rewind(&d);
			while (ext4_dir_entry_get(&d, &de) == EOK && de)
				;
			ext4_dir_close(&d);
		}
		break;
	}
	case 24: /* remove a tree */
		ext4_dir_rm(dir());
		break;
	case 25: { /* mknod and symlink into a directory, then remove */
		const char *p = file();
		ext4_mknod(p, EXT4_DE_FIFO, 0);
		ext4_fremove(p);
		break;
	}
	case 26: /* the journal stopped and started again */
		ext4_journal_stop("/fz/");
		ext4_journal_start("/fz/");
		break;
	case 27: { /* read past the end, seek around */
		ext4_file f;
		if (ext4_fopen(&f, file(), "rb") == EOK) {
			ext4_fseek(&f, (int64_t)arg16() * 512, SEEK_SET);
			ext4_fread(&f, buf, sizeof(buf), &n);
			ext4_fseek(&f, -(int64_t)arg8(), SEEK_END);
			ext4_fread(&f, buf, sizeof(buf), &n);
			ext4_fclose(&f);
		}
		break;
	}
	default: /* 28-31: the operations of fuzz_rw, more often */
		run_op_code(arg8() % 16);
		break;
	}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	size_t slen;
	const uint8_t *fault;

	if (size < 6)
		return 0;
	slen = data[size - 2] | data[size - 1] << 8;
	if (slen > MAX_SCRIPT || slen > size - 6)
		return 0;
	if (!ram_load(data, size - 6 - slen))
		return 0;

	fault = data + size - 6;
	fault_ops = fault[0] & 3;
	fault_sticky = fault[0] & 4;
	fault_nth = fault[1] | fault[2] << 8 | (unsigned)fault[3] << 16;
	fault_reads = fault_writes = 0;
	memset(big, 'B', sizeof(big));

	if (ext4_device_register(&ram, "fz") == EOK) {
		if (ext4_mount("fz", "/fz/", false) == EOK) {
			ext4_recover("/fz/");
			ext4_journal_start("/fz/");
			sp = data + size - 6 - slen;
			se = sp + slen;
			for (int i = 0; i < MAX_OPS && sp < se; i++)
				run_op_ext();
			ext4_cache_write_back("/fz/", false);
			ext4_journal_stop("/fz/");
			ext4_umount("/fz/");
		}
		/* The device works again: what was written must be readable */
		fault_nth = 0;
		if (ext4_mount("fz", "/fz/", true) == EOK) {
			budget = 3000;
			walk("/fz/", 0);
			ext4_umount("/fz/");
		}
		ext4_device_unregister("fz");
	}
	fault_nth = 0;
	ram_unload();
	return 0;
}
