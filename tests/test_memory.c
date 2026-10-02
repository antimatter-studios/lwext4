/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Memory use of lwext4 (fork issue #95): how much heap each workload
 * needs, how often it allocates, whether it leaks, and how much a simple
 * microcontroller allocator loses to fragmentation.
 *
 * The library is built with CONFIG_USE_USER_MALLOC=1 (test_memory.cmake);
 * ext4_user_malloc and friends below record every allocation and free in a
 * trace (common/mem_sim.h). Each workload runs on a RAM disk formatted as
 * ext4 with a journal, once with 1 KiB and once with 4 KiB blocks (the
 * block cache holds blocks, so its size scales with the block size),
 * starting from the same freshly formatted image, and unmounts at the
 * end. For each one the test reports:
 *
 *   allocs     allocations (calls to malloc/calloc/realloc)
 *   peak       peak of the bytes lwext4 holds at once
 *   largest    largest single allocation
 *   leak       bytes still allocated after unmount (must be 0)
 *   ff-min     smallest area in which a first-fit allocator with 8 byte
 *   bf-min     headers (best-fit) runs the whole trace: the RAM a
 *              microcontroller heap needs for the workload
 *   ff/peak    ff-min relative to the peak plus headers: the memory lost
 *              to fragmentation. churn-1 and churn-20 run the same mixed
 *              workload for 1 and 20 rounds; if ff-min grows with the
 *              rounds, fragmentation accumulates over time.
 *
 * The test fails if a workload leaks, or if its peak or ff-min exceeds the
 * ceiling in test_memory_budget.h. Ceilings are measured on a 64-bit
 * (LP64) host, where lwext4's structures are largest, with the block cache
 * size of the hosted builds; other cache sizes only report. Like
 * ci/coverage-floor, ceilings may only be lowered.
 *
 * red-green: guard (a measurement, not the test of a fix: it passes on any
 * base that builds with CONFIG_USE_USER_MALLOC=1)
 */

#include <ext4.h>
#include <ext4_mkfs.h>
#include <ext4_types.h>

#include "mem_sim.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Recording allocator */

struct hdr {
	uint32_t id;
	uint32_t size;
	union {
		long double ld;
		void *p;
		uint64_t u;
	} align[];
};

static struct mem_trace trace;
static size_t live_bytes, peak_bytes, largest;
static long live_blocks;
static unsigned long n_allocs;

static void record_alloc(struct hdr *h, size_t size)
{
	h->id = mem_trace_alloc(&trace, size);
	h->size = (uint32_t)size;
	live_bytes += size;
	live_blocks++;
	n_allocs++;
	if (live_bytes > peak_bytes)
		peak_bytes = live_bytes;
	if (size > largest)
		largest = size;
}

static void record_free(struct hdr *h)
{
	mem_trace_release(&trace, h->id);
	live_bytes -= h->size;
	live_blocks--;
}

void *ext4_user_malloc(size_t size)
{
	struct hdr *h;

	TEST_ASSERT(size <= UINT32_MAX);
	h = malloc(sizeof(*h) + size);
	if (!h)
		return NULL;
	record_alloc(h, size);
	return h->align;
}

void *ext4_user_calloc(size_t nmemb, size_t size)
{
	void *p;

	TEST_ASSERT(!size || nmemb <= SIZE_MAX / size);
	p = ext4_user_malloc(nmemb * size);
	if (p)
		memset(p, 0, nmemb * size);
	return p;
}

static struct hdr *hdr_of(void *ptr)
{
	return (struct hdr *)((uint8_t *)ptr - sizeof(struct hdr));
}

void ext4_user_free(void *ptr)
{
	struct hdr *h;

	if (!ptr)
		return;
	h = hdr_of(ptr);
	record_free(h);
	free(h);
}

/* A realloc is a new block and the release of the old one, as a simple
 * allocator that cannot grow in place does it. */
void *ext4_user_realloc(void *ptr, size_t size)
{
	void *p;
	struct hdr *old;

	if (!ptr)
		return ext4_user_malloc(size);
	p = ext4_user_malloc(size);
	if (!p)
		return NULL;
	old = hdr_of(ptr);
	memcpy(p, ptr, old->size < size ? old->size : size);
	ext4_user_free(ptr);
	return p;
}

/* ------------------------------------------------------------------------ */
/* RAM disk and its freshly formatted image */

#define RD_BSIZE 512u
#define RD_SIZE (16u * 1024u * 1024u)

static uint8_t rd[RD_SIZE], rd_formatted[RD_SIZE];

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
/* Workloads */

#define CHECK(expr) TEST_ASSERT_EQ(EOK, (expr))

static uint32_t rng_state;

static uint32_t rng(void)
{
	/* xorshift32: the same sequence on every platform */
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static char data[65536];

static void path_of(char *buf, size_t len, const char *dir, unsigned i)
{
	snprintf(buf, len, TEST_MP "%s/file-%05u", dir, i);
}

static void write_file(const char *path, size_t size, const char *mode)
{
	ext4_file f;
	size_t done = 0, n;

	CHECK(ext4_fopen(&f, path, mode));
	while (done < size) {
		size_t chunk = size - done < sizeof(data) ? size - done
							  : sizeof(data);

		CHECK(ext4_fwrite(&f, data, chunk, &n));
		TEST_ASSERT_EQ(chunk, n);
		done += chunk;
	}
	CHECK(ext4_fclose(&f));
}

static void mount_fs(void)
{
	CHECK(ext4_device_register(&rd_dev, TEST_DEV));
	CHECK(ext4_mount(TEST_DEV, TEST_MP, false));
	CHECK(ext4_recover(TEST_MP));
	CHECK(ext4_journal_start(TEST_MP));
}

static void umount_fs(void)
{
	CHECK(ext4_journal_stop(TEST_MP));
	CHECK(ext4_umount(TEST_MP));
	CHECK(ext4_device_unregister(TEST_DEV));
}

static uint32_t block_size;

static void w_mkfs(void)
{
	static struct ext4_fs fs;
	/* inodes for the 1500 files of the directory workload */
	struct ext4_mkfs_info info = {
		.block_size = block_size, .journal = true, .inodes = 4096};

	CHECK(ext4_mkfs(&fs, &rd_dev, &info, F_SET_EXT4));
}

static void w_mount(void)
{
	mount_fs();
	umount_fs();
}

static void w_small_files(void)
{
	char path[64];
	unsigned i;

	mount_fs();
	CHECK(ext4_dir_mk(TEST_MP "small"));
	for (i = 0; i < 300; i++) {
		path_of(path, sizeof(path), "small", i);
		write_file(path, rng() % 6000, "wb");
	}
	for (i = 0; i < 300; i += 2) {
		path_of(path, sizeof(path), "small", i);
		CHECK(ext4_fremove(path));
	}
	umount_fs();
}

static void w_large_file(void)
{
	ext4_file f;
	size_t n, done;

	mount_fs();
	write_file(TEST_MP "large", 3u * 1024u * 1024u, "wb");
	CHECK(ext4_fopen(&f, TEST_MP "large", "rb"));
	for (done = 0; done < 3u * 1024u * 1024u; done += n) {
		CHECK(ext4_fread(&f, data, sizeof(data), &n));
		TEST_ASSERT(n > 0);
	}
	CHECK(ext4_fclose(&f));
	CHECK(ext4_fopen(&f, TEST_MP "large", "r+b"));
	CHECK(ext4_ftruncate(&f, 65536));
	CHECK(ext4_fclose(&f));
	CHECK(ext4_fremove(TEST_MP "large"));
	umount_fs();
}

static void w_directory(void)
{
	char path[64];
	ext4_dir d;
	unsigned i, entries = 0;

	mount_fs();
	CHECK(ext4_dir_mk(TEST_MP "big"));
	for (i = 0; i < 1500; i++) {
		path_of(path, sizeof(path), "big", i);
		write_file(path, 0, "wb");
	}
	CHECK(ext4_dir_open(&d, TEST_MP "big"));
	while (ext4_dir_entry_next(&d))
		entries++;
	CHECK(ext4_dir_close(&d));
	TEST_ASSERT_EQ(1500 + 2, entries);
	for (i = 0; i < 1500; i++) {
		path_of(path, sizeof(path), "big", i);
		CHECK(ext4_fremove(path));
	}
	CHECK(ext4_dir_rm(TEST_MP "big"));
	umount_fs();
}

static void w_xattr(void)
{
	char path[64], name[16], list[256];
	static char buf[512];
	unsigned i, j;
	size_t n;

	mount_fs();
	CHECK(ext4_dir_mk(TEST_MP "xattr"));
	for (i = 0; i < 64; i++) {
		path_of(path, sizeof(path), "xattr", i);
		write_file(path, 100, "wb");
		for (j = 0; j < 4; j++) {
			snprintf(name, sizeof(name), "user.a%u", j);
			CHECK(ext4_setxattr(path, name, strlen(name), data,
					    16 + (rng() % 200)));
		}
		CHECK(ext4_getxattr(path, "user.a1", 7, buf, sizeof(buf), &n));
		CHECK(ext4_listxattr(path, list, sizeof(list), &n));
		CHECK(ext4_removexattr(path, "user.a0", 7));
		CHECK(ext4_removexattr(path, "user.a2", 7));
	}
	umount_fs();
}

/* A mixed workload: files created, appended, renamed, truncated and
 * deleted, a fifth of them kept, round after round. */
static void churn(unsigned rounds)
{
	char path[64], path2[64];
	unsigned r, i, next = 0;
	ext4_file f;

	mount_fs();
	CHECK(ext4_dir_mk(TEST_MP "churn"));
	for (r = 0; r < rounds; r++) {
		unsigned first = next;

		for (i = 0; i < 40; i++, next++) {
			path_of(path, sizeof(path), "churn", next);
			write_file(path, rng() % 20000, "wb");
		}
		write_file(TEST_MP "churn/log", 1000 + rng() % 3000, "ab");
		for (i = first; i < next; i++) {
			path_of(path, sizeof(path), "churn", i);
			switch (i % 5) {
			case 0: /* kept */
				break;
			case 1:
				path_of(path2, sizeof(path2), "churn",
					100000 + i);
				CHECK(ext4_frename(path, path2));
				CHECK(ext4_fremove(path2));
				break;
			case 2:
				CHECK(ext4_fopen(&f, path, "r+b"));
				CHECK(ext4_ftruncate(&f, 100));
				CHECK(ext4_fclose(&f));
				CHECK(ext4_fremove(path));
				break;
			default:
				CHECK(ext4_fremove(path));
				break;
			}
		}
	}
	umount_fs();
}

static void w_churn_1(void)
{
	churn(1);
}

static void w_churn_20(void)
{
	churn(20);
}

/* ------------------------------------------------------------------------ */
/* Ceilings and report */

struct budget {
	const char *name;
	size_t peak;
	size_t first_fit;
};

#include "test_memory_budget.h"

static const struct workload {
	const char *name;
	void (*run)(void);
} workloads[] = {
	{"mkfs", w_mkfs},
	{"mount", w_mount},
	{"small-files", w_small_files},
	{"large-file", w_large_file},
	{"directory", w_directory},
	{"xattr", w_xattr},
	{"churn-1", w_churn_1},
	{"churn-20", w_churn_20},
};

static const struct budget *budget_of(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(memory_budget) / sizeof(memory_budget[0]); i++)
		if (!strcmp(memory_budget[i].name, name))
			return &memory_budget[i];
	return NULL;
}

int main(int argc, char **argv)
{
	int enforce = CONFIG_BLOCK_DEV_CACHE_SIZE == MEMORY_BUDGET_CACHE_SIZE;
	int failed = 0;
	size_t g, w;

	(void)argc;
	(void)argv;
	memset(data, 0x5a, sizeof(data));

	printf("lwext4 memory use: ext4 with a journal, %d block cache "
	       "buffers, %u-bit pointers\n",
	       CONFIG_BLOCK_DEV_CACHE_SIZE, (unsigned)(sizeof(void *) * 8));
	if (!enforce)
		printf("(ceilings are for %d cache buffers: report only)\n",
		       MEMORY_BUDGET_CACHE_SIZE);
	printf("%-15s %8s %9s %8s %6s %9s %9s %8s\n", "workload", "allocs",
	       "peak", "largest", "leak", "ff-min", "bf-min", "ff/peak");

	for (g = 0; g < 2; g++)
	for (w = 0; w < sizeof(workloads) / sizeof(workloads[0]); w++) {
		const struct workload *wl = &workloads[w];
		const struct budget *b;
		size_t ff, bf, sim_peak;
		char name[32];

		block_size = g ? 4096 : 1024;
		snprintf(name, sizeof(name), "%s/%uk", wl->name,
			 (unsigned)(block_size / 1024));
		b = budget_of(name);

		if (w > 0) /* every workload starts from the formatted image */
			memcpy(rd, rd_formatted, RD_SIZE);
		rng_state = 0x2545f491u;
		mem_trace_init(&trace);
		live_bytes = peak_bytes = largest = 0;
		live_blocks = 0;
		n_allocs = 0;

		wl->run();
		if (w == 0)
			memcpy(rd_formatted, rd, RD_SIZE);

		sim_peak = mem_sim_peak(&trace);
		ff = mem_sim_min_arena(&trace, MEM_FIRST_FIT, 64);
		bf = mem_sim_min_arena(&trace, MEM_BEST_FIT, 64);
		printf("%-15s %8lu %9lu %8lu %6lu %9lu %9lu %7.2fx\n",
		       name, n_allocs, (unsigned long)peak_bytes,
		       (unsigned long)largest, (unsigned long)live_bytes,
		       (unsigned long)ff, (unsigned long)bf,
		       sim_peak ? (double)ff / (double)sim_peak : 0.0);

		if (live_bytes || live_blocks) {
			printf("FAIL %s: %lu bytes in %ld blocks still allocated "
			       "after unmount\n",
			       name, (unsigned long)live_bytes, live_blocks);
			failed = 1;
		}
		if (!b) {
			printf("FAIL %s: no ceiling in test_memory_budget.h\n",
			       name);
			failed = 1;
		} else if (enforce) {
			if (peak_bytes > b->peak) {
				printf("FAIL %s: peak %lu > ceiling %lu\n",
				       name, (unsigned long)peak_bytes,
				       (unsigned long)b->peak);
				failed = 1;
			}
			if (ff > b->first_fit) {
				printf("FAIL %s: ff-min %lu > ceiling %lu\n",
				       name, (unsigned long)ff,
				       (unsigned long)b->first_fit);
				failed = 1;
			}
		}
		mem_trace_free(&trace);
	}
	return failed;
}
