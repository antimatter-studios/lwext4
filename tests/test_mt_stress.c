/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Many threads on one mount point (gkostka/lwext4#79, #83). The public API
 * serialises every call on a mount point with the lock callbacks installed
 * by ext4_mount_setup_locks(); here they are backed by an error checking
 * pthread mutex, so that a call that locks the mount point while it holds
 * it (a plain mutex would deadlock), unlocks it without holding it, or
 * returns with it held fails the test at once.
 *
 * Every thread works on its own files, directories and symlinks with a
 * fixed pseudo random sequence of operations (create, write, append,
 * overwrite, read back, truncate, rename into and out of a shared
 * directory, hard links, remove, directory trees made and removed,
 * xattrs, mode/owner/times, symlinks), keeps a model of what they must
 * contain and checks every result against it. All threads also read one
 * shared file and list the shared directory while the others change it.
 * Every block device access must come from the thread that holds the
 * mount point lock. At the end the whole tree is compared with the model,
 * after a remount as well, and e2fsck -fn must find nothing to fix.
 *
 * A thread that makes no progress for STALL_SECONDS fails the test as a
 * deadlock. Run it under ThreadSanitizer (LWEXT4_SANITIZE=thread) to find
 * data races as well.
 */

#include "test_util.h"
#include "mt_util.h"

#include "../blockdev/linux/file_dev.h"

#include <ext4_inode.h>
#include <ext4_misc.h>

#include <stdint.h>
#include <string.h>

#define THREADS 6
#define ITERATIONS 250
#define FILES 6	      /* regular files per thread */
#define LINKS 3	      /* symlinks per thread */
#define MAX_FILE 40000 /* bytes */
#define MAX_TARGET 200
#define MAX_XATTR 48
#define SHARED_SIZE 50000
#define STALL_SECONDS 120
#define SEED 0x6c77657874340079ull

#define SHARED_DIR TEST_MP "shared"
#define SHARED_FILE TEST_MP "shared.dat"

MT_LOCK(mp)

static const struct ext4_lock mp_locks = {
	.lock = mp_lock,
	.unlock = mp_unlock,
};

static bool check_io_lock; /* set while the threads run */

#define fail mt_fail
#define CHECK MT_CHECK
#define CHECK_EQ MT_CHECK_EQ

/* Every public call returns with the mount point unlocked. */
#define BALANCED(what)                                                         \
	do {                                                                   \
		if (mp_depth != 0)                                             \
			fail("%s:%d: returned with the mount point locked: "   \
			     "%s",                                             \
			     __FILE__, __LINE__, what);                        \
	} while (0)

static int api_check(int r, const char *call, int line)
{
	if (mp_depth != 0)
		fail("%s:%d: returned with the mount point locked: %s",
		     __FILE__, line, call);
	return r;
}

/* API(ext4_xxx(...)): the call's result, after checking the lock. */
#define API(call) api_check((call), #call, __LINE__)
#define API_EQ(expected, call) CHECK_EQ(expected, API(call))

/******************* block device: I/O only under the lock *****************/

static int (*file_bread)(struct ext4_blockdev *bdev, void *buf,
			 uint64_t blk_id, uint32_t blk_cnt);
static int (*file_bwrite)(struct ext4_blockdev *bdev, const void *buf,
			  uint64_t blk_id, uint32_t blk_cnt);

static int locked_bread(struct ext4_blockdev *bdev, void *buf,
			uint64_t blk_id, uint32_t blk_cnt)
{
	if (check_io_lock && mp_depth != 1)
		fail("block %llu read without the mount point lock",
		     (unsigned long long)blk_id);
	return file_bread(bdev, buf, blk_id, blk_cnt);
}

static int locked_bwrite(struct ext4_blockdev *bdev, const void *buf,
			 uint64_t blk_id, uint32_t blk_cnt)
{
	if (check_io_lock && mp_depth != 1)
		fail("block %llu written without the mount point lock",
		     (unsigned long long)blk_id);
	return file_bwrite(bdev, buf, blk_id, blk_cnt);
}

/********************************** model **********************************/

struct file_model {
	bool exists;
	bool shared; /* in SHARED_DIR instead of the thread's directory */
	uint32_t size;
	uint8_t data[MAX_FILE];
	bool has_xattr;
	size_t xattr_len;
	uint8_t xattr[MAX_XATTR];
	bool attrs_known;
	/* The times are what op_attrs set: no write, truncate, rename, link
	 * or xattr change (which stamp them, see ext4_clock_setup) since */
	bool times_known;
	uint32_t mode, uid, gid, mtime;
};

struct link_model {
	bool exists;
	char target[MAX_TARGET + 1];
};

struct worker {
	int id;
	uint64_t rng;
	unsigned long ops;
	struct file_model files[FILES];
	struct link_model links[LINKS];
	uint8_t buf[MAX_FILE];
};

/* One allocation per thread (not one static array), so that the address
 * sanitizer sees an overrun of one thread's buffers into the next. */
static struct worker *workers[THREADS];
static uint8_t shared_data[SHARED_SIZE];

static uint64_t rnd(struct worker *w)
{
	return mt_rnd(&w->rng);
}

static uint32_t rnd_below(struct worker *w, uint32_t n)
{
	return mt_rnd_below(&w->rng, n);
}

static void rnd_bytes(struct worker *w, uint8_t *p, size_t n)
{
	mt_rnd_bytes(&w->rng, p, n);
}

static void file_path(char *p, size_t size, int t, int k, bool shared)
{
	if (shared)
		snprintf(p, size, SHARED_DIR "/t%d_f%d", t, k);
	else
		snprintf(p, size, TEST_MP "t%d/f%d", t, k);
}

static void link_path(char *p, size_t size, int t, int k)
{
	snprintf(p, size, TEST_MP "t%d/l%d", t, k);
}

/****************************** file contents ******************************/

/* Write n bytes at the handle's position in chunks of random sizes. */
static void write_chunks(struct worker *w, ext4_file *f, const uint8_t *p,
			 size_t n)
{
	while (n) {
		size_t c = 1 + rnd_below(w, 9000), wcnt = 0;

		if (c > n)
			c = n;
		API_EQ(EOK, ext4_fwrite(f, p, c, &wcnt));
		CHECK_EQ(c, wcnt);
		p += c;
		n -= c;
	}
}

/* The whole file, in chunks of random sizes, and some random ranges. */
static void verify_contents(struct worker *w, const char *path,
			    const uint8_t *data, uint32_t size)
{
	ext4_file f;
	size_t done = 0, rcnt;

	API_EQ(EOK, ext4_fopen(&f, path, "rb"));
	CHECK_EQ(size, ext4_fsize(&f));
	while (done < size) {
		size_t c = 1 + rnd_below(w, 9000);

		if (c > size - done)
			c = size - done;
		API_EQ(EOK, ext4_fread(&f, w->buf, c, &rcnt));
		CHECK_EQ(c, rcnt);
		if (memcmp(w->buf, data + done, c))
			fail("%s: bytes %zu..%zu differ", path, done,
			     done + c);
		done += c;
	}
	API_EQ(EOK, ext4_fread(&f, w->buf, 1, &rcnt));
	CHECK_EQ(0, rcnt);

	for (int i = 0; i < 3 && size; i++) {
		uint32_t off = rnd_below(w, size);
		size_t c = 1 + rnd_below(w, size - off);

		/* the shared file is larger than the buffer */
		if (c > sizeof(w->buf))
			c = sizeof(w->buf);

		CHECK_EQ(EOK, ext4_fseek(&f, off, SEEK_SET));
		CHECK_EQ(off, ext4_ftell(&f));
		API_EQ(EOK, ext4_fread(&f, w->buf, c, &rcnt));
		CHECK_EQ(c, rcnt);
		if (memcmp(w->buf, data + off, c))
			fail("%s: bytes %u..%zu differ", path, off, off + c);
	}
	API_EQ(EOK, ext4_fclose(&f));
}

/******************************** operations *******************************/

static void op_create(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64];
	ext4_file f;
	uint32_t size = rnd_below(w, MAX_FILE + 1);

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	rnd_bytes(w, m->data, size);
	API_EQ(EOK, ext4_fopen(&f, path, "wb"));
	CHECK_EQ(0, ext4_fsize(&f));
	write_chunks(w, &f, m->data, size);
	CHECK_EQ(size, ext4_fsize(&f));
	API_EQ(EOK, ext4_fclose(&f));
	if (!m->exists) {
		m->has_xattr = false;
		m->attrs_known = false;
	}
	m->exists = true;
	m->size = size;
}

