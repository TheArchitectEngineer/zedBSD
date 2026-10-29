/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the compositor's lanes and thread register
 * (userland/desktop/wayland/lane.c, threads.c): order, the wake byte,
 * replacement by key, a pool's threads at once, finish and stop, the
 * register's names and the blocked signals.
 */

#include "userland/desktop/wayland/lane.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* One test job: its number, how long its run sleeps, and what came of it. */
struct test_job {
	struct kwl_lane_job lane;
	unsigned number;
	unsigned sleep_ms;
	unsigned ran;
	unsigned blocked;
};

/* What the test's main thread saw: the order done and dropped, and the most runs at once. */
static unsigned done_order[64];
static unsigned done_count;
static unsigned dropped_count;
static unsigned failures;
static pthread_mutex_t running_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned running_now;
static unsigned running_most;

static void job_run(struct kwl_lane_job *job);
static void job_done(void *context, struct kwl_lane_job *job);
static void job_drop(struct kwl_lane_job *job);
static struct test_job *job_new(unsigned number, unsigned sleep_ms);
static void check(int condition, const char *what);
static unsigned wake_bytes(int descriptor);
static void sleep_ms(unsigned milliseconds);
static unsigned take_until(struct kwl_lane *lane, int descriptor, unsigned wanted);
static void *waiter(void *argument);

/* The operations of the test's jobs. */
static const struct kwl_lane_ops job_ops = { job_run, job_done, job_drop };

