/* SPDX-License-Identifier: BSD-3-Clause */

/*
 * Helpers for the tests that call lwext4 from several threads (test_mt_*):
 * failing from any thread, lock callbacks that check how they are used,
 * a watchdog for deadlocks and e2fsck. Header only, like the other test
 * helpers that only some tests need.
 */

#ifndef LWEXT4_MT_UTIL_H_
#define LWEXT4_MT_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* exit() from one thread while the others still run lwext4 would run the
 * atexit handlers under them: report and leave at once instead. */
static void mt_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
	_exit(1);
}

#define MT_CHECK(cond)                                                         \
	do {                                                                   \
		if (!(cond))                                                   \
			mt_fail("%s:%d: check failed: %s", __FILE__,           \
				__LINE__, #cond);                              \
	} while (0)

#define MT_CHECK_EQ(expected, actual)                                          \
	do {                                                                   \
		long long e_ = (long long)(expected);                          \
		long long a_ = (long long)(actual);                            \
		if (e_ != a_)                                                  \
			mt_fail("%s:%d: %s == %s failed: expected %lld, "      \
				"got %lld",                                    \
				__FILE__, __LINE__, #expected, #actual, e_,    \
				a_);                                           \
	} while (0)

/*************************** checked lock callbacks ************************/

/* An error checking mutex: locking it again from the thread that holds it
 * fails (EDEADLK) instead of deadlocking, unlocking it from a thread that
 * does not hold it fails (EPERM) instead of being undefined. The depth is
 * per thread, so a thread can check that it holds the lock. */
static void mt_mutex_init(pthread_mutex_t *m)
{
	pthread_mutexattr_t attr;

	MT_CHECK_EQ(0, pthread_mutexattr_init(&attr));
	MT_CHECK_EQ(0, pthread_mutexattr_settype(&attr,
						 PTHREAD_MUTEX_ERRORCHECK));
	MT_CHECK_EQ(0, pthread_mutex_init(m, &attr));
	MT_CHECK_EQ(0, pthread_mutexattr_destroy(&attr));
}

static void mt_mutex_lock(pthread_mutex_t *m, int *depth, const char *what)
{
	int e = pthread_mutex_lock(m);

	if (e == EDEADLK)
		mt_fail("%s locked by the thread that holds it (a plain mutex "
			"deadlocks here)",
			what);
	if (e)
		mt_fail("%s: pthread_mutex_lock: %s", what, strerror(e));
	if (*depth != 0)
		mt_fail("%s: depth %d after locking", what, *depth);
	(*depth)++;
}

static void mt_mutex_unlock(pthread_mutex_t *m, int *depth, const char *what)
{
	int e;

	if (*depth != 1)
		mt_fail("%s unlocked by a thread that does not hold it", what);
	(*depth)--;
	e = pthread_mutex_unlock(m);
	if (e)
		mt_fail("%s: pthread_mutex_unlock: %s", what, strerror(e));
}

/* MT_LOCK(name): name_mutex, the per thread name_depth and the callbacks
 * name_lock()/name_unlock() for struct ext4_lock. */
#define MT_LOCK(name)                                                          \
	static pthread_mutex_t name##_mutex;                                   \
	static __thread int name##_depth;                                      \
	static void name##_lock(void)                                          \
	{                                                                      \
		mt_mutex_lock(&name##_mutex, &name##_depth, #name " lock");    \
	}                                                                      \
	static void name##_unlock(void)                                        \
	{                                                                      \
		mt_mutex_unlock(&name##_mutex, &name##_depth, #name " lock");  \
	}

/********************************* watchdog ********************************/

static pthread_mutex_t mt_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mt_progress_cond = PTHREAD_COND_INITIALIZER;
static unsigned long mt_progress_ops;
static int mt_threads_done;

/* A thread finished one operation (done: all of its operations). */
static void mt_progress(bool done)
{
	pthread_mutex_lock(&mt_progress_mutex);
	mt_progress_ops++;
	if (done)
		mt_threads_done++;
	pthread_cond_signal(&mt_progress_cond);
	pthread_mutex_unlock(&mt_progress_mutex);
}

/* Wait until threads threads are done; no operation completing for
 * stall_seconds is a deadlock. */
static void mt_wait(int threads, int stall_seconds)
{
	unsigned long last = (unsigned long)-1;
	int stalled = 0;

	pthread_mutex_lock(&mt_progress_mutex);
	while (mt_threads_done < threads) {
		struct timespec ts;

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
		pthread_cond_timedwait(&mt_progress_cond, &mt_progress_mutex,
				       &ts);
		if (mt_progress_ops != last) {
			last = mt_progress_ops;
			stalled = 0;
		} else if (++stalled >= stall_seconds) {
			mt_fail("no progress for %d s after %lu operations: "
				"deadlock",
				stall_seconds, mt_progress_ops);
		}
	}
	pthread_mutex_unlock(&mt_progress_mutex);
}

/********************************** random *********************************/

/* xorshift64*: every thread has its own fixed sequence. */
static uint64_t mt_rnd(uint64_t *state)
{
	*state ^= *state >> 12;
	*state ^= *state << 25;
	*state ^= *state >> 27;
	return *state * 0x2545f4914f6cdd1dull;
}

static uint32_t mt_rnd_below(uint64_t *state, uint32_t n)
{
	return n ? (uint32_t)(mt_rnd(state) % n) : 0;
}

static void mt_rnd_bytes(uint64_t *state, uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		p[i] = (uint8_t)(mt_rnd(state) >> 56);
}

/* Seed of thread t for the test's fixed seed. */
static uint64_t mt_seed(uint64_t seed, int t)
{
	for (int i = 0; i <= t; i++)
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
	return seed | 1;
}

/********************************** e2fsck *********************************/

/* e2fsck -fn must find nothing to fix. */
static void mt_fsck(const char *image)
{
	char cmd[1024];
	int r;

	snprintf(cmd, sizeof(cmd),
		 "PATH=\"$PATH:/sbin:/usr/sbin\" e2fsck -fn '%s'", image);
	r = system(cmd);
	if (r != 0)
		mt_fail("e2fsck -fn %s: exit status %d", image, r);
}

#endif /* LWEXT4_MT_UTIL_H_ */
