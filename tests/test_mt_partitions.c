/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Several threads on two mount points on one disk (two partitions of one
 * block device interface). Each mount point has its own lock; the two share
 * the interface and its lock callbacks, and with them the one physical
 * block buffer that ext4_block_readbytes()/ext4_block_writebytes() use for
 * the parts of physical blocks at the start and end of a range. That buffer
 * must only be used under the interface lock: here the lock callbacks are
 * an error checking mutex, and under ThreadSanitizer (the native tsan CI
 * job) any use of the buffer outside the lock is reported as a data race.
 *
 * Each thread writes files at unaligned offsets in pieces of odd sizes (so
 * that most writes and reads start or end inside a physical block), reads
 * them back, renames and removes them, and checks every byte against what
 * it wrote. At the end both filesystems are compared once more after a
 * remount, and e2fsck -fn must find nothing to fix on either partition.
 */

#include "test_util.h"
#include "mt_util.h"

#include <ext4_blockdev.h>

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PARTS 2
#define THREADS_PER_PART 3
#define THREADS (PARTS * THREADS_PER_PART)
#define ITERATIONS 120
#define FILES 3
#define MAX_FILE 30000
#define PH_BSIZE 512
#define PART_SIZE (8ull << 20)
#define STALL_SECONDS 120
#define SEED 0x70617274696f6e73ull

static int fd = -1;

/*********************** one interface, two partitions **********************/

MT_LOCK(dev)
MT_LOCK(mp0)
MT_LOCK(mp1)

static bool check_io_lock;

static int dev_open(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int dev_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int dev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		     uint32_t blk_cnt)
{
	size_t len = (size_t)blk_cnt * PH_BSIZE;

	(void)bdev;
	if (check_io_lock && dev_depth != 1)
		mt_fail("block device read without the interface lock");
	if (pread(fd, buf, len, (off_t)(blk_id * PH_BSIZE)) != (ssize_t)len)
		return EIO;
	return EOK;
}

static int dev_bwrite(struct ext4_blockdev *bdev, const void *buf,
		      uint64_t blk_id, uint32_t blk_cnt)
{
	size_t len = (size_t)blk_cnt * PH_BSIZE;

	(void)bdev;
	if (check_io_lock && dev_depth != 1)
		mt_fail("block device write without the interface lock");
	if (pwrite(fd, buf, len, (off_t)(blk_id * PH_BSIZE)) != (ssize_t)len)
		return EIO;
	return EOK;
}

static int dev_lock_cb(struct ext4_blockdev *bdev)
{
	(void)bdev;
	dev_lock();
	return EOK;
}

static int dev_unlock_cb(struct ext4_blockdev *bdev)
{
	(void)bdev;
	dev_unlock();
	return EOK;
}

static uint8_t ph_bbuf[PH_BSIZE];

static struct ext4_blockdev_iface iface = {
	.open = dev_open,
	.bread = dev_bread,
	.bwrite = dev_bwrite,
	.close = dev_close,
	.lock = dev_lock_cb,
	.unlock = dev_unlock_cb,
	.ph_bsize = PH_BSIZE,
	.ph_bcnt = PARTS * PART_SIZE / PH_BSIZE,
	.ph_bbuf = ph_bbuf,
};

static struct ext4_blockdev parts[PARTS] = {
	{.bdif = &iface, .part_offset = 0, .part_size = PART_SIZE},
	{.bdif = &iface, .part_offset = PART_SIZE, .part_size = PART_SIZE},
};

static const char *const devs[PARTS] = {"part0", "part1"};
static const char *const mps[PARTS] = {"/p0/", "/p1/"};
static const struct ext4_lock mp_locks[PARTS] = {
	{.lock = mp0_lock, .unlock = mp0_unlock},
	{.lock = mp1_lock, .unlock = mp1_unlock},
};

/********************************** model **********************************/

struct file_model {
	bool exists;
	uint32_t size;
	uint8_t data[MAX_FILE];
};

struct worker {
	int id;   /* thread number */
	int part; /* partition it works on */
	uint64_t rng;
	struct file_model files[FILES];
	uint8_t buf[MAX_FILE];
};

static struct worker *workers[THREADS];

static uint32_t rnd_below(struct worker *w, uint32_t n)
{
	return mt_rnd_below(&w->rng, n);
}

static void file_path(char *p, size_t size, struct worker *w, int k,
		      bool renamed)
{
	snprintf(p, size, "%st%d/%s%d", mps[w->part], w->id,
		 renamed ? "r" : "f", k);
}

#define API_EQ(expected, call) MT_CHECK_EQ(expected, (call))

/* Write n bytes at the handle's position in pieces of odd sizes. */
static void write_pieces(struct worker *w, ext4_file *f, const uint8_t *p,
			 size_t n)
{
	while (n) {
		size_t c = 1 + rnd_below(w, 1500), wcnt = 0;

		if (c > n)
			c = n;
		API_EQ(EOK, ext4_fwrite(f, p, c, &wcnt));
		MT_CHECK_EQ(c, wcnt);
		p += c;
		n -= c;
	}
}

static void verify(struct worker *w, const char *path, struct file_model *m)
{
	ext4_file f;
	size_t done = 0, rcnt;

	API_EQ(EOK, ext4_fopen(&f, path, "rb"));
	MT_CHECK_EQ(m->size, ext4_fsize(&f));
	while (done < m->size) {
		size_t c = 1 + rnd_below(w, 1500);

		if (c > m->size - done)
			c = m->size - done;
		API_EQ(EOK, ext4_fread(&f, w->buf, c, &rcnt));
		MT_CHECK_EQ(c, rcnt);
		if (memcmp(w->buf, m->data + done, c))
			mt_fail("%s: bytes %zu..%zu differ", path, done,
				done + c);
		done += c;
	}
	API_EQ(EOK, ext4_fclose(&f));
}

