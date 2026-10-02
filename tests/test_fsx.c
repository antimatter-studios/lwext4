/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Random data-integrity exerciser in the style of fsx (fork issue #102).
 *
 * A long random sequence of operations runs on a set of files: writes at
 * random offsets and lengths (overwriting and extending), appends, reads,
 * truncates, renames, deletes and re-creates, and every so often an unmount
 * and remount. An in-memory model holds what every file must contain;
 * every read is compared with it, and after every remount every file is
 * read back completely and the directory is listed. At the end the model
 * of every file is written next to the image (<image>.model.<name>) and
 * the check script runs e2fsck and compares each file, as debugfs reads
 * it, with its model.
 *
 * The sequence is fixed by a seed, so a failure reproduces: it prints the
 * seed, the image, the operation number and the last operations.
 * LWEXT4_FSX_SEED and LWEXT4_FSX_OPS (operations per image) change the
 * defaults for longer local runs, LWEXT4_FSX_TRACE=1 prints every
 * operation, LWEXT4_FSX_CHECK_EACH=1 reads the changed file back completely
 * after every operation (slow: to find the operation that breaks a file).
 *
 * It runs on each image the setup script made: ext4 with 1 KiB and 4 KiB
 * blocks (extents, journal), ext4 without the journal and ext2 (block
 * maps). Writes may start past the end of a file and truncates may grow
 * it (fork issue #103): both leave holes, which must read as zeros.
 *
 * red-green: guard (an exerciser, not the test of a fix: it passes on any
 * base that keeps the data intact)
 */

#include "test_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFILES 8
#define MAX_SIZE (512u * 1024u)
#define MAX_IO (70u * 1024u)
#define REMOUNT_EVERY 250
#define DEFAULT_OPS 3000
#define DIR TEST_MP "fsx"

struct mfile {
	char path[64];
	uint8_t *data;
	size_t size;
	int exists;
};

static struct mfile files[NFILES];
static uint64_t rng_state;
static unsigned long op_no, generation;
static const char *image_name;
static uint32_t seed;

static uint64_t rng(void)
{
	/* xorshift64*: the same sequence on every platform */
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return rng_state * 0x2545f4914f6cdd1dull;
}

static size_t rnd(size_t n)
{
	return n ? (size_t)(rng() % n) : 0;
}

/* ------------------------------------------------------------------------ */
/* Operation log: the last operations, printed on failure */

enum op { OP_WRITE, OP_APPEND, OP_READ, OP_TRUNCATE, OP_RENAME, OP_RECREATE,
	  OP_REMOUNT };

static const char *const op_names[] = {"write", "append", "read", "truncate",
				       "rename", "recreate", "remount"};

#define LOG_SIZE 32

static struct {
	unsigned long no;
	enum op op;
	int file;
	size_t off, len;
} oplog[LOG_SIZE];

static int trace, check_each;

static void log_op(enum op op, int file, size_t off, size_t len)
{
	unsigned i = (unsigned)(op_no % LOG_SIZE);

	if (trace)
		printf("%6lu %-8s file %d off %lu len %lu\n", op_no,
		       op_names[op], file, (unsigned long)off,
		       (unsigned long)len);

	oplog[i].no = op_no;
	oplog[i].op = op;
	oplog[i].file = file;
	oplog[i].off = off;
	oplog[i].len = len;
}

static void fail(const char *fmt, const char *path, unsigned long long a,
		 unsigned long long b)
{
	unsigned long i, first = op_no >= LOG_SIZE ? op_no - LOG_SIZE + 1 : 0;

	fprintf(stderr, "FAIL %s, seed %lu, operation %lu: ", image_name,
		(unsigned long)seed, op_no);
	fprintf(stderr, fmt, path, a, b);
	fprintf(stderr, "\nlast operations:\n");
	for (i = first; i <= op_no; i++) {
		unsigned k = (unsigned)(i % LOG_SIZE);

		if (oplog[k].no != i)
			continue;
		fprintf(stderr, "  %6lu %-8s file %d off %lu len %lu\n", i,
			op_names[oplog[k].op], oplog[k].file,
			(unsigned long)oplog[k].off,
			(unsigned long)oplog[k].len);
	}
	exit(1);
}

#define CHECK(expr, path)                                                      \
	do {                                                                   \
		int r_ = (expr);                                               \
		if (r_ != EOK)                                                 \
			fail(#expr " on %s = %llu (line %llu)", (path),        \
			     (unsigned long long)r_,                           \
			     (unsigned long long)__LINE__);                    \
	} while (0)

/* ------------------------------------------------------------------------ */
/* Operations, each applied to lwext4 and to the model */

static uint8_t iobuf[MAX_IO], readbuf[MAX_IO];

static void new_path(struct mfile *m, int i)
{
	snprintf(m->path, sizeof(m->path), DIR "/f%d-%lu", i, generation++);
}

static void do_write(int i, size_t off, size_t len, enum op op)
{
	struct mfile *m = &files[i];
	ext4_file f;
	size_t k, n;

	if (off > MAX_SIZE)
		off = MAX_SIZE;
	if (off + len > MAX_SIZE)
		len = MAX_SIZE - off;
	log_op(op, i, off, len);
	for (k = 0; k < len; k++)
		iobuf[k] = (uint8_t)rng();
	CHECK(ext4_fopen(&f, m->path, "r+b"), m->path);
	CHECK(ext4_fseek(&f, (int64_t)off, SEEK_SET), m->path);
	CHECK(ext4_fwrite(&f, iobuf, len, &n), m->path);
	if (n != len)
		fail("%s: wrote %llu of %llu bytes", m->path, n, len);
	CHECK(ext4_fclose(&f), m->path);
	/* A hole between the old end and the write reads as zeros */
	if (off > m->size)
		memset(m->data + m->size, 0, off - m->size);
	memcpy(m->data + off, iobuf, len);
	if (off + len > m->size)
		m->size = off + len;
}

static void compare(const struct mfile *m, size_t off, const uint8_t *got,
		    size_t len)
{
	size_t k;

	for (k = 0; k < len; k++)
		if (got[k] != m->data[off + k])
			fail("%s: byte %llu differs from the model (0x%02llx)",
			     m->path, (unsigned long long)(off + k),
			     (unsigned long long)got[k]);
}

static void do_read(int i, size_t off, size_t len)
{
	struct mfile *m = &files[i];
	size_t want = off >= m->size ? 0
		      : off + len > m->size ? m->size - off : len, n;
	ext4_file f;

	log_op(OP_READ, i, off, len);
	CHECK(ext4_fopen(&f, m->path, "rb"), m->path);
	if (ext4_fsize(&f) != m->size)
		fail("%s: size %llu, model %llu", m->path, ext4_fsize(&f),
		     m->size);
	CHECK(ext4_fseek(&f, (int64_t)off, SEEK_SET), m->path);
	CHECK(ext4_fread(&f, readbuf, len, &n), m->path);
	if (n != want)
		fail("%s: read %llu bytes, model %llu", m->path, n, want);
	CHECK(ext4_fclose(&f), m->path);
	compare(m, off, readbuf, n);
}

static void do_truncate(int i, size_t size)
{
	struct mfile *m = &files[i];
	ext4_file f;

	log_op(OP_TRUNCATE, i, size, 0);
	CHECK(ext4_fopen(&f, m->path, "r+b"), m->path);
	CHECK(ext4_ftruncate(&f, size), m->path);
	if (ext4_fsize(&f) != size)
		fail("%s: size %llu after truncate to %llu", m->path,
		     ext4_fsize(&f), size);
	CHECK(ext4_fclose(&f), m->path);
	if (size > m->size)
		memset(m->data + m->size, 0, size - m->size);
	m->size = size;
}

static void do_rename(int i)
{
	struct mfile *m = &files[i];
	char old[sizeof(m->path)];

	log_op(OP_RENAME, i, 0, 0);
	strcpy(old, m->path);
	new_path(m, i);
	CHECK(ext4_frename(old, m->path), old);
}

static void do_recreate(int i)
{
	struct mfile *m = &files[i];
	ext4_file f;

	log_op(OP_RECREATE, i, 0, 0);
	if (m->exists)
		CHECK(ext4_fremove(m->path), m->path);
	new_path(m, i);
	CHECK(ext4_fopen(&f, m->path, "wb"), m->path);
	CHECK(ext4_fclose(&f), m->path);
	m->size = 0;
	m->exists = 1;
}

/* Every file read back completely, and the directory lists exactly them. */
static void verify_all(void)
{
	const ext4_direntry *de;
	ext4_dir d;
	unsigned entries = 0;
	int i;

	for (i = 0; i < NFILES; i++) {
		struct mfile *m = &files[i];
		size_t off;

		for (off = 0; off < m->size; off += MAX_IO)
			do_read(i, off, MAX_IO);
		if (!m->size)
			do_read(i, 0, 1);
	}
	CHECK(ext4_dir_open(&d, DIR), DIR);
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		char name[300];

		memcpy(name, de->name, de->name_length);
		name[de->name_length] = 0;
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		entries++;
		for (i = 0; i < NFILES; i++)
			if (!strcmp(files[i].path + sizeof(DIR), name))
				break;
		if (i == NFILES)
			fail("unexpected directory entry %s", name, 0, 0);
	}
	CHECK(ext4_dir_close(&d), DIR);
	if (entries != NFILES)
		fail("%s: %llu entries, model %llu", DIR, entries, NFILES);
}

/* <image>.model.<name>: what each file must contain, for the check
 * script. */
static void save_models(const char *image)
{
	char path[600];
	FILE *out;
	int i;

	for (i = 0; i < NFILES; i++) {
		snprintf(path, sizeof(path), "%s.model.%s", image,
			 files[i].path + sizeof(DIR));
		out = fopen(path, "wb");
		TEST_ASSERT(out != NULL);
		TEST_ASSERT_EQ(files[i].size,
			       fwrite(files[i].data, 1, files[i].size, out));
		TEST_ASSERT_EQ(0, fclose(out));
	}
}

static void mount_image(const char *image)
{
	int r;

	CHECK(test_mount(image, false), image);
	/* ENOTSUP: no journal to recover */
	r = ext4_recover(TEST_MP);
	if (r != ENOTSUP)
		CHECK(r, image);
	CHECK(ext4_journal_start(TEST_MP), image);
}

static void umount_image(const char *image)
{
	CHECK(ext4_journal_stop(TEST_MP), image);
	test_umount();
}

static void run(const char *image, unsigned long ops)
{
	int i;

	image_name = image;
	rng_state = 0x9e3779b97f4a7c15ull ^ seed;
	op_no = 0;
	generation = 0;
	mount_image(image);
	CHECK(ext4_dir_mk(DIR), DIR);
	for (i = 0; i < NFILES; i++) {
		files[i].exists = 0;
		do_recreate(i);
	}

	for (op_no = 1; op_no <= ops; op_no++) {
		size_t k = rnd(100);
		int fi = (int)rnd(NFILES);
		struct mfile *m = &files[fi];

		/* Mostly short I/O, sometimes up to MAX_IO */
		size_t len = rnd(8) ? rnd(4096) + 1 : rnd(MAX_IO) + 1;

		if (op_no % REMOUNT_EVERY == 0) {
			log_op(OP_REMOUNT, -1, 0, 0);
			umount_image(image);
			mount_image(image);
			verify_all();
		} else if (k < 35) {
			do_write(fi, rnd(m->size + 16384), len, OP_WRITE);
		} else if (k < 50) {
			do_write(fi, m->size, len, OP_APPEND);
		} else if (k < 80) {
			do_read(fi, rnd(m->size + 4096), len);
		} else if (k < 90) {
			size_t sz = rnd(m->size + 32768);

			do_truncate(fi, sz < MAX_SIZE ? sz : MAX_SIZE);
		} else if (k < 95) {
			do_rename(fi);
		} else {
			do_recreate(fi);
		}
		if (check_each) {
			size_t off;

			for (off = 0; off < m->size; off += MAX_IO)
				do_read(fi, off, MAX_IO);
		}
	}
	verify_all();
	umount_image(image);
	save_models(image);
	printf("%s: %lu operations, seed %lu: OK\n", image, ops,
	       (unsigned long)seed);
}

int main(int argc, char **argv)
{
	static const char *const suffixes[] = {"", ".4k", ".nojournal",
					       ".ext2"};
	const char *image = test_image_arg(argc, argv);
	const char *s_seed = getenv("LWEXT4_FSX_SEED");
	const char *s_ops = getenv("LWEXT4_FSX_OPS");
	unsigned long ops = s_ops ? strtoul(s_ops, NULL, 0) : DEFAULT_OPS;
	char path[512];
	size_t i;

	seed = s_seed ? (uint32_t)strtoul(s_seed, NULL, 0) : 20261002u;
	trace = getenv("LWEXT4_FSX_TRACE") != NULL;
	check_each = getenv("LWEXT4_FSX_CHECK_EACH") != NULL;
	for (i = 0; i < NFILES; i++) {
		files[i].data = malloc(MAX_SIZE);
		TEST_ASSERT(files[i].data != NULL);
	}
	for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
		snprintf(path, sizeof(path), "%s%s", image, suffixes[i]);
		run(path, ops);
	}
	for (i = 0; i < NFILES; i++)
		free(files[i].data);
	return 0;
}
