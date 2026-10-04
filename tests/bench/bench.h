/* SPDX-License-Identifier: BSD-3-Clause */
/* Platform hooks of the benchmark (bench.c), see README.md here. */
#ifndef LWEXT4_BENCH_H_
#define LWEXT4_BENCH_H_

#include <stdint.h>

/* Name of the platform and what bench_counter() counts. */
extern const char *const bench_platform;
extern const char *const bench_counter_unit;

/* A counter that only goes up: instructions on an emulator with an
 * instruction count, nanoseconds elsewhere. */
uint64_t bench_counter(void);

/* Fill the unused stack below the caller with a pattern, and after the
 * operation report how many bytes of it were used (0 if not measured). */
void bench_stack_paint(void);
uint32_t bench_stack_used(void);

/* Print a line (with the newline). */
void bench_print(const char *line);

/* Peak bytes allocated since bench_heap_reset() (malloc wrapped). */
void bench_heap_reset(void);
uint32_t bench_heap_peak(void);

#endif
