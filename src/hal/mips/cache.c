/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The MIPS caches.
 *
 * The primary caches are virtually indexed and physically tagged.  Their
 * shape comes from Config1: the line, the sets and the ways of each.  The
 * range operations work line by line with the Hit operations of the cache
 * instruction on the address given.  When a way is larger than a page,
 * one physical page can sit in lines of several colors (the index bits
 * above the page offset), and a Hit operation reaches only the color of
 * the address it is given; mips_cache_flush_page_colors() then empties a
 * page in every color with the Index operations.  The emulator does not
 * model caches, so there every operation is accepted and does nothing.
 *
 * Values referenced: MIPS32 Architecture for Programmers Volume III
 * (Config1: IS bits 22-24, IL bits 19-21, IA bits 16-18, DS bits 13-15,
 * DL bits 10-12, DA bits 7-9; the cache instruction's operations
 * Index_Invalidate_I 0x00, Index_Writeback_Inv_D 0x01, Hit_Invalidate_I
 * 0x10, Hit_Invalidate_D 0x11, Hit_Writeback_Inv_D 0x15 and
 * Hit_Writeback_D 0x19).
 */

#include <hal/hal.h>
#include "asm.h"
#include "cache.h"
#include "cpu.h"
#include "io.h"

/* The fields of Config1 that describe each cache. */
#define CACHE_I_SETS_SHIFT	22
#define CACHE_I_LINE_SHIFT	19
#define CACHE_I_WAYS_SHIFT	16
#define CACHE_D_SETS_SHIFT	13
#define CACHE_D_LINE_SHIFT	10
#define CACHE_D_WAYS_SHIFT	7
#define CACHE_FIELD_MASK	0x7U

/* The smallest line the range operations step by when a cache reports none. */
#define CACHE_LINE_FALLBACK	16U

/*
 * The shape of one primary cache: its line in bytes (zero when there is
 * no such cache), the bytes of one way and the number of ways.
 */
struct cache_shape {
	uint32_t line;
	uint32_t way_bytes;
	uint32_t ways;
};

/*
 * The instruction and data caches as Config1 describes them.  Filled once
 * by mips_cache_init() before the first space is made; read-only after.
 */
static struct cache_shape cache_instruction;
static struct cache_shape cache_data;

/*
 * Nonzero when either cache's way is larger than a page, so that a
 * physical page has more than one color.  Set by mips_cache_init().
 */
static int cache_alias;

/*
 * The pages emptied in every color since boot, for the self-test.  It
 * only grows.
 */
static uint32_t cache_color_flushes;

static void read_shape(uint32_t config1, unsigned sets_shift, unsigned line_shift, unsigned ways_shift, struct cache_shape *shape);
static uint32_t step_of(const struct cache_shape *shape);
static void instruction_hazard(void);

/*
 * Reads the shape of the caches and whether pages have colors.
 */
void
mips_cache_init(
	void)
{
	uint32_t config1;

	/* Both caches from Config1. */
	config1 = mips_read_config1();
	read_shape(config1, CACHE_I_SETS_SHIFT, CACHE_I_LINE_SHIFT, CACHE_I_WAYS_SHIFT, &cache_instruction);
	read_shape(config1, CACHE_D_SETS_SHIFT, CACHE_D_LINE_SHIFT, CACHE_D_WAYS_SHIFT, &cache_data);

	/* A way larger than a page gives one physical page several colors. */
	cache_alias = 0;
	if (cache_instruction.way_bytes > MIPS_PAGE_SIZE || cache_data.way_bytes > MIPS_PAGE_SIZE)
		cache_alias = 1;

	hal_printf(MIPS_HAL_NAME " CACHE I %ux%ux%u D %ux%ux%u alias=%d\n",
		   cache_instruction.line,
		   cache_instruction.way_bytes,
		   cache_instruction.ways,
		   cache_data.line,
		   cache_data.way_bytes,
		   cache_data.ways,
		   cache_alias);
}

/*
 * Reports whether a physical page can sit in lines of several colors.
 */
bool
mips_cache_aliases(
	void)
{
	/* Pages have colors when a way is larger than a page. */
	if (cache_alias)
		return true;

	return false;
}

/*
 * Writes back and drops a physical page in every color of the data
 * cache, and of the instruction cache when asked.
 *
 * Nothing is done when pages have one color only: the Hit operations on
 * the kernel's address then reach every line the page can be in.
 */
