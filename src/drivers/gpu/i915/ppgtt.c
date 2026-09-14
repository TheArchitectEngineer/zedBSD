/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Private per-process address spaces: 48-bit, four levels of 512
 * entries.
 *
 * Every table page is one physically contiguous 4 KiB page below the
 * GPU's address limit, written through the kernel direct map.
 * Alder Lake's LLC keeps the GPU's table walks coherent with these CPU
 * stores.
 *
 * References: Linux gen8_ppgtt.c gen8_ppgtt_create,
 * gen8_init_scratch, gen12_pte_encode, gen8_pde_encode,
 * __gen8_ppgtt_alloc and __gen8_ppgtt_clear, re-expressed.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* Index of one level's entry within the walk of a virtual address. */
#define I915_PPGTT_INDEX(va, level)	((unsigned)(((va) >> (12U + 9U * (level))) & (I915_PPGTT_ENTRIES - 1U)))

/* Object pages: present, writable, PAT index 0 (write-back through the LLC). */
#define I915_PPGTT_PAGE_BITS		(GEN8_PAGE_PRESENT | GEN8_PAGE_RW)

/* Tables the driver fills: present, writable, PAT index 0. */
#define I915_PPGTT_TABLE_BITS		(GEN8_PAGE_PRESENT | GEN8_PAGE_RW)

/* Scratch tables mirror Linux's uncached page-directory encoding (PAT index 3). */
#define I915_PPGTT_SCRATCH_TABLE_BITS	(GEN8_PAGE_PRESENT | GEN8_PAGE_RW | GEN12_PPGTT_PTE_PAT0 | GEN12_PPGTT_PTE_PAT1)

static int i915_ppgtt_page_alloc(struct i915_ppgtt *vm, struct kern_pmem *run, uint64_t fill);
static uint64_t *i915_ppgtt_table(uint64_t entry);
static uint64_t *i915_ppgtt_walk(struct i915_ppgtt *vm, uint64_t va, unsigned allocate);
static void i915_ppgtt_fill(void *table, uint64_t value);

/*
 * Builds an empty address space whose every range resolves to the scratch page.
 */
int
drv_i915_ppgtt_create(
	struct i915_ppgtt *vm)
{
	unsigned level;
	int error;

	/* A space is created once; the caller destroys it before reuse. */
	if (vm->created != 0U)
		return EBUSY;
	memset(vm, 0, sizeof(*vm));

	/* Level 0 is the zeroed data page every unmapped address reads. */
	error = i915_ppgtt_page_alloc(vm, &vm->scratch[0], 0U);
	if (error != 0) {
		drv_i915_ppgtt_destroy(vm);
		return error;
	}

	vm->scratch_entry[0] = (uint64_t)vm->scratch[0].paddr | I915_PPGTT_PAGE_BITS;

	/* Each higher scratch level is a table whose entries all name the level below. */
	for (level = 1U; level < I915_PPGTT_LEVELS; level++) {
		/* The table is filled with the previous level's scratch entry. */
		error = i915_ppgtt_page_alloc(vm, &vm->scratch[level], vm->scratch_entry[level - 1U]);
		if (error != 0) {
			drv_i915_ppgtt_destroy(vm);
			return error;
		}

		vm->scratch_entry[level] = (uint64_t)vm->scratch[level].paddr | I915_PPGTT_SCRATCH_TABLE_BITS;
	}

	/* The top table starts out pointing every 512 GiB slice at the scratch directory pointer. */
	error = i915_ppgtt_page_alloc(vm, &vm->pml4, vm->scratch_entry[I915_PPGTT_LEVELS - 1U]);
	if (error != 0) {
		drv_i915_ppgtt_destroy(vm);
		return error;
	}

	/* Objects are placed above the first 4 GiB so a small stray address faults. */
	vm->next_va = I915_PPGTT_VA_START;
	vm->created = 1U;
	kern_io_write_barrier();

	/* Succeeded: the PML4 physical address can be written into a context image. */
	return 0;
}

/*
 * Returns every table page, the scratch chain and the top table to the pool.
 */
void
drv_i915_ppgtt_destroy(
	struct i915_ppgtt *vm)
{
	struct i915_ppgtt_page *page;
	unsigned level;

