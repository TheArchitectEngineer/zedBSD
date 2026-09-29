/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws073-p038 (BUG-110): the thread-local storage of libc (errno, the
 * floating-point exceptions) seen from several threads and from signal
 * handlers, and the time of one access.
 *
 * Each of TLS_THREADS threads writes its own errno and floating-point
 * exceptions many times and checks that it reads back its own values, and
 * that errno's address is its own; each sends itself SIGUSR1, whose handler
 * checks that it sees the interrupted thread's errno address and value and
 * that its own changes are not seen after it returns.  The main thread
 * does the same before and after.  Then the time of errno's address
 * (__libc_errno_location, a global-dynamic TLS access in libc.so) and of
 * feraiseexcept is printed.  The last line is TLS-CHECK:PASS or
 * TLS-CHECK:FAIL.  Built dynamic (libc.so, the loader's __tls_get_addr) and
 * static (libc.a) by run-tls-check.sh.  The static start (crt0) ends with
 * _exit, so the output is flushed before each return.
 */

#include <errno.h>
#include <fenv.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The threads, each one's rounds, and the timed accesses. */
#define TLS_THREADS		8
#define TLS_ROUNDS		20000
#define TLS_TIMED		2000000

/* One thread's record: its number, its errno's address, what the signal handler saw, and its verdict. */
struct tls_thread {
	int number;
	int *errno_address;
	volatile int handler_ran;
	volatile int handler_ok;
	int failures;
};

/*
 * The record of the thread the signal is delivered to (each thread sends
 * it to itself only, one at a time under the lock).
 */
static struct tls_thread *volatile tls_signalled;
static pthread_mutex_t tls_signal_lock = PTHREAD_MUTEX_INITIALIZER;

static void *tls_thread_main(void *argument);
static void tls_exercise(struct tls_thread *record);
static void tls_handler(int number);
static double tls_seconds(void);

/*
 * Runs the threads and the timing, and prints the verdict.
 */
int
main(
	void)
{
	static struct tls_thread records[TLS_THREADS + 1];
	pthread_t threads[TLS_THREADS];
	struct sigaction action;
	volatile int sink;
	double start;
	double errno_ns;
	double fenv_ns;
	int failures;
	int error;
	int index;

	/* The handler of SIGUSR1. */
	memset(&action, 0, sizeof(action));
	action.sa_handler = tls_handler;
	sigemptyset(&action.sa_mask);
	error = sigaction(SIGUSR1, &action, NULL);
	if (error != 0) {
		printf("sigaction errno=%d\nTLS-CHECK:FAIL\n", errno);
		fflush(stdout);
		return 1;
	}

	/* The main thread first, then the threads together, then the main thread again. */
	records[TLS_THREADS].number = 1000;
	tls_exercise(&records[TLS_THREADS]);
	for (index = 0; index < TLS_THREADS; index++) {
		records[index].number = index + 1;
		error = pthread_create(&threads[index], NULL, tls_thread_main, &records[index]);
		if (error != 0) {
			printf("pthread_create %d failed\nTLS-CHECK:FAIL\n", index);
			fflush(stdout);
			return 1;
		}
	}

	/* The threads' end, then the main thread again. */
	for (index = 0; index < TLS_THREADS; index++)
		(void)pthread_join(threads[index], NULL);
	tls_exercise(&records[TLS_THREADS]);

	/* Every thread's verdict, and that the errno addresses differ. */
	failures = 0;
	for (index = 0; index <= TLS_THREADS; index++) {
		failures += records[index].failures;
		if (index < TLS_THREADS && records[index].errno_address == records[TLS_THREADS].errno_address) {
			printf("thread %d shares the main thread's errno\n", index);
			failures++;
		}
	}

	/* The time of one errno access and one feraiseexcept. */
	start = tls_seconds();
	for (index = 0; index < TLS_TIMED; index++)
		errno = index;
	errno_ns = (tls_seconds() - start) / TLS_TIMED * 1e9;
	start = tls_seconds();
	for (index = 0; index < TLS_TIMED; index++)
		sink = feraiseexcept(FE_INEXACT);
	fenv_ns = (tls_seconds() - start) / TLS_TIMED * 1e9;
	(void)sink;
	printf("TLS-TIME errno_ns=%.1f feraiseexcept_ns=%.1f\n", errno_ns, fenv_ns);

	/* The verdict. */
	if (failures != 0) {
		printf("failures=%d\nTLS-CHECK:FAIL\n", failures);
		fflush(stdout);
		return 1;
	}

	/* Succeeded: every thread and handler saw its own TLS. */
	printf("TLS-CHECK:PASS threads=%d rounds=%d\n", TLS_THREADS, TLS_ROUNDS);
	fflush(stdout);
	return 0;
}

/* Runs one thread's exercise. */
static void *
tls_thread_main(
	void *argument)
{
	/* The thread's record. */
	tls_exercise(argument);
	return NULL;
}

/* Writes and reads back this thread's errno and exceptions, and has a signal handler look at them. */
static void
tls_exercise(
	struct tls_thread *record)
{
	int round;
	int value;
	int flags;
	int expected;

	/* The thread's own errno, written and read back many times while the other threads do the same. */
	record->errno_address = &errno;
	for (round = 0; round < TLS_ROUNDS; round++) {
		value = record->number * 100000 + round;
		errno = value;
		(void)feclearexcept(FE_ALL_EXCEPT);
		if ((round & 1) != 0)
			(void)feraiseexcept(FE_INEXACT);
		sched_yield();
		flags = fetestexcept(FE_ALL_EXCEPT);
		if (errno != value || &errno != record->errno_address) {
			record->failures++;
			continue;
		}

		/* The inexact exception is raised on the odd rounds only. */
		expected = 0;
		if ((round & 1) != 0)
			expected = FE_INEXACT;
		if (flags != expected)
			record->failures++;
	}

	/* A signal to itself: the handler sees this thread's errno, and its changes do not stay. */
	pthread_mutex_lock(&tls_signal_lock);
	errno = record->number;
	tls_signalled = record;
	record->handler_ran = 0;
	(void)pthread_kill(pthread_self(), SIGUSR1);
	if (!record->handler_ran || !record->handler_ok) {
		printf("thread %d: handler ran=%d ok=%d\n", record->number, record->handler_ran, record->handler_ok);
		record->failures++;
	}

	/* The handler's change of errno did not stay. */
	if (errno != record->number) {
		printf("thread %d: errno %d after the handler\n", record->number, errno);
		record->failures++;
	}

	/* The signal is over. */
	tls_signalled = NULL;
	pthread_mutex_unlock(&tls_signal_lock);
}

/* Checks the interrupted thread's errno from its signal handler, and changes it (the handler's return restores it). */
static void
tls_handler(
	int number)
{
	struct tls_thread *record;
	int saved;

	/* The record of the thread the signal was sent to. */
	(void)number;
	saved = errno;
	record = tls_signalled;
	if (record == NULL)
		return;

	/* The same errno, at the same address. */
	record->handler_ok = 0;
	if (&errno == record->errno_address && errno == record->number)
		record->handler_ok = 1;

	/* Changes it, then puts it back as a handler must. */
	errno = -1;
	errno = saved;
	record->handler_ran = 1;
}

/* Reports a monotonic time in seconds. */
static double
tls_seconds(
	void)
{
	struct timespec now;

	/* The monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}
