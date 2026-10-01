/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * File system workload, see workload.h.
 */
#include "workload.h"
#include "sysmem.h"

#include <ext4.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef BIG_FILE_KIB
#define BIG_FILE_KIB 256
#endif

#define CHECK(expr)                                                            \
	do {                                                                   \
		int r_ = (expr);                                               \
		if (r_ != EOK)                                                 \
			test_fail(__FILE__, __LINE__, #expr, r_);              \
	} while (0)

#define ASSERT(cond)                                                           \
	do {                                                                   \
		if (!(cond))                                                   \
			test_fail(__FILE__, __LINE__, #cond, 0);               \
	} while (0)

static uint8_t io_buf[1024];
static char path[128];
static char path2[128];

uint8_t pattern_byte(uint32_t seed, uint32_t off)
{
	uint32_t x = off + seed * 0x9e3779b9u;
	x ^= x >> 15;
	x *= 0x2c1b3c6du;
	x ^= x >> 12;
	return (uint8_t)x;
}

uint32_t small_file_size(uint32_t i)
{
	return 1u + (i * 997u) % 5000u;
}

uint32_t big_file_size(void)
{
	return BIG_FILE_KIB * 1024u + 123u;
}

static const char *mkpath(char *buf, const char *mp, const char *fmt,
			  unsigned arg)
{
	size_t n = strlen(mp);
	memcpy(buf, mp, n);
	snprintf(buf + n, 128 - n, fmt, arg);
	return buf;
}

static void fill(uint8_t *buf, uint32_t seed, uint32_t off, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++)
		buf[i] = pattern_byte(seed, off + i);
}

/* Writes len pattern bytes at the current position in chunks of chunk. */
static void write_pattern(ext4_file *f, uint32_t seed, uint32_t off,
			  uint32_t len, uint32_t chunk)
{
	size_t n;

	if (chunk > sizeof(io_buf))
		chunk = sizeof(io_buf);
	while (len) {
		uint32_t l = len < chunk ? len : chunk;
		fill(io_buf, seed, off, l);
		CHECK(ext4_fwrite(f, io_buf, l, &n));
		ASSERT(n == l);
		off += l;
		len -= l;
	}
}

static void verify_pattern(ext4_file *f, uint32_t seed, uint32_t off,
			   uint32_t len, uint32_t chunk, const char *name)
{
	size_t n;

	if (chunk > sizeof(io_buf))
		chunk = sizeof(io_buf);
	while (len) {
		uint32_t l = len < chunk ? len : chunk;
		CHECK(ext4_fread(f, io_buf, l, &n));
		ASSERT(n == l);
		for (uint32_t i = 0; i < l; i++) {
			if (io_buf[i] != pattern_byte(seed, off + i)) {
				printf("%s: mismatch at offset %lu\n", name,
				       (unsigned long)(off + i));
				test_fail(__FILE__, __LINE__, "data mismatch",
					  0);
			}
		}
		off += l;
		len -= l;
	}
}

static void create_file(const char *p, uint32_t seed, uint32_t size,
			uint32_t chunk)
{
	ext4_file f;

	CHECK(ext4_fopen(&f, p, "wb"));
	write_pattern(&f, seed, 0, size, chunk);
	ASSERT(ext4_fsize(&f) == size);
	CHECK(ext4_fclose(&f));
}

static void verify_file(const char *p, uint32_t seed, uint32_t size,
			uint32_t chunk)
{
	ext4_file f;

	CHECK(ext4_fopen(&f, p, "rb"));
	if (ext4_fsize(&f) != size) {
		printf("%s: size %lu, expected %lu\n", p,
		       (unsigned long)ext4_fsize(&f), (unsigned long)size);
		test_fail(__FILE__, __LINE__, "size mismatch", 0);
	}
	verify_pattern(&f, seed, 0, size, chunk, p);
	CHECK(ext4_fclose(&f));
}

static int count_entries(const char *p)
{
	ext4_dir d;
	const ext4_direntry *de;
	int n = 0;

	CHECK(ext4_dir_open(&d, p));
	while ((de = ext4_dir_entry_next(&d)) != NULL)
		n++;
	CHECK(ext4_dir_close(&d));
	return n;
}

static bool exists(const char *p, int type)
{
	return ext4_inode_exist(p, type) == EOK;
}

/* ------------------------------------------------------------------------ */
/* Files created on the host                                                 */

static const char host_hello[] = "hello from mke2fs -d\n";
static const char host_append[] = "lwext4 was here\n";

static void verify_text(const char *p, const char *a, const char *b)
{
	ext4_file f;
	size_t n, la = strlen(a), lb = b ? strlen(b) : 0;

	CHECK(ext4_fopen(&f, p, "rb"));
	ASSERT(ext4_fsize(&f) == la + lb);
	CHECK(ext4_fread(&f, io_buf, la + lb, &n));
	ASSERT(n == la + lb);
	ASSERT(!memcmp(io_buf, a, la));
	ASSERT(!b || !memcmp(io_buf + la, b, lb));
	CHECK(ext4_fclose(&f));
}

void workload_check_host_files(const char *mp)
{
	verify_text(mkpath(path, mp, "host/hello.txt", 0), host_hello, NULL);
	verify_file(mkpath(path, mp, "host/pattern.bin", 0), HOST_PATTERN_SEED,
		    HOST_PATTERN_SIZE, 999);
	ASSERT(exists(mkpath(path, mp, "host/deep/a/b/c/d/e/leaf.txt", 0),
		      EXT4_DE_REG_FILE));
	/* e2fsck -D turned this into an htree (dir_index) directory */
	ASSERT(count_entries(mkpath(path, mp, "host/many", 0)) ==
	       (int)HOST_MANY_FILES + 2);
	for (unsigned i = 0; i < HOST_MANY_FILES; i += 37)
		ASSERT(exists(mkpath(path, mp, "host/many/f_%04u", i),
			      EXT4_DE_REG_FILE));
}

void workload_modify_host_files(const char *mp)
{
	ext4_file f;
	size_t n;

	CHECK(ext4_fopen(&f, mkpath(path, mp, "host/hello.txt", 0), "ab"));
	CHECK(ext4_fwrite(&f, host_append, strlen(host_append), &n));
	CHECK(ext4_fclose(&f));

	CHECK(ext4_fremove(mkpath(path, mp, "host/many/f_%04u", 100)));

	/* overwrite a range inside the existing file */
	CHECK(ext4_fopen(&f, mkpath(path, mp, "host/pattern.bin", 0), "r+b"));
	CHECK(ext4_fseek(&f, 50000, SEEK_SET));
	write_pattern(&f, 99, 50000, 1000, 1000);
	CHECK(ext4_fclose(&f));
}

void workload_verify_host_files_modified(const char *mp)
{
	ext4_file f;

	verify_text(mkpath(path, mp, "host/hello.txt", 0), host_hello,
		    host_append);
	ASSERT(!exists(mkpath(path, mp, "host/many/f_%04u", 100),
		       EXT4_DE_REG_FILE));
	ASSERT(count_entries(mkpath(path, mp, "host/many", 0)) ==
	       (int)HOST_MANY_FILES + 1);
	CHECK(ext4_fopen(&f, mkpath(path, mp, "host/pattern.bin", 0), "rb"));
	verify_pattern(&f, HOST_PATTERN_SEED, 0, 50000, 1024, "pattern.bin");
	verify_pattern(&f, 99, 50000, 1000, 1000, "pattern.bin");
	verify_pattern(&f, HOST_PATTERN_SEED, 51000, HOST_PATTERN_SIZE - 51000,
		       1024, "pattern.bin");
	CHECK(ext4_fclose(&f));
}

/* ------------------------------------------------------------------------ */
/* Workload                                                                  */

#define SMALL_NAME "fw/small/file_%03u_with_a_long_name_to_fill_dir_blocks.bin"

void workload_run(const char *mp)
{
	ext4_file f;
	size_t n;

	CHECK(ext4_dir_mk(mkpath(path, mp, "fw", 0)));
	for (unsigned i = 0; i < WL_SUBDIRS; i++)
		CHECK(ext4_dir_mk(mkpath(path, mp, "fw/sub%02u", i)));
	CHECK(ext4_dir_mk(mkpath(path, mp, "fw/small", 0)));

	printf("  %u small files\n", WL_SMALL_FILES);
	for (unsigned i = 0; i < WL_SMALL_FILES; i++)
		create_file(mkpath(path, mp, SMALL_NAME, i), i + 1,
			    small_file_size(i), 1024);

	sysmem_phase("small files");
	printf("  big file, %lu bytes\n", (unsigned long)big_file_size());
	create_file(mkpath(path, mp, "fw/big.bin", 0), WL_BIG_SEED,
		    big_file_size(), WL_BIG_CHUNK);
	verify_file(path, WL_BIG_SEED, big_file_size(), 777);

	/* seek + short reads at odd offsets */
	CHECK(ext4_fopen(&f, path, "rb"));
	for (uint32_t off = 3; off < big_file_size(); off += 40961) {
		CHECK(ext4_fseek(&f, off, SEEK_SET));
		verify_pattern(&f, WL_BIG_SEED, off, 17, 17, "big.bin");
	}
	CHECK(ext4_fclose(&f));

	sysmem_phase("big file");
	printf("  truncate\n");
	mkpath(path, mp, "fw/trunc.bin", 0);
	create_file(path, 7, 20000, 1024);
	CHECK(ext4_fopen(&f, path, "r+b"));
	CHECK(ext4_ftruncate(&f, 5000));
	ASSERT(ext4_fsize(&f) == 5000);
	CHECK(ext4_fseek(&f, 0, SEEK_END));
	write_pattern(&f, 8, 5000, 3000, 1024);
	CHECK(ext4_fclose(&f));

	printf("  rename, remove, symlink, xattr\n");
	CHECK(ext4_frename(mkpath(path, mp, SMALL_NAME, 1),
			   mkpath(path2, mp, "fw/renamed.bin", 0)));
	for (unsigned i = 0; i < WL_SMALL_FILES; i += 3)
		CHECK(ext4_fremove(mkpath(path, mp, SMALL_NAME, i)));
	CHECK(ext4_dir_rm(mkpath(path, mp, "fw/sub%02u", 5)));
	CHECK(ext4_dir_mk(mkpath(path, mp, "fw/empty", 0)));
	CHECK(ext4_dir_rm(path));
	CHECK(ext4_fsymlink("big.bin", mkpath(path, mp, "fw/link", 0)));
	CHECK(ext4_setxattr(mkpath(path, mp, "fw/big.bin", 0), "user.lwext4",
			    11, "renode", 6));
	sysmem_phase("workload");
	(void)n;
}

void workload_verify(const char *mp)
{
	ext4_file f;
	char buf[16];
	size_t n;

	printf("  verify\n");
	ASSERT(count_entries(mkpath(path, mp, "fw", 0)) ==
	       2 + (int)WL_SUBDIRS - 1 + 5);
	for (unsigned i = 0; i < WL_SUBDIRS; i++)
		ASSERT(exists(mkpath(path, mp, "fw/sub%02u", i), EXT4_DE_DIR) ==
		       (i != 5));
	ASSERT(!exists(mkpath(path, mp, "fw/empty", 0), EXT4_DE_DIR));

	int small = 0;
	for (unsigned i = 0; i < WL_SMALL_FILES; i++) {
		mkpath(path, mp, SMALL_NAME, i);
		bool want = (i % 3) != 0 && i != 1;
		ASSERT(exists(path, EXT4_DE_REG_FILE) == want);
		if (want) {
			verify_file(path, i + 1, small_file_size(i), 1024);
			small++;
		}
	}
	ASSERT(count_entries(mkpath(path, mp, "fw/small", 0)) == 2 + small);
	verify_file(mkpath(path, mp, "fw/renamed.bin", 0), 2,
		    small_file_size(1), 1024);

	verify_file(mkpath(path, mp, "fw/big.bin", 0), WL_BIG_SEED,
		    big_file_size(), 1024);
	CHECK(ext4_getxattr(path, "user.lwext4", 11, buf, sizeof(buf), &n));
	ASSERT(n == 6 && !memcmp(buf, "renode", 6));

	CHECK(ext4_fopen(&f, mkpath(path, mp, "fw/trunc.bin", 0), "rb"));
	ASSERT(ext4_fsize(&f) == 8000);
	verify_pattern(&f, 7, 0, 5000, 1024, "trunc.bin");
	verify_pattern(&f, 8, 5000, 3000, 1024, "trunc.bin");
	CHECK(ext4_fclose(&f));

	CHECK(ext4_readlink(mkpath(path, mp, "fw/link", 0), buf, sizeof(buf),
			    &n));
	ASSERT(n == 7 && !memcmp(buf, "big.bin", 7));
}

/* ------------------------------------------------------------------------ */
/* Power cut torture                                                         */

#define TORTURE_SLOTS 8u
#define MARKER_SEED 777u
#define MARKER_SIZE 4096u

void workload_torture_prepare(const char *mp)
{
	if (!exists(mkpath(path, mp, "tort", 0), EXT4_DE_DIR))
		CHECK(ext4_dir_mk(path));
	create_file(mkpath(path, mp, "tort/marker", 0), MARKER_SEED,
		    MARKER_SIZE, 1024);
}

static uint32_t torture_size(uint32_t i)
{
	return 100u + (i * 7919u) % 30000u;
}

void workload_torture_step(const char *mp, uint32_t i)
{
	unsigned slot = i % TORTURE_SLOTS;

	mkpath(path, mp, "tort/f_%u", slot);
	create_file(path, i, torture_size(i), 1000);
	mkpath(path2, mp, "tort/g_%u", slot);
	if (exists(path2, EXT4_DE_REG_FILE))
		CHECK(ext4_fremove(path2));
	CHECK(ext4_frename(path, path2));
	CHECK(ext4_dir_mk(mkpath(path, mp, "tort/d_%u", i)));
	if (i >= 2)
		CHECK(ext4_dir_rm(mkpath(path, mp, "tort/d_%u", i - 2)));
	if (i % 5 == 0)
		CHECK(ext4_setxattr(path2, "user.iter", 9, &i, sizeof(i)));
}

void workload_torture_check(const char *mp)
{
	ext4_dir d;
	const ext4_direntry *de;
	int files = 0, dirs = 0;

	verify_file(mkpath(path, mp, "tort/marker", 0), MARKER_SEED,
		    MARKER_SIZE, 1024);

	/* Every entry must be reachable and fully readable. */
	CHECK(ext4_dir_open(&d, mkpath(path, mp, "tort", 0)));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		ext4_file f;
		size_t n, total = 0;
		char name[64];

		if (de->name_length >= sizeof(name) - 6)
			test_fail(__FILE__, __LINE__, "odd name", 0);
		memcpy(name, de->name, de->name_length);
		name[de->name_length] = '\0';
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		mkpath(path2, mp, "tort/", 0);
		strcat(path2, name);
		if (de->inode_type == EXT4_DE_DIR) {
			dirs++;
			continue;
		}
		ASSERT(de->inode_type == EXT4_DE_REG_FILE);
		files++;
		CHECK(ext4_fopen(&f, path2, "rb"));
		do {
			CHECK(ext4_fread(&f, io_buf, sizeof(io_buf), &n));
			total += n;
		} while (n);
		ASSERT(total == ext4_fsize(&f));
		CHECK(ext4_fclose(&f));
	}
	CHECK(ext4_dir_close(&d));
	printf("  tort: %d files, %d dirs\n", files, dirs);
	ASSERT(dirs <= 3);
	ASSERT(files <= (int)(2 * TORTURE_SLOTS + 2));

	/* The recovered file system must be writable. */
	create_file(mkpath(path, mp, "tort/after_recovery", 0), 4711, 12345,
		    1000);
	verify_file(path, 4711, 12345, 1000);
}
