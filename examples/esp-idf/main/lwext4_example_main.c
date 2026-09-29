/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * lwext4 example for ESP-IDF, doubling as the on-target test.
 *
 * Runs on a real board or in Espressif's QEMU, through the regular ESP-IDF
 * storage drivers (see ports/esp-idf/lwext4/include/ext4_esp.h):
 *
 *  - SPI flash: data partitions "ext4host" (ext4 made by mke2fs on the build
 *    host and flashed together with the app) and "ext4dev" (formatted here
 *    with ext4_mkfs), accessed with esp_partition_read/erase/write.
 *  - SD card (optional, menuconfig -> lwext4 example): on the SDMMC host
 *    controller or on a SPI bus (SD card module). MBR partition 1 is made by
 *    mke2fs on a PC (host/mkimage.sh sd), partition 2 is formatted here;
 *    accessed with sdmmc_read/write_sectors.
 *
 * Every reset advances one step, recorded by marker files on the host-made
 * filesystem:
 *   boot 1: verify the host-made filesystems and write to them, format the
 *           other ones with ext4_mkfs and fill them
 *   boot 2: everything survived the reset; read files the host may have
 *           added in between (fromhost.bin); truncate a file, remove a
 *           directory
 *   boot 3+: the final state is stable (read only)
 *
 * The result is printed on the console UART as a single line:
 *     LWEXT4-TEST: PASS (boot N)
 *     LWEXT4-TEST: FAIL: <reason>
 * after which the firmware idles. Re-flash (idf.py flash) to start over.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_EXAMPLE_SD_SDMMC
#include "driver/sdmmc_host.h"
#elif CONFIG_EXAMPLE_SD_SDSPI
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#endif
#include "sdmmc_cmd.h"

#include <ext4.h>
#include <ext4_mbr.h>
#include <ext4_mkfs.h>

#include "ext4_esp.h"

/* -------------------------------------------------------------- helpers */

static void halt(void)
{
	fflush(stdout);
	for (;;)
		vTaskDelay(portMAX_DELAY);
}

