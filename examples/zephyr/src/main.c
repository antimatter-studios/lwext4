/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * lwext4 Zephyr example: the whole life cycle of an ext4 file system on a
 * disk of Zephyr's disk access API, here the SD card of the board.
 *
 * It formats the card (the whole card, no partition table), mounts it,
 * starts the journal, creates, writes, reads, lists, renames and removes
 * files and directories, unmounts, mounts again and checks that everything
 * is still there, then prints
 *
 *   LWEXT4-TEST: PASS            or   LWEXT4-TEST: FAIL: <reason>
 *
 * and idles. Afterwards the card is an ordinary ext4 file system: CI checks
 * the card image with "e2fsck -fn" and reads the files back with debugfs
 * (host/run_qemu_test.py).
 *
 * The storage side is Zephyr's: devicetree describes the SD card slot
 * (boards/<board>.overlay), Zephyr's SD stack and disk driver make the card
 * disk "SD", and ext4_zephyr_disk_init() (ports/zephyr, part of the lwext4
 * Zephyr module) turns that disk into the struct ext4_blockdev all of
 * lwext4 works on. With a RAM disk, eMMC or NVMe disk instead only the
 * disk name changes. The lwext4 part of this program is the same as in
 * ../basic/main.c, which runs on a PC.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <sys_malloc.h>

/* The lwext4 API (ext4.h, ext4_mkfs.h) and its Zephyr glue */
#include <ext4_zephyr.h>

#include <stdarg.h>
#include <string.h>

/* The disk access name of the card: the disk-name property of the
 * "zephyr,sdmmc-disk" node in boards/<board>.overlay. */
#define DISK "SD"

/* Name under which the block device is registered with lwext4, and where
 * the file system appears. All paths passed to lwext4 start with the mount
 * point, which must end in '/'. */
#define DEVICE "sd"
#define MP "/sd/"

/* A larger file, written in chunks that do not line up with the 1 KiB
 * blocks, so that the SD card sees multi block reads and writes as well as
 * partial blocks. Byte i of the file is pattern_byte(i); the host check
 * compares it with the same formula. */
#define BIG_FILE MP "data/pattern.bin"
#define BIG_SIZE (64u * 1024u + 123u)
#define CHUNK 1000u

static uint8_t pattern_byte(uint32_t i)
{
	return (uint8_t)((i * 7u + i / 256u) & 0xffu);
}

/* Every lwext4 call returns EOK (0) or an errno value. On an error the
 * example reports FAIL and stops. */
#define CHECK(call)                                                            \
	do {                                                                   \
		int r_ = (call);                                               \
		if (r_ != EOK) {                                               \
			fail("%s:%d: %s: error %d", __FILE__, __LINE__, #call, \
			     r_);                                              \
		}                                                              \
	} while (0)

/* After the verdict the firmware idles; the test runner (or you, with
 * Ctrl-a x) ends QEMU. */
static FUNC_NORETURN void finish(void)
{
	k_sleep(K_FOREVER);
	CODE_UNREACHABLE;
}

static FUNC_NORETURN void fail(const char *fmt, ...)
{
	va_list ap;

	printk("LWEXT4-TEST: FAIL: ");
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
	printk("\n");
	finish();
}

static void write_file(const char *path, const char *text)
{
	ext4_file f;
	size_t written;

	/* ext4_fopen() takes fopen() style modes: "wb" creates or truncates */
	CHECK(ext4_fopen(&f, path, "wb"));
	CHECK(ext4_fwrite(&f, text, strlen(text), &written));
	CHECK(ext4_fclose(&f));
	if (written != strlen(text)) {
		fail("%s: short write", path);
	}
}

/* Reads the file and compares it with the expected text. */
static void check_file(const char *path, const char *text)
{
	ext4_file f;
	char buf[64];
	size_t n;

	CHECK(ext4_fopen(&f, path, "rb"));
	CHECK(ext4_fread(&f, buf, sizeof(buf) - 1, &n));
	CHECK(ext4_fclose(&f));
	buf[n] = '\0';
	if (strcmp(buf, text) != 0) {
		fail("%s: read back \"%s\"", path, buf);
	}
	printk("%s: %s", path, buf);
}

static void write_big_file(void)
{
	static uint8_t chunk[CHUNK];
	ext4_file f;
	size_t written;

	CHECK(ext4_fopen(&f, BIG_FILE, "wb"));
	for (uint32_t off = 0; off < BIG_SIZE; off += CHUNK) {
		uint32_t len = MIN(CHUNK, BIG_SIZE - off);

		for (uint32_t i = 0; i < len; i++) {
			chunk[i] = pattern_byte(off + i);
		}
		CHECK(ext4_fwrite(&f, chunk, len, &written));
		if (written != len) {
			fail("%s: short write at %u", BIG_FILE, off);
		}
	}
	CHECK(ext4_fclose(&f));
}

static void check_big_file(void)
{
	static uint8_t chunk[CHUNK];
	ext4_file f;
	uint32_t off = 0;
	size_t n;

	CHECK(ext4_fopen(&f, BIG_FILE, "rb"));
	if (ext4_fsize(&f) != BIG_SIZE) {
		fail("%s: size %llu", BIG_FILE,
		     (unsigned long long)ext4_fsize(&f));
	}
	do {
		CHECK(ext4_fread(&f, chunk, sizeof(chunk), &n));
		for (uint32_t i = 0; i < n; i++) {
			if (chunk[i] != pattern_byte(off + i)) {
				fail("%s: wrong byte at %u", BIG_FILE, off + i);
			}
		}
		off += n;
	} while (n == sizeof(chunk));
	CHECK(ext4_fclose(&f));
	if (off != BIG_SIZE) {
		fail("%s: read %u bytes", BIG_FILE, off);
	}
	printk("%s: %u bytes ok\n", BIG_FILE, off);
}