	/* Tables allocated by inserts are released first; their entries are not walked. */
	while (vm->pages != NULL) {
		page = vm->pages;
		vm->pages = page->next;
		(void)kern_pmem_free(&page->run);
		kern_free(page);
		vm->page_count--;
	}

	/* The top table follows so no entry names a released scratch table. */
	if (vm->pml4.size != 0U) {
		(void)kern_pmem_free(&vm->pml4);
		vm->pml4.size = 0U;
	}

	/* The scratch chain goes last, from the directory pointer down to the data page. */
	for (level = I915_PPGTT_LEVELS; level > 0U; level--) {
		/* A level that was never allocated has nothing to free. */
		if (vm->scratch[level - 1U].size != 0U) {
			(void)kern_pmem_free(&vm->scratch[level - 1U]);
			vm->scratch[level - 1U].size = 0U;
		}
	}

	vm->created = 0U;
}

/*
 * Reserves an aligned virtual range; ranges are never reused within the space.
 */
int
drv_i915_ppgtt_va_alloc(
	struct i915_ppgtt *vm,
	uint64_t bytes,
	uint64_t *va)
{
	uint64_t rounded;
	uint64_t limit;

	/* No caller receives an address for an empty or failed reservation. */
	*va = 0U;
	if (bytes == 0U)
		return EINVAL;

	/* Every object starts on a 2 MiB boundary so later huge-page use stays possible. */
	rounded = (bytes + I915_PPGTT_VA_ALIGN - 1ULL) & ~(I915_PPGTT_VA_ALIGN - 1ULL);
	limit = 1ULL << I915_PPGTT_ADDRESS_BITS;
	if (rounded > limit - vm->next_va)
		return ENOSPC;

	/* The bump pointer advances past the reserved range. */
	*va = vm->next_va;
	vm->next_va += rounded;

	/* Succeeded: the range belongs to the caller for the life of the space. */
	return 0;
}

/*
 * Maps physically contiguous pages at a virtual address, growing tables as needed.
 */
int
drv_i915_ppgtt_insert(
	struct i915_ppgtt *vm,
	uint64_t va,
	uint64_t physical,
	unsigned pages)
{
	uint64_t *entry;
	uint64_t page_va;
	uint64_t page_physical;
	unsigned index;

	/* Both addresses must be page aligned and inside their respective limits. */
	if ((va % I915_PAGE_BYTES) != 0U ||
	    (physical % I915_PAGE_BYTES) != 0U ||
	    pages == 0U)
		return EINVAL;
	if (va + (uint64_t)pages * I915_PAGE_BYTES > (1ULL << I915_PPGTT_ADDRESS_BITS))
		return EINVAL;
	if (physical + (uint64_t)pages * I915_PAGE_BYTES > I915_DMA_MAX_ADDRESS)
		return EINVAL;

	/* Each page's leaf entry is located, allocating the tables above it on demand. */
	for (index = 0U; index < pages; index++) {
		page_va = va + (uint64_t)index * I915_PAGE_BYTES;
		page_physical = physical + (uint64_t)index * I915_PAGE_BYTES;
		entry = i915_ppgtt_walk(vm, page_va, 1U);
		if (entry == NULL) {
			drv_i915_ppgtt_clear(vm, va, index);
			return ENOMEM;
		}

		*entry = page_physical | I915_PPGTT_PAGE_BITS;
	}

	/* Table stores must be globally visible before a context using them is submitted. */
	kern_io_write_barrier();

	/* Succeeded: the GPU translates the range to the given physical pages. */
	return 0;
}

/*
 * Points a mapped range back at the scratch page; tables stay allocated.
 */
void
drv_i915_ppgtt_clear(
	struct i915_ppgtt *vm,
	uint64_t va,
	unsigned pages)
{
	uint64_t *entry;
	uint64_t page_va;
	unsigned index;

	/* Ranges outside the space are ignored rather than walking garbage. */
	if ((va % I915_PAGE_BYTES) != 0U || va + (uint64_t)pages * I915_PAGE_BYTES > (1ULL << I915_PPGTT_ADDRESS_BITS))
		return;

	/* A leaf that was never allocated already resolves to scratch. */
	for (index = 0U; index < pages; index++) {
		page_va = va + (uint64_t)index * I915_PAGE_BYTES;
		entry = i915_ppgtt_walk(vm, page_va, 0U);
		if (entry != NULL)
			*entry = vm->scratch_entry[0];
	}

	kern_io_write_barrier();
}

