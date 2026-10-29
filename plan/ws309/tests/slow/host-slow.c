/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the event loop's side of the compositor's threads while
 * every thread is slow (the design's review 11): the lanes as the
 * compositor starts them (userland/desktop/wayland/lane.c) run jobs that
 * stand for the slow calls -- a flush that takes 2 s (io-file), a device
 * open of 80 ms three times (io-device), a present that does not return
 * for 2 s (present), a daemon that never accepts until the end (daemon),
 * four handwriting matches of 500 ms (a pool of 4) -- while the main
 * thread, for 2.5 s, gives each lane a job, takes what came back after its
 * wake pipe and replaces a keyed job, as its passes do.  Every give and
 * take must return within 5 ms; the jobs must all come back.
 * Prints "host-slow: PASS" or "host-slow: FAIL failures=N".
 */

#include "userland/desktop/wayland/lane.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* The longest a give or a take of the event loop may take (us), and how long the event loop runs (ms). */
#define TEST_CALL_MAX_US	5000U
#define TEST_LOOP_MS		2500U

/* The lanes the test starts. */
#define TEST_LANES		5U

/* One test job: how long its run takes, or whether it waits for the daemon's release. */
struct test_job {
	struct kwl_lane_job lane;
	unsigned sleep_ms;
	unsigned daemon;
};

static unsigned failures;
static unsigned done_count;
static unsigned dropped_count;

/* The daemon's stall: its job waits on this pipe's read end until the test writes. */
static int daemon_pipe[2];

static void job_run(struct kwl_lane_job *job);
static void job_done(void *context, struct kwl_lane_job *job);
static void job_drop(struct kwl_lane_job *job);
static struct test_job *job_new(unsigned sleep_for, unsigned daemon);
static void check(int condition, const char *what);
static uint64_t now_us(void);
static void give(struct kwl_lane *lane, unsigned sleep_for, unsigned daemon, uint64_t key, uint64_t *longest);
static void drain_wake(int descriptor);

/* The operations of the test's jobs. */
static const struct kwl_lane_ops job_ops = { job_run, job_done, job_drop };

/*
 * Starts the lanes, makes them slow, runs the event loop's side and checks
 * how long its calls took.
 */
int
main(
	void)
{
	static const char *const names[TEST_LANES] = { "io-file", "io-device", "present", "audiod", "hand" };
	static const unsigned counts[TEST_LANES] = { 1U, 1U, 1U, 1U, 4U };
	struct kwl_lane lanes[TEST_LANES];
	struct pollfd watch;
	uint64_t longest_give;
	uint64_t longest_take;
	uint64_t started;
	uint64_t mark;
	uint64_t spent;
	unsigned given;
	unsigned index;
	unsigned pass;
	int wake[2];
	int error;

	/* The wake pipe as the compositor makes it, and the daemon's stall. */
	error = pipe(wake);
	check(error == 0, "wake pipe");
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);
	error = pipe(daemon_pipe);
	check(error == 0, "daemon pipe");

	/* The lanes. */
	for (index = 0U; index < TEST_LANES; index++) {
		error = kwl_lane_start(&lanes[index], names[index], counts[index], wake[1]);
		check(error == 0, "lane started");
	}

	/* Each made slow: a 2 s flush, three 80 ms opens, a 2 s present, a daemon that does not accept, four 500 ms matches. */
	longest_give = 0U;
	given = 0U;
	give(&lanes[0], 2000U, 0U, 0U, &longest_give);
	for (index = 0U; index < 3U; index++)
		give(&lanes[1], 80U, 0U, 0U, &longest_give);
	give(&lanes[2], 2000U, 0U, 0U, &longest_give);
	give(&lanes[3], 0U, 1U, 0U, &longest_give);
	for (index = 0U; index < 4U; index++)
		give(&lanes[4], 500U, 0U, 0U, &longest_give);
	given += 1U + 3U + 1U + 1U + 4U;

	/* The event loop: a pass every 2 ms gives each lane a keyed job and takes what came back. */
	longest_take = 0U;
	started = now_us();
	pass = 0U;
	while (now_us() - started < (uint64_t)TEST_LOOP_MS * 1000U) {
		/* A wake, or the pass's timeout. */
		watch.fd = wake[0];
		watch.events = POLLIN;
		watch.revents = 0;
		(void)poll(&watch, 1U, 2);
		drain_wake(wake[0]);

		/* Each lane's newest request of its kind replaces the one not started. */
		for (index = 0U; index < TEST_LANES; index++) {
			give(&lanes[index], 0U, 0U, 1U + index, &longest_give);
			given++;
		}

		/* What came back. */
		for (index = 0U; index < TEST_LANES; index++) {
			mark = now_us();
			(void)kwl_lane_take(&lanes[index], NULL);
			spent = now_us() - mark;
			if (spent > longest_take)
				longest_take = spent;
		}
		pass++;
	}

	/* The calls' times. */
	printf("host-slow: passes=%u given=%u longest_give_us=%llu longest_take_us=%llu\n", pass, given, (unsigned long long)longest_give, (unsigned long long)longest_take);
	check(pass >= 100U, "the event loop ran its passes meanwhile");
	check(longest_give < TEST_CALL_MAX_US, "every give returned within 5 ms");
	check(longest_take < TEST_CALL_MAX_US, "every take returned within 5 ms");

	/* The daemon accepts at last; every job comes back, run or replaced. */
	(void)write(daemon_pipe[1], "x", 1U);
	for (index = 0U; index < TEST_LANES; index++)
		kwl_lane_finish(&lanes[index], NULL);
	printf("host-slow: done=%u dropped=%u\n", done_count, dropped_count);
	check(done_count + dropped_count == given, "every job came back");
	for (index = 0U; index < TEST_LANES; index++)
		kwl_lane_stop(&lanes[index]);

	/* The summary. */
	if (failures != 0U) {
		printf("host-slow: FAIL failures=%u\n", failures);
		return 1;
	}
	printf("host-slow: PASS\n");
	return 0;
}

