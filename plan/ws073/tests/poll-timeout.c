/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-123: does poll(2) return when its timeout passes, and does time(3)
 * advance meanwhile?  Built with the guest's clang:
 *
 *   cc -O2 -o poll-timeout poll-timeout.c
 *   poll-timeout MODE COUNT TIMEOUT_MS
 *
 * MODE picks what the one descriptor polled for POLLIN is:
 *
 *   pipe   the read end of a pipe nobody writes;
 *   unix   one end of a socketpair(AF_UNIX, SOCK_STREAM) nobody writes;
 *   busy   one end of a socketpair whose other end a child writes one byte to
 *          every 5 ms, read at once, so poll often returns with data (what
 *          desktop-probe's connection to the compositor sees);
 *   probe  desktop-probe's loop (plan/ws094/tests/desktop-probe.c): poll with
 *          TIMEOUT_MS until time() has advanced 3 s, COUNT times, on a
 *          socketpair nobody writes;
 *   hup    COUNT times a new socketpair whose other end a child closes by
 *          exiting after 0 to 20 ms (having written 0 to 3 bytes): the
 *          parent's poll for POLLIN must end at the close, a second poll at
 *          once too (the end of the stream stays readable, as libwayland's
 *          wl_display_dispatch, which polls again without a timeout,
 *          relies on), and the reads must reach the end of the stream.
 *
 * Every poll is timed with CLOCK_MONOTONIC.  A call that returns later than
 * its timeout by more than 100 ms is "late", one that returns earlier than
 * its timeout with nothing ready is "early", and one that reports POLLIN
 * when a read of the (non-blocking) descriptor then finds nothing is
 * "spurious".  One line per
 * late or spurious call, then a summary: the calls, the shortest and longest
 * time a timed-out call took, and the counts.  A call that never returns is
 * seen as the program not ending (the caller bounds it with timeout(1)).
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The counts of the summary. */
static unsigned long calls;
static unsigned long timed_out;
static unsigned long late;
static unsigned long early;
static unsigned long spurious;
static int64_t shortest_ms = INT64_MAX;
static int64_t longest_ms;

/* Reads CLOCK_MONOTONIC in milliseconds. */
static int64_t
now_ms(void)
{
	struct timespec clock;

	/* The monotonic time; a failure is reported as zero. */
	if (clock_gettime(CLOCK_MONOTONIC, &clock) != 0)
		return 0;
	return (int64_t)clock.tv_sec * 1000 + clock.tv_nsec / 1000000;
}

/* Polls the descriptor once for POLLIN, timing and checking the outcome; returns poll's result. */
static int
poll_once(
	int descriptor,
	int timeout_ms,
	int drain)
{
	struct pollfd request;
	int64_t started;
	int64_t took;
	char byte;
	ssize_t got;
	int status;

	/* The call, timed. */
	request.fd = descriptor;
	request.events = POLLIN;
	request.revents = 0;
	started = now_ms();
	status = poll(&request, 1, timeout_ms);
	took = now_ms() - started;
	calls++;

	/* A timed-out call: its length, and whether it was early or late. */
	if (status == 0) {
		timed_out++;
		if (took < shortest_ms)
			shortest_ms = took;
		if (took > longest_ms)
			longest_ms = took;
		if (took + 1 < timeout_ms) {
			early++;
			printf("early: %lld ms for a %d ms timeout\n", (long long)took, timeout_ms);
		}
	}

	/* Any call that took much longer than its timeout. */
	if (took > timeout_ms + 100) {
		late++;
		printf("late: %lld ms for a %d ms timeout (poll %d, revents 0x%x)\n", (long long)took, timeout_ms, status, request.revents);
	}

	/* Readiness: something must be there to read (and is read when asked). */
	if (status > 0 && (request.revents & POLLIN) != 0) {
		got = read(descriptor, &byte, 1);
		if (got < 0 && errno == EAGAIN) {
			spurious++;
			printf("spurious: POLLIN with nothing to read after %lld ms\n", (long long)took);
		}
		while (drain && got > 0)
			got = read(descriptor, &byte, 1);
	}
	if (status < 0)
		printf("error: poll %d errno %d\n", status, errno);

	/* The call's result. */
	return status;
}

/*
 * One round of the hup mode: a child writes `bytes` bytes, waits `delay_ms`
 * and exits; the parent polls (TIMEOUT_MS), reads what is there, and polls
 * again: both polls must report the stream readable (POLLIN or POLLHUP)
 * well before the timeout, and the reads must end at 0 (the end).  Returns
 * 1 when the round went wrong, after one line saying how.
 */
static int
hup_round(
	int timeout_ms,
	unsigned round)
{
	struct pollfd request;
	struct timespec pause;
	int64_t started;
	int64_t first_ms;
	int64_t second_ms;
	int ends[2];
	int first;
	int second;
	unsigned bytes;
	unsigned index;
	ssize_t got;
	pid_t child;
	char byte;

	/* The pair; the child keeps the other end. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0)
		return 1;
	bytes = round % 4U;
	pause.tv_sec = 0;
	pause.tv_nsec = (long)(round % 21U) * 1000000L;
	child = fork();
	if (child == 0) {
		(void)close(ends[0]);
		byte = 'y';
		for (index = 0; index < bytes; index++)
			(void)write(ends[1], &byte, 1);
		(void)nanosleep(&pause, NULL);
		_exit(0);
	}
	(void)close(ends[1]);
	(void)fcntl(ends[0], F_SETFL, O_NONBLOCK);

	/* The first poll ends at data or at the close, and what is there is read. */
	request.fd = ends[0];
	request.events = POLLIN;
	request.revents = 0;
	started = now_ms();
	first = poll(&request, 1, timeout_ms);
	first_ms = now_ms() - started;
	do {
		got = read(ends[0], &byte, 1);
	} while (got > 0);

	/* Until the close, more polls; then one more poll must end at once. */
	while (got < 0 && errno == EAGAIN) {
		request.revents = 0;
		first = poll(&request, 1, timeout_ms);
		first_ms = now_ms() - started;
		if (first <= 0)
			break;
		do {
			got = read(ends[0], &byte, 1);
		} while (got > 0);
	}
	request.revents = 0;
	started = now_ms();
	second = poll(&request, 1, timeout_ms);
	second_ms = now_ms() - started;
	(void)waitpid(child, NULL, 0);
	(void)close(ends[0]);
	calls += 2;

	/* The stream ended (read 0), and neither poll waited for its timeout. */
	if (got != 0 || first <= 0 || second <= 0 || second_ms > 50) {
		printf("hup round %u: bytes %u, delay %ld ms: read %zd (errno %d), poll %d after %lld ms, again %d after %lld ms (revents 0x%x)\n",
		       round, bytes, pause.tv_nsec / 1000000L, got, got < 0 ? errno : 0,
		       first, (long long)first_ms, second, (long long)second_ms, request.revents);
		return 1;
	}

	/* Succeeded. */
	return 0;
}

/* Writes one byte to the descriptor every 5 ms until the parent goes. */
static void
writer(
	int descriptor)
{
	struct timespec pause;
	char byte;

	/* Until a write fails (the other end closed). */
	byte = 'x';
	pause.tv_sec = 0;
	pause.tv_nsec = 5000000;
	while (write(descriptor, &byte, 1) == 1)
		(void)nanosleep(&pause, NULL);
	_exit(0);
}

int
main(
	int argc,
	char **argv)
{
	unsigned long count;
	unsigned long index;
	int ends[2];
	int timeout_ms;
	int polled;
	pid_t child;
	time_t started;
	int64_t loop_started;
	int64_t loop_ms;
	int64_t loop_longest;
	unsigned long failures;

	/* The mode, the count and the timeout. */
	if (argc != 4) {
		fprintf(stderr, "usage: poll-timeout pipe|unix|busy|probe|hup COUNT TIMEOUT_MS\n");
		return 2;
	}
	count = strtoul(argv[2], NULL, 10);
	timeout_ms = atoi(argv[3]);

	/* The hup mode makes a pair per round. */
	if (strcmp(argv[1], "hup") == 0) {
		failures = 0;
		for (index = 0; index < count; index++)
			failures += (unsigned long)hup_round(timeout_ms, (unsigned)index);
		printf("hup: %lu rounds, %lu wrong\n", count, failures);
		return 0;
	}

	/* The descriptor. */
	child = -1;
	if (strcmp(argv[1], "pipe") == 0) {
		if (pipe(ends) != 0)
			return 1;
	} else {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0)
			return 1;
	}
	polled = ends[0];
	(void)fcntl(polled, F_SETFL, O_NONBLOCK);

	/* The busy mode's writer. */
	if (strcmp(argv[1], "busy") == 0) {
		child = fork();
		if (child == 0) {
			(void)close(ends[0]);
			writer(ends[1]);
		}
	}

	/* desktop-probe's loop: poll until time() has moved 3 s on, COUNT times. */
	if (strcmp(argv[1], "probe") == 0) {
		loop_longest = 0;
		for (index = 0; index < count; index++) {
			started = time(NULL);
			loop_started = now_ms();
			while (time(NULL) - started < 3)
				(void)poll_once(polled, timeout_ms, 1);
			loop_ms = now_ms() - loop_started;
			if (loop_ms > loop_longest)
				loop_longest = loop_ms;
			if (loop_ms > 4100)
				printf("probe: a 3 s loop took %lld ms\n", (long long)loop_ms);
		}
		printf("probe loops %lu, longest %lld ms\n", count, (long long)loop_longest);
	} else {
		for (index = 0; index < count; index++)
			(void)poll_once(polled, timeout_ms, 1);
	}

	/* The busy mode's writer goes with the socket. */
	if (child > 0) {
		(void)close(ends[0]);
		(void)waitpid(child, NULL, 0);
	}

	/* The summary. */
	if (timed_out == 0U)
		shortest_ms = 0;
	printf("%s: %lu calls, %lu timed out (%lld..%lld ms for %d ms), %lu late, %lu early, %lu spurious\n",
	       argv[1], calls, timed_out, (long long)shortest_ms, (long long)longest_ms, timeout_ms, late, early, spurious);
	return 0;
}