static void op_append(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64];
	ext4_file f;
	uint32_t n = rnd_below(w, MAX_FILE - m->size + 1);

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	rnd_bytes(w, m->data + m->size, n);
	API_EQ(EOK, ext4_fopen(&f, path, "ab"));
	CHECK_EQ(m->size, ext4_ftell(&f));
	write_chunks(w, &f, m->data + m->size, n);
	API_EQ(EOK, ext4_fclose(&f));
	m->size += n;
}

static void op_overwrite(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64];
	ext4_file f;
	uint32_t off = rnd_below(w, m->size + 1);
	uint32_t n = rnd_below(w, MAX_FILE - off + 1);

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	rnd_bytes(w, m->data + off, n);
	API_EQ(EOK, ext4_fopen(&f, path, "r+b"));
	CHECK_EQ(EOK, ext4_fseek(&f, off, SEEK_SET));
	write_chunks(w, &f, m->data + off, n);
	API_EQ(EOK, ext4_fclose(&f));
	if (off + n > m->size)
		m->size = off + n;
}

static void op_truncate(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64];
	ext4_file f;
	uint32_t size = rnd_below(w, m->size + 1);

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	API_EQ(EOK, ext4_fopen(&f, path, "r+b"));
	API_EQ(EOK, ext4_ftruncate(&f, size));
	CHECK_EQ(size, ext4_fsize(&f));
	API_EQ(EOK, ext4_fclose(&f));
	m->size = size;
}

