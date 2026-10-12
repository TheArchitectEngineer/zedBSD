#!/bin/sh
# Host test of the arm64 HAL's per-CPU state under preemption and migration
# (ws213-p007, the follow-ups of the Pi 4 SMP trap of 2026-10-13).
# A task's floating-point and TLS registers live in the CPU while it runs and
# a task switch saves them over the task's copies; a CPU's record is the one
# TPIDR_EL1 names.  Code that reads either and then uses it with interrupts on
# can be moved to another CPU in between.  These functions are cut out of the
# HAL and run by real pthreads, one per task, on two simulated CPUs:
#  - hal_cpu_current() (smp.c): only calls with interrupts off count down the
#    CPU's stall-watch countdown, so each record's countdown is written by
#    its own CPU alone, and the stall watch's self-check runs only then;
#  - hal_task_exec_current() (task.c): the new program starts with the initial
#    floating-point state and a zero TLS, in the register and in the task;
#  - hal_task_set_tls() (task.c): the value reaches the register and the task;
#  - hal_task_set_user_fpregs() (debug.c): the new values reach the registers
#    of the running task.
# A migrator thread plays the scheduler: while neither task has interrupts
# off it switches both out (saving each CPU's live registers into its task),
# swaps their CPUs and switches them in (loading the registers from the
# task).  hal_memcpy() and arm64_irq_save() yield, which is where an
# interrupt lands.  Every check must pass, plainly and under ThreadSanitizer.
# Usage: plan/ws213/tests/percpu-host-test.sh [build-dir [source-root]]
#        (from the tree root; source-root defaults to the tree itself)
# Last line: percpu-host-test: PASS.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
out=${1:-build/ws213-p007-host}
root=${2:-.}
mkdir -p "$out"

# Cuts a function (its return-type line and its body) out of a source file.
cut_function() {
	awk -v name="$2" '$0 ~ "^"name"\\(" {p=1; print previous} p{print} p&&/^}/{exit} {previous=$0}' "$1"
}

# The TLS register writes become calls of the simulated CPU's register.
{
	cut_function "$root/src/hal/arm64/smp.c" hal_cpu_current
	cut_function "$root/src/hal/arm64/task.c" running_task
	cut_function "$root/src/hal/arm64/task.c" arm64_task_running
	cut_function "$root/src/hal/arm64/task.c" hal_task_exec_validate
	cut_function "$root/src/hal/arm64/task.c" hal_task_exec_current
	cut_function "$root/src/hal/arm64/task.c" hal_task_set_tls
	cut_function "$root/src/hal/arm64/debug.c" hal_task_set_user_fpregs
} | sed -e 's/__asm__ volatile("msr tpidr_el0, xzr"[^;]*;/tls_register_write(0);/' \
	-e 's/__asm__ volatile("msr tpidr_el0, %0" : : "r"(\(.*\)));/tls_register_write(\1);/' \
	> "$out/percpu-functions.inc"

# The cut-out code must not keep an instruction the host cannot assemble.
if grep -n '__asm__' "$out/percpu-functions.inc"; then
	echo "percpu-host-test: FAIL (an asm statement was not replaced)"
	exit 1
fi

cat > "$out/percpu-main.c" <<'EOT'
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *hal_task_t;
typedef void *hal_space_t;
typedef uint32_t hal_cpu_id_t;

#define HAL_SPACE_SYS ((hal_space_t)0)
#define ARM64_WATCH_SELF_INTERVAL 64U
#define CPU_COUNT 2
#define TASK_COUNT 2
#define ROUNDS 200000UL
#define FP_BYTES 528
#define INITIAL_BYTE 0xaa

/* The frame a task entered the kernel with. */
struct arm64_exception_frame {
	uint64_t x[31];
	uint64_t user_sp;
	uint64_t elr;
	uint64_t spsr;
};

/* A task: where it runs and its interrupt mask (0 on, 1 off, 2 being moved), then the HAL's fields. */
struct arm64_task {
	int cpu;
	int irq;
	struct arm64_exception_frame *active_user_frame;
	hal_space_t space;
	uintptr_t tls;
	unsigned signal_depth;
	uint8_t fpregs[FP_BYTES];
};

