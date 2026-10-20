/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR core's page tables (three levels, 4 KiB pages
 * only, never a snoop), a context's address space, and the firmware
 * heap's page map.
 *
 * Tables are plain host pages whose "physical" address is a counter, so
 * every entry can be decoded and walked back.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/fixed-area.h"
#include "drivers/gpu/powervr/gpu-mmu.h"
#include "drivers/gpu/powervr/interface.h"
#include "drivers/gpu/powervr/mips-table.h"

#define MAX_TABLES	64U

static int failures;
static int passes;

/* The fake physical memory: page number i lives at tables[i]. */
static void *tables[MAX_TABLES];
static unsigned table_count;
static unsigned tables_live;
static unsigned memory_live;
static int fail_table_after = -1;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL: %s\n", what);
}

static int
take_table(
	void *context,
	struct powervr_gpu_table *table)
{
	void *page;

	(void)context;
	if (fail_table_after == 0)
		return ENOMEM;
	if (fail_table_after > 0)
		fail_table_after--;
	if (table_count == MAX_TABLES)
		return ENOMEM;
	page = aligned_alloc(4096, 4096);
	if (page == NULL)
		return ENOMEM;
	memset(page, 0, 4096);
	tables[table_count] = page;
	table->physical = 0x80000000ULL + (uint64_t)table_count * 4096U;
	table->view = page;
	table->opaque = NULL;
	table_count++;
	tables_live++;
	return 0;
}

static void
give_table(
	void *context,
	struct powervr_gpu_table *table)
{
	unsigned index;

	(void)context;
	index = (unsigned)((table->physical - 0x80000000ULL) / 4096U);
	free(tables[index]);
	tables[index] = NULL;
	tables_live--;
}

static void *
take_memory(
	void *context,
	size_t size)
{
	(void)context;
	memory_live++;
	return calloc(1, size);
}

static void
give_memory(
	void *context,
	void *memory)
{
	(void)context;
	memory_live--;
	free(memory);
}

static const struct powervr_gpu_memory memory = {
	take_table, give_table, take_memory, give_memory, NULL
};

/* Walks the three levels from the catalogue as the core would. */
static uint64_t
walk(
	const struct powervr_gpu_mmu *mmu,
	uint64_t address)
{
	const volatile uint32_t *catalogue;
	const volatile uint64_t *directory;
	const volatile uint64_t *table;
	uint32_t pce;
	uint64_t pde;
	uint64_t pte;
	unsigned index;

	catalogue = (const volatile uint32_t *)mmu->catalogue.view;
	pce = catalogue[(address >> 30) & 1023U];
	if ((pce & 1U) == 0U)
		return 0;
	index = (unsigned)(((uint64_t)(pce >> 4) << 12) - 0x80000000ULL) / 4096U;
	directory = tables[index];
	pde = directory[(address >> 21) & 511U];
	if ((pde & 1U) == 0U)
		return 0;
	if ((pde & 0xeU) != 0U)
		return 0;	/* not a 4 KiB page size */
	index = (unsigned)(((pde & 0xfffffff000ULL) - 0x80000000ULL) / 4096U);
	table = tables[index];
	pte = table[(address >> 12) & 511U];
	return pte;
}

static void
test_entries(void)
{
	uint64_t pte;

	check(powervr_gpu_catalogue_entry(0x12345000ULL) == ((0x12345U << 4) | 1U), "catalogue entry: page number shifted by four, valid");
	check(powervr_gpu_directory_entry(0x40001000ULL) == (0x40001000ULL | 1U), "directory entry: address, 4 KiB size zero, valid");
	pte = powervr_gpu_page_entry(0x40002000ULL, 0);
	check(pte == (0x40002000ULL | 1U), "page entry: address and valid only");
	check((pte & POWERVR_GPU_PAGE_SNOOP) == 0U, "page entry: never asks for a snoop");
	pte = powervr_gpu_page_entry(0x40002000ULL, POWERVR_GPU_MAP_SLC_BYPASS | POWERVR_GPU_MAP_READ_ONLY);
	check(pte == (0x40002000ULL | 8U | 2U | 1U), "page entry: SLC bypass and read-only bits");
	check((pte & POWERVR_GPU_PAGE_SNOOP) == 0U, "page entry with flags: still no snoop");
	pte = powervr_gpu_page_entry(0xfff0000000ULL, 0);
	check((pte & POWERVR_GPU_PAGE_ADDRESS) == 0xfff0000000ULL, "page entry: 40-bit address kept");
}