static void op_rename(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char from[64], to[64];

	m->times_known = false;
	file_path(from, sizeof(from), w->id, k, m->shared);
	file_path(to, sizeof(to), w->id, k, !m->shared);
	API_EQ(EOK, ext4_frename(from, to));
	API_EQ(ENOENT, ext4_inode_exist(from, EXT4_DE_REG_FILE));
	API_EQ(EOK, ext4_inode_exist(to, EXT4_DE_REG_FILE));
	m->shared = !m->shared;
}

static void op_hardlink(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64], link[64];

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	snprintf(link, sizeof(link), SHARED_DIR "/t%d_h%d", w->id, k);
	API_EQ(EOK, ext4_flink(path, link));
	API_EQ(EEXIST, ext4_flink(path, link));
	verify_contents(w, link, m->data, m->size);
	API_EQ(EOK, ext4_fremove(link));
	API_EQ(ENOENT, ext4_inode_exist(link, EXT4_DE_REG_FILE));
}

static void op_remove(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64];

	file_path(path, sizeof(path), w->id, k, m->shared);
	API_EQ(EOK, ext4_fremove(path));
	API_EQ(ENOENT, ext4_inode_exist(path, EXT4_DE_REG_FILE));
	API_EQ(ENOENT, ext4_fremove(path));
	m->exists = false;
}

