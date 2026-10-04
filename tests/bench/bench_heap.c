/* SPDX-License-Identifier: BSD-3-Clause */
/* Heap accounting of the benchmark: malloc, calloc, realloc and free are
 * wrapped (-Wl,--wrap=...), so the library needs no special build. Each
 * block carries its size in a header in front of it. */
#include "bench.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *__real_malloc(size_t size);
void __real_free(void *p);
void *__real_realloc(void *p, size_t size);

#define HDR 8u
static uint32_t current, peak;

void bench_heap_reset(void)
{
	peak = current;
}

uint32_t bench_heap_peak(void)
{
	return peak;
}

static void *account(uint8_t *p, size_t size)
{
	if (!p)
		return NULL;
	memcpy(p, &size, sizeof(size));
	current += (uint32_t)size;
	if (current > peak)
		peak = current;
	return p + HDR;
}

void *__wrap_malloc(size_t size)
{
	return account(__real_malloc(size + HDR), size);
}

void *__wrap_calloc(size_t n, size_t size)
{
	uint8_t *p;

	if (size && n > (SIZE_MAX - HDR) / size)
		return NULL;
	p = __wrap_malloc(n * size);
	if (p)
		memset(p, 0, n * size);
	return p;
}

void __wrap_free(void *q)
{
	uint8_t *p = q;
	size_t size;

	if (!p)
		return;
	p -= HDR;
	memcpy(&size, p, sizeof(size));
	current -= (uint32_t)size;
	__real_free(p);
}

void *__wrap_realloc(void *q, size_t size)
{
	uint8_t *p;
	size_t old;

	if (!q)
		return __wrap_malloc(size);
	p = (uint8_t *)q - HDR;
	memcpy(&old, p, sizeof(old));
	p = __real_realloc(p, size + HDR);
	if (!p)
		return NULL;
	current -= (uint32_t)old;
	return account(p, size);
}
