/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of poll's deadline (src/kern/poll.c compiled unchanged,
 * T1-129): a descriptor that became ready as the deadline passed (the
 * system busy past it, the wake and the deadline together) is reported,
 * not lost as a timeout.  The waits are simulated: the sleep returns
 * ETIMEDOUT or wakes, and may make the descriptor ready, and the clock
 * may be past the deadline.
 *
 *   sh plan/ws132/tests/run-host-poll-timeout.sh
 */

#include <kern/poll.h>
#include <kern/file.h>
#include <kern/fd-object.h>
#include <kern/filedesc.h>
#include <kern/lock.h>
#include <kern/process.h>
#include <kern/sched.h>
#include <kern/signal.h>
#include <kern/thread.h>
#include <kern/waitq.h>

#include <stdio.h>
#include <string.h>
#include <uapi/errno.h>

/* What the simulated sleep does: the result it gives, whether the descriptor becomes ready, the clock after it. */
static int sleep_result;
static int sleep_makes_ready;
static uint64_t sleep_clock;

/* The simulated state: the descriptor's readiness, the clock, and the sleeps taken. */
static int device_ready;
static uint64_t simulated_ticks;
static unsigned sleeps;

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* The polled file and its operations. */
static struct file polled;
static struct file_ops polled_ops;
static struct filedesc table;
static struct process owner;

static void check(int condition, const char *what);
static int polled_poll(struct file *file, short events, short *revents);
static int run(int result, int makes_ready, uint64_t clock_after, int ready_before, int *revents);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* The polled file's readiness. */
static int
polled_poll(
	struct file *file,
	short events,
	short *revents)
{
	/* Readable while the device is ready. */
	(void)file;
	*revents = 0;
	if (device_ready)
		*revents = (short)(events & POLLIN);
	return 0;
}

/* Polls once with a deadline of 100 ticks, the sleep simulated as told.  Gives the ready count. */
static int
run(
	int result,
	int makes_ready,
	uint64_t clock_after,
	int ready_before,
	int *revents)
{
	struct pollfd fds[1];
	int ready;
	int error;

	/* The simulation. */
	sleep_result = result;
	sleep_makes_ready = makes_ready;
	sleep_clock = clock_after;
	device_ready = ready_before;
	simulated_ticks = 0;
	sleeps = 0;

	/* The poll. */
	memset(fds, 0, sizeof(fds));
	fds[0].fd = 3;
	fds[0].events = POLLIN;
	ready = -1;
	error = kern_poll_wait(&owner, fds, 1, 100, 0, &ready);
	check(error == 0, "the poll succeeds");
	*revents = fds[0].revents;
	return ready;
}

/* Runs every case. */
int
main(void)
{
	int revents;
	int ready;

	/* The process, its table and the file. */
	polled_ops.poll = polled_poll;
	polled.f_ops = &polled_ops;
	owner.fd = &table;
	poll_init();

	/* 1. Ready at the first scan: no sleep. */
	ready = run(ETIMEDOUT, 0, 200, 1, &revents);
	check(ready == 1 && revents == POLLIN && sleeps == 0U, "ready at once, no sleep");

	/* 2. The deadline passes with nothing: a timeout. */
	ready = run(ETIMEDOUT, 0, 200, 0, &revents);
	check(ready == 0 && revents == 0, "nothing by the deadline: a timeout");

	/* 3. Ready as the deadline passed (the sleep timed out, the change came with it): reported (T1-129). */
	ready = run(ETIMEDOUT, 1, 200, 0, &revents);
	check(ready == 1 && revents == POLLIN, "ready as the deadline passed: reported");

	/* 4. Woken by the change before the deadline: reported. */
	ready = run(0, 1, 50, 0, &revents);
	check(ready == 1 && revents == POLLIN, "woken by the change: reported");

	/* 5. Woken by the change, the clock already past the deadline: reported. */
	ready = run(0, 1, 200, 0, &revents);
	check(ready == 1 && revents == POLLIN, "woken past the deadline by the change: reported");

	/* 6. Woken by nothing past the deadline: a timeout. */
	ready = run(0, 0, 200, 0, &revents);
	check(ready == 0 && revents == 0, "woken past the deadline by nothing: a timeout");

	/* The result. */
	if (failures != 0) {
		printf("host-poll-timeout: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-poll-timeout: ok (%d checks)\n", checks);
	return 0;
}

/* The kernel's parts poll.c uses, simulated. */

/* No locks on the host. */
void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	/* Nothing to set up. */
	(void)lock;
	(void)rank;
	(void)name;
}

/* No interrupts to keep off. */
unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* Nothing to save. */
	(void)lock;
	return 0;
}

/* Nothing to give back. */
void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	/* Nothing to restore. */
	(void)lock;
	(void)enabled;
}

/* A queue needs nothing. */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* Nothing to set up. */
	(void)queue;
	(void)name;
}

/* The queue's sequence never matters here. */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Always the same. */
	(void)queue;
	return 0;
}

/* Nobody sleeps for real. */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* Nothing to wake. */
	(void)queue;
}

/* The sleep: the change it brings, the clock after it, and its result. */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *condition_lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* As the case says. */
	(void)queue;
	(void)condition_lock;
	(void)observed;
	(void)deadline;
	(void)flags;
	sleeps++;
	if (sleep_makes_ready)
		device_ready = 1;
	simulated_ticks = sleep_clock;
	return sleep_result;
}

/* The simulated clock. */
uint64_t
sched_ticks(void)
{
	/* Its ticks. */
	return simulated_ticks;
}

/* No thread: no signal either. */
struct thread *
thread_current(void)
{
	/* None. */
	return NULL;
}

/* No signal is pending. */
int
signal_pending_unblocked(
	const struct thread *thread)
{
	/* Never. */
	(void)thread;
	return 0;
}

/* The table needs no count of its polls. */
void
filedesc_poll_begin(
	struct filedesc *fd)
{
	/* Nothing to count. */
	(void)fd;
}

/* Nor its end. */
void
filedesc_poll_end(
	struct filedesc *fd)
{
	/* Nothing to count. */
	(void)fd;
}

/* Descriptor 3 is the polled file; others are closed. */
int
filedesc_get_object_ref(
	struct filedesc *fd,
	int descriptor,
	struct fd_object *result)
{
	/* Only the one. */
	(void)fd;
	if (descriptor != 3)
		return EBADF;
	result->type = FD_OBJECT_FILE;
	result->data.file = &polled;
	return 0;
}

/* The reference needs no release. */
int
fd_object_put(
	struct fd_object *object)
{
	/* Nothing to release. */
	(void)object;
	return 0;
}
