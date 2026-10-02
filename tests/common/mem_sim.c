/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Allocation traces and their replay through first-fit and best-fit
 * allocators on a fixed area (see mem_sim.h).
 */

#include "mem_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xrealloc(void *p, size_t size)
{
	p = realloc(p, size);
	if (!p) {
		fprintf(stderr, "mem_sim: out of host memory\n");
		exit(2);
	}
	return p;
}

void mem_trace_init(struct mem_trace *t)
{
	memset(t, 0, sizeof(*t));
}

void mem_trace_free(struct mem_trace *t)
{
	free(t->ev);
	mem_trace_init(t);
}

static void push(struct mem_trace *t, uint32_t id, size_t size, int is_free)
{
	if (t->n == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 4096;
		t->ev = xrealloc(t->ev, t->cap * sizeof(*t->ev));
	}
	t->ev[t->n].id = id;
	t->ev[t->n].size = (uint32_t)size;
	t->ev[t->n].free = (uint8_t)is_free;
	t->n++;
}

uint32_t mem_trace_alloc(struct mem_trace *t, size_t size)
{
	uint32_t id = t->next_id++;

	push(t, id, size, 0);
	return id;
}

void mem_trace_release(struct mem_trace *t, uint32_t id)
{
	push(t, id, 0, 1);
}

static size_t block_size(uint32_t size)
{
	size_t s = ((size_t)size + MEM_SIM_ALIGN - 1) & ~(size_t)(MEM_SIM_ALIGN - 1);

	return s + MEM_SIM_HDR;
}

/* ------------------------------------------------------------------------ */
/* Simulated heap: free ranges sorted by address, coalesced on free */

struct range {
	size_t off, len;
};

struct heap {
	struct range *free;
	size_t nfree, capfree;
	size_t *where; /* block id -> offset, (size_t)-1 if not placed */
	size_t *len;   /* block id -> length */
};

static void heap_insert(struct heap *h, size_t i, size_t off, size_t len)
{
	if (h->nfree == h->capfree) {
		h->capfree = h->capfree ? h->capfree * 2 : 64;
		h->free = xrealloc(h->free, h->capfree * sizeof(*h->free));
	}
	memmove(&h->free[i + 1], &h->free[i],
		(h->nfree - i) * sizeof(*h->free));
	h->free[i].off = off;
	h->free[i].len = len;
	h->nfree++;
}

static void heap_remove(struct heap *h, size_t i)
{
	memmove(&h->free[i], &h->free[i + 1],
		(h->nfree - i - 1) * sizeof(*h->free));
	h->nfree--;
}

static int heap_alloc(struct heap *h, size_t need, enum mem_fit fit,
		      size_t *off)
{
	size_t i, pick = (size_t)-1;

	for (i = 0; i < h->nfree; i++) {
		if (h->free[i].len < need)
			continue;
		if (fit == MEM_FIRST_FIT) {
			pick = i;
			break;
		}
		if (pick == (size_t)-1 || h->free[i].len < h->free[pick].len)
			pick = i;
	}
	if (pick == (size_t)-1)
		return 0;
	*off = h->free[pick].off;
	/* Split unless the rest could not hold even an empty block. */
	if (h->free[pick].len - need >= MEM_SIM_HDR + MEM_SIM_ALIGN) {
		h->free[pick].off += need;
		h->free[pick].len -= need;
	} else {
		heap_remove(h, pick);
	}
	return 1;
}

static void heap_free(struct heap *h, size_t off, size_t len)
{
	size_t i = 0;

	while (i < h->nfree && h->free[i].off < off)
		i++;
	heap_insert(h, i, off, len);
	if (i + 1 < h->nfree &&
	    h->free[i].off + h->free[i].len == h->free[i + 1].off) {
		h->free[i].len += h->free[i + 1].len;
		heap_remove(h, i + 1);
	}
	if (i > 0 && h->free[i - 1].off + h->free[i - 1].len == h->free[i].off) {
		h->free[i - 1].len += h->free[i].len;
		heap_remove(h, i);
	}
}

size_t mem_sim_run(const struct mem_trace *t, size_t arena, enum mem_fit fit)
{
	struct heap h;
	size_t i, failed = 0;

	memset(&h, 0, sizeof(h));
	h.where = xrealloc(NULL, (t->next_id + 1) * sizeof(*h.where));
	h.len = xrealloc(NULL, (t->next_id + 1) * sizeof(*h.len));
	heap_insert(&h, 0, 0, arena);
	for (i = 0; i < t->n; i++) {
		const struct mem_event *e = &t->ev[i];

		if (!e->free) {
			size_t need = block_size(e->size), off;

			h.len[e->id] = need;
			if (heap_alloc(&h, need, fit, &off)) {
				h.where[e->id] = off;
			} else {
				h.where[e->id] = (size_t)-1;
				failed++;
			}
		} else if (h.where[e->id] != (size_t)-1) {
			heap_free(&h, h.where[e->id], h.len[e->id]);
		}
	}
	free(h.free);
	free(h.where);
	free(h.len);
	return failed;
}

size_t mem_sim_peak(const struct mem_trace *t)
{
	size_t *len = xrealloc(NULL, (t->next_id + 1) * sizeof(*len));
	size_t i, now = 0, peak = 0;

	for (i = 0; i < t->n; i++) {
		const struct mem_event *e = &t->ev[i];

		if (!e->free) {
			len[e->id] = block_size(e->size);
			now += len[e->id];
			if (now > peak)
				peak = now;
		} else {
			now -= len[e->id];
		}
	}
	free(len);
	return peak;
}

size_t mem_sim_min_arena(const struct mem_trace *t, enum mem_fit fit,
			 size_t step)
{
	size_t lo, hi, i;

	/* Below the peak nothing fits; with the sum of all blocks nothing is
	 * ever reused, so everything fits. */
	lo = mem_sim_peak(t) / step;
	hi = 0;
	for (i = 0; i < t->n; i++)
		if (!t->ev[i].free)
			hi += block_size(t->ev[i].size);
	hi = (hi + step - 1) / step;
	if (lo == 0)
		lo = 1;
	if (hi < lo)
		hi = lo;
	if (mem_sim_run(t, lo * step, fit) == 0)
		return lo * step;
	/* invariant: lo fails, hi succeeds */
	while (hi - lo > 1) {
		size_t mid = lo + (hi - lo) / 2;

		if (mem_sim_run(t, mid * step, fit) == 0)
			hi = mid;
		else
			lo = mid;
	}
	return hi * step;
}