#define FAILF(fmt, ...)                                                        \
	do {                                                                   \
		printf("LWEXT4-TEST: FAIL: %s:%d: " fmt "\n", __FILE__,        \
		       __LINE__, ##__VA_ARGS__);                               \
		halt();                                                        \
	} while (0)

#define CHECK(expr)                                                            \
	do {                                                                   \
		int r_ = (expr);                                               \
		if (r_ != EOK)                                                 \
			FAILF("%s = %d", #expr, r_);                           \
	} while (0)

#define ASSERT(cond)                                                           \
	do {                                                                   \
		if (!(cond))                                                   \
			FAILF("%s", #cond);                                    \
	} while (0)

/* Must match host/pattern.py. */
static inline uint8_t pattern(uint32_t seed, uint32_t off)
{
	return (uint8_t)(off * 7u + (off >> 9) + seed * 31u);
}

/* Host side constants, see host/mkimage.sh and host/run-qemu-test.sh. */
#define HOST_HELLO "Hello from mke2fs on the build host\n"
#define HOST_PATTERN_SEED 2
#define HOST_PATTERN_SIZE 300000u
#define HOST_MANY_FILES 40
#define HOST_INJECT_SEED 3
#define HOST_INJECT_SIZE 50000u
#define DEV_SEED 1
#define TRUNC_SIZE 100000u
#define SUBDIRS 20
#define DEV_MANY_FILES 100

static uint8_t wbuf[4096];
static uint8_t rbuf[4096];
static char path[128];

static const char *mkpath(const char *mp, const char *rel)
{
	snprintf(path, sizeof(path), "%s%s", mp, rel);
	return path;
}

static void write_pattern(const char *p, uint32_t seed, uint32_t size)
{
	ext4_file f;
	size_t n;
	uint32_t off = 0, i = 0;

	CHECK(ext4_fopen(&f, p, "wb"));
	while (off < size) {
		/* Odd chunk sizes: exercise partial block writes. */
		uint32_t len = 1000u + (i++ * 397u) % 3000u;
		if (len > size - off)
			len = size - off;
		for (uint32_t k = 0; k < len; k++)
			wbuf[k] = pattern(seed, off + k);
		CHECK(ext4_fwrite(&f, wbuf, len, &n));
		ASSERT(n == len);
		off += len;
	}
	ASSERT(ext4_fsize(&f) == size);
	CHECK(ext4_fclose(&f));
}

static void verify_pattern(const char *p, uint32_t seed, uint32_t size)
{
	ext4_file f;
	size_t n;
	uint32_t off = 0;

	CHECK(ext4_fopen(&f, p, "rb"));
	if (ext4_fsize(&f) != size)
		FAILF("%s: size %llu, expected %u", p,
		      (unsigned long long)ext4_fsize(&f), (unsigned)size);
	while (off < size) {
		CHECK(ext4_fread(&f, rbuf, sizeof(rbuf), &n));
		ASSERT(n > 0);
		for (uint32_t k = 0; k < n; k++)
			if (rbuf[k] != pattern(seed, off + k))
				FAILF("%s: mismatch at %u", p,
				      (unsigned)(off + k));
		off += n;
	}
	ASSERT(off == size);

	/* Unaligned seeks and short reads. */
	for (uint32_t pos = 1027; pos + 13 < size; pos += size / 7) {
		CHECK(ext4_fseek(&f, pos, SEEK_SET));
		CHECK(ext4_fread(&f, rbuf, 13, &n));
		ASSERT(n == 13);
		for (uint32_t k = 0; k < 13; k++)
			ASSERT(rbuf[k] == pattern(seed, pos + k));
	}
	CHECK(ext4_fclose(&f));
}

static void write_string(const char *p, const char *s)
{
	ext4_file f;
	size_t n;

	CHECK(ext4_fopen(&f, p, "wb"));
	CHECK(ext4_fwrite(&f, s, strlen(s), &n));
	ASSERT(n == strlen(s));
	CHECK(ext4_fclose(&f));
}

static void verify_string(const char *p, const char *s)
{
	ext4_file f;
	size_t n;
	char buf[64];

	CHECK(ext4_fopen(&f, p, "rb"));
	CHECK(ext4_fread(&f, buf, sizeof(buf), &n));
	CHECK(ext4_fclose(&f));
	if (n != strlen(s) || memcmp(buf, s, n) != 0)
		FAILF("%s: unexpected content (%u bytes)", p, (unsigned)n);
}

static bool exists(const char *p, int type)
{
	return ext4_inode_exist(p, type) == EOK;
}

static int count_entries(const char *p)
{
	ext4_dir d;
	int n = 0;

	CHECK(ext4_dir_open(&d, p));
	while (ext4_dir_entry_next(&d) != NULL)
		n++;
	CHECK(ext4_dir_close(&d));
	return n;
}

/* ---------------------------------------------------- mount/umount */

static void mount(struct ext4_blockdev *bd, const char *dev, const char *mp,
		  bool write_back)
{
	CHECK(ext4_device_register(bd, dev));
	CHECK(ext4_mount(dev, mp, false));
	CHECK(ext4_mount_setup_locks(mp, ext4_esp_mount_locks()));
	int r = ext4_recover(mp);
	if (r != EOK && r != ENOTSUP)
		FAILF("ext4_recover(%s) = %d", mp, r);
	CHECK(ext4_journal_start(mp));
	if (write_back)
		CHECK(ext4_cache_write_back(mp, true));
}

static void umount(const char *dev, const char *mp, bool write_back)
{
	if (write_back)
		CHECK(ext4_cache_write_back(mp, false));
	CHECK(ext4_journal_stop(mp));
	CHECK(ext4_umount(mp));
	CHECK(ext4_device_unregister(dev));
}

/* ----------------------------------------------------------- the test */

struct medium {
	const char *name;
	struct ext4_blockdev *host_bd; /* mke2fs made on the host */
	struct ext4_blockdev *dev_bd;  /* formatted by the test */
	uint32_t mkfs_block_size;
	uint32_t big_size;             /* size of /device/data.bin */
	bool write_back;               /* use the write back cache */
};

/* Files the host put there: see host/mkimage.sh. */
static void host_tree_verify(const char *mp)
{
	char name[16];
	char target[32];
	size_t n;
	bool seen[HOST_MANY_FILES] = { false };
	ext4_dir d;
	const ext4_direntry *de;

	verify_string(mkpath(mp, "hello.txt"), HOST_HELLO);
	verify_string(mkpath(mp, "host/a/b/c/deep.txt"), "deep\n");
	verify_pattern(mkpath(mp, "host/pattern.bin"), HOST_PATTERN_SEED,
		       HOST_PATTERN_SIZE);

	CHECK(ext4_dir_open(&d, mkpath(mp, "host/many")));
	int entries = 0;
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		unsigned idx;
		entries++;
		if (de->name_length == 4 && de->name[0] == 'f' &&
		    sscanf((const char *)de->name + 1, "%3u", &idx) == 1 &&
		    idx < HOST_MANY_FILES)
			seen[idx] = true;
	}
	CHECK(ext4_dir_close(&d));
	ASSERT(entries == HOST_MANY_FILES + 2);
	for (int i = 0; i < HOST_MANY_FILES; i++)
		ASSERT(seen[i]);
	snprintf(name, sizeof(name), "file %03d\n", 17);
	verify_string(mkpath(mp, "host/many/f017"), name);

	CHECK(ext4_readlink(mkpath(mp, "host/link"), target,
			    sizeof(target) - 1, &n));
	target[n] = 0;
	ASSERT(strcmp(target, "../hello.txt") == 0);
}

/* Boot 1: create /device on a mounted filesystem. */
static void device_tree_create(const struct medium *m, const char *mp,
			       int many)
{
	char p[64];

	CHECK(ext4_dir_mk(mkpath(mp, "device")));
	for (int i = 0; i < SUBDIRS; i++) {
		snprintf(p, sizeof(p), "%sdevice/sub%02d", mp, i);
		CHECK(ext4_dir_mk(p));
	}
	write_pattern(mkpath(mp, "device/data.bin"), DEV_SEED, m->big_size);
	verify_pattern(mkpath(mp, "device/data.bin"), DEV_SEED, m->big_size);

	write_pattern(mkpath(mp, "device/scratch.bin"), 9, 64 * 1024);
	CHECK(ext4_fremove(mkpath(mp, "device/scratch.bin")));
	ASSERT(!exists(mkpath(mp, "device/scratch.bin"), EXT4_DE_REG_FILE));

	write_string(mkpath(mp, "device/note.txt"), m->name);
	snprintf(p, sizeof(p), "%sdevice/renamed.txt", mp);
	CHECK(ext4_frename(mkpath(mp, "device/note.txt"), p));

	if (many) {
		/* Enough entries to need an htree indexed directory. */
		CHECK(ext4_dir_mk(mkpath(mp, "device/many")));
		for (int i = 0; i < many; i++) {
			snprintf(p, sizeof(p), "%sdevice/many/file-with-a-long"
				 "ish-name-%03d", mp, i);
			write_string(p, "x");
		}
	}
}

static void device_tree_verify(const struct medium *m, const char *mp,
			       int many, uint32_t data_size, int subdirs)
{
	ASSERT(count_entries(mkpath(mp, "device")) ==
	       2 + subdirs + 2 /* data.bin renamed.txt */ + (many ? 1 : 0) +
		       /* boot markers */
		       (exists(mkpath(mp, "device/boot1.done"),
			       EXT4_DE_REG_FILE) ? 1 : 0) +
		       (exists(mkpath(mp, "device/boot2.done"),
			       EXT4_DE_REG_FILE) ? 1 : 0));
	verify_pattern(mkpath(mp, "device/data.bin"), DEV_SEED, data_size);
	verify_string(mkpath(mp, "device/renamed.txt"), m->name);
	ASSERT(!exists(mkpath(mp, "device/note.txt"), EXT4_DE_REG_FILE));
	if (many)
		ASSERT(count_entries(mkpath(mp, "device/many")) == many + 2);
}

/* Truncate data.bin and drop a directory: boot 2 modification. */
static void device_tree_modify(const char *mp)
{
	ext4_file f;

	CHECK(ext4_fopen(&f, mkpath(mp, "device/data.bin"), "r+b"));
	CHECK(ext4_ftruncate(&f, TRUNC_SIZE));
	CHECK(ext4_fclose(&f));
	CHECK(ext4_dir_rm(mkpath(mp, "device/sub05")));
	ASSERT(!exists(mkpath(mp, "device/sub05"), EXT4_DE_DIR));
}

#define HOST_DEV "hostfs"
#define HOST_MP "/host/"
#define DEVF_DEV "devfs"
#define DEVF_MP "/dev/"

static int64_t t_step;

static void step(const struct medium *m, const char *what)
{
	int64_t now = esp_timer_get_time();
	if (t_step)
		printf("[%s]   (%lld ms)\n", m->name,
		       (long long)((now - t_step) / 1000));
	t_step = now;
	if (what)
		printf("[%s] %s\n", m->name, what);
}

/* An optional file the host adds between boots (host/run_qemu_test.py). */
static void verify_fromhost(const struct medium *m, const char *mp)
{
	if (!exists(mkpath(mp, "fromhost.bin"), EXT4_DE_REG_FILE)) {
		printf("[%s] %sfromhost.bin: not present\n", m->name, mp);
		return;
	}
	verify_pattern(mkpath(mp, "fromhost.bin"), HOST_INJECT_SEED,
		       HOST_INJECT_SIZE);
	printf("[%s] %sfromhost.bin: verified\n", m->name, mp);
}

/* Boot 1: use the host-made filesystem, format and fill the other one. */
static void phase1(const struct medium *m)
{
	struct ext4_fs fs;
	struct ext4_mkfs_info info = {
		.block_size = m->mkfs_block_size,
		.journal = true,
		.label = "lwext4dev",
	};

	step(m, "host-made filesystem: verify, write");
	mount(m->host_bd, HOST_DEV, HOST_MP, m->write_back);
	host_tree_verify(HOST_MP);
	device_tree_create(m, HOST_MP, 0);
	umount(HOST_DEV, HOST_MP, m->write_back);

	step(m, "host-made filesystem: remount, verify");
	mount(m->host_bd, HOST_DEV, HOST_MP, m->write_back);
	host_tree_verify(HOST_MP);
	device_tree_verify(m, HOST_MP, 0, m->big_size, SUBDIRS);
	write_string(mkpath(HOST_MP, "device/boot1.done"), "1");
	umount(HOST_DEV, HOST_MP, m->write_back);

	step(m, "ext4_mkfs");
	printf("[%s] block size %u\n", m->name, (unsigned)m->mkfs_block_size);
	memset(&fs, 0, sizeof(fs));
	CHECK(ext4_mkfs(&fs, m->dev_bd, &info, F_SET_EXT4));

	step(m, "device-made filesystem: write");
	mount(m->dev_bd, DEVF_DEV, DEVF_MP, m->write_back);
	device_tree_create(m, DEVF_MP, DEV_MANY_FILES);
	umount(DEVF_DEV, DEVF_MP, m->write_back);

	step(m, "device-made filesystem: remount, verify");
	mount(m->dev_bd, DEVF_DEV, DEVF_MP, m->write_back);
	device_tree_verify(m, DEVF_MP, DEV_MANY_FILES, m->big_size, SUBDIRS);
	write_string(mkpath(DEVF_MP, "device/boot1.done"), "1");
	umount(DEVF_DEV, DEVF_MP, m->write_back);
	step(m, NULL);
}

/* Boot 2: everything survived the reset; modify once more. */
static void phase2(const struct medium *m)
{
	step(m, "host-made filesystem after reset: verify, modify");
	mount(m->host_bd, HOST_DEV, HOST_MP, m->write_back);
	host_tree_verify(HOST_MP);
	device_tree_verify(m, HOST_MP, 0, m->big_size, SUBDIRS);
	verify_fromhost(m, HOST_MP);
	device_tree_modify(HOST_MP);
	write_string(mkpath(HOST_MP, "device/boot2.done"), "2");
	umount(HOST_DEV, HOST_MP, m->write_back);

	step(m, "device-made filesystem after reset: verify, modify");
	mount(m->dev_bd, DEVF_DEV, DEVF_MP, m->write_back);
	device_tree_verify(m, DEVF_MP, DEV_MANY_FILES, m->big_size, SUBDIRS);
	verify_fromhost(m, DEVF_MP);
	device_tree_modify(DEVF_MP);
	write_string(mkpath(DEVF_MP, "device/boot2.done"), "2");
	umount(DEVF_DEV, DEVF_MP, m->write_back);
	step(m, NULL);
}

/* Boot 3 and later: the final state is stable. Read only. */
static void phase3(const struct medium *m)
{
	step(m, "final state: verify");
	mount(m->host_bd, HOST_DEV, HOST_MP, m->write_back);
	host_tree_verify(HOST_MP);
	device_tree_verify(m, HOST_MP, 0, TRUNC_SIZE, SUBDIRS - 1);
	umount(HOST_DEV, HOST_MP, m->write_back);

	mount(m->dev_bd, DEVF_DEV, DEVF_MP, m->write_back);
	device_tree_verify(m, DEVF_MP, DEV_MANY_FILES, TRUNC_SIZE,
			   SUBDIRS - 1);
	umount(DEVF_DEV, DEVF_MP, m->write_back);
	step(m, NULL);
}

static int detect_phase(const struct medium *m)
{
	int phase = 1;

	mount(m->host_bd, HOST_DEV, HOST_MP, false);
	if (exists(mkpath(HOST_MP, "device/boot2.done"), EXT4_DE_REG_FILE))
		phase = 3;
	else if (exists(mkpath(HOST_MP, "device/boot1.done"),
			EXT4_DE_REG_FILE))
		phase = 2;
	umount(HOST_DEV, HOST_MP, false);
	return phase;
}

static void run_medium(const struct medium *m, int phase)
{
	struct ext4_blockdev_iface *a = m->host_bd->bdif, *b = m->dev_bd->bdif;
	uint32_t r0 = a->bread_ctr + (a != b ? b->bread_ctr : 0);
	uint32_t w0 = a->bwrite_ctr + (a != b ? b->bwrite_ctr : 0);
	int64_t t0 = esp_timer_get_time();

	t_step = 0;
	if (phase == 1)
		phase1(m);
	else if (phase == 2)
		phase2(m);
	else
		phase3(m);

	printf("[%s] ok: %lld ms, %u device reads, %u device writes\n",
	       m->name, (long long)((esp_timer_get_time() - t0) / 1000),
	       (unsigned)(a->bread_ctr + (a != b ? b->bread_ctr : 0) - r0),
	       (unsigned)(a->bwrite_ctr + (a != b ? b->bwrite_ctr : 0) - w0));
}

/* ------------------------------------------------------------- storage */

static ext4_esp_blockdev_t flash_host, flash_dev;

static void flash_medium(struct medium *m)
{
	const esp_partition_t *ph, *pd;

	ph = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
				      ESP_PARTITION_SUBTYPE_ANY, "ext4host");
	pd = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
				      ESP_PARTITION_SUBTYPE_ANY, "ext4dev");
	ASSERT(ph && pd);
	printf("flash: ext4host @0x%lx %lu KiB, ext4dev @0x%lx %lu KiB\n",
	       (unsigned long)ph->address, (unsigned long)ph->size / 1024,
	       (unsigned long)pd->address, (unsigned long)pd->size / 1024);
	ESP_ERROR_CHECK(ext4_esp_blockdev_init_partition(&flash_host, ph));
	ESP_ERROR_CHECK(ext4_esp_blockdev_init_partition(&flash_dev, pd));

	/* Raw driver speed, for reference (emulated time under QEMU). */
	int64_t t0 = esp_timer_get_time();
	for (uint32_t off = 0; off < 64 * 1024; off += sizeof(rbuf))
		ESP_ERROR_CHECK(esp_partition_read(ph, off, rbuf, sizeof(rbuf)));
	printf("flash: esp_partition_read 64 KiB: %lld ms\n",
	       (long long)((esp_timer_get_time() - t0) / 1000));
	t0 = esp_timer_get_time();
	for (uint32_t blk = 16; blk < 32; blk++)
		CHECK(flash_host.iface.bread(ext4_esp_blockdev(&flash_host),
					     rbuf, blk, 1));
	printf("flash: block device read 64 KiB: %lld ms\n",
	       (long long)((esp_timer_get_time() - t0) / 1000));

	*m = (struct medium){
		.name = "flash",
		.host_bd = ext4_esp_blockdev(&flash_host),
		.dev_bd = ext4_esp_blockdev(&flash_dev),
		.mkfs_block_size = 4096,
		.big_size = 256 * 1024 + 123,
		.write_back = false,
	};
}

