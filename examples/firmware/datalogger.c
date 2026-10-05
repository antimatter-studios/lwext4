/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * datalogger: what most MCU applications of a filesystem do. Append
 * records to log files through the journal, start a new file when one is
 * full, keep only the newest few, and survive losing power at any moment.
 *
 * At start it mounts the disk (and formats it if it holds no
 * filesystem), replays the journal, and checks every record left by
 * earlier runs: they must be whole and numbered without a gap. Then it
 * appends records=<n> more (command line, default 300). Each record is
 * appended in its own transaction, so a power cut loses at most the
 * record being written, never what was there before. With trace on the
 * command line it prints "appended <seq>" when a record is on the disk.
 *
 *   /log/0001.csv, /log/0002.csv, ...   lines "<seq>,<value>\n": seq
 *                                       counts from 1, value = seq * 7
 *                                       mod 10000, both zero padded
 */
#include <platform.h>

#include <ext4.h>
#include <ext4_mkfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MP "/mp/"
#define RECORD_LEN 12   /* "000001,0007\n" */
#define FILE_RECORDS 50 /* a new file after this many records */
#define KEEP_FILES 4    /* the oldest file is removed beyond this */

static unsigned long first_seq, last_seq;
static int trace;
static unsigned first_file, last_file;

static int fail(const char *what, int r)
{
	printf("datalogger: %s failed: %d\n", what, r);
	return 1;
}

static void file_name(char *path, size_t size, unsigned n)
{
	snprintf(path, size, MP "log/%04u.csv", n);
}

static int mount_or_format(struct ext4_blockdev *bd)
{
	static struct ext4_fs fs;
	struct ext4_mkfs_info info = {.block_size = 1024, .journal = true};
	int r;

	r = ext4_device_register(bd, "disk");
	if (r != EOK)
		return r;
	r = ext4_mount("disk", MP, false);
	if (r == EOK)
		return EOK;
	/* No filesystem yet (first start): format, then mount */
	printf("datalogger: no filesystem (%d), formatting\n", r);
	ext4_device_unregister("disk");
	r = ext4_mkfs(&fs, bd, &info, F_SET_EXT4);
	if (r != EOK)
		return r;
	r = ext4_device_register(bd, "disk");
	if (r != EOK)
		return r;
	return ext4_mount("disk", MP, false);
}

/* The log files there are, oldest first: /log/<n>.csv, n first..last */
static int find_files(void)
{
	const ext4_direntry *de;
	ext4_dir d;
	int r;

	first_file = last_file = 0;
	r = ext4_dir_open(&d, MP "log");
	if (r != EOK)
		return r;
	while ((de = ext4_dir_entry_next(&d)) != NULL) {
		char name[16];
		unsigned n;

		if (de->name_length != 8)
			continue;
		memcpy(name, de->name, 8);
		name[8] = 0;
		if (sscanf(name, "%4u.csv", &n) != 1 || n == 0)
			continue;
		if (!first_file || n < first_file)
			first_file = n;
		if (n > last_file)
			last_file = n;
	}
	return ext4_dir_close(&d);
}

/* Every record of every file must be whole and follow the one before */
static int check_records(void)
{
	char path[32], rec[RECORD_LEN + 1];
	unsigned n;

	first_seq = last_seq = 0;
	for (n = first_file; n && n <= last_file; n++) {
		ext4_file f;
		size_t got;
		int r;

		file_name(path, sizeof(path), n);
		r = ext4_fopen(&f, path, "rb");
		if (r != EOK)
			return fail("opening a log file", r);
		for (;;) {
			unsigned long seq, value;

			r = ext4_fread(&f, rec, RECORD_LEN, &got);
			if (r != EOK || got == 0)
				break;
			rec[got] = 0;
			if (got != RECORD_LEN ||
			    sscanf(rec, "%6lu,%4lu\n", &seq, &value) != 2 ||
			    rec[RECORD_LEN - 1] != '\n' ||
			    value != seq * 7 % 10000 ||
			    (last_seq && seq != last_seq + 1)) {
				printf("datalogger: bad record after %lu in "
				       "%s\n", last_seq, path);
				ext4_fclose(&f);
				return 1;
			}
			if (!first_seq)
				first_seq = seq;
			last_seq = seq;
		}
		ext4_fclose(&f);
		if (r != EOK)
			return fail("reading a log file", r);
	}
	return 0;
}

static int append(unsigned long seq)
{
	char path[32], rec[RECORD_LEN + 1];
	ext4_file f;
	size_t done;
	unsigned n;
	int r;

	/* Record seq goes to file (seq - 1) / FILE_RECORDS + 1. Beyond
	 * KEEP_FILES files the oldest is removed. */
	n = (unsigned)((seq - 1) / FILE_RECORDS + 1);
	if (n > last_file) {
		last_file = n;
		if (!first_file)
			first_file = n;
		while (last_file - first_file + 1 > KEEP_FILES) {
			file_name(path, sizeof(path), first_file);
			r = ext4_fremove(path);
			if (r != EOK)
				return fail("removing the oldest file", r);
			first_file++;
		}
	}
	file_name(path, sizeof(path), last_file);
	snprintf(rec, sizeof(rec), "%06lu,%04lu\n", seq, seq * 7 % 10000);
	r = ext4_fopen(&f, path, "ab");
	if (r != EOK)
		return fail("opening the log", r);
	r = ext4_fwrite(&f, rec, RECORD_LEN, &done);
	if (r == EOK && done != RECORD_LEN)
		r = EIO;
	if (r == EOK)
		r = ext4_fclose(&f);
	else
		ext4_fclose(&f);
	if (r != EOK)
		return fail("appending a record", r);
	if (trace)
		printf("appended %lu\n", seq);
	return 0;
}

int main(void)
{
	const char *opt;
	unsigned long records = 300, seq;
	int r;

	platform_init();
	opt = strstr(platform_cmdline(), "records=");
	if (opt)
		records = strtoul(opt + 8, NULL, 10);
	trace = strstr(platform_cmdline(), "trace") != NULL;

	r = mount_or_format(platform_disk());
	if (r != EOK)
		return fail("mounting", r);
	r = ext4_recover(MP);
	if (r != EOK && r != ENOTSUP)
		return fail("ext4_recover", r);
	r = ext4_journal_start(MP);
	if (r != EOK)
		return fail("ext4_journal_start", r);

	if (ext4_inode_exist(MP "log", EXT4_DE_DIR) != EOK) {
		r = ext4_dir_mk(MP "log");
		if (r != EOK)
			return fail("creating /log", r);
	}
	r = find_files();
	if (r != EOK)
		return fail("listing /log", r);
	if (check_records())
		return 1;
	if (last_seq)
		printf("datalogger: found records %lu..%lu in files %u..%u\n",
		       first_seq, last_seq, first_file, last_file);

	for (seq = last_seq + 1; seq <= last_seq + records; seq++)
		if (append(seq))
			return 1;

	/* What is on the disk now, read back */
	r = find_files();
	if (r != EOK)
		return fail("listing /log", r);
	if (check_records())
		return 1;
	if (last_seq != seq - 1)
		return fail("reading the last record back", (int)last_seq);

	r = ext4_journal_stop(MP);
	if (r != EOK)
		return fail("ext4_journal_stop", r);
	r = ext4_umount(MP);
	if (r != EOK)
		return fail("ext4_umount", r);
	ext4_device_unregister("disk");
	printf("datalogger: records %lu..%lu in files %u..%u\n", first_seq,
	       last_seq, first_file, last_file);
	return 0;
}
