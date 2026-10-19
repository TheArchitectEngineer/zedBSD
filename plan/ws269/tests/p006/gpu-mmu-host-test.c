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

#include "drivers/gpu/powervr/gpu-mmu.h"
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

int
main(void)
{
	test_entries();
	test_map();
	test_space();
	test_heap_pages();
	test_buffer_rules();
	printf("PASS %d FAIL %d\n", passes, failures);
	return failures == 0 ? 0 : 1;
}
