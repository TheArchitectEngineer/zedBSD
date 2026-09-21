/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the session address space of src/drivers/gpu/i915/ppgtt.c
 * (drv_i915_ppgtt_*): the scratch chain a new space starts as, the tables
 * an insert allocates along the four-level walk, clear, and the range
 * allocator, against the fake physical page pool.
 */

#include "i915-host-stubs.inc"

#include "../../../src/drivers/gpu/i915/ppgtt.h"

/* The entry bits of a writable, present page and of an uncached one (PAT index 3). */
#define FIXTURE_PAGE_BITS	(I915_GEN8_PAGE_PRESENT_B | I915_GEN8_PAGE_RW_B)
#define FIXTURE_UNCACHED_BITS	(FIXTURE_PAGE_BITS | I915_GEN12_PTE_PAT0 | I915_GEN12_PTE_PAT1)

static uint64_t *fixture_table(uint64_t entry);
static uint64_t fixture_lookup(struct i915_ppgtt *vm, uint64_t va);
static void test_ppgtt_create(void);
static void test_ppgtt_walk(void);
static void test_ppgtt_uncached(void);
static void test_ppgtt_va(void);

/*
 * Runs the session address space checks.
 */
int
main(void)
{
	/* Checks creation, the walk, uncached inserts and the range allocator. */
	test_ppgtt_create();
	test_ppgtt_walk();
	test_ppgtt_uncached();
	test_ppgtt_va();

	/* Every page and block went back. */
	assert(stub_pool_live == 0U);
	assert(stub_live == 0U);

	/* Succeeded: every check held. */
	printf("i915 ppgtt host test PASS\n");
	return 0;
}

/* Views the table an entry names through the fake direct map. */
static uint64_t *
fixture_table(
	uint64_t entry)
{
	uint64_t *page;

	/* The address bits of the entry name the table page. */
	page = kern_pmem_to_kernel((hal_physaddr_t)(entry & I915_PPGTT_ADDRESS_MASK));
	assert(page != NULL);

	/* Reports the table. */
	return page;
}

/*
 * Walks a space as the GPU does and reports the leaf entry of a page.
 *
 * Every level indexes with nine bits above the twelve-bit page offset; a
 * scratch entry leads down the scratch chain, so every address resolves.
 */
static uint64_t
fixture_lookup(
	struct i915_ppgtt *vm,
	uint64_t va)
{
	uint64_t *table;
	uint64_t entry;
	unsigned level;
	unsigned index;

	/* Descends from the top table to the leaf table. */
	table = fixture_table(vm->pml4.paddr);
	for (level = I915_PPGTT_LEVELS - 1U; level > 0U; level--) {
		index = (unsigned)((va >> (12U + 9U * level)) & (I915_PPGTT_ENTRIES - 1U));
		table = fixture_table(table[index]);
	}

	/* Reads the page's entry from the leaf table. */
	index = (unsigned)((va >> 12) & (I915_PPGTT_ENTRIES - 1U));
	entry = table[index];

	/* Reports the entry. */
	return entry;
}

/* A new space is a scratch chain: every level names the level below. */
static void
test_ppgtt_create(void)
{
	struct i915_ppgtt vm;
	uint64_t *page;
	uint64_t entry;
	unsigned index;
	unsigned live;
	int error;

	/* Creates a space: the data page, three scratch tables and the top table. */
	memset(&vm, 0, sizeof(vm));
	live = stub_pool_live;
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	assert(vm.created == 1U);
	assert(stub_pool_live == live + 5U);
	assert(vm.next_va == I915_PPGTT_VA_START);

	/* The data page is zero and its entry is a writable present PTE with PAT index 0. */
	page = kern_pmem_to_kernel(vm.scratch[0].paddr);
	for (index = 0U; index < I915_PPGTT_ENTRIES; index++)
		assert(page[index] == 0U);
	assert(vm.scratch_entry[0] == ((uint64_t)vm.scratch[0].paddr | FIXTURE_PAGE_BITS));

	/* Each scratch table is full of the entry below it and is named uncached (PAT index 3). */
	for (index = 1U; index < I915_PPGTT_LEVELS; index++) {
		page = kern_pmem_to_kernel(vm.scratch[index].paddr);
		assert(page[0] == vm.scratch_entry[index - 1U]);
		assert(page[I915_PPGTT_ENTRIES - 1U] == vm.scratch_entry[index - 1U]);
		assert(vm.scratch_entry[index] == ((uint64_t)vm.scratch[index].paddr | FIXTURE_UNCACHED_BITS));
	}

	/* The top table names the scratch directory pointer everywhere, so every address reads scratch. */
	page = kern_pmem_to_kernel(vm.pml4.paddr);
	assert(page[0] == vm.scratch_entry[3]);
	assert(page[511] == vm.scratch_entry[3]);
	entry = fixture_lookup(&vm, 0U);
	assert(entry == vm.scratch_entry[0]);
	entry = fixture_lookup(&vm, 1ULL << 47);
	assert(entry == vm.scratch_entry[0]);

	/* A second creation is refused; destroy returns every page. */
	error = drv_i915_ppgtt_create(&vm);
	assert(error == EBUSY);
	drv_i915_ppgtt_destroy(&vm);
	assert(vm.created == 0U);
	assert(stub_pool_live == live);
}