static void one_op(struct worker *w)
{
	int k = (int)rnd_below(w, FILES);
	struct file_model *m = &w->files[k];
	char path[64], other[64];
	ext4_file f;

	file_path(path, sizeof(path), w, k, false);
	file_path(other, sizeof(other), w, k, true);
	if (!m->exists || rnd_below(w, 4) == 0) {
		/* (re)write the whole file */
		m->size = rnd_below(w, MAX_FILE + 1);
		mt_rnd_bytes(&w->rng, m->data, m->size);
		API_EQ(EOK, ext4_fopen(&f, path, "wb"));
		write_pieces(w, &f, m->data, m->size);
		API_EQ(EOK, ext4_fclose(&f));
		m->exists = true;
	} else if (rnd_below(w, 3) == 0) {
		/* overwrite a range at an unaligned offset */
		uint32_t off = rnd_below(w, m->size + 1);
		uint32_t n = rnd_below(w, MAX_FILE - off + 1);

		mt_rnd_bytes(&w->rng, m->data + off, n);
		API_EQ(EOK, ext4_fopen(&f, path, "r+b"));
		API_EQ(EOK, ext4_fseek(&f, off, SEEK_SET));
		write_pieces(w, &f, m->data + off, n);
		API_EQ(EOK, ext4_fclose(&f));
		if (off + n > m->size)
			m->size = off + n;
	} else if (rnd_below(w, 4) == 0) {
		/* rename away and back, or remove */
		API_EQ(EOK, ext4_frename(path, other));
		verify(w, other, m);
		if (rnd_below(w, 2)) {
			API_EQ(EOK, ext4_fremove(other));
			m->exists = false;
			return;
		}
		API_EQ(EOK, ext4_frename(other, path));
	}
	verify(w, path, m);
}

static void *worker_main(void *arg)
{
	struct worker *w = arg;

	for (int i = 0; i < ITERATIONS; i++) {
		one_op(w);
		mt_progress(false);
	}
	mt_progress(true);
	return NULL;
}

/********************************* main ************************************/

static void mount_all(bool journal)
{
	for (int p = 0; p < PARTS; p++) {
		TEST_ASSERT_EQ(EOK, ext4_mount(devs[p], mps[p], false));
		TEST_ASSERT_EQ(EOK, ext4_mount_setup_locks(mps[p], &mp_locks[p]));
		if (journal) {
			TEST_ASSERT_EQ(EOK, ext4_recover(mps[p]));
			TEST_ASSERT_EQ(EOK, ext4_journal_start(mps[p]));
		}
	}
}

static void umount_all(bool journal)
{
	for (int p = 0; p < PARTS; p++) {
		if (journal)
			TEST_ASSERT_EQ(EOK, ext4_journal_stop(mps[p]));
		TEST_ASSERT_EQ(EOK, ext4_umount(mps[p]));
	}
}

/* The partition as an image file of its own, for e2fsck -fn. */
static void fsck_part(const char *image, int p)
{
	char part[512], cmd[1200];

	snprintf(part, sizeof(part), "%s.p%d", image, p);
	snprintf(cmd, sizeof(cmd),
		 "dd if='%s' of='%s' bs=1M skip=%llu count=%llu status=none && "
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'",
		 image, part, (unsigned long long)(p * PART_SIZE >> 20),
		 (unsigned long long)(PART_SIZE >> 20), part);
	if (system(cmd) != 0)
		mt_fail("e2fsck -fn of partition %d failed", p);
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	pthread_t threads[THREADS];

	mt_mutex_init(&dev_mutex);
	mt_mutex_init(&mp0_mutex);
	mt_mutex_init(&mp1_mutex);

	fd = open(image, O_RDWR);
	TEST_ASSERT(fd >= 0);
	for (int p = 0; p < PARTS; p++)
		TEST_ASSERT_EQ(EOK, ext4_device_register(&parts[p], devs[p]));

	mount_all(true);
	for (int t = 0; t < THREADS; t++) {
		char dir[32];

		workers[t] = calloc(1, sizeof(*workers[t]));
		TEST_ASSERT(workers[t] != NULL);
		workers[t]->id = t;
		workers[t]->part = t % PARTS;
		workers[t]->rng = mt_seed(SEED, t);
		snprintf(dir, sizeof(dir), "%st%d", mps[t % PARTS], t);
		TEST_ASSERT_EQ(EOK, ext4_dir_mk(dir));
	}

	check_io_lock = true;
	for (int t = 0; t < THREADS; t++)
		TEST_ASSERT_EQ(0, pthread_create(&threads[t], NULL,
						 worker_main, workers[t]));
	mt_wait(THREADS, STALL_SECONDS);
	for (int t = 0; t < THREADS; t++)
		TEST_ASSERT_EQ(0, pthread_join(threads[t], NULL));
	check_io_lock = false;
	umount_all(true);

	/* Everything once more after a remount */
	mount_all(false);
	for (int t = 0; t < THREADS; t++) {
		struct worker *w = workers[t];

		for (int k = 0; k < FILES; k++) {
			char path[64];

			file_path(path, sizeof(path), w, k, false);
			if (w->files[k].exists)
				verify(w, path, &w->files[k]);
			else
				API_EQ(ENOENT,
				       ext4_inode_exist(path, EXT4_DE_REG_FILE));
		}
	}
	umount_all(false);

	for (int p = 0; p < PARTS; p++)
		TEST_ASSERT_EQ(EOK, ext4_device_unregister(devs[p]));
	close(fd);

	for (int p = 0; p < PARTS; p++)
		fsck_part(image, p);
	for (int t = 0; t < THREADS; t++)
		free(workers[t]);
	return 0;
}