#if !CONFIG_EXAMPLE_SD_NONE
static sdmmc_card_t card;
static ext4_esp_blockdev_t sd_disk;
static struct ext4_mbr_bdevs sd_parts;

static void sd_card_init(void)
{
#if CONFIG_EXAMPLE_SD_SDMMC
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

	slot.width = CONFIG_EXAMPLE_SDMMC_WIDTH_4 ? 4 : 1;
	slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
	ESP_ERROR_CHECK(host.init());
	ESP_ERROR_CHECK(sdmmc_host_init_slot(host.slot, &slot));
	printf("sd: SDMMC host, slot %d, %d-bit\n", host.slot, slot.width);
#else /* CONFIG_EXAMPLE_SD_SDSPI */
	sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	spi_bus_config_t bus = {
		.mosi_io_num = CONFIG_EXAMPLE_SDSPI_MOSI,
		.miso_io_num = CONFIG_EXAMPLE_SDSPI_MISO,
		.sclk_io_num = CONFIG_EXAMPLE_SDSPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 4096,
	};
	sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
	sdspi_dev_handle_t handle;

	ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA));
	ESP_ERROR_CHECK(host.init());
	dev.gpio_cs = CONFIG_EXAMPLE_SDSPI_CS;
	dev.host_id = host.slot;
	ESP_ERROR_CHECK(sdspi_host_init_device(&dev, &handle));
	host.slot = handle;
	printf("sd: SPI mode, MOSI %d MISO %d SCK %d CS %d\n",
	       CONFIG_EXAMPLE_SDSPI_MOSI, CONFIG_EXAMPLE_SDSPI_MISO,
	       CONFIG_EXAMPLE_SDSPI_CLK, CONFIG_EXAMPLE_SDSPI_CS);