static void list_dir(const char *path)
{
	const ext4_direntry *de;
	ext4_dir d;

	printk("%s:\n", path);
	CHECK(ext4_dir_open(&d, path));
	/* Entries come in on-disk order, including "." and ".." */
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		printk("  %-5s %.*s\n",
		       de->inode_type == EXT4_DE_DIR ? "dir" : "file",
		       (int)de->name_length, (const char *)de->name);
	}
	CHECK(ext4_dir_close(&d));
}

int main(void)
{
	/* The block device must outlive its registration, and ext4_mkfs
	 * needs a struct ext4_fs to work in: both are large, so keep them
	 * out of the stack. */
	static struct ext4_zephyr_disk sd;
	static struct ext4_fs fs;
	struct ext4_mkfs_info info;
	struct ext4_blockdev *bd;
	struct sys_memory_stats heap;
	size_t stack_unused;

	printk("lwext4 Zephyr example on %s\n", CONFIG_BOARD_TARGET);

	/* 1. The block device: the SD card, initialised through the disk
	 *    access API (card identification happens here). */
	CHECK(ext4_zephyr_disk_init(&sd, DISK));
	bd = ext4_zephyr_disk_bdev(&sd);
	printk("disk %s: %llu bytes\n", DISK,
	       (unsigned long long)bd->part_size);

	/* 2. Format it. Fields left 0 get defaults computed from the device
	 *    size (inode count, journal size, ...). F_SET_EXT4 selects the
	 *    ext4 feature set, which includes a journal. 1 KiB blocks keep
	 *    the RAM lwext4 needs small: the block cache holds whole blocks. */
	memset(&info, 0, sizeof(info));
	info.block_size = 1024;
	info.journal = true;
	info.label = "lwext4-zephyr";
	printk("ext4_mkfs...\n");
	CHECK(ext4_mkfs(&fs, bd, &info, F_SET_EXT4));

	/* 3. Register the device under a name and mount it. Several threads
	 *    may use one mount point once it has locks. */
	CHECK(ext4_device_register(bd, DEVICE));
	CHECK(ext4_mount(DEVICE, MP, false));
	CHECK(ext4_mount_setup_locks(MP, ext4_zephyr_mount_locks()));

	/* 4. Replay the journal if the last session did not unmount cleanly
	 *    (a no-op here, but always do it before writing), then start
	 *    journaling: from now on metadata updates are transactions that
	 *    survive a power loss. */
	CHECK(ext4_recover(MP));
	CHECK(ext4_journal_start(MP));

	/* 5. Write-back cache: blocks are written when the cache needs room
	 *    or on ext4_cache_flush()/ext4_cache_write_back(.., false)
	 *    instead of after every operation. Fewer card writes; must be
	 *    switched off again before unmounting. */
	CHECK(ext4_cache_write_back(MP, true));

	/* 6. Directories and files. */
	CHECK(ext4_dir_mk(MP "docs"));
	CHECK(ext4_dir_mk(MP "data"));
	CHECK(ext4_dir_mk(MP "tmp"));
	write_file(MP "docs/hello.txt", "Hello from lwext4 on Zephyr!\n");
	write_file(MP "docs/notes.txt", "This file is renamed below.\n");
	write_file(MP "tmp/scratch.txt", "This file is removed below.\n");
	write_big_file();

	check_file(MP "docs/hello.txt", "Hello from lwext4 on Zephyr!\n");
	list_dir(MP "docs");

	/* ext4_frename() renames or moves files; ext4_dir_mv() does the
	 * same for directories. */
	CHECK(ext4_frename(MP "docs/notes.txt", MP "docs/readme.txt"));

	/* ext4_fremove() deletes a file, ext4_dir_rm() a directory together
	 * with everything in it. */
	CHECK(ext4_fremove(MP "tmp/scratch.txt"));
	CHECK(ext4_dir_rm(MP "tmp"));

	list_dir(MP);

	/* 7. Shut down in reverse order: write the cache back, stop the
	 *    journal, unmount. After ext4_umount() the card is consistent
	 *    and can be removed. */
	CHECK(ext4_cache_write_back(MP, false));
	CHECK(ext4_journal_stop(MP));
	CHECK(ext4_umount(MP));

	/* 8. Mount again, read only, and check that everything reached the
	 *    card. */
	CHECK(ext4_mount(DEVICE, MP, true));
	check_file(MP "docs/hello.txt", "Hello from lwext4 on Zephyr!\n");
	check_file(MP "docs/readme.txt", "This file is renamed below.\n");
	check_big_file();
	list_dir(MP "docs");
	CHECK(ext4_umount(MP));

	/* 9. Release the device and the disk. */
	CHECK(ext4_device_unregister(DEVICE));
	CHECK(ext4_zephyr_disk_deinit(&sd));

	/* RAM actually used: the heap peak (lwext4's block cache, journal
	 * records and file system state) and main()'s stack. */
	malloc_runtime_stats_get(&heap);
	k_thread_stack_space_get(k_current_get(), &stack_unused);
	printk("MEM: heap peak %zu bytes, main stack used %zu of %u bytes\n",
	       heap.max_allocated_bytes,
	       (size_t)CONFIG_MAIN_STACK_SIZE - stack_unused,
	       CONFIG_MAIN_STACK_SIZE);

	printk("LWEXT4-TEST: PASS\n");
	finish();
}
