/* SPDX-License-Identifier: BSD-3-Clause */
/* Benchmark hooks on a host: time in nanoseconds (informational: not
 * reproducible), no stack measurement. */
#include "bench.h"

#include <stdio.h>
#include <time.h>

const char *const bench_platform = "host";
const char *const bench_counter_unit = "ns";

uint64_t bench_counter(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void bench_stack_paint(void)
{
}

uint32_t bench_stack_used(void)
{
	return 0;
}

void bench_print(const char *line)
{
	puts(line);
	fflush(stdout);
}

int bench_main(void);

int main(void)
{
	return bench_main();
}