static void
test_map(void)
{
	struct powervr_gpu_mmu mmu;
	uint64_t base;
	uint64_t pte;
	int error;

	table_count = 0;
	error = powervr_gpu_mmu_init(&mmu, &memory);
	check(error == 0, "init");
	check(tables_live == 1, "init takes only the catalogue");

	base = 0x8000000000ULL;
	error = powervr_gpu_mmu_map(&mmu, base, 0x40000000ULL, 3U * 4096U, POWERVR_GPU_MAP_SLC_BYPASS);
	check(error == 0, "map three pages");
	pte = walk(&mmu, base);
	check(pte == (0x40000000ULL | 8U | 1U), "walk: first page");
	pte = walk(&mmu, base + 2U * 4096U);
	check(pte == (0x40002000ULL | 8U | 1U), "walk: third page");
	check(walk(&mmu, base + 3U * 4096U) == 0, "walk: past the range is invalid");
	check(powervr_gpu_mmu_lookup(&mmu, base + 4096U) == (0x40001000ULL | 8U | 1U), "lookup agrees with the walk");
	check(tables_live == 3, "one directory and one table were made");

	/* A range crossing a 2 MiB table boundary and a 1 GiB directory boundary. */
	error = powervr_gpu_mmu_map(&mmu, base + 0x3fffe000ULL, 0x50000000ULL, 4U * 4096U, 0);
	check(error == 0, "map across a directory boundary");
	check(walk(&mmu, base + 0x3fffe000ULL) == (0x50000000ULL | 1U), "walk: before the boundary");
	check(walk(&mmu, base + 0x40001000ULL) == (0x50003000ULL | 1U), "walk: after the boundary");

	/* Overlap is refused and changes nothing. */
	error = powervr_gpu_mmu_map(&mmu, base + 4096U, 0x60000000ULL, 4096U, 0);
	check(error == EEXIST, "a mapped page is refused");
	check(walk(&mmu, base + 4096U) == (0x40001000ULL | 8U | 1U), "the refused map left the entry alone");

	/* Bad arguments. */
	check(powervr_gpu_mmu_map(&mmu, base + 1U, 0x40000000ULL, 4096U, 0) == EINVAL, "unaligned address refused");
	check(powervr_gpu_mmu_map(&mmu, base, 0x40000001ULL, 4096U, 0) == EINVAL, "unaligned physical refused");
	check(powervr_gpu_mmu_map(&mmu, base, 0x40000000ULL, 0, 0) == EINVAL, "empty range refused");
	check(powervr_gpu_mmu_map(&mmu, 0xfffffff000ULL, 0x40000000ULL, 8192U, 0) == EINVAL, "range past 40 bits refused");
	check(powervr_gpu_mmu_map(&mmu, base, 0x10000000000ULL, 4096U, 0) == EINVAL, "physical past 40 bits refused");

	/* Unmap clears entries only. */
	powervr_gpu_mmu_unmap(&mmu, base, 3U * 4096U);
	check(walk(&mmu, base) == 0 && walk(&mmu, base + 8192U) == 0, "unmapped pages read invalid");
	check(powervr_gpu_mmu_map(&mmu, base, 0x70000000ULL, 4096U, 0) == 0, "an unmapped page can be mapped again");

	/* A failure part way leaves nothing of the range mapped. */
	fail_table_after = 0;
	error = powervr_gpu_mmu_map(&mmu, base + 0x80000000ULL - 4096U, 0x40000000ULL, 2U * 4096U, 0);
	check(error == ENOMEM, "a table that cannot be made fails the map");
	check(walk(&mmu, base + 0x80000000ULL - 4096U) == 0, "the failed map left its first page unmapped");
	fail_table_after = -1;

	powervr_gpu_mmu_fini(&mmu);
	check(tables_live == 0, "fini returns every table");
	check(memory_live == 0, "fini returns every directory's bookkeeping");
}

static void
test_space(void)
{
	static struct powervr_gpu_space space;
	uint64_t a;
	uint64_t b;
	uint64_t c;
	uint64_t d;

	powervr_gpu_space_init(&space, 0x8000000000ULL, 0x100000ULL);
	check(powervr_gpu_space_take(&space, 0x10000U, &a) == 0 && a == 0x8000000000ULL, "first range at the base");
	check(powervr_gpu_space_take(&space, 0x1000U, &b) == 0 && b == a + 0x10000U + 0x1000U, "next range after a guard page");
	check(powervr_gpu_space_take(&space, 0x2000U, &c) == 0 && c == b + 0x1000U + 0x1000U, "third range after another guard");
	check(powervr_gpu_space_give(&space, b) == 0, "give back the middle");
	check(powervr_gpu_space_take(&space, 0x1000U, &d) == 0 && d == b, "the gap is reused");
	check(powervr_gpu_space_give(&space, b + 1U) == EINVAL, "giving a non-start is refused");
	check(powervr_gpu_space_take(&space, 0x100000U, &d) == ENOMEM, "too big for the window");
	check(powervr_gpu_space_take(&space, 0x123U, &d) == EINVAL, "not whole pages refused");
	check(powervr_gpu_space_take(&space, 0, &d) == EINVAL, "empty refused");
	check(powervr_gpu_space_take(&space, 0x100000U - 0x10000U - 0x1000U - 0x1000U - 0x1000U - 0x2000U - 0x1000U - 0x1000U, &d) == 0, "the rest of the window fits exactly");
	check(powervr_gpu_space_take(&space, 0x1000U, &d) == ENOMEM, "the window is full");
}

