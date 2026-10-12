#!/bin/sh
# Host test of the arm64 HAL's current task under preemption and migration
# (ws213-p007, the Pi 4 SMP trap of 2026-10-13: mutex_unlock() of vm->lock in
# vmspace_pin_user_pages() found another thread as the owner).
# running_task() and hal_task_get_current() are cut out of
# src/hal/arm64/task.c and run by real pthreads, one per task, on two
# simulated CPUs:
#  - each CPU has a record whose running_task names the task on it, and
#    arm64_percpu_current() returns the record of the CPU the calling task is
#    on now (TPIDR_EL1);
#  - a migrator thread plays the scheduler: it swaps the two tasks between the
#    CPUs, but only while neither has its interrupts off (arm64_irq_save()),
#    which is when a tick may preempt a task and resume it on the other CPU;
#  - arm64_percpu_current() sometimes yields after choosing the record, which
#    is where an interrupt between the two reads (TPIDR_EL1, then the field)
#    lands.
# Every hal_task_get_current() must report the calling task.  Reading the
# record and then its field with interrupts on reports the other CPU's task,
# another thread's, after a move in between.
# Usage: plan/ws213/tests/current-host-test.sh [build-dir]  (from the tree root)
# Last line: current-host-test: PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws213-p007-host}
mkdir -p "$out"

# Cuts a function (its return-type line and its body) out of a source file.
cut_function() {
	awk -v name="$2" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' "$1"
}

{
	cut_function src/hal/arm64/task.c running_task
	cut_function src/hal/arm64/task.c hal_task_get_current
} > "$out/current-functions.inc"

cat > "$out/current-main.c" <<'EOT'
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef void *hal_task_t;

#define CPU_COUNT 2
#define TASK_COUNT 2
#define ROUNDS 2000000UL

/* A task: the CPU it is on and its interrupt mask (0 on, 1 off, 2 being moved). */
struct arm64_task {
	int cpu;
	int irq;
};

/* A CPU's record: the task it runs. */
struct arm64_percpu {
	struct arm64_task *running_task;
};

static struct arm64_percpu records[CPU_COUNT];
static struct arm64_task tasks[TASK_COUNT];
static __thread struct arm64_task *me;
static __thread unsigned long looks;
static int stop;
static unsigned long moves;

static struct arm64_task *running_task(void);
hal_task_t hal_task_get_current(void);

/* TPIDR_EL1: the record of the CPU the caller is on now; an interrupt may follow. */
static struct arm64_percpu *
arm64_percpu_current(void)
{
	struct arm64_percpu *record;

	record = &records[__atomic_load_n(&me->cpu, __ATOMIC_ACQUIRE)];
	looks++;
	if ((looks & 15U) == 0)
		sched_yield();
	return record;
}

/* Masks the caller's interrupts, waiting out a move in progress. */
static uint64_t
arm64_irq_save(void)
{
	int expected;

	for (;;) {
		expected = 0;
		if (__atomic_compare_exchange_n(&me->irq, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return 0;
		sched_yield();
	}
}

/* Unmasks the caller's interrupts when they were on before. */
static void
arm64_irq_restore(uint64_t state)
{
	if (state == 0)
		__atomic_store_n(&me->irq, 0, __ATOMIC_RELEASE);
}

#include "current-functions.inc"

/* The scheduler: swaps the two tasks between the CPUs while both can be preempted. */
static void *
migrate(void *argument)
{
	struct arm64_task *first = &tasks[0];
	struct arm64_task *second = &tasks[1];
	int expected;
	int a;
	int b;

	(void)argument;
	while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
		expected = 0;
		if (!__atomic_compare_exchange_n(&first->irq, &expected, 2, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			sched_yield();
			continue;
		}
		expected = 0;
		if (!__atomic_compare_exchange_n(&second->irq, &expected, 2, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			__atomic_store_n(&first->irq, 0, __ATOMIC_RELEASE);
			sched_yield();
			continue;
		}
		a = __atomic_load_n(&first->cpu, __ATOMIC_RELAXED);
		b = __atomic_load_n(&second->cpu, __ATOMIC_RELAXED);
		__atomic_store_n(&records[a].running_task, second, __ATOMIC_RELAXED);
		__atomic_store_n(&records[b].running_task, first, __ATOMIC_RELAXED);
		__atomic_store_n(&first->cpu, b, __ATOMIC_RELEASE);
		__atomic_store_n(&second->cpu, a, __ATOMIC_RELEASE);
		moves++;
		__atomic_store_n(&second->irq, 0, __ATOMIC_RELEASE);
		__atomic_store_n(&first->irq, 0, __ATOMIC_RELEASE);
		sched_yield();
	}
	return NULL;
}

/* A task: asks for the current task again and again, and counts wrong answers. */
static void *
run_task(void *argument)
{
	unsigned long round;
	unsigned long wrong = 0;
	hal_task_t current;

	me = argument;
	for (round = 0; round < ROUNDS; round++) {
		current = hal_task_get_current();
		if (current != (hal_task_t)me)
			wrong++;
	}
	if (__atomic_load_n(&me->irq, __ATOMIC_ACQUIRE) != 0)
		wrong++;
	return (void *)wrong;
}

int
main(void)
{
	pthread_t threads[TASK_COUNT];
	pthread_t migrator;
	unsigned long wrong = 0;
	void *answer;
	int index;

	for (index = 0; index < TASK_COUNT; index++) {
		tasks[index].cpu = index;
		records[index].running_task = &tasks[index];
	}
	pthread_create(&migrator, NULL, migrate, NULL);
	for (index = 0; index < TASK_COUNT; index++)
		pthread_create(&threads[index], NULL, run_task, &tasks[index]);
	for (index = 0; index < TASK_COUNT; index++) {
		pthread_join(threads[index], &answer);
		wrong += (unsigned long)answer;
	}
	__atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
	pthread_join(migrator, NULL);

	printf("current-host-test: %lu moves, %lu wrong answers\n", moves, wrong);
	if (moves == 0) {
		printf("FAIL the migrator never moved a task\n");
		printf("current-host-test: FAIL\n");
		return 1;
	}
	if (wrong != 0) {
		printf("FAIL hal_task_get_current() reported another task\n");
		printf("current-host-test: FAIL\n");
		return 1;
	}
	printf("current-host-test: PASS\n");
	return 0;
}
EOT

# Plainly, and with ThreadSanitizer (the moves synchronize through the mask).
for mode in plain thread; do
	extra=
	if [ "$mode" = thread ]; then
		extra='-fsanitize=thread'
	fi
	# shellcheck disable=SC2086
	cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -pthread $extra \
		-I"$out" "$out/current-main.c" -o "$out/current-host-test-$mode"
	timeout 120 "$out/current-host-test-$mode"
done