/*
 * Reports the leaf entry a virtual address resolves to; scratch when unmapped.
 */
uint64_t
drv_i915_ppgtt_lookup(
	const struct i915_ppgtt *vm,
	uint64_t va)
{
	uint64_t *entry;

	/* The walk does not allocate, so a missing table means the scratch entry. */
	entry = i915_ppgtt_walk((struct i915_ppgtt *)vm, va & ~((uint64_t)I915_PAGE_BYTES - 1ULL), 0U);
	if (entry == NULL)
		return vm->scratch_entry[0];

	return *entry;
}

/* Allocates one table page, records it for release and fills every entry. */
static int
i915_ppgtt_page_alloc(
	struct i915_ppgtt *vm,
	struct kern_pmem *run,
	uint64_t fill)
{
	void *table;
	int error;

	/* Table pages must be reachable by the GPU's address bits. */
	error = kern_pmem_alloc_limited(I915_PAGE_BYTES, I915_PAGE_BYTES, I915_DMA_MAX_ADDRESS, 0U, run);
	if (error != 0)
		return error;

	/* The direct map gives the CPU view of the new page. */
	table = kern_pmem_to_kernel(run->paddr);
	if (table == NULL) {
		(void)kern_pmem_free(run);
		run->size = 0U;
		return EFAULT;
	}

	/* A freshly allocated table names only what the caller asked for. */
	i915_ppgtt_fill(table, fill);
	(void)vm;

	/* Succeeded: the page holds 512 identical entries. */
	return 0;
}

/* Resolves the CPU view of the table an entry points at. */
static uint64_t *
i915_ppgtt_table(
	uint64_t entry)
{
	uint64_t physical;
	void *table;

	/* Entry bits below the page and above the address width are attributes. */
	physical = entry & I915_PPGTT_ADDRESS_MASK;
	table = kern_pmem_to_kernel((hal_physaddr_t)physical);

	return table;
}

/* Walks to the leaf entry of a page, optionally allocating the missing tables. */
static uint64_t *
i915_ppgtt_walk(
	struct i915_ppgtt *vm,
	uint64_t va,
	unsigned allocate)
{
	struct i915_ppgtt_page *page;
	uint64_t *table;
	uint64_t *entry;
	unsigned level;
	int error;

	/*
	 * The walk starts at the top table and descends three times to reach the
	 * leaf table. An entry at a level either names a real child table or holds
	 * that level's scratch entry, which names the scratch table below it.
	 */
	table = kern_pmem_to_kernel(vm->pml4.paddr);
	for (level = I915_PPGTT_LEVELS - 1U; level > 0U; level--) {
		/* A real child is entered directly. */
		entry = &table[I915_PPGTT_INDEX(va, level)];
		if (*entry != vm->scratch_entry[level]) {
			table = i915_ppgtt_table(*entry);
			continue;
		}

		/* A lookup or clear stops at the first scratch table. */
		if (allocate == 0U)
			return NULL;

		/* The new child's entries start as the scratch entry one level down. */
		page = kern_calloc(1U, sizeof(*page));
		if (page == NULL)
			return NULL;
		error = i915_ppgtt_page_alloc(vm, &page->run, vm->scratch_entry[level - 1U]);
		if (error != 0) {
			kern_free(page);
			return NULL;
		}

		/* The page list keeps the table reachable for destroy. */
		page->next = vm->pages;
		vm->pages = page;
		vm->page_count++;

		/* The parent now names the real table with cacheable table bits. */
		*entry = (uint64_t)page->run.paddr | I915_PPGTT_TABLE_BITS;
		table = kern_pmem_to_kernel(page->run.paddr);
	}

	/* The leaf table holds the page entry itself. */
	entry = &table[I915_PPGTT_INDEX(va, 0U)];

	return entry;
}

/* Stores one value into all 512 entries of a table page. */
static void
i915_ppgtt_fill(
	void *table,
	uint64_t value)
{
	uint64_t *entries;
	unsigned index;

	/* The direct-map store is coherent with the GPU's later table walk. */
	entries = table;
	for (index = 0U; index < I915_PPGTT_ENTRIES; index++)
		entries[index] = value;
}
