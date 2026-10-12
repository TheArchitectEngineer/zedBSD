/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Exercises the actual allocator against a first-fit reference with fragmented DMA ranges. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../src/hal/arm64/page.c"

/* The host fixture serializes all bitmap changes as the current one-CPU HAL does. */
static unsigned irq_held;

static uint32_t old_search(size_t size, size_t alignment, hal_physaddr_t max_paddr, size_t boundary);
static void check_against_old(size_t size, size_t alignment, hal_physaddr_t max_paddr, size_t boundary, hal_physaddr_t *owner);
static void compare_old(void);

/*
 * Checks that allocation enters the bitmap exclusion once.
 */
uint64_t
arm64_irq_save(
	void)
{
	assert(irq_held == 0);
	irq_held = 1;

	/* Succeeded: the fixture owns the bitmap until restore. */
	return 1;
}

/*
 * Checks that every allocation outcome restores the bitmap exclusion.
 */
void
arm64_irq_restore(
	uint64_t state)
{
	assert(state == 1 && irq_held == 1);
	irq_held = 0;

	/* Succeeded: no simulated IRQ exclusion escaped the operation. */
	return;
}

/*
 * Compares constrained allocations and low-address reuse with the original first-fit contract.
 */
int
main(
	void)
{
	hal_physaddr_t owners[128];
	size_t lengths[128];
	uint32_t random;
	uint32_t need;
	uint32_t alignment;
	uint32_t boundary;
	uint32_t limit;
	uint32_t candidate;
	uint32_t index;
	uint32_t slot;
	uint32_t step;
	uint32_t occupied;
	int error;

	/* A large occupied prefix models a running desktop rather than an empty allocator. */
	memset(page_bitmap, 0xff, sizeof(page_bitmap));
	memset(reserved_bitmap, 0xff, sizeof(reserved_bitmap));
	memset(owners, 0, sizeof(owners));
	memset(lengths, 0, sizeof(lengths));
	phys_pages = 4096;
	allocated_pages = 0;
	first_free_candidate = 1;
	for (index = 1024; index < phys_pages; index++) {
		BIT_CLEAR(page_bitmap, index);
		BIT_CLEAR(reserved_bitmap, index);
	}

	/* Interleaves frees with allocations of different alignment, size and DMA windows. */
	random = 0x141;
	for (step = 0; step < 4096; step++) {
		random = random * 1664525U + 1013904223U;
		slot = (random >> 16) % 128;
		if (owners[slot] != 0) {
			error = hal_pmem_free(&owners[slot], lengths[slot]);
			assert(error == HAL_OK && owners[slot] == 0);
		}

		/* Chooses an independent combination of physical placement constraints. */
		need = 1 + ((random >> 8) % 32);
		alignment = 1U << ((random >> 24) % 4);
		boundary = 0;
		if ((random & 1U) != 0)
			boundary = 64;
		limit = 4096;
		if ((random & 2U) != 0)
			limit = 1536;

		/* The reference starts at page one and never uses the production cursor. */
		for (candidate = 1; candidate + need <= limit; candidate++) {
			if ((candidate & (alignment - 1)) != 0)
				continue;
			if (boundary != 0 && candidate / boundary != (candidate + need - 1) / boundary)
				continue;
			for (index = 0; index < need; index++) {
				occupied = BIT_GET(page_bitmap, candidate + index);
				if (occupied != 0)
					break;
			}

			/* The first wholly free candidate is the reference result. */
			if (index == need)
				break;
		}

		/* Both allocators must select the exact same first fitting physical range. */
		error = hal_pmem_alloc_limited(
			(size_t)need * 4096,
			(size_t)alignment * 4096,
			(uint64_t)limit * 4096 - 1,
			(size_t)boundary * 4096,
			&owners[slot]);
		if (candidate + need <= limit) {
			assert(error == HAL_OK);
			assert(owners[slot] == (uint64_t)candidate * 4096);
			lengths[slot] = (size_t)need * 4096;
		} else {
			assert(error == HAL_ERR_NOMEM && owners[slot] == 0);
		}

		/* Failed constrained searches must not hide free pages from a later request. */
		assert(irq_held == 0 && first_free_candidate >= 1024);
		for (index = 1; index < first_free_candidate; index++)
			assert(BIT_GET(page_bitmap, index));
	}

	/* Frees every surviving run and proves that low-address reuse rewinds the cursor. */
	for (slot = 0; slot < 128; slot++) {
		if (owners[slot] != 0) {
			error = hal_pmem_free(&owners[slot], lengths[slot]);
			assert(error == HAL_OK);
		}
	}

	/* Reuses the earliest newly free page after every surviving owner is gone. */
	error = hal_pmem_alloc(4096, 4096, &owners[0]);
	assert(error == HAL_OK && owners[0] == 1024U * 4096U);
	assert(allocated_pages == 1);
	puts("WS141 ARM64 page cursor / 4096 first-fit comparisons / constrained reuse: PASS");

	/* The word-at-a-time search returns the old page-at-a-time search's run in every case. */
	compare_old();

	/* Succeeded: allocation semantics and cursor ownership survived fragmentation. */
	return 0;
}