static void op_xattr(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	char path[64], name[16], list[256];
	uint8_t val[MAX_XATTR];
	size_t len, list_len;
	bool listed = false;

	m->times_known = false;
	file_path(path, sizeof(path), w->id, k, m->shared);
	snprintf(name, sizeof(name), "user.t%d", w->id);

	if (m->has_xattr && rnd_below(w, 3) == 0) {
		API_EQ(EOK, ext4_removexattr(path, name, strlen(name)));
		m->has_xattr = false;
	} else if (rnd_below(w, 2)) {
		m->xattr_len = 1 + rnd_below(w, MAX_XATTR);
		rnd_bytes(w, m->xattr, m->xattr_len);
		API_EQ(EOK, ext4_setxattr(path, name, strlen(name), m->xattr,
					  m->xattr_len));
		m->has_xattr = true;
	}

	if (m->has_xattr) {
		API_EQ(EOK, ext4_getxattr(path, name, strlen(name), val,
					  sizeof(val), &len));
		CHECK_EQ(m->xattr_len, len);
		CHECK(memcmp(val, m->xattr, len) == 0);
	} else {
		API_EQ(ENODATA, ext4_getxattr(path, name, strlen(name), val,
					      sizeof(val), &len));
	}

	list_len = 0;
	API_EQ(EOK, ext4_listxattr(path, list, sizeof(list), &list_len));
	for (size_t i = 0; i < list_len; i += strlen(list + i) + 1)
		if (!strcmp(list + i, name))
			listed = true;
	CHECK_EQ(m->has_xattr, listed);
}

static void op_attrs(struct worker *w, int k)
{
	struct file_model *m = &w->files[k];
	struct ext4_inode inode;
	char path[64];
	uint32_t ino = 0, mode, uid, gid, mtime, t;

	file_path(path, sizeof(path), w->id, k, m->shared);
	if (!m->attrs_known || !m->times_known || rnd_below(w, 2)) {
		m->mode = rnd_below(w, 01000);
		m->uid = rnd_below(w, 70000);
		m->gid = rnd_below(w, 70000);
		m->mtime = (uint32_t)rnd(w);
		API_EQ(EOK, ext4_mode_set(path, m->mode));
		API_EQ(EOK, ext4_owner_set(path, m->uid, m->gid));
		API_EQ(EOK, ext4_mtime_set(path, m->mtime));
		API_EQ(EOK, ext4_atime_set(path, m->mtime + 1));
		API_EQ(EOK, ext4_ctime_set(path, m->mtime + 2));
		m->attrs_known = true;
		m->times_known = true;
	}
	API_EQ(EOK, ext4_mode_get(path, &mode));
	CHECK_EQ(m->mode, mode & 0777);
	CHECK_EQ(EXT4_INODE_MODE_FILE, mode & EXT4_INODE_MODE_TYPE_MASK);
	API_EQ(EOK, ext4_owner_get(path, &uid, &gid));
	CHECK_EQ(m->uid & 0xffff, uid & 0xffff);
	CHECK_EQ(m->gid & 0xffff, gid & 0xffff);
	API_EQ(EOK, ext4_mtime_get(path, &mtime));
	CHECK_EQ(m->mtime, mtime);
	API_EQ(EOK, ext4_atime_get(path, &t));
	CHECK_EQ(m->mtime + 1, t);
	API_EQ(EOK, ext4_ctime_get(path, &t));
	CHECK_EQ(m->mtime + 2, t);

	API_EQ(EOK, ext4_raw_inode_fill(path, &ino, &inode));
	CHECK(ino > EXT4_INODE_ROOT_INDEX);
	CHECK_EQ(m->size, to_le32(inode.size_lo));
	CHECK_EQ(1, to_le16(inode.links_count));
}

static void op_symlink(struct worker *w, int k)
{
	struct link_model *m = &w->links[k];
	char path[64], buf[MAX_TARGET + 1];
	size_t len, rcnt = 0;

	link_path(path, sizeof(path), w->id, k);
	if (m->exists) {
		memset(buf, 0, sizeof(buf));
		API_EQ(EOK, ext4_readlink(path, buf, sizeof(buf), &rcnt));
		CHECK_EQ(strlen(m->target), rcnt);
		CHECK(memcmp(buf, m->target, rcnt) == 0);
		if (rnd_below(w, 2)) {
			API_EQ(EOK, ext4_fremove(path));
			m->exists = false;
		}
		return;
	}

	/* Fast (in the inode, below 60 bytes) and slow symlinks */
	len = 1 + rnd_below(w, MAX_TARGET);
	for (size_t i = 0; i < len; i++)
		m->target[i] = (char)('a' + rnd_below(w, 26));
	m->target[len] = 0;
	API_EQ(EOK, ext4_fsymlink(m->target, path));
	API_EQ(EOK, ext4_inode_exist(path, EXT4_DE_SYMLINK));
	m->exists = true;
}

