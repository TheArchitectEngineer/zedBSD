/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the GGTT restore after a resume (ws052-p009,
 * drv_i915_gt_ggtt_restore() of ggtt.c).
 *
 * ggtt.c is compiled with a GGTT table in host memory: objects are bound
 * into the GT window and the display window (with guards), a firmware
 * range is borrowed, and the table is kept.  Then the table is filled with
 * garbage, as if the GTT had lost it, the restore runs, and the table must
 * equal what the binds left, entry for entry.  An unbound object and a
 * given-back borrowed range must point at scratch after the restore.
 *
 *   make -C plan/ws052/tests i915-ggtt && build/ws052/host/i915-ggtt
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/i915/i915.h"
#include "drivers/gpu/i915/ggtt.h"
#include "drivers/gpu/i915/memory.h"
#include <kern/device-io.h>
#include <kern/klog.h>

/* The simulated table: a small GGTT with both windows at its top. */
#define TABLE_ENTRIES		512U
#define GT_WINDOW_PAGES		128U
#define DISPLAY_WINDOW_PAGES	128U

/* Where the simulated objects' pages are, and the scratch page's encoding. */
#define OBJECT_DMA_BASE		0x100000000ULL
#define SCRATCH_PTE		0x7777000ULL

static void check(bool condition, const char *what);
static void prepare(struct i915_gt_mem *gm, uint64_t *table, struct i915_gt_object *block);
static unsigned scratch_count(const uint64_t *table, unsigned first, unsigned pages);

/* How many checks failed. */
static unsigned failures;

/*
 * Runs the checks and reports how many failed.
 */
