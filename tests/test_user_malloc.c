/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * CONFIG_USE_USER_MALLOC=1 (fork issue #94): the library is built with the
 * application's allocator (test_user_malloc.cmake). The four functions must
 * be declared by ext4_types.h, so that this file only compiles if the
 * definitions below match them, and every allocation the library makes
 * must go through them: format, mount with the journal, write a file and
 * unmount a RAM disk, then check that allocations happened and that every
 * pointer freed came from ext4_user_malloc/calloc/realloc, with nothing left
 * allocated.
 */

#include <ext4.h>
#include <ext4_mkfs.h>
#include <ext4_types.h>

#include "test_util.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* The application's allocator: malloc with a header that marks the block */

#define MAGIC 0x6c776578u /* "lwex" */

struct hdr {
	uint32_t magic;
	size_t size;
	/* keep the payload aligned like malloc's */
	union {
		long double ld;
		void *p;
		uint64_t u;
	} align[];
};

static unsigned long n_malloc, n_calloc, n_realloc, n_free;
static long live;

void *ext4_user_malloc(size_t size)
{
	struct hdr *h = malloc(sizeof(*h) + size);

	if (!h)
		return NULL;
	h->magic = MAGIC;
	h->size = size;
	n_malloc++;
	live++;
	return h->align;
}

void *ext4_user_calloc(size_t nmemb, size_t size)
{
	void *p;

	TEST_ASSERT(!size || nmemb <= SIZE_MAX / size);
	p = ext4_user_malloc(nmemb * size);
	if (p) {
		memset(p, 0, nmemb * size);
		n_malloc--;
		n_calloc++;
	}
	return p;
}

static struct hdr *hdr_of(void *ptr)
{
	struct hdr *h = (struct hdr *)((uint8_t *)ptr - sizeof(struct hdr));

	TEST_ASSERT(h->magic == MAGIC);
	return h;
}

void *ext4_user_realloc(void *ptr, size_t size)
{
	struct hdr *h;

	if (!ptr)
		return ext4_user_malloc(size);
	h = realloc(hdr_of(ptr), sizeof(*h) + size);
	if (!h)
		return NULL;
	h->size = size;
	n_realloc++;
	return h->align;
}

void ext4_user_free(void *ptr)
{
	struct hdr *h;

	if (!ptr)
		return;
	h = hdr_of(ptr);
	h->magic = 0;
	free(h);
	n_free++;
	live--;
}

/* ------------------------------------------------------------------------ */
/* RAM disk */

#define RD_BSIZE 512u
#define RD_SIZE (4u * 1024u * 1024u)

static uint8_t rd[RD_SIZE];

static int rd_open(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

static int rd_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
		    uint32_t blk_cnt)
{
	(void)bdev;
	if ((blk_id + blk_cnt) * RD_BSIZE > RD_SIZE)
		return EIO;
	memcpy(buf, rd + blk_id * RD_BSIZE, blk_cnt * RD_BSIZE);
	return EOK;
}

static int rd_bwrite(struct ext4_blockdev *bdev, const void *buf,
		     uint64_t blk_id, uint32_t blk_cnt)
{
	(void)bdev;
	if ((blk_id + blk_cnt) * RD_BSIZE > RD_SIZE)
		return EIO;
	memcpy(rd + blk_id * RD_BSIZE, buf, blk_cnt * RD_BSIZE);
	return EOK;
}

static int rd_close(struct ext4_blockdev *bdev)
{
	(void)bdev;
	return EOK;
}

EXT4_BLOCKDEV_STATIC_INSTANCE(rd_dev, RD_BSIZE, RD_SIZE / RD_BSIZE, rd_open,
			      rd_bread, rd_bwrite, rd_close, 0, 0);

/* ------------------------------------------------------------------------ */

static struct ext4_fs fs;

int main(int argc, char **argv)
{
	struct ext4_mkfs_info info = {.block_size = 1024, .journal = true};
	static char data[20000];
	ext4_file f;
	size_t n;

	(void)argc;
	(void)argv;
	memset(data, 0xa5, sizeof(data));

	TEST_ASSERT_EQ(EOK, ext4_mkfs(&fs, &rd_dev, &info, F_SET_EXT4));
	TEST_ASSERT_EQ(EOK, ext4_device_register(&rd_dev, TEST_DEV));
	TEST_ASSERT_EQ(EOK, ext4_mount(TEST_DEV, TEST_MP, false));
	TEST_ASSERT_EQ(EOK, ext4_recover(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_journal_start(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_fopen(&f, TEST_MP "file", "wb"));
	TEST_ASSERT_EQ(EOK, ext4_fwrite(&f, data, sizeof(data), &n));
	TEST_ASSERT_EQ(sizeof(data), n);
	TEST_ASSERT_EQ(EOK, ext4_fclose(&f));
	TEST_ASSERT_EQ(EOK, ext4_journal_stop(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_umount(TEST_MP));
	TEST_ASSERT_EQ(EOK, ext4_device_unregister(TEST_DEV));

	printf("ext4_user_malloc %lu, calloc %lu, realloc %lu, free %lu\n",
	       n_malloc, n_calloc, n_realloc, n_free);
	/* mkfs, the block cache and the journal all allocate */
	TEST_ASSERT(n_malloc > 0);
	TEST_ASSERT(n_calloc > 0);
	TEST_ASSERT_EQ(n_malloc + n_calloc, n_free);
	TEST_ASSERT_EQ(0, live);
	return 0;
}