/* A small tree in the thread's directory, listed and removed again. */
static void op_tree(struct worker *w)
{
	char dir[64], sub[80], path[96];
	ext4_file f;
	ext4_dir d;
	const ext4_direntry *de;
	int seen = 0;

	snprintf(dir, sizeof(dir), TEST_MP "t%d/d%u", w->id, rnd_below(w, 4));
	snprintf(sub, sizeof(sub), "%s/sub", dir);
	API_EQ(EOK, ext4_dir_mk(dir));
	API_EQ(EOK, ext4_dir_mk(dir)); /* exists: nothing to do */
	API_EQ(EOK, ext4_dir_mk(sub));
	for (int i = 0; i < 3; i++) {
		size_t wcnt;

		snprintf(path, sizeof(path), "%s/x%d", i ? sub : dir, i);
		API_EQ(EOK, ext4_fopen(&f, path, "wb"));
		API_EQ(EOK, ext4_fwrite(&f, path, strlen(path), &wcnt));
		API_EQ(EOK, ext4_fclose(&f));
	}
	API_EQ(ENOENT, ext4_fopen(&f, dir, "rb"));

	API_EQ(EOK, ext4_dir_open(&d, dir));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		BALANCED("ext4_dir_entry_next");
		if (!de->inode)
			continue;
		if (de->name_length == 3 && !memcmp(de->name, "sub", 3))
			seen |= 1;
		else if (de->name_length == 2 && !memcmp(de->name, "x0", 2))
			seen |= 2;
		else if (!(de->name_length == 1 && de->name[0] == '.') &&
			 !(de->name_length == 2 && !memcmp(de->name, "..", 2)))
			fail("%s: unexpected entry %.*s", dir,
			     de->name_length, de->name);
	}
	BALANCED("ext4_dir_entry_next");
	ext4_dir_entry_rewind(&d);
	CHECK(ext4_dir_entry_next(&d) != NULL);
	BALANCED("ext4_dir_entry_next");
	API_EQ(EOK, ext4_dir_close(&d));
	CHECK_EQ(3, seen);

	API_EQ(EOK, ext4_dir_rm(dir));
	API_EQ(ENOENT, ext4_inode_exist(dir, EXT4_DE_DIR));
	API_EQ(ENOENT, ext4_dir_rm(dir));
}

/* The shared directory changes under the listing: entries may be missed
 * or seen twice (the iteration is by offset), but every entry must be
 * well formed and the listing must end. */
static void op_list_shared(void)
{
	ext4_dir d;
	const ext4_direntry *de;
	int n = 0;

	API_EQ(EOK, ext4_dir_open(&d, SHARED_DIR));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		BALANCED("ext4_dir_entry_next");
		CHECK(de->name_length > 0);
		CHECK(++n < 100000);
	}
	BALANCED("ext4_dir_entry_next");
	API_EQ(EOK, ext4_dir_close(&d));
}

static void op_read_shared(struct worker *w)
{
	verify_contents(w, SHARED_FILE, shared_data, SHARED_SIZE);
}