static void
test_heap_pages(void)
{
	uint64_t map[POWERVR_HEAP_MAP_WORDS];
	uint32_t first;

	memset(map, 0, sizeof(map));
	check(powervr_heap_pages_claim(map, 0, 25) == 0, "claim the code's place");
	check(powervr_heap_pages_claim(map, 24, 2) == EEXIST, "an overlapping claim is refused");
	check(powervr_heap_pages_take(map, 4, &first) == 0 && first == 25, "the first free run follows");
	check(powervr_heap_pages_take(map, 1, &first) == 0 && first == 29, "and the next");
	powervr_heap_pages_give(map, 25, 4);
	check(powervr_heap_pages_take(map, 2, &first) == 0 && first == 25, "a freed run is reused");
	check(powervr_heap_pages_take(map, 3, &first) == 0 && first == 30, "a run that does not fit the hole goes past it");
	check(powervr_heap_pages_take(map, POWERVR_HEAP_GENERAL_PAGES, &first) == ENOMEM, "too long a run");
	check(powervr_heap_pages_take(map, 0, &first) == EINVAL, "empty run refused");
	check(powervr_heap_pages_claim(map, POWERVR_HEAP_GENERAL_PAGES - 1U, 2) == EINVAL, "claim past the general part refused");
	memset(map, 0, sizeof(map));
	check(powervr_heap_pages_take(map, POWERVR_HEAP_GENERAL_PAGES, &first) == 0 && first == 0, "the whole general part in one run");
	check(powervr_heap_pages_take(map, 1, &first) == ENOMEM, "then nothing is free");
	check(POWERVR_HEAP_GENERAL_PAGES * 4096U + 0x30000U + 0x100000U == 0x1000000U, "the general part, the config area and the reserve fill the heap");
}

/* The SLC choice per buffer (WS277 R3) and the placement check of a run (R5). */
static void
test_buffer_rules(void)
{
	check(powervr_gpu_buffer_flags(0, 0) == 0U, "a private buffer keeps the SLC");
	check(powervr_gpu_buffer_flags(1, 0) == POWERVR_GPU_MAP_SLC_BYPASS, "a mappable buffer bypasses the SLC");
	check(powervr_gpu_buffer_flags(0, 1) == POWERVR_GPU_MAP_SLC_BYPASS, "a shareable, not mappable buffer bypasses the SLC");
	check(powervr_gpu_buffer_flags(1, 1) == POWERVR_GPU_MAP_SLC_BYPASS, "a mappable shareable buffer bypasses the SLC");
	check(powervr_gpu_run_fits(0x80000000ULL, 0x1000U, 0xffffffffULL, 0) == 0, "a run below 4 GiB fits DMA32");
	check(powervr_gpu_run_fits(0xfffff000ULL, 0x1000U, 0xffffffffULL, 0) == 0, "a run ending at the limit fits");
	check(powervr_gpu_run_fits(0xfffff000ULL, 0x2000U, 0xffffffffULL, 0) == ENOTSUP, "a run crossing the limit is refused");
	check(powervr_gpu_run_fits(0x100000000ULL, 0x1000U, 0, 0) == 0, "no limit takes any address");
	check(powervr_gpu_run_fits(0x80010000ULL, 0x1000U, 0, 0x10000U) == 0, "an aligned base fits");
	check(powervr_gpu_run_fits(0x80011000ULL, 0x1000U, 0, 0x10000U) == ENOTSUP, "an unaligned base is refused");
	check(powervr_gpu_run_fits(0x80011000ULL, 0x1000U, 0, 1U) == 0, "alignment one means any");
	check(powervr_gpu_run_fits(0x80000000ULL, 0x1000U, 0, 0x3000U) == EINVAL, "a non power of two alignment is refused");
	check(powervr_gpu_run_fits(0x80000000ULL, 0, 0, 0) == EINVAL, "an empty run is refused");
	check(powervr_gpu_run_fits(~0ULL - 0xfffU, 0x2000U, 0, 0) == EINVAL, "a wrapping run is refused");
}

