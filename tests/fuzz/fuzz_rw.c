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
#include "fuzz_common.h"

#define MAX_OPS 64
#define MAX_SCRIPT 1024

static const uint8_t *sp, *se;

static unsigned arg8(void) { return sp < se ? *sp++ : 0; }
static unsigned arg16(void) { unsigned v = arg8(); return v | arg8() << 8; }

/* Paths: names that exist in the seed images (tests/fuzz/make-seeds.sh)
 * and new ones, in the root, a directory and a subdirectory. */
static const char *const files[] = {
	"/fz/a.txt", "/fz/dir/b.bin", "/fz/sl", "/fz/dir/long",
	"/fz/dir/sub/f_with_a_longer_name_1", "/fz/n0", "/fz/n1",
	"/fz/dir/n2", "/fz/dir/sub/n3", "/fz/d0/n4", "/fz/d0/d1/n5",
	"/fz/dir/sub/f_with_a_longer_name_7",
};
static const char *const dirs[] = {
	"/fz/dir", "/fz/dir/sub", "/fz/d0", "/fz/d0/d1", "/fz/dir/d2",
	"/fz/lost+found",
};
static const char *const xnames[] = {
	"user.a", "user.longer_attribute_name", "trusted.t", "security.s",
	"system.posix_acl_access", "user.",
};
#define N(a) (sizeof(a) / sizeof((a)[0]))

static const char *file(void) { return files[arg8() % N(files)]; }
static const char *dir(void) { return dirs[arg8() % N(dirs)]; }

static uint8_t wbuf[65536];
static uint8_t rbuf[16384];

static void write_at(const char *path, const char *mode, uint32_t off,
		     size_t len)
{
	ext4_file f;
	size_t n;

	if (ext4_fopen(&f, path, mode) != EOK)
		return;
	if (off)
		ext4_fseek(&f, off, SEEK_SET);
	ext4_fwrite(&f, wbuf, len, &n);
	ext4_fclose(&f);
}

static void run_op(void)
{
	static const char *const modes[] = {"wb", "ab", "r+b", "w+b"};
	char path[128];
	size_t n;

	switch (arg8() % 16) {
	case 0: { /* create or append, up to 16 KiB */
		const char *p = file();
		const char *m = modes[arg8() % N(modes)];
		write_at(p, m, 0, (arg8() * 64) % sizeof(wbuf));
		break;
	}
	case 1: { /* write anywhere: holes, extents, block maps */
		const char *p = file();
		uint32_t off = arg16() * 512u;
		write_at(p, "r+b", off, arg16() % sizeof(wbuf));
		break;
	}
	case 2: { /* truncate, shrink or grow */
		ext4_file f;
		const char *p = file();
		uint32_t len = arg16() * 64u;
		if (ext4_fopen(&f, p, "r+b") == EOK) {
			ext4_ftruncate(&f, len);
			ext4_fclose(&f);
		}
		break;
	}
	case 3:
		ext4_fremove(file());
		break;
	case 4:
		ext4_dir_mk(dir());
		break;
	case 5:
		ext4_dir_rm(dir());
		break;
	case 6: { /* rename files, or directories */
		if (arg8() & 1) {
			const char *a = file();
			ext4_frename(a, file());
		} else {
			const char *a = dir();
			ext4_dir_mv(a, dir());
		}
		break;
	}
	case 7: { /* set an xattr, value up to 2 KiB (in inode or block) */
		const char *p = arg8() & 1 ? file() : dir();
		const char *x = xnames[arg8() % N(xnames)];
		ext4_setxattr(p, x, strlen(x), wbuf, (arg8() * 8) % 2048);
		break;
	}
	case 8: {
		const char *p = arg8() & 1 ? file() : dir();
		const char *x = xnames[arg8() % N(xnames)];
		ext4_removexattr(p, x, strlen(x));
		break;
	}
	case 9: { /* fast (< 60 bytes) or slow symlink */
		const char *p = file();
		size_t len = arg8() + 1;
		memset(path, 'x', sizeof(path));
		path[len < sizeof(path) ? len : sizeof(path) - 1] = '\0';
		ext4_fsymlink(path, p);
		break;
	}
	case 10: {
		const char *a = file();
		ext4_flink(a, file());
		break;
	}
	case 11: { /* read a file through */
		ext4_file f;
		if (ext4_fopen(&f, file(), "rb") == EOK) {
			for (int i = 0; i < 8; i++)
				if (ext4_fread(&f, rbuf, sizeof(rbuf), &n) !=
					EOK || !n)
					break;
			ext4_fclose(&f);
		}
		break;
	}
	case 12: { /* many entries: linear directory to htree, splits */
		const char *d = dir();
		unsigned count = arg8() % 96;
		unsigned base = arg8();
		for (unsigned i = 0; i < count; i++) {
			snprintf(path, sizeof(path),
				 "%s/entry_with_a_long_name_%u", d, base + i);
			write_at(path, "wb", 0, 0);
		}
		break;
	}
	case 13:
		ext4_cache_write_back("/fz/", arg8() & 1);
		break;
	case 14: { /* inode metadata */
		const char *p = file();
		ext4_mode_set(p, arg16());
		ext4_owner_set(p, arg16(), arg16());
		ext4_mtime_set(p, arg16() << 16);
		break;
	}
	case 15:
		budget = 200;
		walk("/fz/", 0);
		break;
	}
}

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