/*
 * The search of the allocator before the word-at-a-time scan, verbatim
 * except that it only reads the bitmap and cursor: the page it would claim,
 * or UINT32_MAX for HAL_ERR_NOMEM.
 */
static uint32_t
old_search(
	size_t size,
	size_t alignment,
	hal_physaddr_t max_paddr,
	size_t boundary)
{
	uint32_t need, end, start, index, align_pages, cursor;
	uint64_t first_byte, last_byte;
	uintptr_t limit = (uintptr_t)phys_pages * ARM64_PAGE_SIZE;

	/* The same refusals and rounding. */
	if (size == 0 || alignment < ARM64_PAGE_SIZE)
		return UINT32_MAX;
	if (boundary != 0 && (boundary & (boundary - 1U)) != 0)
		return UINT32_MAX;
	need = (uint32_t)((size + ARM64_PAGE_SIZE - 1) / ARM64_PAGE_SIZE);
	align_pages = (uint32_t)(alignment / ARM64_PAGE_SIZE);
	end = (uint32_t)(limit / ARM64_PAGE_SIZE);
	if (max_paddr < (hal_physaddr_t)limit)
		end = (uint32_t)((max_paddr + 1U) / ARM64_PAGE_SIZE);
	if (need == 0 || need >= end)
		return UINT32_MAX;

	/* The cursor advanced page by page, on a copy. */
	cursor = first_free_candidate;
	while (cursor < phys_pages && BIT_GET(page_bitmap, cursor))
		cursor++;

	/* The old candidate loop. */
	for (start = cursor; start + need <= end; start++) {
		if ((start & (align_pages - 1U)) != 0)
			continue;
		if (boundary != 0) {
			first_byte = (uint64_t)start * ARM64_PAGE_SIZE;
			last_byte = first_byte + (uint64_t)need * ARM64_PAGE_SIZE - 1U;
			if (first_byte / boundary != last_byte / boundary)
				continue;
		}
		for (index = 0; index < need && !BIT_GET(page_bitmap, start + index); index++)
			;
		if (index == need)
			break;
		start += index;
	}

	/* No run left. */
	if (start + need > end)
		return UINT32_MAX;

	/* Succeeded: the old allocator would claim from start. */
	return start;
}

/* Allocates through the production path and asserts the old search's answer. */
static void
check_against_old(
	size_t size,
	size_t alignment,
	hal_physaddr_t max_paddr,
	size_t boundary,
	hal_physaddr_t *owner)
{
	uint32_t expected;
	uint32_t index;
	int error;

	/* The same physical run, or the same exhaustion. */
	expected = old_search(size, alignment, max_paddr, boundary);
	*owner = 0;
	error = hal_pmem_alloc_limited(size, alignment, max_paddr, boundary, owner);
	if (expected == UINT32_MAX) {
		assert(error == HAL_ERR_NOMEM && *owner == 0);
		return;
	}
	assert(error == HAL_OK && *owner == (hal_physaddr_t)expected * ARM64_PAGE_SIZE);

	/* Every page of the run is claimed, and the cursor invariant holds. */
	for (index = 0; index < (size + ARM64_PAGE_SIZE - 1) / ARM64_PAGE_SIZE; index++)
		assert(BIT_GET(page_bitmap, expected + index));
	for (index = 1; index < first_free_candidate; index++)
		assert(BIT_GET(page_bitmap, index));
	assert(irq_held == 0);
}