/* Reads one little-endian word of a page. */
static uint32_t
page_word(
	const uint8_t *page,
	unsigned offset)
{
	return (uint32_t)page[offset] | ((uint32_t)page[offset + 1U] << 8) |
	    ((uint32_t)page[offset + 2U] << 16) | ((uint32_t)page[offset + 3U] << 24);
}

/* Counts the non-zero bytes of a page in [from, to). */
static unsigned
nonzero_bytes(
	const uint8_t *page,
	unsigned from,
	unsigned to)
{
	unsigned count;
	unsigned index;

	count = 0;
	for (index = from; index < to; index++) {
		if (page[index] != 0)
			count++;
	}
	return count;
}

/*
 * The fixed area at the start of the general, data sequencer and shading
 * cluster heaps (BUG-342): where its parts lie, that a session's
 * allocations start after it, that its mappings do not collide with the
 * executor's program places (0x10000 into each program heap), and what
 * its programs hold.
 *
 * The expected words are worked out by hand from Mesa 26.2.5: the task
 * issue "dout" with the end bit and DOUTU as the destination, from the
 * zero 64-bit constant (opcode 0xf at 28, end at 26, destination 2), is
 * 0xf4000002; the task word for offset 0 with no temporaries and the
 * instance rate is zero.  The shading cluster program is one main group
 * (pco_map.py O_UVSW_EMIT_ENDTASK) laid out by pco_group_instrs.c,
 * pco_map.h.py and pco_binary.c: a 3-byte header, the 1-byte back end
 * operation (uvsw_op emit_endtask 0b101), the unused lower and upper
 * source words of one byte each (a main group always carries both), the
 * 1-byte internal selector and a word padding byte 0xff, 8 bytes in all,
 * so no alignment padding; the header has the decode assist 4 (header
 * and back end) and the length 4 half-words (0x44), the extension bit
 * and the back end organisation 0b010 (0xa0), and the end bit (0x80).
 */
