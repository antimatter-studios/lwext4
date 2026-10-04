/* SPDX-License-Identifier: BSD-3-Clause */
/* Benchmark hooks on the MPS2 boards under QEMU (tests/baremetal).
 *
 * The counter is instructions. QEMU runs with -icount shift=0: the virtual
 * clock advances 1 ns per instruction executed, whatever the host does.
 * The boards' CMSDK APB timer 0 (0x40000000) counts down at the system
 * clock of that virtual time, so its ticks over an operation are an exact,
 * repeatable instruction count, to the resolution of one tick (40
 * instructions at 25 MHz). Instructions per tick are calibrated once with
 * a loop of known length. (Semihosting's SYS_ELAPSED cannot be used: QEMU
 * answers it with the host's clock.)
 *
 * The stack is measured by painting the free stack. */
#include "bench.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PAINT 0xa5u

/* CMSDK APB timer: control (bit 0: enable), current value, reload value */
#define TIMER ((volatile uint32_t *)0x40000000u)
#define TIMER_CTRL 0
#define TIMER_VALUE 1
#define TIMER_RELOAD 2

const char *const bench_platform = BENCH_PLATFORM;
const char *const bench_counter_unit = "insns";

static uint32_t insns_per_tick, last;
static uint64_t ticks;

static uint32_t elapsed_ticks(void)
{
	uint32_t now = TIMER[TIMER_VALUE];
	uint32_t d = last - now; /* counts down; wraps after 2^32 ticks */

	last = now;
	return d;
}

/* n iterations of two instructions: subs r0, #1 (0x3801) and bne back to
 * it (0xd1fd), encoded as numbers to be the same 16-bit instructions on
 * every core and with either assembler syntax */
static void spin(uint32_t n)
{
	register uint32_t r0 __asm__("r0") = n;

	__asm__ volatile(".short 0x3801\n\t.short 0xd1fd" : "+r"(r0) : : "cc");
}

static void timer_start(void)
{
	uint32_t t;

	TIMER[TIMER_CTRL] = 0;
	TIMER[TIMER_RELOAD] = 0xffffffffu;
	TIMER[TIMER_VALUE] = 0xffffffffu;
	TIMER[TIMER_CTRL] = 1;
	last = TIMER[TIMER_VALUE];
	elapsed_ticks();
	spin(1000000);
	t = elapsed_ticks();
	insns_per_tick = t ? (2000000u + t / 2) / t : 0;
	printf("bench: %u instructions per timer tick\n",
	       (unsigned)insns_per_tick);
}

uint64_t bench_counter(void)
{
	if (!insns_per_tick)
		timer_start();
	ticks += elapsed_ticks();
	return ticks * insns_per_tick;
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