/* Compares the word-at-a-time search with the old one on fragmented maps and at word and map edges. */
static void
compare_old(
	void)
{
	static hal_physaddr_t owners[512];
	static size_t lengths[512];
	hal_physaddr_t extra;
	uint32_t random;
	uint32_t index;
	uint32_t slot;
	uint32_t step;
	uint32_t kind;
	uint32_t need;
	uint32_t alignment;
	uint32_t boundary;
	uint32_t limit;
	int error;

	/* A map whose last word is partial (4109 pages), with a 700-page occupied prefix. */
	memset(page_bitmap, 0xff, sizeof(page_bitmap));
	memset(reserved_bitmap, 0xff, sizeof(reserved_bitmap));
	memset(owners, 0, sizeof(owners));
	memset(lengths, 0, sizeof(lengths));
	phys_pages = 4109;
	allocated_pages = 0;
	first_free_candidate = 1;
	for (index = 700; index < phys_pages; index++) {
		BIT_CLEAR(page_bitmap, index);
		BIT_CLEAR(reserved_bitmap, index);
	}

	/* A free run across a word boundary: pages 704..767 are taken except 730..769 (40 pages). */
	for (slot = 0; slot < 3; slot++) {
		check_against_old(4096, 4096, UINT64_MAX, 0, &owners[slot]);
		lengths[slot] = 4096;
	}
	check_against_old((size_t)27 * 4096, 4096, UINT64_MAX, 0, &owners[3]);
	lengths[3] = (size_t)27 * 4096;
	check_against_old((size_t)40 * 4096, 4096, UINT64_MAX, 0, &owners[4]);
	lengths[4] = (size_t)40 * 4096;
	check_against_old((size_t)100 * 4096, (size_t)64 * 4096, UINT64_MAX, 0, &owners[5]);
	lengths[5] = (size_t)100 * 4096;
	error = hal_pmem_free(&owners[4], lengths[4]);
	assert(error == HAL_OK);
	check_against_old((size_t)41 * 4096, 4096, UINT64_MAX, 0, &owners[4]);
	lengths[4] = (size_t)41 * 4096;
	check_against_old((size_t)40 * 4096, 4096, UINT64_MAX, 0, &owners[6]);
	lengths[6] = (size_t)40 * 4096;

	/* The partial last word: the tail's 13 pages fit a 13-page request at 4096, a 14-page one only lower or nowhere. */
	check_against_old((size_t)13 * 4096, (size_t)4096 * 4096, UINT64_MAX, 0, &owners[7]);
	lengths[7] = (size_t)13 * 4096;
	check_against_old(4096, (size_t)4096 * 4096, UINT64_MAX, 0, &extra);
	assert(extra == 0);
	check_against_old((size_t)5000 * 4096, 4096, UINT64_MAX, 0, &extra);
	assert(extra == 0);

	/* A boundary smaller than the run, and a non-power-of-two alignment, as before. */
	check_against_old((size_t)3 * 4096, 4096, UINT64_MAX, (size_t)2 * 4096, &extra);
	assert(extra == 0);
	check_against_old((size_t)2 * 4096, (size_t)3 * 4096, UINT64_MAX, 0, &owners[8]);
	lengths[8] = (size_t)2 * 4096;

	/* Random fragmentation: single pages, large runs, DMA-limited and boundary runs, and frees. */
	random = 0x5eed141;
	for (step = 0; step < 20000; step++) {
		random = random * 1664525U + 1013904223U;
		slot = 16 + (random >> 16) % 496;
		if (owners[slot] != 0) {
			error = hal_pmem_free(&owners[slot], lengths[slot]);
			assert(error == HAL_OK && owners[slot] == 0);
			continue;
		}

		/* Chooses the kind of request. */
		kind = (random >> 4) % 10;
		need = 1;
		alignment = 1;
		boundary = 0;
		limit = phys_pages;
		if (kind >= 5 && kind < 7) {
			need = 33 + (random >> 8) % 600;
			alignment = 1U << ((random >> 20) % 5);
		} else if (kind >= 7) {
			need = 1 + (random >> 8) % 64;
			alignment = 1U << ((random >> 20) % 4);
			limit = 900 + (random >> 12) % 3300;
			if ((random & 1U) != 0)
				boundary = 64U << ((random >> 24) % 3);
		}
		check_against_old(
			(size_t)need * 4096,
			(size_t)alignment * 4096,
			(hal_physaddr_t)limit * 4096 - 1,
			(size_t)boundary * 4096,
			&owners[slot]);
		if (owners[slot] != 0)
			lengths[slot] = (size_t)need * 4096;
	}

	/* A freed low page rewinds the cursor and is the next single page. */
	for (slot = 0; slot < 512; slot++) {
		if (owners[slot] != 0) {
			error = hal_pmem_free(&owners[slot], lengths[slot]);
			assert(error == HAL_OK);
		}
	}
	assert(allocated_pages == 0);
	check_against_old(4096, 4096, UINT64_MAX, 0, &extra);
	assert(extra == 700U * 4096U);
	puts("WS141 ARM64 word-at-a-time search equals the old search (edges, partial word, 20000 fragmented requests): PASS");
}