/* A CPU's record. */
struct arm64_percpu {
	hal_cpu_id_t logical_id;
	struct arm64_task *running_task;
	unsigned watch_countdown;
};

/* What a CPU holds of the running task: its floating-point and TLS registers. */
struct cpu_registers {
	uint8_t fp[FP_BYTES];
	uint64_t tls;
};

/* The arm64 layout of the debugger's floating-point registers. */
struct hal_fpregs {
	uint8_t v[32][16];
	uint32_t fpsr;
	uint32_t fpcr;
};

static struct arm64_percpu records[CPU_COUNT];
static struct cpu_registers live[CPU_COUNT];
static struct arm64_task tasks[TASK_COUNT];
static struct arm64_exception_frame frames[TASK_COUNT];
static uint8_t initial_fpregs[FP_BYTES];
static __thread struct arm64_task *me;
static __thread unsigned long ticks;
static int stop;
static unsigned long moves;

/* What the checks counted. */
static unsigned long masked_calls[CPU_COUNT];
static unsigned long self_checks[CPU_COUNT];
static unsigned long unmasked_self_checks;
static unsigned long wrong_cpu;
static unsigned long wrong_exec_fp;
static unsigned long wrong_exec_tls;
static unsigned long wrong_tls;
static unsigned long wrong_fpregs;

hal_cpu_id_t hal_cpu_current(void);
static struct arm64_task *running_task(void);
struct arm64_task *arm64_task_running(void);
int hal_task_exec_validate(hal_space_t new_space, uintptr_t entry, uintptr_t user_stack_pointer);
int hal_task_exec_current(hal_space_t new_space, uintptr_t entry, uintptr_t user_stack_pointer);
void hal_task_set_tls(hal_task_t handle, uintptr_t value);
int hal_task_set_user_fpregs(hal_task_t handle, const struct hal_fpregs *registers);

/* Lets an interrupt in now and then. */
static void
interrupt_point(unsigned mask)
{
	ticks++;
	if ((ticks & mask) == 0)
		sched_yield();
}

/* TPIDR_EL1: the record of the CPU the caller is on now. */
static struct arm64_percpu *
arm64_percpu_current(void)
{
	struct arm64_percpu *record;

	record = &records[__atomic_load_n(&me->cpu, __ATOMIC_ACQUIRE)];
	interrupt_point(15U);
	return record;
}

/* Masks the caller's interrupts, which may first take one; nests like DAIF. */
static uint64_t
arm64_irq_save(void)
{
	int expected;

	if (__atomic_load_n(&me->irq, __ATOMIC_ACQUIRE) == 1)
		return 1U << 7;
	interrupt_point(7U);
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
	if ((state & (1U << 7)) == 0)
		__atomic_store_n(&me->irq, 0, __ATOMIC_RELEASE);
}

/* DAIF.I of the caller. */
static int
arm64_irq_masked(void)
{
	return __atomic_load_n(&me->irq, __ATOMIC_ACQUIRE) == 1;
}

/* The stall watch's look at the CPU's own tick: counted per CPU. */
static void
arm64_watch_self_check(void)
{
	if (!arm64_irq_masked()) {
		__atomic_fetch_add(&unmasked_self_checks, 1U, __ATOMIC_RELAXED);
		return;
	}
	self_checks[me->cpu]++;
}

static void *
hal_memset(void *target, int value, size_t size)
{
	return memset(target, value, size);
}

/* Copies, and lets an interrupt in after it. */
static void *
hal_memcpy(void *target, const void *source, size_t size)
{
	memcpy(target, source, size);
	sched_yield();
	return target;
}

/* The caller's CPU's floating-point registers. */
static void
arm64_fp_save(void *state)
{
	memcpy(state, live[me->cpu].fp, FP_BYTES);
}

static void
arm64_fp_restore(const void *state)
{
	memcpy(live[me->cpu].fp, state, FP_BYTES);
}

/* The caller's CPU's TLS register. */
static void
tls_register_write(uint64_t value)
{
	live[me->cpu].tls = value;
}

/* The space selection is not looked at here. */
static void
hal_space_switch(hal_space_t handle)
{
	(void)handle;
}

#include "percpu-functions.inc"

