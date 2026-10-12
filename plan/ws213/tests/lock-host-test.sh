#!/bin/sh
# Host test of the sleeping mutex (ws213-p007 K2-1, K2-8).
# The mutex functions are cut out of src/kern/lock.c and compiled with the
# real struct mutex and struct thread against a fake wait queue, a fake guard
# and a fake CPU count:
#  - mutex_lock() with a signal or a termination pending sleeps once,
#    uninterruptibly, instead of retrying the interruptible form (which
#    returns EINTR without sleeping, a busy loop on the holder's CPU);
#  - on one CPU a held mutex is looked at once and not spun on, and on
#    several CPUs it is still looked at MUTEX_SPIN_LIMIT more times;
#  - a free mutex is taken without the guard or a sleep;
#  - an interruptible waiter that ends its wait with EINTR after a release
#    passes the wakeup on to the next sleeper, and wakes nobody when it was
#    the only one.
# The fake sleep gives up after a bound of EINTR returns, so the old busy
# loop fails the test instead of hanging it.
# Usage: plan/ws213/tests/lock-host-test.sh [build-dir]  (from the tree root)
# Last line: lock-host-test: PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws213-p007-host}
mkdir -p "$out"

# Cuts a function (its return-type line and its body) out of a source file.
cut_function() {
	awk -v name="$2" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' "$1"
}

grep -E '^#define MUTEX_(SPIN_LIMIT|FREE|HELD|CONTENDED)[[:space:]]' src/kern/lock.c > "$out/lock-defines.inc"
{
	# The static functions' declarations, so the cut bodies may call one another.
	grep -E '^static [a-z]+ mutex_' src/kern/lock.c
	for name in mutex_lock_interruptible mutex_lock mutex_unlock mutex_take_free mutex_spin_take \
		mutex_sleep_take mutex_sleep_give_up mutex_check_owner; do
		cut_function src/kern/lock.c "$name"
	done
} > "$out/lock-functions.inc"

cat > "$out/lock-main.c" <<'EOT'
#include "kern/lock.h"
#include "kern/thread.h"
#include <uapi/errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lock-defines.inc"

static struct thread *fake_current;
static struct thread *fake_holder;
static unsigned fake_cpus = 1;
static unsigned relax_count;
static unsigned guard_takes;
static unsigned sleeps_plain;
static unsigned sleeps_interruptible;
static unsigned eintr_returns;
static unsigned wakes;
static int signal_pending;
static int terminate_after_release;

static void fake_relax(void);
#define hal_atomic_relax() fake_relax()

static void
fake_relax(void)
{
	relax_count++;
}

unsigned
hal_cpu_count(void)
{
	return fake_cpus;
}

struct thread *
thread_current(void)
{
	return fake_current;
}

unsigned long
spin_lock_irqsave(struct spinlock *lock)
{
	(void)lock;
	guard_takes++;
	return 0;
}

void
spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled)
{
	(void)lock;
	(void)enabled;
}

uint64_t
waitq_sequence(const struct wait_queue *queue)
{
	return queue->sequence;
}

void
waitq_wake_one(struct wait_queue *queue)
{
	struct wait_token *token;

	wakes++;
	queue->sequence++;
	token = queue->head;
	if (token == NULL)
		return;
	queue->head = token->next;
	if (queue->tail == token)
		queue->tail = NULL;
	token->next = NULL;
	token->queue = NULL;
}

void mutex_unlock(struct mutex *mutex);

/*
 * The fake sleep on the mutex's waiters.  An interruptible sleep with a
 * signal pending ends at once with EINTR (as waitq_sleep() does); past a
 * bound the holder lets go so the old busy loop ends and is counted.  Any
 * other sleep runs the holder's release, which wakes this sleeper; with
 * terminate_after_release the interruptible sleeper then still ends with
 * EINTR, having taken the release's wakeup.
 */
int
waitq_sleep(struct wait_queue *queue, struct spinlock *lock, uint64_t observed,
    uint64_t deadline, unsigned flags)
{
	struct thread *waiter;

	(void)lock;
	(void)deadline;
	if (queue->sequence != observed)
		return EAGAIN;
	if ((flags & WAITQ_INTERRUPTIBLE) != 0 && signal_pending) {
		eintr_returns++;
		if (eintr_returns == 50U) {
			waiter = fake_current;
			fake_current = fake_holder;
			mutex_unlock((struct mutex *)((char *)queue - offsetof(struct mutex, waiters)));
			fake_current = waiter;
		}
		return EINTR;
	}
	if ((flags & WAITQ_INTERRUPTIBLE) != 0)
		sleeps_interruptible++;
	else
		sleeps_plain++;

	/* The holder runs and lets go; its wakeup is the one this sleeper takes. */
	if (terminate_after_release) {
		struct mutex *mutex = (struct mutex *)((char *)queue - offsetof(struct mutex, waiters));

		mutex->owner = NULL;
		mutex->locked = MUTEX_FREE;
		queue->sequence++;
		return EINTR;
	}
	waiter = fake_current;
	fake_current = fake_holder;
	{
		struct wait_token *head = queue->head;

		/* This sleeper is at the head while the release wakes it. */
		queue->head = NULL;
		mutex_unlock((struct mutex *)((char *)queue - offsetof(struct mutex, waiters)));
		queue->head = head;
	}
	fake_current = waiter;
	return 0;
}

