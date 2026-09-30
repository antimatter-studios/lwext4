/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Partitions of one disk share its block device interface, and with it the
 * one physical block buffer (ph_bbuf) that ext4_block_writebytes() and
 * ext4_block_readbytes() use for parts of physical blocks. The interface
 * lock callbacks exist for exactly this multi partition case, but they
 * were taken around the read and around the write only: the buffer was
 * changed and copied out with the lock released, so a mount point on
 * another partition (another mount point lock) could read its own block
 * into the buffer in between and have it written over this block.
 *
 * The buffer here is a page that is only accessible while the interface
 * is locked (mprotect in the lock callbacks), so every use outside the
 * lock faults. Two partitions of one image are mounted and small files
 * are written and read at unaligned offsets on both, then read back after
 * a remount.
 */

#include "test_util.h"

#include <ext4_blockdev.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PH_BSIZE 512
#define PART_SIZE (4ull << 20)

static int fd = -1;
static uint8_t *ph_bbuf;
static size_t page;
static bool locked;

static void segv(int sig, siginfo_t *si, void *ctx)
{
	static const char msg[] = "physical block buffer used without the "
				  "block device lock\n";
	uint8_t *addr = si->si_addr;

	(void)ctx;
	if (addr >= ph_bbuf && addr < ph_bbuf + page) {
		(void)!write(2, msg, sizeof(msg) - 1);
		_exit(1);
	}
	signal(sig, SIG_DFL);
}

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
	TEST_ASSERT(locked);
	if (pread(fd, buf, len, (off_t)(blk_id * PH_BSIZE)) != (ssize_t)len)
		return EIO;
	return EOK;
}

static int dev_bwrite(struct ext4_blockdev *bdev, const void *buf,
		      uint64_t blk_id, uint32_t blk_cnt)
{
	size_t len = (size_t)blk_cnt * PH_BSIZE;

	(void)bdev;
	TEST_ASSERT(locked);
	if (pwrite(fd, buf, len, (off_t)(blk_id * PH_BSIZE)) != (ssize_t)len)
		return EIO;
	return EOK;
}

static int dev_lock(struct ext4_blockdev *bdev)
{
	(void)bdev;
	TEST_ASSERT(!locked);
	TEST_ASSERT_EQ(0, mprotect(ph_bbuf, page, PROT_READ | PROT_WRITE));
	locked = true;
	return EOK;
}

static int dev_unlock(struct ext4_blockdev *bdev)
{
	(void)bdev;
	TEST_ASSERT(locked);
	locked = false;
	TEST_ASSERT_EQ(0, mprotect(ph_bbuf, page, PROT_NONE));
	return EOK;
}

static struct ext4_blockdev_iface iface = {
	.open = dev_open,
	.bread = dev_bread,
	.bwrite = dev_bwrite,
	.close = dev_close,
	.lock = dev_lock,
	.unlock = dev_unlock,
	.ph_bsize = PH_BSIZE,
	.ph_bcnt = 2 * PART_SIZE / PH_BSIZE,
};

static struct ext4_blockdev parts[2] = {
	{.bdif = &iface, .part_offset = 0, .part_size = PART_SIZE},
	{.bdif = &iface, .part_offset = PART_SIZE, .part_size = PART_SIZE},
};

static const char *const devs[2] = {"part0", "part1"};
static const char *const mps[2] = {"/a/", "/b/"};

static uint8_t pattern(int part, size_t off)
{
	return (uint8_t)(off * 7 + part * 101 + 1);
}

static void mount_all(void)
{
	for (int i = 0; i < 2; i++)
		TEST_ASSERT_EQ(EOK, ext4_mount(devs[i], mps[i], false));
}

static void umount_all(void)
{
	for (int i = 0; i < 2; i++)
		TEST_ASSERT_EQ(EOK, ext4_umount(mps[i]));
}

static void path_of(char *path, size_t size, int part)
{
	snprintf(path, size, "%sfile", mps[part]);
}

/* Write the file in pieces of odd sizes, so that most of them start and
 * end inside a physical block. */
static void write_file(int part, size_t size)
{
	uint8_t buf[700];
	char path[32];
	ext4_file f;
	size_t off = 0, n = 1, wcnt;

	path_of(path, sizeof(path), part);
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "wb"));
	while (off < size) {
		if (n > size - off)
			n = size - off;
		for (size_t i = 0; i < n; i++)
			buf[i] = pattern(part, off + i);
		TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, buf, n, &wcnt));
		TEST_ASSERT_EQ(n, wcnt);
		off += n;
		n = (n * 5 + 3) % sizeof(buf) + 1;
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

static void check_file(int part, size_t size)
{
	uint8_t buf[700];
	char path[32];
	ext4_file f;
	size_t off = 0, n = 3, rcnt;

	path_of(path, sizeof(path), part);
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, path, "rb"));
	TEST_ASSERT_EQ(size, ext4_fsize(&f));
	while (off < size) {
		if (n > size - off)
			n = size - off;
		TEST_ASSERT_EQ(EOK, ext4_fread(&f, buf, n, &rcnt));
		TEST_ASSERT_EQ(n, rcnt);
		for (size_t i = 0; i < n; i++)
			TEST_ASSERT_EQ(pattern(part, off + i), buf[i]);
		off += n;
		n = (n * 3 + 1) % sizeof(buf) + 1;
	}
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
}

int main(int argc, char **argv)
{
	const char *image = test_image_arg(argc, argv);
	struct sigaction sa;
	const size_t size = 20000;

	page = (size_t)sysconf(_SC_PAGESIZE);
	ph_bbuf = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		       0);
	TEST_ASSERT(ph_bbuf != MAP_FAILED);
	iface.ph_bbuf = ph_bbuf;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv;
	sa.sa_flags = SA_SIGINFO;
	TEST_ASSERT_EQ(0, sigaction(SIGSEGV, &sa, NULL));

	fd = open(image, O_RDWR);
	TEST_ASSERT(fd >= 0);
	for (int i = 0; i < 2; i++)
		TEST_ASSERT_EQ(EOK, ext4_device_register(&parts[i], devs[i]));

	mount_all();
	for (int i = 0; i < 2; i++)
		write_file(i, size);
	for (int i = 0; i < 2; i++)
		check_file(i, size);
	umount_all();

	mount_all();
	for (int i = 0; i < 2; i++)
		check_file(i, size);
	umount_all();

	TEST_ASSERT(!locked);
	for (int i = 0; i < 2; i++)
		TEST_ASSERT_EQ(EOK, ext4_device_unregister(devs[i]));
	close(fd);
	return 0;
}
