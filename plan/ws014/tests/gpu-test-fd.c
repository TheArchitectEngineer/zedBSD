/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Process and scheduler collaborators for GPU tests linked to real descriptor code.
 */

#include <kern/process.h>
#include <kern/thread.h>
#include <kern/filedesc.h>
#include <kern/record-lock.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One host caller selects the process namespace used by actual handle_fd functions. */
static struct thread gpu_test_thread;

/* The controlled clock advances only at an explicit fixture scheduling boundary. */
uint64_t gpu_test_ticks = 1U;

/* Selected worker allocation failure stays entirely within the scheduler peer. */
int gpu_test_thread_error;

/* A selected test schedules one controlled interleaving at the actual production wait boundary. */
int (*gpu_test_wait_hook)(struct wait_queue *, struct spinlock *, uint64_t, uint64_t, unsigned);

/*
 * Selects a process between completed test operations.
 */
void
gpu_test_set_process(
	struct process *process)
{
	/* No production descriptor operation is replaced by this caller selection. */
	gpu_test_thread.proc = process;

	/* Succeeded: subsequent real syscalls use the selected process's descriptor table. */
	return;
}

/*
 * Supplies the kernel caller without borrowing host thread ABI declarations.
 */
struct thread *
thread_current(
	void)
{
	/* Succeeded: the fixture controls this thread throughout each operation. */
	return &gpu_test_thread;
}

/*
 * Reports assertions made through the target libc declarations.
 */
void
__libc_assert_fail(
	const char *expression,
	const char *file,
	int line)
{
	/* Preserve the actual failed invariant instead of masking it in the peer. */
	fprintf(stderr, "ASSERT %s:%d: %s\n", file, line, expression);
	abort();
}

/*
 * Initializes descriptor reservation wait channels.
 */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* Real descriptor code owns every subsequent generation transition. */
	memset(queue, 0, sizeof(*queue));
	queue->name = name;

	/* Succeeded: the channel begins with no published reservation change. */
	return;
}

/*
 * Observes the reservation wake generation.
 */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Succeeded: the scalar snapshot is stable in this bounded single-threaded peer. */
	return queue->sequence;
}

/*
 * Publishes reservation completion to the real descriptor transaction logic.
 */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* A changed sequence invalidates an earlier reservation wait observation. */
	queue->sequence++;

	/* Succeeded: a waiter can now distinguish the completed reservation. */
	return;
}

/*
 * Refuses an unexpected blocking path in this sequential ownership fixture.
 */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	int error;

	/* The real wait primitive observes a changed condition before entering the scheduler. */
	if (queue->sequence != observed)
		return EAGAIN;

	/* An expired finite observation does not need a fabricated scheduling event. */
	if (deadline != 0U && gpu_test_ticks >= deadline)
		return ETIMEDOUT;

	/* Only explicitly selected race scenarios supply scheduler progress at this real sleep boundary. */
	if (gpu_test_wait_hook != NULL) {
		error = gpu_test_wait_hook(queue, lock, observed, deadline, flags);
		return error;
	}

	/* The GPU test does not use sleeping descriptor operations to fake progress. */
	(void)queue;
	(void)lock;
	(void)observed;
	(void)deadline;
	(void)flags;
	abort();
}

/*
 * Detects accidental treatment of a kernel capability as an inode-backed file.
 */
void
record_lock_release_process_inode(
	struct process *process,
	struct inode *inode)
{
	/* GPU capability descriptors have no inode or POSIX record-lock cleanup. */
	(void)process;
	(void)inode;
	abort();
}

/*
 * Supplies the fixed clock used by ownership tests that never enter a timed wait.
 */
uint64_t
sched_ticks(
	void)
{
	/* Succeeded: the fixture owns every explicit transition of this monotonic clock. */
	return gpu_test_ticks;
}

/*
 * Initializes a host-owned mutex without replacing any GPU admission decisions.
 */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	/* The peer detects recursive use and unmatched release through the real object fields. */
	memset(mutex, 0, sizeof(*mutex));
	mutex->guard.rank = rank;
	mutex->guard.name = name;

	/* Succeeded: this lock begins without an owner. */
	return 0;
}

/*
 * Acquires one uncontended mutex in the deterministic scheduler peer.
 */
void
mutex_lock(
	struct mutex *mutex)
{
	/* Unexpected blocking requires an explicit test interleaving rather than silent success. */
	assert(mutex->locked == 0U);
	mutex->locked = 1U;
	mutex->owner = &gpu_test_thread;

	/* Succeeded: the current fixture caller owns this critical section. */
	return;
}

/*
 * Releases an exactly paired mutex ownership interval.
 */
void
mutex_unlock(
	struct mutex *mutex)
{
	/* The peer refuses unmatched release or borrowing another caller's lock. */
	assert(mutex->locked == 1U);
	assert(mutex->owner == &gpu_test_thread);
	mutex->locked = 0U;
	mutex->owner = NULL;

	/* Succeeded: no fixture caller retains this lock. */
	return;
}

/*
 * Retains the actual production worker entry for finite supervisor scenarios.
 */
int
kthread_create(
	void (*entry)(void *),
	void *argument,
	int priority,
	struct thread **result)
{
	struct thread *worker;

	(void)priority;

	/* Failure must occur before any worker or native reservation is retained. */
	*result = NULL;
	if (gpu_test_thread_error != 0)
		return gpu_test_thread_error;

	/* The peer owns the same creator reference until the production reap boundary. */
	worker = calloc(1U, sizeof(*worker));
	if (worker == NULL)
		return ENOMEM;

	/* Scheduler policy is modeled; the actual production entry remains unchanged. */
	worker->kernel_entry = entry;
	worker->kernel_arg = argument;
	*result = worker;

	/* Succeeded: publication can now precede deterministic worker scheduling. */
	return 0;
}

/*
 * Validates publication while dedicated scenarios control worker scheduling.
 */
void
thread_start(
	struct thread *worker)
{
	/* Ordinary ownership fixtures do not invent background progress. */
	assert(worker->kernel_entry != NULL);

	/* Succeeded: the retained worker may be inspected at an explicit test boundary. */
	return;
}

/*
 * Runs the real worker's stop check before consuming its creator reference.
 */
int
thread_wait(
	struct thread *worker,
	int *status)
{
	/* A missing stop transition reaches the peer's unexpected-sleep assertion. */
	worker->kernel_entry(worker->kernel_arg);

	/* Successful reap releases the peer's retained worker exactly once. */
	free(worker);
	if (status != NULL)
		*status = 0;

	/* Succeeded: no kernel thread entry retains the tested backend. */
	return 0;
}

/*
 * Advances only the deterministic scheduler clock used by finite teardown.
 */
void
sched_sleep(
	uint64_t deadline)
{
	/* Sleeping cannot move the monotonic clock backward. */
	assert(deadline >= gpu_test_ticks);
	gpu_test_ticks = deadline;

	/* Succeeded: the next policy observation sees the selected time boundary. */
	return;
}
