/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The private arm64 per-CPU state.
 */

#ifndef KERN_HAL_ARM64_PERCPU_H
#define KERN_HAL_ARM64_PERCPU_H

#include <hal/hal.h>

/* The most CPUs the HAL brings up: the four cores of the BCM2711. */
#define ARM64_CPU_MAX 4U

struct arm64_task;

/*
 * The state one CPU keeps for itself.
 *
 * Each CPU finds its own record through TPIDR_EL1, which holds the
 * record's address from the CPU's first C code on.  A record is written
 * only by its own CPU, except the fields marked otherwise, which other
 * CPUs read with acquire loads.
 */
struct arm64_percpu {
	/* The record's own address, which TPIDR_EL1 holds. */
	struct arm64_percpu *self;

	/* The CPU's number in the HAL, 0 for the boot CPU. */
	hal_cpu_id_t logical_id;

	/* The CPU's affinity value in MPIDR_EL1. */
	uint64_t mpidr;

	/* Set once the CPU takes interrupts and runs its initial task; read by others. */
	volatile unsigned ready;

	/* The task running on this CPU. */
	struct arm64_task *running_task;

	/*
	 * The task whose context is still being left on this CPU.
	 *
	 * hal_task_context_switch() records the departing task here before the
	 * stack switch, and the arriving context, whether resumed or started
	 * for the first time, marks it as no longer running.  Until then
	 * another CPU must not resume it: its stack is still in use.
	 */
	struct arm64_task *switching_from;

	/* The address space TTBR0_EL1 selects on this CPU; read by others. */
	hal_space_t current_space;

	/* The acknowledgement of the interrupt this CPU is handling, or none. */
	hal_irq_ack_t active_ack;

	/* When this CPU's next scheduling tick is due, in counter ticks. */
	uint64_t timer_deadline;

	/* How many scheduling ticks this CPU has taken. */
	uint64_t timer_ticks;

	/* Whether this CPU's breakpoint and watchpoint registers hold a task's points. */
	int debug_points_loaded;
};

void arm64_percpu_bootstrap(hal_cpu_id_t cpu, uint64_t mpidr);
struct arm64_percpu *arm64_percpu_current(void);
struct arm64_percpu *arm64_percpu_get(hal_cpu_id_t cpu);
void arm64_smp_bootstrap(void);
void arm64_smp_configure(void);

#endif
