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

/*
 * The counter's frequency and the counter ticks between two scheduling
 * ticks.  The boot hart sets both once, before any other hart starts; a
 * zero frequency means the tick is not set up.
 */
static uint64_t timer_frequency;
static uint64_t timer_interval;

/*
 * Starts the calling hart's scheduling tick.
 */
void
riscv64_clock_init(
	void)
{
	struct riscv64_percpu *cpu;

	/* The boot hart derives the tick's length from the timebase. */
	cpu = riscv64_percpu_current();
	if (cpu->logical_id == 0) {
		timer_frequency = riscv64_boot_fdt()->timebase_frequency;
		if (timer_frequency == 0)
			HAL_FATAL("riscv64 device tree gives no timebase");
		timer_interval = timer_frequency / HAL_TIMER_FREQUENCY;
		if (timer_interval == 0)
			timer_interval = 1;
	}

	/* Arms the first tick one interval from now and lets its interrupt in. */
	cpu->timer_deadline = riscv64_read_time() + timer_interval;
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

	/* Moves the deadline past now in whole intervals and arms it. */
	do {
		cpu->timer_deadline += timer_interval;
	} while (cpu->timer_deadline <= now);
	riscv64_sbi_set_timer(cpu->timer_deadline);

	/* The boot hart's first tick marks the timer working in the boot log. */
	if (cpu->logical_id == 0 && cpu->timer_ticks == 1)
		hal_puts("RISCV64 TIMER TICK\n");

	/* Refuses a tick while this hart still holds another interrupt. */
	if (cpu->active_ack != HAL_IRQ_ACK_NONE)
		HAL_FATAL("nested riscv64 IRQ acknowledgement");

	/* The kernel ends the tick, which needs no controller, and schedules. */
	cpu->active_ack = RISCV64_ACK_LOCAL;
	kernel_timer_handler(cpu->logical_id, RISCV64_ACK_LOCAL);
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

	/* Succeeded: the counter now and its frequency. */
	*value = riscv64_read_time();
	*freq_hz = timer_frequency;
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
