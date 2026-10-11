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

	/* Succeeded: allocation semantics and cursor ownership survived fragmentation. */
	return 0;
}