void
mips_cache_flush_page_colors(
	hal_physaddr_t page,
	bool instructions)
{
	uintptr_t address;
	uint32_t way;
	uint32_t offset;
	uint32_t step;

	UNUSED_PARAMETER(page);

	/* A page of one color needs no index operation. */
	if (!cache_alias)
		return;

	/* Counts the flush for the self-test. */
	cache_color_flushes++;

	/*
	 * Every line of every way of the data cache whose index the page
	 * can take: the page's offset in any of the way's page-sized
	 * colors.  The index operations write back and drop whatever line
	 * is there, which includes the page's own.
	 */
	step = step_of(&cache_data);
	for (way = 0; way < cache_data.ways; way++) {
		for (offset = 0; offset < cache_data.way_bytes; offset += step) {
			address = mips_segment_pointer(MIPS_KSEG0_BASE + way * cache_data.way_bytes + offset);
			__asm__ volatile("cache 0x01, 0(%0)" : : "r"(address) : "memory");
		}
	}

	/* The same for the instruction cache, when the page is to run. */
	if (instructions) {
		step = step_of(&cache_instruction);
		for (way = 0; way < cache_instruction.ways; way++) {
			for (offset = 0; offset < cache_instruction.way_bytes; offset += step) {
				address = mips_segment_pointer(MIPS_KSEG0_BASE + way * cache_instruction.way_bytes + offset);
				__asm__ volatile("cache 0x00, 0(%0)" : : "r"(address) : "memory");
			}
		}
	}

	/* Orders the write-backs before what follows, and drops prefetched instructions. */
	__asm__ volatile("sync" : : : "memory");
	instruction_hazard();
}

/*
 * Reports how many pages were emptied in every color since boot.
 */
uint32_t
mips_cache_color_flushes(
	void)
{
	/* Succeeded: the counter. */
	return cache_color_flushes;
}

/*
 * Writes back a range of the data cache.
 */
void
hal_dcache_clean_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* Each line of the range, then the ordering. */
	step = step_of(&cache_data);
	end = address + size;
	for (line = address & ~(uintptr_t)(step - 1U); line < end; line += step)
		__asm__ volatile("cache 0x19, 0(%0)" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

/*
 * Drops a range of the data cache without writing it back.
 */
void
hal_dcache_invalidate_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* Each line of the range, then the ordering. */
	step = step_of(&cache_data);
	end = address + size;
	for (line = address & ~(uintptr_t)(step - 1U); line < end; line += step)
		__asm__ volatile("cache 0x11, 0(%0)" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

/*
 * Writes back and drops a range of the data cache.
 */
void
hal_dcache_clean_invalidate_range(
	uintptr_t address,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* Each line of the range, then the ordering. */
	step = step_of(&cache_data);
	end = address + size;
	for (line = address & ~(uintptr_t)(step - 1U); line < end; line += step)
		__asm__ volatile("cache 0x15, 0(%0)" : : "r"(line) : "memory");
	__asm__ volatile("sync" : : : "memory");
}

/*
 * Makes code written through the data side visible to instruction fetch.
 */
void
hal_sync_instruction_stream(
	void *addr,
	size_t size)
{
	uintptr_t line;
	uintptr_t end;
	uint32_t step;

	/* Writes the range back from the data cache. */
	hal_dcache_clean_range((uintptr_t)addr, size);

	/* Drops the range's lines from the instruction cache. */
	step = step_of(&cache_instruction);
	end = (uintptr_t)addr + size;
	for (line = (uintptr_t)addr & ~(uintptr_t)(step - 1U); line < end; line += step)
		__asm__ volatile("cache 0x10, 0(%0)" : : "r"(line) : "memory");

	/* Orders the operations and drops instructions fetched before them. */
	__asm__ volatile("sync" : : : "memory");
	instruction_hazard();
}

/*
 * Makes a range of instructions current.
 */
void
hal_icache_invalidate_range(
	uintptr_t addr,
	size_t size)
{
	/* The same synchronization. */
	hal_sync_instruction_stream((void *)addr, size);
}

/* Reads one cache's line, way size and ways from its three fields of Config1. */
static void
read_shape(
	uint32_t config1,
	unsigned sets_shift,
	unsigned line_shift,
	unsigned ways_shift,
	struct cache_shape *shape)
{
	uint32_t sets;
	uint32_t line_field;

	/*
	 * The line is two bytes shifted by the field (zero: no cache), the
	 * sets 64 shifted by theirs, the ways one more than theirs.
	 */
	line_field = (config1 >> line_shift) & CACHE_FIELD_MASK;
	sets = 64U << ((config1 >> sets_shift) & CACHE_FIELD_MASK);
	shape->ways = ((config1 >> ways_shift) & CACHE_FIELD_MASK) + 1U;
	shape->line = 0;
	shape->way_bytes = 0;
	if (line_field != 0) {
		shape->line = 2U << line_field;
		shape->way_bytes = sets * shape->line;
	}
}

/* Reports the step of a cache's range operations: its line, or a small fallback. */
static uint32_t
step_of(
	const struct cache_shape *shape)
{
	/* A cache that reports no line is stepped through at the smallest line. */
	if (shape->line == 0)
		return CACHE_LINE_FALLBACK;

	/* Succeeded: the cache's line. */
	return shape->line;
}

/* Clears the instruction hazard: a jump with the hazard barrier to the next instruction. */
static void
instruction_hazard(
	void)
{
	uintptr_t target;

	/*
	 * Jumps to the label that follows with jr.hb.  The label's address
	 * is built from its halves, which both widths sign-extend alike.
	 */
	__asm__ volatile(".set push\n\t"
			 ".set noreorder\n\t"
			 "lui %0, %%hi(.Lhazard%=)\n\t"
			 "addiu %0, %0, %%lo(.Lhazard%=)\n\t"
			 "jr.hb %0\n\t"
			 "nop\n"
			 ".Lhazard%=:\n\t"
			 ".set pop"
			 : "=r"(target)
			 :
			 : "memory");
}