static void op_misc(struct worker *w)
{
	struct ext4_mount_stats st;
	ext4_file f;
	char path[64];

	API_EQ(EOK, ext4_mount_point_stats(TEST_MP, &st));
	CHECK(st.free_blocks_count <= st.blocks_count);
	CHECK(st.free_inodes_count <= st.inodes_count);
	CHECK_EQ(1024, st.block_size);

	switch (rnd_below(w, 4)) {
	case 0:
		API_EQ(EOK, ext4_cache_flush(TEST_MP));
		break;
	case 1:
		snprintf(path, sizeof(path), TEST_MP "t%d/missing", w->id);
		API_EQ(ENOENT, ext4_fopen(&f, path, "rb"));
		API_EQ(ENOENT, ext4_frename(path, path));
		API_EQ(ENOENT, ext4_mode_set(path, 0644));
		API_EQ(ENOENT, ext4_readlink(path, path, sizeof(path), NULL));
		break;
	case 2:
		/* Write-back mode is counted, so threads may nest it. */
		API_EQ(EOK, ext4_cache_write_back(TEST_MP, true));
		op_read_shared(w);
		API_EQ(EOK, ext4_cache_write_back(TEST_MP, false));
		break;
	default:
		API_EQ(EOK, ext4_inode_exist(TEST_MP, EXT4_DE_DIR));
		API_EQ(EOK, ext4_inode_exist(SHARED_DIR, EXT4_DE_DIR));
		break;
	}
}

static void one_op(struct worker *w)
{
	int k = (int)rnd_below(w, FILES);
	struct file_model *m = &w->files[k];
	uint32_t op = rnd_below(w, 100);
	char path[64];

	if (op < 8) {
		op_symlink(w, (int)rnd_below(w, LINKS));
	} else if (op < 12) {
		op_tree(w);
	} else if (op < 17) {
		op_list_shared();
	} else if (op < 21) {
		op_read_shared(w);
	} else if (op < 25) {
		op_misc(w);
	} else if (!m->exists || op < 35) {
		op_create(w, k);
	} else if (op < 45) {
		op_append(w, k);
	} else if (op < 55) {
		op_overwrite(w, k);
	} else if (op < 62) {
		op_truncate(w, k);
	} else if (op < 69) {
		op_rename(w, k);
	} else if (op < 74) {
		op_hardlink(w, k);
	} else if (op < 79) {
		op_remove(w, k);
	} else if (op < 86) {
		op_xattr(w, k);
	} else if (op < 92) {
		op_attrs(w, k);
	} else {
		file_path(path, sizeof(path), w->id, k, m->shared);
		verify_contents(w, path, m->data, m->size);
	}
}

static void *worker_main(void *arg)
{
	struct worker *w = arg;

	for (int i = 0; i < ITERATIONS; i++) {
		one_op(w);
		w->ops++;
		mt_progress(false);
	}
	mt_progress(true);
	return NULL;
}

/**************************** final verification ***************************/

/* A directory must hold exactly the entries in names (plus "." and ".."). */
static void verify_dir(const char *dir, char names[][16], int n)
{
	bool seen[THREADS * FILES + LINKS] = {false};
	ext4_dir d;
	const ext4_direntry *de;

	TEST_ASSERT_EQ(EOK, ext4_dir_open(&d, dir));
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		int i;

		if (!de->inode)
			continue;
		if ((de->name_length == 1 && de->name[0] == '.') ||
		    (de->name_length == 2 && !memcmp(de->name, "..", 2)))
			continue;
		for (i = 0; i < n; i++)
			if (strlen(names[i]) == de->name_length &&
			    !memcmp(names[i], de->name, de->name_length))
				break;
		if (i == n || seen[i])
			fail("%s: unexpected entry %.*s", dir, de->name_length,
			     de->name);
		seen[i] = true;
	}
	TEST_ASSERT_EQ(EOK, ext4_dir_close(&d));
	for (int i = 0; i < n; i++)
		if (!seen[i])
			fail("%s: missing entry %s", dir, names[i]);
}