/* Claims a task for a move, if its interrupts are on. */
static int
claim(struct arm64_task *task)
{
	int expected;

	expected = 0;
	return __atomic_compare_exchange_n(&task->irq, &expected, 2, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

/* The scheduler: switches both tasks out, swaps their CPUs and switches them in. */
static void *
migrate(void *argument)
{
	struct arm64_task *first = &tasks[0];
	struct arm64_task *second = &tasks[1];
	struct arm64_task *task;
	int a;
	int b;
	int index;

	(void)argument;
	while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
		if (!claim(first)) {
			sched_yield();
			continue;
		}
		if (!claim(second)) {
			__atomic_store_n(&first->irq, 0, __ATOMIC_RELEASE);
			sched_yield();
			continue;
		}

		/* Switches both out: each CPU's registers go to its task. */
		for (index = 0; index < TASK_COUNT; index++) {
			task = &tasks[index];
			memcpy(task->fpregs, live[task->cpu].fp, FP_BYTES);
			task->tls = live[task->cpu].tls;
		}

		/* Swaps the CPUs. */
		a = first->cpu;
		b = second->cpu;
		records[a].running_task = second;
		records[b].running_task = first;
		__atomic_store_n(&first->cpu, b, __ATOMIC_RELEASE);
		__atomic_store_n(&second->cpu, a, __ATOMIC_RELEASE);

		/* Switches both in: each task's registers go to its new CPU. */
		for (index = 0; index < TASK_COUNT; index++) {
			task = &tasks[index];
			memcpy(live[task->cpu].fp, task->fpregs, FP_BYTES);
			live[task->cpu].tls = task->tls;
		}
		moves++;
		__atomic_store_n(&second->irq, 0, __ATOMIC_RELEASE);
		__atomic_store_n(&first->irq, 0, __ATOMIC_RELEASE);
		sched_yield();
	}
	return NULL;
}

/* Asks for the CPU with interrupts on, then off, and counts the latter. */
static void
round_cpu(void)
{
	uint64_t irq;
	hal_cpu_id_t cpu;

	(void)hal_cpu_current();
	irq = arm64_irq_save();
	cpu = hal_cpu_current();
	if (cpu != (hal_cpu_id_t)me->cpu)
		__atomic_fetch_add(&wrong_cpu, 1U, __ATOMIC_RELAXED);
	masked_calls[me->cpu]++;
	arm64_irq_restore(irq);
}

/* Runs an old program's state into an exec and looks at what the new one starts with. */
static void
round_exec(unsigned long round)
{
	uint64_t irq;
	unsigned index;
	int bad;

	irq = arm64_irq_save();
	memset(live[me->cpu].fp, 0x10 + (int)(round % 64U), FP_BYTES);
	live[me->cpu].tls = 0x1000U + round;
	me->tls = 0x1000U + round;
	arm64_irq_restore(irq);

	if (hal_task_exec_current((hal_space_t)(uintptr_t)(0x10000U + round), 0x400000U, 0x7fff0000U) != 0) {
		fprintf(stderr, "exec refused\n");
		exit(1);
	}

	irq = arm64_irq_save();
	bad = 0;
	for (index = 0; index < FP_BYTES; index++) {
		if (live[me->cpu].fp[index] != INITIAL_BYTE)
			bad = 1;
	}
	if (bad)
		__atomic_fetch_add(&wrong_exec_fp, 1U, __ATOMIC_RELAXED);
	if (live[me->cpu].tls != 0 || me->tls != 0)
		__atomic_fetch_add(&wrong_exec_tls, 1U, __ATOMIC_RELAXED);
	arm64_irq_restore(irq);
}

/* Sets the TLS and looks at the register and the task. */
static void
round_tls(unsigned long round)
{
	uint64_t irq;
	uint64_t value;

	value = 0x200000U + round;
	hal_task_set_tls(me, (uintptr_t)value);

	irq = arm64_irq_save();
	if (live[me->cpu].tls != value || me->tls != value)
		__atomic_fetch_add(&wrong_tls, 1U, __ATOMIC_RELAXED);
	arm64_irq_restore(irq);
}

/* Writes the floating-point registers as a debugger would, and looks at the CPU's. */
static void
round_fpregs(unsigned long round)
{
	struct hal_fpregs registers;
	uint64_t irq;

	irq = arm64_irq_save();
	memset(live[me->cpu].fp, 0x30, FP_BYTES);
	arm64_irq_restore(irq);

	memset(registers.v, 0x60 + (int)(round % 32U), sizeof(registers.v));
	registers.fpsr = 0;
	registers.fpcr = 0;
	if (hal_task_set_user_fpregs(me, &registers) != 0) {
		fprintf(stderr, "set_user_fpregs refused\n");
		exit(1);
	}

	irq = arm64_irq_save();
	if (memcmp(live[me->cpu].fp, registers.v, sizeof(registers.v)) != 0)
		__atomic_fetch_add(&wrong_fpregs, 1U, __ATOMIC_RELAXED);
	arm64_irq_restore(irq);
}

/* A task: runs the four kinds of rounds in turn. */
static void *
run_task(void *argument)
{
	unsigned long round;

	me = argument;
	for (round = 0; round < ROUNDS; round++) {
		switch (round % 4U) {
		case 0:
			round_cpu();
			break;
		case 1:
			round_exec(round);
			break;
		case 2:
			round_tls(round);
			break;
		default:
			round_fpregs(round);
			break;
		}
	}
	return NULL;
}

int
main(void)
{
	pthread_t threads[TASK_COUNT];
	pthread_t migrator;
	unsigned long counted;
	int failed;
	int index;

	memset(initial_fpregs, INITIAL_BYTE, sizeof(initial_fpregs));
	for (index = 0; index < TASK_COUNT; index++) {
		tasks[index].cpu = index;
		tasks[index].active_user_frame = &frames[index];
		tasks[index].space = (hal_space_t)(uintptr_t)0x1000U;
		records[index].logical_id = (hal_cpu_id_t)index;
		records[index].running_task = &tasks[index];
		records[index].watch_countdown = ARM64_WATCH_SELF_INTERVAL;
	}
	pthread_create(&migrator, NULL, migrate, NULL);
	for (index = 0; index < TASK_COUNT; index++)
		pthread_create(&threads[index], NULL, run_task, &tasks[index]);
	for (index = 0; index < TASK_COUNT; index++)
		pthread_join(threads[index], NULL);
	__atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
	pthread_join(migrator, NULL);

	failed = 0;
	printf("percpu-host-test: %lu moves\n", moves);
	if (moves == 0) {
		printf("FAIL the migrator never moved a task\n");
		failed = 1;
	}
	for (index = 0; index < CPU_COUNT; index++) {
		counted = self_checks[index] * ARM64_WATCH_SELF_INTERVAL +
		    (ARM64_WATCH_SELF_INTERVAL - records[index].watch_countdown);
		printf("cpu%d: %lu calls with interrupts off, countdown moved %lu\n",
		       index, masked_calls[index], counted);
		if (counted != masked_calls[index]) {
			printf("FAIL cpu%d's countdown counted other calls than its own with interrupts off\n", index);
			failed = 1;
		}
	}
	if (unmasked_self_checks != 0) {
		printf("FAIL %lu self-checks ran with interrupts on\n", unmasked_self_checks);
		failed = 1;
	}
	if (wrong_cpu != 0) {
		printf("FAIL %lu hal_cpu_current() with interrupts off named another CPU\n", wrong_cpu);
		failed = 1;
	}
	if (wrong_exec_fp != 0) {
		printf("FAIL %lu execs started the new program with old floating-point registers\n", wrong_exec_fp);
		failed = 1;
	}
	if (wrong_exec_tls != 0) {
		printf("FAIL %lu execs started the new program with the old TLS\n", wrong_exec_tls);
		failed = 1;
	}
	if (wrong_tls != 0) {
		printf("FAIL %lu hal_task_set_tls() left the register and the task apart\n", wrong_tls);
		failed = 1;
	}
	if (wrong_fpregs != 0) {
		printf("FAIL %lu hal_task_set_user_fpregs() lost the new values\n", wrong_fpregs);
		failed = 1;
	}
	if (failed) {
		printf("percpu-host-test: FAIL\n");
		return 1;
	}
	printf("percpu-host-test: PASS\n");
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
		-I"$out" "$out/percpu-main.c" -o "$out/percpu-host-test-$mode"
	timeout 300 "$out/percpu-host-test-$mode"
done
