/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws056-p002 (BUG-046): a program that sets its whole signal mask (here
 * to the empty set) must not see EINTR from a blocking call when a
 * SIGEV_THREAD timer fires.  The library's reserved wake signal has to
 * stay blocked in the program's thread, so that the kernel hands the wake
 * to the library's worker.  Each round sets the mask, arms a timer whose
 * callback posts a semaphore, and waits for the semaphore; the rounds
 * with EINTR are counted.  Prints "EINTR n/ROUNDS" and exits 1 when n is
 * not 0.
 */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The rounds of the check. */
#define ROUNDS 300

/* The semaphore the timer's callback posts. */
static sem_t fired;

static void callback(union sigval value);
static int one_round(int use_pthread);

/*
 * Runs the rounds, half with sigprocmask and half with pthread_sigmask,
 * and reports the rounds that saw EINTR.
 */
int
main(
	void)
{
	int interrupted;
	int failed;
	int round;

	/* Prepares the semaphore. */
	sem_init(&fired, 0, 0);

	/* Runs each round; one that went wrong otherwise stops the check. */
	interrupted = 0;
	for (round = 0; round < ROUNDS; round++) {
		failed = one_round(round % 2);
		if (failed < 0)
			return 2;
		interrupted += failed;
	}

	/* Reports the count. */
	printf("EINTR %d/%d\n", interrupted, ROUNDS);
	if (interrupted != 0)
		return 1;

	/* Succeeded: no round saw EINTR. */
	return 0;
}

/* Posts the semaphore, as the timer's callback. */
static void
callback(
	union sigval value)
{
	(void)value;

	/* Tells the waiting thread that the timer fired. */
	sem_post(&fired);
}

/*
 * Sets the whole mask to the empty set, arms a SIGEV_THREAD timer and
 * waits for its callback.  Returns 1 for EINTR, 0 when the callback came,
 * and -1 on any other failure.
 */
static int
one_round(
	int use_pthread)
{
	struct sigevent event;
	struct itimerspec value;
	struct timespec deadline;
	sigset_t empty;
	timer_t timer;
	int status;
	int error;

	/* Replaces the whole mask, as programs restoring a saved mask do. */
	sigemptyset(&empty);
	if (use_pthread)
		pthread_sigmask(SIG_SETMASK, &empty, NULL);
	else
		sigprocmask(SIG_SETMASK, &empty, NULL);

	/* Arms a one-shot timer of one millisecond whose callback posts. */
	memset(&event, 0, sizeof(event));
	event.sigev_notify = SIGEV_THREAD;
	event.sigev_notify_function = callback;
	status = timer_create(CLOCK_MONOTONIC, &event, &timer);
	if (status != 0)
		return -1;
	memset(&value, 0, sizeof(value));
	value.it_value.tv_nsec = 1000000L;
	status = timer_settime(timer, 0, &value, NULL);
	if (status != 0)
		return -1;

	/* Waits for the callback for up to two seconds. */
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 2;
	status = sem_timedwait(&fired, &deadline);
	error = errno;

	/* The timer is not needed again. */
	timer_delete(timer);

	/* EINTR is what BUG-046 was. */
	if (status != 0 && error == EINTR)
		return 1;

	/* Any other failure (a timeout, say) stops the check. */
	if (status != 0)
		return -1;

	/* Succeeded: the callback came. */
	return 0;
}