/* Inserts allocate tables along the walk and use the canonical index bits. */
static void
test_ppgtt_walk(void)
{
	struct i915_ppgtt vm;
	struct i915_ppgtt other;
	uint64_t *pml4;
	uint64_t *pdp;
	uint64_t *pd;
	uint64_t *pt;
	uint64_t entry;
	uint64_t va;
	unsigned live;
	int error;

	/* Creates two spaces. */
	memset(&vm, 0, sizeof(vm));
	memset(&other, 0, sizeof(other));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	error = drv_i915_ppgtt_create(&other);
	assert(error == 0);
	live = stub_pool_live;

	/* An address with all four indices distinct exercises every shift. */
	va = ((uint64_t)3U << 39) | ((uint64_t)17U << 30) | ((uint64_t)200U << 21) | ((uint64_t)511U << 12);
	error = drv_i915_ppgtt_insert(&vm, va, STUB_POOL_BASE + 0x40000U, 2U);
	assert(error == 0);

	/* Four tables were allocated: a directory pointer, a directory and two leaf tables. */
	assert(stub_pool_live == live + 4U);
	assert(vm.page_count == 4U);
	pml4 = kern_pmem_to_kernel(vm.pml4.paddr);
	assert(pml4[3] != vm.scratch_entry[3]);
	assert((pml4[3] & 0xfffU) == FIXTURE_PAGE_BITS);
	pdp = fixture_table(pml4[3]);
	assert(pdp[17] != vm.scratch_entry[2]);
	assert(pdp[16] == vm.scratch_entry[2]);
	pd = fixture_table(pdp[17]);
	assert(pd[200] != vm.scratch_entry[1]);
	assert(pd[199] == vm.scratch_entry[1]);
	pt = fixture_table(pd[200]);
	assert(pt[511] == ((STUB_POOL_BASE + 0x40000U) | FIXTURE_PAGE_BITS));
	assert(pt[510] == vm.scratch_entry[0]);

	/* The second page crossed into the next leaf table. */
	assert(pd[201] != vm.scratch_entry[1]);
	pt = fixture_table(pd[201]);
	assert(pt[0] == ((STUB_POOL_BASE + 0x41000U) | FIXTURE_PAGE_BITS));
	entry = fixture_lookup(&vm, va + I915_PAGE_BYTES);
	assert(entry == pt[0]);

	/* The other space is untouched by this space's tables. */
	entry = fixture_lookup(&other, va);
	assert(entry == other.scratch_entry[0]);
	assert(other.page_count == 0U);

	/* Clearing restores scratch leaves but keeps the tables allocated. */
	drv_i915_ppgtt_clear(&vm, va, 2U);
	entry = fixture_lookup(&vm, va);
	assert(entry == vm.scratch_entry[0]);
	assert(pt[0] == vm.scratch_entry[0]);
	assert(stub_pool_live == live + 4U);

	/* Misaligned, empty, out-of-range and unreachable inserts are refused. */
	error = drv_i915_ppgtt_insert(&vm, va + 1U, STUB_POOL_BASE, 1U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, va, STUB_POOL_BASE + 1U, 1U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, va, STUB_POOL_BASE, 0U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, (1ULL << 48) - I915_PAGE_BYTES, STUB_POOL_BASE, 2U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, va, I915_DMA_MAX_ADDRESS + 1U, 1U);
	assert(error == EINVAL);
	assert(stub_pool_live == live + 4U);

	/* Destroy returns the inserted tables with the rest of each space. */
	drv_i915_ppgtt_destroy(&vm);
	drv_i915_ppgtt_destroy(&other);
	assert(stub_pool_live == 0U);
	assert(stub_live == 0U);
}

/* An uncached insert names its pages with PAT index 3, as the display's scanout needs. */
static void
test_ppgtt_uncached(void)
{
	struct i915_ppgtt vm;
	uint64_t entry;
	int error;

	/* Creates a space and maps one page uncached at the first range. */
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	error = drv_i915_ppgtt_insert_uncached(&vm, I915_PPGTT_VA_START, STUB_POOL_BASE + 0x20000U, 1U);
	assert(error == 0);

	/* The leaf entry carries the page and the uncached PAT bits; the next page is scratch. */
	entry = fixture_lookup(&vm, I915_PPGTT_VA_START);
	assert(entry == ((STUB_POOL_BASE + 0x20000U) | FIXTURE_UNCACHED_BITS));
	entry = fixture_lookup(&vm, I915_PPGTT_VA_START + I915_PAGE_BYTES);
	assert(entry == vm.scratch_entry[0]);

	/* Destroys the space. */
	drv_i915_ppgtt_destroy(&vm);
	assert(stub_pool_live == 0U);
}

/* The bump allocator starts above 4 GiB, aligns to 2 MiB and never reuses ranges. */
static void
test_ppgtt_va(void)
{
	struct i915_ppgtt vm;
	uint64_t first;
	uint64_t second;
	uint64_t third;
	int error;

	/* Creates a space. */
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);

	/* Three ranges, each on its own 2 MiB boundary. */
	error = drv_i915_ppgtt_va_alloc(&vm, 1U, &first);
	assert(error == 0);
	assert(first == I915_PPGTT_VA_START);
	error = drv_i915_ppgtt_va_alloc(&vm, I915_PPGTT_VA_ALIGN + 1U, &second);
	assert(error == 0);
	assert(second == first + I915_PPGTT_VA_ALIGN);
	error = drv_i915_ppgtt_va_alloc(&vm, 4096U, &third);
	assert(error == 0);
	assert(third == second + 2U * I915_PPGTT_VA_ALIGN);

	/* Zero and exhaustion are refused, and neither hands out an address. */
	error = drv_i915_ppgtt_va_alloc(&vm, 0U, &third);
	assert(error == EINVAL);
	assert(third == 0U);
	error = drv_i915_ppgtt_va_alloc(&vm, 1ULL << 48, &third);
	assert(error == ENOSPC);
	assert(third == 0U);

	/* Destroys the space. */
	drv_i915_ppgtt_destroy(&vm);
}
