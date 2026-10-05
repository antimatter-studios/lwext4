/* SPDX-License-Identifier: BSD-3-Clause */
/* Benchmark hooks on the MPS2 boards under QEMU (tests/baremetal).
 *
 * The counter is instructions: platform_counter() of platforms/mps2 (see
 * counter.c there; QEMU runs with -icount shift=0). The stack is measured
 * by painting the free stack. */
#include "bench.h"

#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PAINT 0xa5u

const char *const bench_platform = BENCH_PLATFORM;
const char *const bench_counter_unit = "insns";

uint64_t bench_counter(void)
{
	return platform_counter();
}

static uint8_t *paint_lo, *paint_hi;

void bench_stack_paint(void)
{
	uint8_t *here = __builtin_frame_address(0);
	uint8_t *brk = sbrk(0);

	/* From below this frame down to the heap, leaving the heap room to
	 * grow: at most 32 KiB */
	paint_hi = here - 64;
	paint_lo = paint_hi - 32 * 1024;
	if (paint_lo < brk + 64 * 1024)
		paint_lo = brk + 64 * 1024;
	if (paint_lo < paint_hi)
		memset(paint_lo, PAINT, (size_t)(paint_hi - paint_lo));
}

uint32_t bench_stack_used(void)
{
	uint8_t *p = paint_lo;

	while (p < paint_hi && *p == PAINT)
		p++;
	return (uint32_t)(paint_hi - p) + 64;
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
