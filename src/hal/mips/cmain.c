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
 * board's display is set up, and what was found is printed on it.  The
 * trap handling, the tick, the tables and the kernel come after this
 * stage; until they are in place the processor stops here.
 */

#include <hal/hal.h>
#include "asm.h"
#include "bsp.h"
#include "cpu.h"

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

	/* Stops: the trap handling and the kernel are not in place yet. */
	hal_puts("MIPS32 HAL STOP AFTER CONSOLE\n");
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
