/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Allocation traces and their replay through simple allocators on a fixed
 * memory area, as a microcontroller without an MMU would have.
 *
 * A trace is the sequence of allocations and frees a program made. The
 * program's behaviour does not depend on where its blocks are placed, so
 * one trace recorded with any allocator can be replayed through others:
 * mem_sim_min_arena() finds the smallest area in which every allocation
 * of the trace succeeds. Compared with the peak of live bytes, it shows
 * how much memory the allocator loses to fragmentation.
 */

#ifndef LWEXT4_MEM_SIM_H_
#define LWEXT4_MEM_SIM_H_

#include <stddef.h>
#include <stdint.h>

/* One allocation (size > 0 or a zero-size allocation) or free (size 0,
 * free = 1) of block <id>. Ids are unique within a trace. */
struct mem_event {
	uint32_t id;
	uint32_t size;
	uint8_t free;
};

struct mem_trace {
	struct mem_event *ev;
	size_t n, cap;
	uint32_t next_id;
};

/* Placement policies of the simulated allocator. */
enum mem_fit {
	MEM_FIRST_FIT, /* lowest address that fits: newlib, many RTOS heaps */
	MEM_BEST_FIT,  /* smallest free range that fits */
};

/* Per-block bookkeeping of the simulated allocator (a size word) and its
 * alignment, like a typical embedded malloc. */
#define MEM_SIM_HDR 8u
#define MEM_SIM_ALIGN 8u

void mem_trace_init(struct mem_trace *t);
void mem_trace_free(struct mem_trace *t);
/* Append an allocation of <size> bytes; returns its id. */
uint32_t mem_trace_alloc(struct mem_trace *t, size_t size);
void mem_trace_release(struct mem_trace *t, uint32_t id);

/* Replay <t> on an area of <arena> bytes. Returns the number of
 * allocations that did not fit (0: the trace runs in that area). */
size_t mem_sim_run(const struct mem_trace *t, size_t arena, enum mem_fit fit);

/* Peak of the bytes the simulated allocator holds (payload rounded up to
 * the alignment plus the header), i.e. the area an allocator without
 * fragmentation would need. */
size_t mem_sim_peak(const struct mem_trace *t);

/* Smallest area, in steps of <step> bytes, in which mem_sim_run() succeeds,
 * found by bisection between mem_sim_peak() and the sum of all blocks. */
size_t mem_sim_min_arena(const struct mem_trace *t, enum mem_fit fit,
			 size_t step);

#endif /* LWEXT4_MEM_SIM_H_ */