/* Stands for a slow call: sleeps, or waits for the daemon's release. */
static void
job_run(
	struct kwl_lane_job *lane_job)
{
	struct test_job *job;
	struct timespec span;
	char byte;

	/* The daemon that does not accept until the end. */
	job = (struct test_job *)lane_job;
	if (job->daemon) {
		(void)read(daemon_pipe[0], &byte, 1U);
		return;
	}

	/* The slow call. */
	span.tv_sec = (time_t)(job->sleep_ms / 1000U);
	span.tv_nsec = (long)(job->sleep_ms % 1000U) * 1000000L;
	(void)nanosleep(&span, NULL);
}

/* Counts a job back. */
static void
job_done(
	void *context,
	struct kwl_lane_job *lane_job)
{
	(void)context;
	done_count++;
	free(lane_job);
}

/* Counts a job replaced or dropped. */
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
	unsigned sleep_for,
	unsigned daemon)
{
	struct test_job *job;

	job = calloc(1U, sizeof(*job));
	if (job == NULL) {
		printf("host-slow: FAIL out of memory\n");
		exit(1);
	}
	job->sleep_ms = sleep_for;
	job->daemon = daemon;
	return job;
}

/* Gives a lane a job and keeps the longest give. */
static void
give(
	struct kwl_lane *lane,
	unsigned sleep_for,
	unsigned daemon,
	uint64_t key,
	uint64_t *longest)
{
	struct test_job *job;
	uint64_t mark;
	uint64_t spent;

	job = job_new(sleep_for, daemon);
	mark = now_us();
	kwl_lane_submit(lane, &job->lane, &job_ops, key);
	spent = now_us() - mark;
	if (spent > *longest)
		*longest = spent;
}

/* Empties the wake pipe. */
static void
drain_wake(
	int descriptor)
{
	char bytes[64];
	ssize_t got;

	do {
		got = read(descriptor, bytes, sizeof(bytes));
	} while (got > 0);
}

/* The monotonic clock in microseconds. */
static uint64_t
now_us(
	void)
{
	struct timespec now;

	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Counts a failed check and prints every check. */
static void
check(
	int condition,
	const char *what)
{
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		failures++;
}