int
main(void)
{
	static uint64_t table[TABLE_ENTRIES];
	static uint64_t kept[TABLE_ENTRIES];
	static struct i915_gt_object block[I915_GT_OBJECT_BLOCK];
	struct i915_gt_mem gm;
	unsigned foreign_page;
	unsigned unbound_page;
	unsigned written;
	int same;
	unsigned index;
	int error;

	/* Binds three objects (two in the GT window, one in the display window) and a borrowed range. */
	prepare(&gm, table, block);
	error = drv_i915_gt_ggtt_bind(&gm, &block[0]);
	check(error == 0, "bind: GT object 0");
	error = drv_i915_gt_ggtt_bind(&gm, &block[1]);
	check(error == 0, "bind: GT object 1");
	error = drv_i915_gt_display_bind(&gm, &block[2], 4U, 2U);
	check(error == 0, "bind: display object 2 with guards");
	error = drv_i915_gt_display_bind_foreign(&gm, 0x80000000ULL, 8U, &foreign_page);
	check(error == 0, "bind: a borrowed range");

	/* Keeps the table the binds left. */
	memcpy(kept, table, sizeof(table));

	/* Loses the table, restores it, and compares it entry for entry. */
	for (index = 0; index < TABLE_ENTRIES; index++)
		table[index] = 0xdeadbeef00000000ULL | index;
	written = drv_i915_gt_ggtt_restore(&gm);
	check(written >= GT_WINDOW_PAGES + DISPLAY_WINDOW_PAGES, "restore: every window entry written");
	for (index = gm.display_first; index < TABLE_ENTRIES; index++) {
		/* Each entry of the windows equals what the binds left. */
		if (table[index] != kept[index]) {
			printf("FAIL restore: entry %u is 0x%llx, was 0x%llx\n", index, (unsigned long long)table[index], (unsigned long long)kept[index]);
			failures++;
			break;
		}
	}

	/* An unbound object and a given-back range point at scratch after the next restore. */
	unbound_page = block[1].ggtt_page;
	drv_i915_gt_ggtt_unbind(&gm, &block[1]);
	drv_i915_gt_display_unbind_foreign(&gm, foreign_page, 8U);
	for (index = 0; index < TABLE_ENTRIES; index++)
		table[index] = 0xdeadbeef00000000ULL | index;
	(void)drv_i915_gt_ggtt_restore(&gm);
	check(scratch_count(table, foreign_page, 8U) == 8U, "restore: a given-back range is scratch");
	check(scratch_count(table, unbound_page, 5U) == 5U, "restore: an unbound object is scratch");
	same = memcmp(&table[block[0].ggtt_page], &kept[block[0].ggtt_page], 3U * sizeof(table[0]));
	check(same == 0, "restore: a GT object keeps its entries");
	same = memcmp(&table[block[2].ggtt_page - 2U], &kept[block[2].ggtt_page - 2U], (2U + 6U + 2U) * sizeof(table[0]));
	check(same == 0, "restore: a display object keeps its entries and its guards");
	check(gm.foreign[0].pages == 0U, "restore: the given-back range is forgotten");

	/* A memory that is not prepared writes nothing. */
	gm.inited = 0;
	written = drv_i915_gt_ggtt_restore(&gm);
	check(written == 0U, "restore: nothing without a prepared memory");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("i915-ggtt: %u checks failed\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("i915-ggtt: every check passed\n");
	return 0;
}

/* Counts a check that failed and names it. */
static void
check(
	bool condition,
	const char *what)
{
	/* A check that held says nothing. */
	if (condition)
		return;

	/* Names the check. */
	printf("FAIL %s\n", what);
	failures++;
}

/* Prepares the memory: the table, the two windows, and four objects in the pool. */
static void
prepare(
	struct i915_gt_mem *gm,
	uint64_t *table,
	struct i915_gt_object *block)
{
	unsigned index;
	int error;

	/* The memory with its windows at the top of the table, every entry scratch. */
	memset(gm, 0, sizeof(*gm));
	memset(block, 0, sizeof(*block) * I915_GT_OBJECT_BLOCK);
	gm->table = (volatile uint8_t *)table;
	gm->entries = TABLE_ENTRIES;
	gm->scratch_pte = SCRATCH_PTE;
	gm->dma_mask = I915_DMA_MAX_ADDRESS;
	gm->window_first = TABLE_ENTRIES - GT_WINDOW_PAGES;
	gm->window_pages = GT_WINDOW_PAGES;
	gm->inited = 1;
	for (index = 0; index < TABLE_ENTRIES; index++)
		table[index] = SCRATCH_PTE;
	error = drv_i915_gt_display_window_init(gm, DISPLAY_WINDOW_PAGES);
	check(error == 0, "prepare: the display window");

	/* Four objects of 3, 5, 6 and 2 pages, each with pages at its own place. */
	gm->object_blocks[0] = block;
	gm->object_block_count = 1U;
	for (index = 0; index < 4U; index++) {
		block[index].in_use = 1;
		block[index].cpu = (void *)(uintptr_t)(OBJECT_DMA_BASE + (uint64_t)index * 0x100000ULL);
	}

	/* Their sizes. */
	block[0].pages = 3U;
	block[1].pages = 5U;
	block[2].pages = 6U;
	block[3].pages = 2U;
}

/* Counts the scratch entries of a run of the table. */
static unsigned
scratch_count(
	const uint64_t *table,
	unsigned first,
	unsigned pages)
{
	unsigned count;
	unsigned page;

	/* Counts each entry that holds scratch. */
	count = 0U;
	for (page = 0; page < pages; page++) {
		/* One entry. */
		if (table[first + page] == SCRATCH_PTE)
			count++;
	}

	/* Reports the count. */
	return count;
}

/*
 * The simulated kernel and object pool.
 */

/* An object's page: its base (kept in cpu) plus the page's offset. */
int
drv_i915_gt_object_page_dma(
	const struct i915_gt_object *o,
	unsigned page,
	uint64_t *dma_out)
{
	/* Refuses a page beyond the object. */
	if (page >= o->pages)
		return EINVAL;

	/* Reports the page's address. */
	*dma_out = (uint64_t)(uintptr_t)o->cpu + (uint64_t)page * I915_GT_PAGE_BYTES;
	return 0;
}

/* Never called by the test (the scratch page is not created). */
struct i915_gt_object *
drv_i915_gt_object_create(
	struct i915_gt_mem *gm,
	uint32_t bytes)
{
	(void)gm;
	(void)bytes;
	return NULL;
}

/* Never called by the test. */
void
drv_i915_gt_object_destroy(
	struct i915_gt_mem *gm,
	struct i915_gt_object *o)
{
	(void)gm;
	(void)o;
}

/* Reads a table entry. */
uint64_t
kern_mmio_read64(
	const volatile void *address)
{
	/* Reads the host memory. */
	return *(const volatile uint64_t *)address;
}

/* Writes a table entry. */
void
kern_mmio_write64(
	volatile void *address,
	uint64_t value)
{
	/* Writes the host memory. */
	*(volatile uint64_t *)address = value;
}

/* Orders the writes: nothing to do in host memory. */
void
kern_io_write_barrier(void)
{
}

/* Drops the driver's log lines. */
void
kern_logf(
	const char *format,
	...)
{
	(void)format;
}