static void verify_all(void)
{
	char names[FILES + LINKS][16];
	int shared_n = 0;
	static char shared_names[THREADS * FILES][16];

	for (int t = 0; t < THREADS; t++) {
		struct worker *w = workers[t];
		char dir[32], path[64], buf[MAX_TARGET + 1];
		int n = 0;

		for (int k = 0; k < FILES; k++) {
			struct file_model *m = &w->files[k];

			if (!m->exists)
				continue;
			file_path(path, sizeof(path), t, k, m->shared);
			verify_contents(w, path, m->data, m->size);
			if (m->shared)
				snprintf(shared_names[shared_n++], 16,
					 "t%d_f%d", t, k);
			else
				snprintf(names[n++], 16, "f%d", k);
		}
		for (int k = 0; k < LINKS; k++) {
			struct link_model *m = &w->links[k];
			size_t rcnt;

			if (!m->exists)
				continue;
			link_path(path, sizeof(path), t, k);
			memset(buf, 0, sizeof(buf));
			TEST_ASSERT_EQ(EOK, ext4_readlink(path, buf,
							  sizeof(buf), &rcnt));
			TEST_ASSERT_EQ(strlen(m->target), rcnt);
			TEST_ASSERT(memcmp(buf, m->target, rcnt) == 0);
			snprintf(names[n++], 16, "l%d", k);
		}
		snprintf(dir, sizeof(dir), TEST_MP "t%d", t);
		verify_dir(dir, names, n);
	}
	verify_dir(SHARED_DIR, shared_names, shared_n);
	verify_contents(workers[0], SHARED_FILE, shared_data, SHARED_SIZE);
}

/* e2fsck -fn must find nothing to fix. (Through system(), which also
 * marks this test as one that needs a POSIX host: tests/CMakeLists.txt
 * disables such tests on Windows.) */
static void mt_fsck(const char *image)
{
	char cmd[1024];
	int r;

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	r = system(cmd);
	if (r != 0)
		mt_fail("e2fsck -fn %s: exit status %d", image, r);
}

static void mount_journal(const char *image)
{
	TEST_ASSERT_EQ(EOK, test_mount(image, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
}

static void umount_journal(void)
{
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	test_umount();
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	pthread_t threads[THREADS];
	ext4_file f;

	mt_mutex_init(&mp_mutex);

	file_dev_name_set(image);
	file_bread = file_dev_get()->bdif->bread;
	file_bwrite = file_dev_get()->bdif->bwrite;
	file_dev_get()->bdif->bread = locked_bread;
	file_dev_get()->bdif->bwrite = locked_bwrite;

	mount_journal(image);
	TEST_ASSERT_EQ(EOK, ext4_mount_setup_locks(TEST_MP, &mp_locks));

	/* Shared file and directories, before the threads start */
	for (size_t i = 0; i < SHARED_SIZE; i++)
		shared_data[i] = (uint8_t)(i * 13 + i / 511);
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, SHARED_FILE, "wb"));
	for (size_t off = 0, wcnt; off < SHARED_SIZE; off += wcnt)
		TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, shared_data + off,
						SHARED_SIZE - off, &wcnt));
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_dir_mk(SHARED_DIR));
	for (int t = 0; t < THREADS; t++) {
		char dir[32];

		snprintf(dir, sizeof(dir), TEST_MP "t%d", t);
		TEST_ASSERT_EQ(EOK, ext4_dir_mk(dir));
		workers[t] = calloc(1, sizeof(*workers[t]));
		CHECK(workers[t] != NULL);
		workers[t]->id = t;
		workers[t]->rng = mt_seed(SEED, t);
	}

	check_io_lock = true;
	for (int t = 0; t < THREADS; t++)
		TEST_ASSERT_EQ(0, pthread_create(&threads[t], NULL,
						 worker_main, workers[t]));
	mt_wait(THREADS, STALL_SECONDS);
	for (int t = 0; t < THREADS; t++)
		TEST_ASSERT_EQ(0, pthread_join(threads[t], NULL));
	check_io_lock = false;
	TEST_ASSERT_EQ(0, mp_depth);

	verify_all();
	umount_journal();
	mt_fsck(image);

	/* Everything must have reached the image. */
	mount_journal(image);
	verify_all();
	umount_journal();
	mt_fsck(image);

	pthread_mutex_destroy(&mp_mutex);
	printf("%d threads, %d operations each: consistent\n", THREADS,
	       ITERATIONS);
	return 0;
}
