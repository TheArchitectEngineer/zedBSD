/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The riscv64 scheduling tick and counter.
 *
 * The counter is the time register, which runs at the device tree's
 * timebase on every hart alike.  Each hart's tick is armed through the
 * firmware's timer call, one interval ahead at a time.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bootinfo.h"
#include "clock.h"
#include "irq.h"
#include "percpu.h"
#include "sbi.h"

/* The time riscv64_clock_ticked() allows the first tick, as a fraction of a second. */
#define CLOCK_TICK_WAIT_DIVISOR	10U

/*
 * The counter's frequency.  The boot hart sets it once, with
 * timer_interval, before any other hart starts; zero means the tick is not
 * set up.
 */
static uint64_t timer_frequency;

/* The counter ticks between two scheduling ticks, set with timer_frequency. */
static uint64_t timer_interval;

/*
 * Starts the calling hart's scheduling tick.
 */
void
riscv64_clock_init(
	void)
{
	const struct riscv64_fdt_info *info;
	struct riscv64_percpu *cpu;
	uint64_t now;

	/* The boot hart derives the tick's length from the timebase. */
	cpu = riscv64_percpu_current();
	if (cpu->logical_id == 0) {
		/* The counter's frequency, without which there is no tick. */
		info = riscv64_boot_fdt();
		timer_frequency = info->timebase_frequency;
		if (timer_frequency == 0)
			HAL_FATAL("riscv64 device tree gives no timebase");

		/* The counter ticks per scheduling tick, at least one. */
		timer_interval = timer_frequency / HAL_TIMER_FREQUENCY;
		if (timer_interval == 0)
			timer_interval = 1;
	}

	/* Arms the first tick one interval from now and lets its interrupt in. */
	now = riscv64_read_time();
	cpu->timer_deadline = now + timer_interval;
	riscv64_sbi_set_timer(cpu->timer_deadline);
	riscv64_set_sie(RISCV64_SIE_STIE);
}

/*
 * Serves the calling hart's scheduling tick.
 *
 * The next deadline moves on by whole intervals past now and is armed
 * before the kernel sees the tick, so a tick that came late does not make
 * the following ones come early, and the kernel may switch tasks freely.
 */
void
riscv64_clock_interrupt(
	void)
{
	struct riscv64_percpu *cpu;
	uint64_t now;

	/* Counts the tick against this hart. */
	cpu = riscv64_percpu_current();
	now = riscv64_read_time();
	cpu->timer_ticks++;

	/* Moves the deadline past now in whole intervals. */
	do {
		cpu->timer_deadline += timer_interval;
	} while (cpu->timer_deadline <= now);

	/* Arms the next tick. */
	riscv64_sbi_set_timer(cpu->timer_deadline);

	/* Refuses a tick while this hart still holds another interrupt. */
	if (cpu->active_ack != HAL_IRQ_ACK_NONE)
		HAL_FATAL("nested riscv64 IRQ acknowledgement");

	/* The kernel ends the tick, which needs no controller, and schedules. */
	cpu->active_ack = RISCV64_ACK_LOCAL;
	kernel_timer_handler(cpu->logical_id, RISCV64_ACK_LOCAL);
}

/*
 * Waits a short while for the boot hart's first tick and reports whether it came.
 *
 * The tick is counted by the interrupt alone, so the answer is written from
 * the caller's own context and never cuts into a line another part of the
 * boot is printing.  A caller with interrupts off is answered at once.
 */
bool
riscv64_clock_ticked(
	void)
{
	struct riscv64_percpu *boot_cpu;
	uint64_t deadline;
	uint64_t now;
	uint64_t ticks;
	bool masked;

	/* A tick not set up has nothing to wait for. */
	if (timer_frequency == 0U)
		return false;

	/* Answers from the count when the first tick is already in. */
	boot_cpu = riscv64_percpu_get(0);
	ticks = __atomic_load_n(&boot_cpu->timer_ticks, __ATOMIC_RELAXED);
	if (ticks != 0U)
		return true;

	/* A caller with interrupts off would wait for a tick that cannot arrive. */
	masked = riscv64_irq_masked();
	if (masked)
		return false;

	/* Waits up to a tenth of a second for the count to move. */
	now = riscv64_read_time();
	deadline = now + timer_frequency / CLOCK_TICK_WAIT_DIVISOR;
	while (now < deadline) {
		/* Leaves the wait once the count has moved. */
		ticks = __atomic_load_n(&boot_cpu->timer_ticks, __ATOMIC_RELAXED);
		if (ticks != 0U)
			break;

		/* Waits a moment before looking again. */
		hal_atomic_relax();
		now = riscv64_read_time();
	}

	/* The boot hart saw no tick in the time allowed. */
	if (ticks == 0U)
		return false;

	/* Succeeded: the first tick came. */
	return true;
}

/*
 * Reads the counter and its frequency.
 *
 * The counter is the same on every hart and runs through every idle state.
 */
bool
hal_rtc_read_counter(
	uint64_t *value,
	uint64_t *freq_hz)
{
	/* Refuses a caller with nowhere to put the answer, or a tick not set up. */
	if (value == NULL || freq_hz == NULL)
		return false;
	if (timer_frequency == 0U)
		return false;

	/* Reads the counter now, with its frequency. */
	*value = riscv64_read_time();
	*freq_hz = timer_frequency;

	/* Succeeded: both are the caller's. */
	return true;
}

/*
 * Reports the wall-clock time; the HAL reads no battery-backed clock.
 */
bool
hal_rtc_read_epoch_time(
	uint64_t *unix_seconds)
{
	UNUSED_PARAMETER(unix_seconds);

	/* Failed: there is no clock to read. */
	return false;
}
