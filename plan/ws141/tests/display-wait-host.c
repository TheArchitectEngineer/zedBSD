/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Single-thread host mutexes, wait queues and scheduler clock for the
 * display flip models (ws214-p003a): a wait releases its spinlock while the
 * scenario's display_test_sleep delivers an IRQ or lets time pass.
 */
#include <assert.h>
#include <stdint.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/waitq.h>
#include <uapi/errno.h>

/*
 * The model's scheduler clock in ticks.  Waits advance it through the
 * scenario's display_test_sleep; nothing else moves it.
 */
uint64_t display_test_ticks;

/*
 * Lets one sleeping wait make progress (deliver an IRQ, or let time pass);
 * every program linking this model defines it.  The condition lock is not
 * held while it runs, as in the kernel.
 */
void display_test_sleep(struct wait_queue *queue, uint64_t deadline);

/*
 * Initializes a host mutex that detects reentry in this single-thread model.
 */
int
mutex_init(
	struct mutex *mutex,
	enum lock_rank rank,
	const char *name)
{
	/* The mutex starts unowned. */
	(void)rank;
	(void)name;
	mutex->locked = 0;

	/* Succeeded: no caller owns the new mutex. */
	return 0;
}

/*
 * Takes a host mutex; a second owner in one thread is a deadlock in the kernel.
 */
void
mutex_lock(
	struct mutex *mutex)
{
	/* Recursion or a missed unlock fails at once. */
	assert(mutex->locked == 0);
	mutex->locked = 1;
}

/*
 * Releases a host mutex taken by mutex_lock.
 */
void
mutex_unlock(
	struct mutex *mutex)
{
	/* An unbalanced release is an ownership error. */
	assert(mutex->locked == 1);
	mutex->locked = 0;
}

/*
 * Initializes a wait queue with no wakeup yet.
 */
void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* The sequence counts wakeups. */
	queue->head = 0;
	queue->tail = 0;
	queue->sequence = 0;
	queue->name = name;
}

/*
 * Reads a wait queue's wakeup count.
 */
uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* The single-thread model reads it directly. */
	return queue->sequence;
}

/*
 * Wakes every waiter of a queue by advancing its count.
 */
void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* A waiter that observed the older count does not sleep past this wakeup. */
	queue->sequence++;
}

/*
 * Sleeps with the condition lock released, as the kernel does: the scenario's
 * display_test_sleep delivers an IRQ or lets time pass.
 */
int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* The kernel's wait is always made under its condition lock. */
	(void)flags;
	assert(lock->held.value == 1);
	if (queue->sequence != observed)
		return EAGAIN;

	/* The lock is released while the thread sleeps. */
	lock->held.value = 0;
	display_test_sleep(queue, deadline);
	lock->held.value = 1;

	/* A wakeup, the deadline, or a spurious return. */
	if (queue->sequence != observed)
		return 0;
	if (deadline != 0 && display_test_ticks >= deadline)
		return ETIMEDOUT;
	return 0;
}

/*
 * Reads the model's scheduler clock.
 */
uint64_t
sched_ticks(
	void)
{
	/* Only the scenario's sleeps advance it. */
	return display_test_ticks;
}
