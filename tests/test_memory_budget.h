/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Memory ceilings of test_memory.c, in bytes: the peak of the bytes lwext4
 * holds at once, and the smallest area a first-fit allocator runs the
 * workload in (ff-min). Measured on a 64-bit (LP64) host with the block
 * cache size of the hosted builds (the allocation sequence is the same on
 * every platform; 32-bit targets need less), rounded up to the next KiB.
 * A change that lowers the measured values lowers these to match; nothing
 * may raise them without a reason in the commit message.
 */

#define MEMORY_BUDGET_CACHE_SIZE 16

static const struct budget memory_budget[] = {
	/* workload           peak    ff-min   (measured peak, ff-min) */
	{"mkfs/1k",          19456,  19456}, /* 18688, 19072 */
	{"mount/1k",          4096,   4096}, /* 3576, 3648 */
	{"small-files/1k",   22528,  23552}, /* 22120, 22912 */
	{"large-file/1k",    14336,  14336}, /* 13736, 14144 */
	{"directory/1k",     22528,  23552}, /* 21960, 22912 */
	{"xattr/1k",         21504,  22528}, /* 20792, 21632 */
	{"churn-1/1k",       20480,  20480}, /* 19576, 20480 */
	{"churn-20/1k",      22528,  23552}, /* 22120, 23360 */
	{"mkfs/4k",          68608,  68608}, /* 67840, 68224 */
	{"mount/4k",         10240,  10240}, /* 9720, 9792 */
	{"small-files/4k",   74752,  74752}, /* 74040, 74496 */
	{"large-file/4k",    39936,  39936}, /* 39048, 39424 */
	{"directory/4k",     78848,  87040}, /* 78280, 86592 */
	{"xattr/4k",         68608,  72704}, /* 68568, 72576 */
	{"churn-1/4k",       60416,  61440}, /* 60248, 60736 */
	{"churn-20/4k",      69632,  76800}, /* 68728, 76800 */
};
