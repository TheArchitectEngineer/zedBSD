/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arm64 CPU lifecycle: which CPUs exist, which are up, and how they
 * reach each other.
 *
 * Only the boot CPU runs so far (ws208-p002); the secondary cores stay in
 * the firmware's spin table until ws208-p003 releases them.
 */

#include <hal/hal.h>
#include "asm.h"
#include "percpu.h"

/*
 * How many CPUs the kernel is told about.
 *
 * It is fixed before the kernel asks for it and never changes: the kernel
 * sizes its per-CPU tables by it before hal_cpu_start_others() runs.
 */
static unsigned cpu_count = 1;

/*
 * The CPUs that are up, one bit per CPU.
 *
 * A CPU sets its own bit with a release once it runs its initial task and
 * takes interrupts; the boot CPU's bit is set by the boot.
 */
static struct hal_cpu_mask ready_mask;

/*
 * Makes the boot CPU known: its record, and its place in the ready mask.
 */
void
arm64_smp_bootstrap(
	void)
{
	uint64_t mpidr;

	/* Reads the boot CPU's affinity, which names it in the interrupt controller. */
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));

	/* Makes the record the boot CPU's own before any lock is taken. */
	arm64_percpu_bootstrap(0, mpidr);

	/* The boot CPU is up from here on. */
	hal_cpu_mask_zero(&ready_mask);
	hal_cpu_mask_set(&ready_mask, 0);
	__atomic_store_n(&arm64_percpu_get(0)->ready, 1U, __ATOMIC_RELEASE);
}

/*
 * Reports the CPU the caller runs on.
 */
hal_cpu_id_t
hal_cpu_current(
	void)
{
	struct arm64_percpu *record;

	/* The record TPIDR_EL1 names carries the CPU's number. */
	record = arm64_percpu_current();

	/* Succeeded: the caller's CPU. */
	return record->logical_id;
}

/*
 * Reports how many CPUs the kernel schedules on.
 */
unsigned
hal_cpu_count(
	void)
{
	/* Succeeded: the count fixed at boot. */
	return cpu_count;
}

/*
 * Reports the CPUs that are up.
 */
void
hal_cpu_ready_mask(
	struct hal_cpu_mask *result)
{
	unsigned word;

	/* A caller with nowhere to put the answer asked for nothing. */
	if (result == NULL)
		return;

	/* Copies each word with an acquire, pairing with the CPUs' own release. */
	for (word = 0; word < HAL_CPU_MASK_WORDS; word++)
		result->bits[word] = __atomic_load_n(&ready_mask.bits[word], __ATOMIC_ACQUIRE);
}

/*
 * Starts the CPUs other than the boot CPU.
 */
int
hal_cpu_start_others(
	void)
{
	/* Succeeded: the boot CPU is the only one so far. */
	return HAL_OK;
}

/*
 * Interrupts another CPU so that it looks for work.
 */
int
hal_cpu_notify(
	hal_cpu_id_t cpu)
{
	/* A CPU the kernel was not told about does not exist. */
	if (cpu >= cpu_count)
		return HAL_ERR_INVALID;

	/* Failed: no CPU has another one to notify yet. */
	return HAL_ERR_UNSUPPORTED;
}

/*
 * Interrupts every CPU of a mask, as hal_cpu_notify() does one.
 */
int
hal_cpu_notify_mask(
	const struct hal_cpu_mask *targets)
{
	hal_cpu_id_t cpu;
	int named;

	/* A missing mask names no CPU. */
	if (targets == NULL)
		return HAL_ERR_INVALID;

	/* Refuses a mask that names a CPU the kernel was not told about. */
	for (cpu = cpu_count; cpu < HAL_CPU_MAX; cpu++) {
		named = hal_cpu_mask_test(targets, cpu);
		if (named)
			return HAL_ERR_INVALID;
	}

	/* An empty mask is not a request. */
	named = hal_cpu_mask_test(targets, 0);
	if (!named)
		return HAL_ERR_INVALID;

	/* Failed: no CPU has another one to notify yet. */
	return HAL_ERR_UNSUPPORTED;
}

/*
 * Stops the calling CPU for good.
 */
_Noreturn void
hal_cpu_park(
	void)
{
	/* Waits with interrupts off, so nothing wakes the CPU into the kernel again. */
	(void)hal_irq_disable();
	for (;;)
		hal_halt();
}

/*
 * Stops every CPU after a panic.
 */
_Noreturn void
hal_cpu_panic_all(
	void)
{
	/* The calling CPU is the only one running. */
	hal_cpu_park();
}