#endif
	ESP_ERROR_CHECK(sdmmc_card_init(&host, &card));
	sdmmc_card_print_info(stdout, &card);
}

static void sd_medium(struct medium *m)
{
	sd_card_init();
	ESP_ERROR_CHECK(ext4_esp_blockdev_init_sdmmc(&sd_disk, &card));
	CHECK(ext4_mbr_scan(ext4_esp_blockdev(&sd_disk), &sd_parts));
	for (int i = 0; i < 2; i++) {
		if (sd_parts.partitions[i].bdif == NULL)
			FAILF("SD card: MBR partition %d missing (prepare the "
			      "card with host/mkimage.sh sd)", i + 1);
		printf("sd: partition %d @%llu, %llu KiB\n", i + 1,
		       (unsigned long long)sd_parts.partitions[i].part_offset,
		       (unsigned long long)sd_parts.partitions[i].part_size /
			       1024);
	}

	*m = (struct medium){
		.name = "sd",
		.host_bd = &sd_parts.partitions[0],
		.dev_bd = &sd_parts.partitions[1],
		.mkfs_block_size = 1024,
		.big_size = 1024 * 1024 + 4321,
		.write_back = true,
	};
}
#endif

/* ---------------------------------------------------------------- main */

static void example_task(void *arg)
{
	struct medium media[2];
	int n = 0;
	esp_chip_info_t chip;

	(void)arg;
	esp_chip_info(&chip);
	printf("LWEXT4-TEST: start (%s rev %d.%d, %d core(s), ESP-IDF %s)\n",
	       CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100,
	       chip.cores, esp_get_idf_version());

	flash_medium(&media[n++]);
#if !CONFIG_EXAMPLE_SD_NONE
	sd_medium(&media[n++]);
#endif

	int phase = detect_phase(&media[0]);
	printf("LWEXT4-TEST: boot %d\n", phase);
	for (int i = 0; i < n; i++) {
		/* All media must be in the same phase. */
		ASSERT(detect_phase(&media[i]) == phase);
		run_medium(&media[i], phase);
	}

	printf("LWEXT4-TEST: stack high water mark: %u of %u bytes unused\n",
	       (unsigned)(uxTaskGetStackHighWaterMark(NULL) *
			  sizeof(StackType_t)),
	       (unsigned)CONFIG_EXAMPLE_TASK_STACK);
	printf("LWEXT4-TEST: minimum free heap: %u bytes\n",
	       (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
	printf("LWEXT4-TEST: PASS (boot %d)\n", phase);
	halt();
}

void app_main(void)
{
	/* lwext4 is not a small-stack library: run it in a task of its own
	 * rather than in the main task (CONFIG_ESP_MAIN_TASK_STACK_SIZE). */
	xTaskCreate(example_task, "lwext4", CONFIG_EXAMPLE_TASK_STACK, NULL, 5,
		    NULL);
}
