/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS scheduling tick and counter.
 *
 * Count runs at a rate the board gives, and Compare raises the timer
 * interrupt when Count reaches it; writing Compare also takes the
 * interrupt down.  Each tick arms the next boundary one interval ahead.
 * Count is 32 bits wide, so the 64-bit counter counts its wraps; it is
 * read at least once a tick, far more often than Count wraps.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bsp.h"
#include "clock.h"
#include "irq.h"
#include "spin.h"

/*
 * Count's rate and the Count cycles between two ticks.  Set once by
 * mips_clock_init() before the timer interrupt is let in; a zero rate
 * means the tick is not set up.
 */
static uint32_t clock_frequency;
static uint32_t clock_interval;

/*
 * The Compare value of the next tick.  Written by the tick alone after
 * the first one is armed.
 */
static uint32_t clock_deadline;

/*
 * The 64-bit counter's state: Count when it was last read, and the wraps
 * of Count before that reading.  Both change together with interrupts
 * off, so that a reading never sees one without the other.
 */
static uint32_t clock_last_count;
static uint32_t clock_wraps;

/*
 * The ticks served since the tick was set up.  Only the tick increases
 * it; any context reads it.
 */
static volatile uint32_t clock_tick_count;

/*
 * Nonzero once the kernel takes the ticks: set by
 * mips_clock_start_kernel() just before the kernel starts.  Until then a
 * tick is only counted, since nothing of the kernel is set up.
 */
static int clock_kernel_running;

static uint32_t read_count(void);
static void write_compare(uint32_t value);

/*
 * Sets up the scheduling tick and arms the first one.
 *
 * The caller lets the timer's line in through Status.IM afterwards.
 */
void
mips_clock_init(
	void)
{
	uint32_t now;

	/* The interval of a tick, from the board's Count rate. */
	clock_frequency = mips_board_count_frequency();
	if (clock_frequency == 0)
		HAL_FATAL("mips board gives no Count rate");
	clock_interval = clock_frequency / HAL_TIMER_FREQUENCY;
	if (clock_interval == 0)
		clock_interval = 1;

	/* Starts the 64-bit counter from Count as it is now. */
	now = read_count();
	clock_last_count = now;
	clock_wraps = 0;

	/* Arms the first tick one interval from now. */
	clock_deadline = now + clock_interval;
	write_compare(clock_deadline);
}

/*
 * Serves the timer interrupt: counts the tick and arms the next one.
 *
 * A boundary already behind Count would only come again after Count
 * wraps, so a late tick moves the deadline one interval past now
 * instead.
 */
void
mips_clock_interrupt(
	void)
{
	uint32_t now;
	int32_t ahead;

	/* Keeps the 64-bit counter's wraps counted. */
	(void)mips_clock_read_counter();

	/* Moves the deadline on by one interval, or past now when that is already behind. */
	now = read_count();
	clock_deadline += clock_interval;
	ahead = (int32_t)(clock_deadline - now);
	if (ahead <= 0)
		clock_deadline = now + clock_interval;

	/* Arms the next tick, which also takes this one's interrupt down. */
	write_compare(clock_deadline);

	/* The tick is what the boot waits on and, later, what the kernel schedules on. */
	clock_tick_count++;

	/* The kernel ends the tick, which needs no controller, and schedules. */
	if (clock_kernel_running) {
		mips_irq_begin_local();
		kernel_timer_handler(0, MIPS_ACK_LOCAL);
	}
}

/*
 * Hands the ticks to the kernel from the next one on.
 */
void
mips_clock_start_kernel(
	void)
{
	/* Every later tick goes to kernel_timer_handler(). */
	clock_kernel_running = 1;
}

/*
 * Reads the free-running counter and its rate.
 */
bool
hal_rtc_read_counter(
	uint64_t *value,
	uint64_t *freq_hz)
{
	/* Refuses a caller with nowhere to put the answer, or a clock not started. */
	if (value == NULL || freq_hz == NULL)
		return false;
	if (clock_frequency == 0)
		return false;

	/* Succeeded: Count with its wraps, at Count's rate. */
	*value = mips_clock_read_counter();
	*freq_hz = clock_frequency;
	return true;
}

/*
 * Refuses the time of day: the board's real-time clock is not read yet.
 */
bool
hal_rtc_read_epoch_time(
	uint64_t *unix_seconds)
{
	UNUSED_PARAMETER(unix_seconds);

	/* Failed: there is no clock the HAL reads. */
	return false;
}

/*
 * Reads the 64-bit counter, which runs at mips_clock_frequency().
 */
uint64_t
mips_clock_read_counter(
	void)
{
	uint32_t now;
	uint32_t wraps;
	bool enabled;

	/* Counts a wrap when Count went below its last reading. */
	enabled = mips_irq_save();

	now = read_count();
	if (now < clock_last_count)
		clock_wraps++;
	clock_last_count = now;
	wraps = clock_wraps;

	mips_irq_restore(enabled);

	/* Succeeded: the wraps above Count. */
	return ((uint64_t)wraps << 32) | now;
}

/*
 * Reports the rate of the 64-bit counter, zero before the tick is set up.
 */
uint64_t
mips_clock_frequency(
	void)
{
	/* Succeeded: Count's rate. */
	return clock_frequency;
}

/*
 * Reports the ticks served since the tick was set up.
 */
uint32_t
mips_clock_ticks(
	void)
{
	/* Succeeded: the tick count. */
	return clock_tick_count;
}

/* Reads Count (coprocessor 0 register 9). */
static uint32_t
read_count(
	void)
{
	uint32_t value;

	/* Moves the register from coprocessor 0. */
	__asm__ volatile("mfc0 %0, $9" : "=r"(value));

	/* Succeeded: the register's value. */
	return value;
}

/* Writes Compare (coprocessor 0 register 11), which also clears the timer interrupt. */
static void
write_compare(
	uint32_t value)
{
	/* The barrier keeps the interrupt from being seen still pending after the write. */
	__asm__ volatile("mtc0 %0, $11\n\tehb" : : "r"(value) : "memory");
}