#include "lock-functions.inc"

static int failures;

static void
check(int condition, const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

static void
reset(struct mutex *mutex, struct thread *holder)
{
	memset(mutex, 0, sizeof(*mutex));
	waitq_init(&mutex->waiters, "test");
	mutex->locked = MUTEX_FREE;
	if (holder != NULL) {
		mutex->locked = MUTEX_HELD;
		mutex->owner = holder;
	}
	fake_holder = holder;
	relax_count = 0;
	guard_takes = 0;
	sleeps_plain = 0;
	sleeps_interruptible = 0;
	eintr_returns = 0;
	wakes = 0;
	signal_pending = 0;
	terminate_after_release = 0;
}

void
waitq_init(struct wait_queue *queue, const char *name)
{
	queue->head = NULL;
	queue->tail = NULL;
	queue->sequence = 1;
	queue->name = name;
}

int
main(void)
{
	static struct thread holder;
	static struct thread waiter;
	static struct thread other;
	struct mutex mutex;
	int error;

	/* A free mutex is taken with no guard and no sleep. */
	fake_cpus = 1;
	fake_current = &waiter;
	reset(&mutex, NULL);
	mutex_lock(&mutex);
	check(mutex.owner == &waiter, "free: owner");
	check(mutex.locked == MUTEX_HELD, "free: held, not contended");
	check(guard_takes == 0, "free: no guard");
	check(sleeps_plain + sleeps_interruptible + eintr_returns == 0, "free: no sleep");
	mutex_unlock(&mutex);
	check(mutex.locked == MUTEX_FREE, "free: released");
	check(wakes == 0, "free: an uncontended release wakes nobody");

	/* K2-1: a held mutex with a signal pending sleeps once, uninterruptibly. */
	reset(&mutex, &holder);
	signal_pending = 1;
	mutex_lock(&mutex);
	check(eintr_returns == 0, "signal pending: no interruptible retry (busy loop)");
	check(sleeps_plain == 1, "signal pending: one uninterruptible sleep");
	check(sleeps_interruptible == 0, "signal pending: no interruptible sleep");
	check(mutex.owner == &waiter, "signal pending: owner after the release");
	check(mutex.locked == MUTEX_CONTENDED, "signal pending: taken marked contended");
	check(wakes == 1, "signal pending: the holder's release woke the sleeper");
	check(relax_count == 0, "signal pending: one CPU, no spin");
	mutex_unlock(&mutex);

	/* The interruptible form still ends with EINTR and leaves the mutex held. */
	reset(&mutex, &holder);
	signal_pending = 1;
	eintr_returns = 1;
	error = mutex_lock_interruptible(&mutex);
	check(error == EINTR, "interruptible: EINTR");
	check(mutex.owner == &holder, "interruptible: holder keeps it");
	check(wakes == 0, "interruptible: nobody else asleep, nobody woken");

	/* K2-8: one CPU looks once; several CPUs look MUTEX_SPIN_LIMIT more times. */
	reset(&mutex, &holder);
	fake_cpus = 1;
	error = mutex_lock_interruptible(&mutex);
	check(error == 0, "one cpu: taken after the release");
	check(relax_count == 0, "one cpu: no spin");
	check(sleeps_interruptible == 1, "one cpu: slept once");
	mutex_unlock(&mutex);
	reset(&mutex, &holder);
	fake_cpus = 4;
	mutex_lock(&mutex);
	check(relax_count == MUTEX_SPIN_LIMIT, "four cpus: spins MUTEX_SPIN_LIMIT times");
	check(sleeps_plain == 1, "four cpus: then sleeps");
	mutex_unlock(&mutex);
	fake_cpus = 1;

	/* A woken interruptible waiter that ends with EINTR passes the wakeup on. */
	{
		static struct wait_token sleeper;

		reset(&mutex, &holder);
		terminate_after_release = 1;
		sleeper.thread = &other;
		sleeper.next = NULL;
		sleeper.queue = &mutex.waiters;
		mutex.waiters.head = &sleeper;
		mutex.waiters.tail = &sleeper;
		error = mutex_lock_interruptible(&mutex);
		check(error == EINTR, "pass on: EINTR");
		check(wakes == 1, "pass on: the next sleeper is woken");
		check(mutex.waiters.head == NULL, "pass on: the sleeper left the queue");
		check(sleeper.queue == NULL, "pass on: the sleeper's token was detached");
	}
	reset(&mutex, &holder);
	terminate_after_release = 1;
	error = mutex_lock_interruptible(&mutex);
	check(error == EINTR, "alone: EINTR");
	check(wakes == 0, "alone: nobody woken");

	if (failures != 0) {
		printf("lock-host-test: FAIL (%d)\n", failures);
		return 1;
	}
	printf("lock-host-test: PASS\n");
	return 0;
}
EOT

# The kernel's own ABI headers (KERN_UAPI_NATIVE), plainly and with ASan and UBSan.
for mode in plain sanitize; do
	extra=
	if [ "$mode" = sanitize ]; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer'
	fi
	# shellcheck disable=SC2086
	cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE \
		-I. -Iinclude -Iinclude/libc -Iinclude/uapi -Isrc -I"$out" $extra "$out/lock-main.c" -o "$out/lock-host-test-$mode"
	UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$out/lock-host-test-$mode"
done
