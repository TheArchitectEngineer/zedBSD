/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The armv7 cache maintenance: the level 1 data and instruction caches,
 * by address.
 *
 * The ranges are kernel addresses of the direct map or the image, which
 * never fault.  A partial line at either end of a range to be discarded is
 * written back first, so that the bytes beside the range that share its
 * line are not lost (Procedure referenced: Linux v6.12
 * arch/arm/mm/cache-v7.S, v7_dma_inv_range).  The board's outer cache, if
 * it has one, is off.
 *
 * Values referenced: ARM DDI 0406C, CTR (DminLine and IminLine), and the
 * maintenance operations DCCMVAC, DCIMVAC, DCCIMVAC, DCCMVAU, ICIMVAU and
 * BPIALL.
 */

#include <hal/hal.h>
#include "asm.h"
#include "defs.h"

static uint32_t data_line_size(void);
static uint32_t instruction_line_size(void);

/*
 * Writes the cached lines of a range back to memory.
 */
void
hal_dcache_clean_range(
	uintptr_t addr,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* An empty range needs nothing. */
	if (size == 0)
		return;

	/* Cleans each line the range touches to the point of coherency. */
	step = data_line_size();
	line = addr & ~(uintptr_t)(step - 1U);
	end = addr + size;
	while (line < end) {
		__asm__ volatile("mcr p15, 0, %0, c7, c10, 1" : : "r"(line) : "memory");
		line += step;
	}

	/* Completes the maintenance before the caller hands the memory over. */
	armv7_dsb();
}

/*
 * Discards the cached lines of a range, after writing back the partial
 * lines at its ends.
 */
void
hal_dcache_invalidate_range(
	uintptr_t addr,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* An empty range needs nothing. */
	if (size == 0)
		return;

	/* A partial first line keeps the bytes before the range. */
	step = data_line_size();
	end = addr + size;
	line = addr & ~(uintptr_t)(step - 1U);
	if (line != addr) {
		__asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(line) : "memory");
		line += step;
	}

	/* A partial last line keeps the bytes after the range. */
	if ((end & (step - 1U)) != 0 && (end & ~(uintptr_t)(step - 1U)) >= line) {
		__asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(end & ~(uintptr_t)(step - 1U)) : "memory");
		end &= ~(uintptr_t)(step - 1U);
	}

	/* Discards each whole line in between. */
	while (line < end) {
		__asm__ volatile("mcr p15, 0, %0, c7, c6, 1" : : "r"(line) : "memory");
		line += step;
	}

	/* Completes the maintenance before the caller reads the memory. */
	armv7_dsb();
}

/*
 * Writes back and discards the cached lines of a range.
 */
void
hal_dcache_clean_invalidate_range(
	uintptr_t addr,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* An empty range needs nothing. */
	if (size == 0)
		return;

	/* Cleans and discards each line the range touches. */
	step = data_line_size();
	line = addr & ~(uintptr_t)(step - 1U);
	end = addr + size;
	while (line < end) {
		__asm__ volatile("mcr p15, 0, %0, c7, c14, 1" : : "r"(line) : "memory");
		line += step;
	}

	/* Completes the maintenance. */
	armv7_dsb();
}

/*
 * Makes code written through the data side visible to instruction fetch:
 * the data cache is cleaned to the point of unification, the instruction
 * cache and the branch predictor dropped for the range.
 */
void
hal_sync_instruction_stream(
	void *addr,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* An empty range needs nothing. */
	if (size == 0)
		return;

	/* Cleans each data line to the point of unification. */
	step = data_line_size();
	line = (uintptr_t)addr & ~(uintptr_t)(step - 1U);
	end = (uintptr_t)addr + size;
	while (line < end) {
		__asm__ volatile("mcr p15, 0, %0, c7, c11, 1" : : "r"(line) : "memory");
		line += step;
	}
	armv7_dsb();

	/* Drops each instruction line, then the branch predictor. */
	step = instruction_line_size();
	line = (uintptr_t)addr & ~(uintptr_t)(step - 1U);
	while (line < end) {
		__asm__ volatile("mcr p15, 0, %0, c7, c5, 1" : : "r"(line) : "memory");
		line += step;
	}
	__asm__ volatile("mcr p15, 0, %0, c7, c5, 6" : : "r"(0) : "memory");

	/* Completes the maintenance and refetches what follows. */
	armv7_dsb();
	armv7_isb();
}

/*
 * Makes a range of instructions current.
 */
void
hal_icache_invalidate_range(
	uintptr_t addr,
	size_t size)
{
	/* The same synchronization, for the caller's range. */
	hal_sync_instruction_stream((void *)addr, size);
}

/* Reports the smallest data cache line, in bytes. */
static uint32_t
data_line_size(
	void)
{
	uint32_t type;

	/* DminLine, bits 19 to 16, is the line's words as a power of two. */
	__asm__ volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(type));

	/* Succeeded: four bytes a word. */
	return 4U << ((type >> 16) & 0xfU);
}

/* Reports the smallest instruction cache line, in bytes. */
static uint32_t
instruction_line_size(
	void)
{
	uint32_t type;

	/* IminLine, bits 3 to 0, is the line's words as a power of two. */
	__asm__ volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(type));

	/* Succeeded: four bytes a word. */
	return 4U << (type & 0xfU);
}
