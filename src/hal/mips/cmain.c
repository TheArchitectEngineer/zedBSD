/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS processor's first C code, from locore.S on.
 *
 * The loader's tables are copied before anything may overwrite them, the
 * board's display is set up, and what was found is printed on it.  Then
 * the trap handling, the context switch and the tick are checked one by
 * one, the board's interrupt controllers are set up with every line
 * masked, and the tick is let in.  The page tables and the kernel come
 * after this stage; until they are in place the processor stops once the
 * tick is seen.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bsp.h"
#include "clock.h"
#include "cpu.h"
#include "spin.h"
#include "task.h"
#include "trap.h"

/* The ticks the boot waits to see before it reports the tick as running. */
#define CMAIN_TICKS_WANTED	10U

/*
 * How long the boot waits for those ticks, in Count cycles per tick:
 * ten intervals' worth for each, so that a slow host does not fail it.
 */
#define CMAIN_TICK_WAIT_FACTOR	10U

static void wait_for_ticks(void);

/*
 * Brings the processor from locore.S to the console.
 *
 * The four arguments are the loader's: the number of entries of its
 * argument table, the KSEG0 addresses of that table and of its
 * environment table, and the size of low memory.
 */
void
mips_cmain(
	uint32_t argument_count,
	uint32_t argument_table,
	uint32_t environment_table,
	uint32_t low_memory_size)
{
	uint32_t status;
	uint32_t exception_base;
	uint32_t processor_id;
	uint32_t config;

	/* Keeps the loader's tables while the memory they are in is untouched. */
	mips_board_boot(argument_count, argument_table, environment_table, low_memory_size);

	/* Shows the display, which then holds what the console printed. */
	mips_board_init();

	/* Describes the processor as locore.S left it. */
	status = mips_read_status();
	exception_base = mips_read_exception_base();
	processor_id = mips_read_processor_id();
	config = mips_read_config();
	hal_printf("MIPS32 HAL ENTRY prid=%08x status=%08x ebase=%08x config=%08x\n",
		   processor_id,
		   status,
		   exception_base,
		   config);

	/* Describes the board. */
	mips_board_describe();

	/* Checks that an exception enters and leaves the trap handling. */
	mips_trap_selftest();

	/* Checks the context switch before any task relies on it. */
	mips_context_selftest();

	/* Masks every external line; nothing is registered yet. */
	mips_board_irq_init();

	/* Arms the tick, lets its line and the board's in, and waits for it to run. */
	mips_clock_init();
	wait_for_ticks();

	/* Stops: the page tables and the kernel are not in place yet. */
	hal_puts("MIPS32 HAL STOP AFTER TICK\n");
	mips_halt_forever();
}

/*
 * Reports an exception taken before the trap handling exists, and stops.
 */
void
mips_early_exception(
	uint32_t cause,
	uint32_t exception_pc,
	uint32_t bad_address,
	uint32_t status)
{
	uint32_t code;

	/* The exception's code, from Cause. */
	code = (cause >> MIPS_CAUSE_CODE_SHIFT) & MIPS_CAUSE_CODE_MASK;

	/* Prints the processor's state at the exception. */
	hal_printf("MIPS32 EARLY EXCEPTION code=%u cause=%08x epc=%08x badvaddr=%08x status=%08x\n",
		   code,
		   cause,
		   exception_pc,
		   bad_address,
		   status);

	/* Stops for good. */
	mips_halt_forever();
}

/*
 * Stops the processor for good, waiting with interrupts off.
 */
void
mips_halt_forever(
	void)
{
	uint32_t status;

	/* Turns interrupts off, so that the wait below never ends. */
	status = mips_read_status();
	status &= ~(uint32_t)MIPS_STATUS_INTERRUPTS;
	__asm__ volatile("mtc0 %0, $12\n\tehb" : : "r"(status) : "memory");

	/* Waits for good. */
	for (;;)
		__asm__ volatile("wait" ::: "memory");
}

/* Lets the tick in and waits a bounded time for the first ticks. */
static void
wait_for_ticks(
	void)
{
	uint64_t start;
	uint64_t limit;
	uint64_t now;
	uint32_t status;
	uint32_t ticks;
	uint32_t frequency;

	/* Opens the timer's and the board's lines, then interrupts. */
	status = mips_read_status();
	status |= MIPS_INTERRUPT_TIMER | MIPS_INTERRUPT_BOARD;
	__asm__ volatile("mtc0 %0, $12\n\tehb" : : "r"(status) : "memory");
	mips_irq_restore(true);

	/* Waits, by the counter rather than by wait, so that a missing tick is reported. */
	start = mips_clock_read_counter();
	frequency = (uint32_t)mips_clock_frequency();
	limit = (uint64_t)frequency / HAL_TIMER_FREQUENCY * CMAIN_TICK_WAIT_FACTOR * CMAIN_TICKS_WANTED;
	for (;;) {
		ticks = mips_clock_ticks();
		if (ticks >= CMAIN_TICKS_WANTED)
			break;
		now = mips_clock_read_counter();
		if (now - start > limit) {
			hal_printf("MIPS32 HAL TIMER FAIL ticks=%u\n", ticks);
			mips_halt_forever();
		}
	}

	/* Reports the tick with the rate it runs on. */
	hal_printf("MIPS32 HAL TIMER TICK ticks=%u count=%u Hz\n", ticks, frequency);
}
