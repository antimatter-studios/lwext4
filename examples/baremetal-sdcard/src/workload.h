/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * File system workload of the Renode test firmware. The host side check
 * (scripts/sdimage.py) mirrors the constants and the data pattern, keep
 * them in sync.
 */
#ifndef WORKLOAD_H_
#define WORKLOAD_H_

#include <stdint.h>

/* Files created by the host with mke2fs -d (scripts/sdimage.py) */
#define HOST_PATTERN_SEED 4242u
#define HOST_PATTERN_SIZE 100000u
#define HOST_MANY_FILES 300u

/* Workload */
#define WL_SUBDIRS 12u
#define WL_SMALL_FILES 48u
#define WL_BIG_SEED 1000u
#define WL_BIG_CHUNK 1000u      /* deliberately not a multiple of 512 */

uint8_t pattern_byte(uint32_t seed, uint32_t off);
uint32_t small_file_size(uint32_t i);
uint32_t big_file_size(void);

void workload_check_host_files(const char *mp);
void workload_modify_host_files(const char *mp);
void workload_verify_host_files_modified(const char *mp);

void workload_run(const char *mp);
void workload_verify(const char *mp);

void workload_torture_prepare(const char *mp);
void workload_torture_step(const char *mp, uint32_t i);
void workload_torture_check(const char *mp);

/* main.c */
void test_fail(const char *file, int line, const char *what, int rc);

#endif /* WORKLOAD_H_ */