static void
test_fixed_area(void)
{
	static const uint8_t shader[8] = { 0x44, 0xa0, 0x80, 0x05, 0x00, 0x00, 0x00, 0xff };
	static struct powervr_gpu_space space;
	struct powervr_gpu_mmu mmu;
	uint8_t *pds;
	uint8_t *usc;
	uint64_t first;
	unsigned index;
	int same;

	/* The parts lie in their heap's reserved bytes, the general ones apart, each reserve whole pages. */
	check(POWERVR_FIXED_FENCE_OFFSET + POWERVR_FIXED_FENCE_BYTES <= POWERVR_FIXED_COLOUR_OFFSET, "fixed: the fence ends before the colour coefficients");
	check(POWERVR_FIXED_COLOUR_OFFSET + POWERVR_FIXED_COLOUR_BYTES <= POWERVR_FIXED_GENERAL_BYTES, "fixed: the general parts fit the general reserve");
	check(POWERVR_FIXED_SYNC_PDS_OFFSET + POWERVR_FIXED_SYNC_PDS_BYTES <= POWERVR_FIXED_TILE_END_OFFSET, "fixed: the sync program ends before the end-of-tile code");
	check(POWERVR_FIXED_TILE_END_OFFSET + POWERVR_FIXED_TILE_END_BYTES <= POWERVR_FIXED_PDS_BYTES, "fixed: the data sequencer parts fit their reserve");
	check(POWERVR_FIXED_SYNC_USC_OFFSET + POWERVR_FIXED_SYNC_USC_BYTES <= POWERVR_FIXED_USC_BYTES, "fixed: the shader fits its reserve");
	check(POWERVR_FIXED_GENERAL_BYTES == 0x1000U && POWERVR_FIXED_PDS_BYTES == 0x1000U && POWERVR_FIXED_USC_BYTES == 0x1000U, "fixed: each reserve is the one page past the last part");

	/* The session's general space as render_open() sets it up: its first range is past the area. */
	powervr_gpu_space_init(&space, POWERVR_GENERAL_HEAP_BASE + POWERVR_FIXED_GENERAL_BYTES, POWERVR_GENERAL_HEAP_BYTES - POWERVR_FIXED_GENERAL_BYTES);
	check(powervr_gpu_space_take(&space, 0x1000U, &first) == 0 && first == POWERVR_GENERAL_HEAP_BASE + POWERVR_FIXED_GENERAL_BYTES, "fixed: the first allocation is the page after the area");

	/* The area mapped at the three heaps' starts, then an allocation and the executor's program places beside it. */
	check(powervr_gpu_mmu_init(&mmu, &memory) == 0, "fixed: context init");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_GENERAL_HEAP_BASE, 0x80000000ULL, POWERVR_FIXED_GENERAL_BYTES, POWERVR_GPU_MAP_SLC_BYPASS) == 0, "fixed: the general page maps");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_PDS_HEAP_BASE, 0x80001000ULL, POWERVR_FIXED_PDS_BYTES, POWERVR_GPU_MAP_SLC_BYPASS) == 0, "fixed: the data sequencer page maps");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_USC_HEAP_BASE, 0x80002000ULL, POWERVR_FIXED_USC_BYTES, POWERVR_GPU_MAP_SLC_BYPASS) == 0, "fixed: the shading cluster page maps");
	check(powervr_gpu_mmu_map(&mmu, first, 0x80003000ULL, 0x1000U, 0) == 0, "fixed: the first allocation maps beside the area");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_PDS_HEAP_BASE + 0x10000U, 0x80010000ULL, 0x3000U, POWERVR_GPU_MAP_SLC_BYPASS) == 0, "fixed: the executor's data sequencer place maps");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_USC_HEAP_BASE + 0x10000U, 0x80020000ULL, 0x10000U, POWERVR_GPU_MAP_SLC_BYPASS) == 0, "fixed: the executor's shader place maps");
	check((powervr_gpu_mmu_lookup(&mmu, POWERVR_GENERAL_HEAP_BASE) & POWERVR_GPU_PAGE_VALID) != 0, "fixed: the fence is backed by a page");
	check(powervr_gpu_mmu_lookup(&mmu, POWERVR_PDS_HEAP_BASE + POWERVR_FIXED_PDS_BYTES) == 0, "fixed: the data sequencer heap is free after the area");
	check(powervr_gpu_mmu_lookup(&mmu, POWERVR_USC_HEAP_BASE + POWERVR_FIXED_USC_BYTES) == 0, "fixed: the shading cluster heap is free after the area");
	check(powervr_gpu_mmu_map(&mmu, POWERVR_GENERAL_HEAP_BASE, 0x80004000ULL, 0x1000U, 0) == EEXIST, "fixed: nothing else maps over the area");
	powervr_gpu_mmu_fini(&mmu);

	/* The programs in zeroed pages. */
	pds = calloc(1, POWERVR_FIXED_PDS_BYTES);
	usc = calloc(1, POWERVR_FIXED_USC_BYTES);
	check(powervr_fixed_area_write(NULL, usc) == EINVAL, "fixed: a missing page is refused");
	check(powervr_fixed_area_write(pds, usc) == 0, "fixed: the programs are written");

	/* The synchronisation program: the zero task word in eight data words, then the task issue that ends. */
	check(nonzero_bytes(pds, POWERVR_FIXED_SYNC_PDS_OFFSET, POWERVR_FIXED_SYNC_PDS_OFFSET + 32U) == 0, "fixed: the sync data is the zero task word");
	check(page_word(pds, POWERVR_FIXED_SYNC_PDS_OFFSET + 32U) == 0xf4000002U, "fixed: the sync code issues the task and ends");
	check(nonzero_bytes(pds, POWERVR_FIXED_SYNC_PDS_OFFSET + 36U, POWERVR_FIXED_TILE_END_OFFSET) == 0, "fixed: nothing else in the sync part");

	/* The end-of-tile code: one task issue that ends. */
	check(page_word(pds, POWERVR_FIXED_TILE_END_OFFSET) == 0xf4000002U, "fixed: the end-of-tile code issues the task and ends");
	check(nonzero_bytes(pds, POWERVR_FIXED_TILE_END_OFFSET + 4U, POWERVR_FIXED_PDS_BYTES) == 0, "fixed: nothing after the end-of-tile code");

	/* The shading cluster program: the one group, padded to the cache alignment. */
	same = 1;
	for (index = 0; index < sizeof(shader); index++) {
		if (usc[POWERVR_FIXED_SYNC_USC_OFFSET + index] != shader[index])
			same = 0;
	}
	check(same, "fixed: the sync shader is the emit-and-end group");
	check(nonzero_bytes(usc, POWERVR_FIXED_SYNC_USC_OFFSET + sizeof(shader), POWERVR_FIXED_USC_BYTES) == 0, "fixed: nothing after the sync shader");
	free(pds);
	free(usc);
}

int
main(void)
{
	test_entries();
	test_map();
	test_space();
	test_heap_pages();
	test_buffer_rules();
	test_fixed_area();
	printf("PASS %d FAIL %d\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