int
main(void)
{
	struct kwl_lane lane;
	struct test_job *job;
	pthread_t other;
	unsigned taken;
	unsigned index;
	int pipes[2];
	int error;

	/* The wake pipe, as the compositor makes it: neither end blocks. */
	error = pipe(pipes);
	check(error == 0, "pipe");
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(pipes[1], F_SETFL, O_NONBLOCK);

	/* One thread: the jobs run in their order and wake the main thread. */
	error = kwl_lane_start(&lane, "io-file", 1U, pipes[1]);
	check(error == 0, "start one");
	check(kwl_threads_count() == 1U, "register one");
	kwl_threads_report();
	for (index = 0U; index < 5U; index++) {
		job = job_new(index, 5U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	}
	taken = take_until(&lane, pipes[0], 5U);
	check(taken == 5U, "five taken");
	for (index = 0U; index < 5U; index++)
		check(done_order[index] == index, "order of one thread");
	check(kwl_lane_busy(&lane) == 0U, "idle after");

	/*
	 * Replacement: a slow job runs, three jobs of key 7 wait behind it;
	 * only the last of them runs, the two before come back dropped.
	 */
	done_count = 0U;
	dropped_count = 0U;
	job = job_new(100U, 50U);
	kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	sleep_ms(10U);
	for (index = 0U; index < 3U; index++) {
		job = job_new(200U + index, 0U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 7U);
	}
	taken = take_until(&lane, pipes[0], 4U);
	check(taken == 4U, "four back");
	check(dropped_count == 2U, "two replaced");
	check(done_count == 2U, "two run");
	check(done_order[0] == 100U, "slow first");
	check(done_order[1] == 202U, "latest of the key");

	/*
	 * Wait from another thread (the display thread for the present
	 * thread): it returns once both jobs ran, and takes none of them.
	 */
	done_count = 0U;
	for (index = 0U; index < 2U; index++) {
		job = job_new(250U + index, 20U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	}
	error = pthread_create(&other, NULL, waiter, &lane);
	check(error == 0, "waiter");
	(void)pthread_join(other, NULL);
	check(kwl_lane_busy(&lane) == 0U, "idle after the wait");
	check(done_count == 0U, "the wait takes nothing");
	taken = take_until(&lane, pipes[0], 2U);
	check(taken == 2U, "two taken after the wait");

	/* Finish: every job given runs, then they are taken. */
	done_count = 0U;
	for (index = 0U; index < 3U; index++) {
		job = job_new(300U + index, 5U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	}
	kwl_lane_finish(&lane, NULL);
	check(done_count == 3U, "finished three");

	/* Stop with jobs waiting: the running one ends, the rest are dropped. */
	dropped_count = 0U;
	job = job_new(400U, 30U);
	kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	sleep_ms(5U);
	for (index = 0U; index < 3U; index++) {
		job = job_new(401U + index, 0U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	}
	kwl_lane_stop(&lane);
	check(dropped_count == 4U, "stop drops the waiting and the untaken");
	check(kwl_threads_count() == 0U, "register empty");
	(void)wake_bytes(pipes[0]);

	/* A pool of four: four jobs run at once, named hand-1 to hand-4. */
	running_most = 0U;
	done_count = 0U;
	error = kwl_lane_start(&lane, "hand", 4U, pipes[1]);
	check(error == 0, "start pool");
	check(kwl_threads_count() == 4U, "register four");
	kwl_threads_report();
	for (index = 0U; index < 8U; index++) {
		job = job_new(500U + index, 40U);
		kwl_lane_submit(&lane, &job->lane, &job_ops, 0U);
	}
	taken = take_until(&lane, pipes[0], 8U);
	check(taken == 8U, "pool eight");
	check(running_most == 4U, "four at once");
	kwl_lane_stop(&lane);

	/* A count out of range is refused. */
	error = kwl_lane_start(&lane, "bad", 0U, pipes[1]);
	check(error == EINVAL, "zero threads refused");
	error = kwl_lane_start(&lane, "bad", KWL_LANE_THREADS_MAX + 1U, pipes[1]);
	check(error == EINVAL, "too many refused");

	/* The processors are counted as at least one. */
	check(kwl_cpu_count() >= 1U, "cpu count");

	/* The verdict. */
	close(pipes[0]);
	close(pipes[1]);
	if (failures != 0U) {
		printf("host-lane: FAIL failures=%u\n", failures);
		return 1;
	}
	printf("host-lane: PASS\n");
	return 0;
}

/* Sleeps as long as the job says, counts the runs at once, and notes whether SIGINT is blocked. */
static void
job_run(
	struct kwl_lane_job *lane_job)
{
	struct test_job *job;
	sigset_t mask;

	job = (struct test_job *)lane_job;

	/* The lane's threads take no signals. */
	(void)pthread_sigmask(SIG_SETMASK, NULL, &mask);
	job->blocked = (unsigned)sigismember(&mask, SIGINT);

	/* The runs at once. */
	pthread_mutex_lock(&running_lock);
	running_now++;
	if (running_now > running_most)
		running_most = running_now;
	pthread_mutex_unlock(&running_lock);

	sleep_ms(job->sleep_ms);
	job->ran = 1U;

	pthread_mutex_lock(&running_lock);
	running_now--;
	pthread_mutex_unlock(&running_lock);
}

/* Notes a job done, in the order taken. */
static void
job_done(
	void *context,
	struct kwl_lane_job *lane_job)
{
	struct test_job *job;

	(void)context;
	job = (struct test_job *)lane_job;
	check(job->ran == 1U, "done after run");
	check(job->blocked == 1U, "signals blocked");
	if (done_count < 64U)
		done_order[done_count] = job->number;
	done_count++;
	free(job);
}

/* Notes a job dropped. */
static void
job_drop(
	struct kwl_lane_job *lane_job)
{
	dropped_count++;
	free(lane_job);
}

/* Makes a job. */
static struct test_job *
job_new(
	unsigned number,
	unsigned sleep_for)
{
	struct test_job *job;

	job = calloc(1U, sizeof(*job));
	if (job == NULL) {
		printf("host-lane: FAIL out of memory\n");
		exit(1);
	}
	job->number = number;
	job->sleep_ms = sleep_for;
	return job;
}

/* Counts a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (condition)
		return;
	printf("host-lane: check failed: %s\n", what);
	failures++;
}

/* Empties the wake pipe and counts its bytes. */
static unsigned
wake_bytes(
	int descriptor)
{
	unsigned char bytes[64];
	unsigned count;
	ssize_t got;

	count = 0U;
	for (;;) {
		got = read(descriptor, bytes, sizeof(bytes));
		if (got <= 0)
			break;
		count += (unsigned)got;
	}
	return count;
}

/* Sleeps a number of milliseconds. */
static void
sleep_ms(
	unsigned milliseconds)
{
	struct timespec span;

	span.tv_sec = (time_t)(milliseconds / 1000U);
	span.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	while (nanosleep(&span, &span) != 0 && errno == EINTR)
		continue;
}

/*
 * Takes jobs as the compositor does (a wake byte, then the taking) until
 * wanted have come back, within 5 s; every taking follows at least one
 * byte.
 */
static unsigned
take_until(
	struct kwl_lane *lane,
	int descriptor,
	unsigned wanted)
{
	unsigned taken;
	unsigned waited;
	unsigned bytes;

	taken = 0U;
	for (waited = 0U; waited < 5000U && taken < wanted; waited++) {
		bytes = wake_bytes(descriptor);
		if (bytes == 0U) {
			sleep_ms(1U);
			continue;
		}
		taken += kwl_lane_take(lane, NULL);
	}
	return taken;
}

/* Waits on another thread until the lane has run every job given to it. */
static void *
waiter(
	void *argument)
{
	/* The wait. */
	kwl_lane_wait(argument);
	return NULL;
}
