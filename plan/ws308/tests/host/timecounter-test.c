/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws308-p002: host test of the amd64 timecounter reader without its global
 * lock (src/hal/amd64/bsp-pcat/timecounter-policy.c).  Eight threads read a
 * published state whose samples come from a shared clock, each thread
 * skewed by its own offset (up to a few hundred counts behind, as KVM's
 * per-vCPU TSCs can be).  Checks: nothing is read before publication; each
 * thread's values never go back; a value read after another thread's read
 * returned is never smaller than it (a global sequence orders the reads);
 * the final shared maximum is the largest value returned; a disabled state
 * reads nothing.  A first round with no skew must log no regression (two
 * CPUs racing to publish are not a step back); the skewed round may.
 *
 *   cc -O2 -Wall -Wextra -pthread -I include -I src/hal/amd64/bsp-pcat -o build/tmp/timecounter-test \
 *       plan/ws308/tests/host/timecounter-test.c && build/tmp/timecounter-test
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include "timecounter-policy.c"

#define THREADS 8
#define READS 200000

static _Atomic int printed;

int
hal_printf(const char *format, ...)
{
	(void)format;
	atomic_fetch_add(&printed, 1);
	return 0;
}

static struct amd64_timecounter_read_state state;
static _Atomic uint64_t clock_now;
static _Atomic uint64_t order;
static _Atomic uint64_t last_returned;
static _Atomic int failures;

/* A shared clock, read late by the thread's own skew. */
static uint64_t
skewed_sample(void *context)
{
	uint64_t skew;

	skew = (uint64_t)(uintptr_t)context;
	return atomic_fetch_add(&clock_now, 3U) + 1000000U - skew;
}

static void *
reader(void *argument)
{
	uint64_t previous;
	uint64_t value;
	uint64_t frequency;
	uint64_t floor;
	int index;

	previous = 0;
	for (index = 0; index < READS; index++) {
		/* Everything returned before this read began is a floor for it. */
		floor = atomic_load(&last_returned);
		if (!amd64_timecounter_read_guarded(&state, skewed_sample, argument,
		    &value, &frequency) || frequency != 24000000U) {
			atomic_fetch_add(&failures, 1);
			break;
		}
		if (value < previous || value < floor) {
			atomic_fetch_add(&failures, 1);
			break;
		}
		previous = value;

		/* Publishes this read as returned (a monotonic max of returned values). */
		floor = atomic_load(&last_returned);
		while (value > floor && !atomic_compare_exchange_weak(&last_returned, &floor, value))
			;
		atomic_fetch_add(&order, 1);
	}
	return NULL;
}

int
main(void)
{
	pthread_t threads[THREADS];
	uint64_t value;
	uint64_t frequency;
	uintptr_t index;

	amd64_timecounter_read_state_init(&state);
	if (amd64_timecounter_read_guarded(&state, skewed_sample, NULL, &value, &frequency)) {
		printf("FAIL read before publication\n");
		return 1;
	}
	if (!amd64_timecounter_read_state_publish(&state, 24000000U) ||
	    amd64_timecounter_read_state_publish(&state, 25000000U)) {
		printf("FAIL publication\n");
		return 1;
	}
	/* Unskewed readers race to publish, and nothing stepped back. */
	for (index = 0; index < THREADS; index++)
		pthread_create(&threads[index], NULL, reader, (void *)(uintptr_t)0);
	for (index = 0; index < THREADS; index++)
		pthread_join(threads[index], NULL);
	if (atomic_load(&printed) != 0) {
		printf("FAIL a regression was logged without skew\n");
		return 1;
	}

	/* Skewed readers. */
	for (index = 0; index < THREADS; index++)
		pthread_create(&threads[index], NULL, reader, (void *)(index * 97U));
	for (index = 0; index < THREADS; index++)
		pthread_join(threads[index], NULL);
	if (atomic_load(&failures) != 0) {
		printf("FAIL %d threads saw a value go back\n", atomic_load(&failures));
		return 1;
	}
	if (state.last_sample != atomic_load(&last_returned)) {
		printf("FAIL shared maximum %llu, largest returned %llu\n",
		    (unsigned long long)state.last_sample,
		    (unsigned long long)atomic_load(&last_returned));
		return 1;
	}
	amd64_timecounter_read_state_disable(&state);
	if (amd64_timecounter_read_guarded(&state, skewed_sample, NULL, &value, &frequency)) {
		printf("FAIL read after disable\n");
		return 1;
	}
	printf("timecounter-test: PASS (%d threads x %d reads)\n", THREADS, READS);
	return 0;
}
