/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises GGTT probing, scratch fill, the page allocator and PTE updates
 * against the synthetic BAR0 and the fake physical page pool.
 */

#include "i915-fixture.inc"
#include "../../../src/drivers/gpu/i915/uncore.c"
#include "../../../src/drivers/gpu/i915/ggtt.c"
#include "../../../src/drivers/gpu/i915/ppgtt.c"

static uint64_t entry(unsigned index);
static uint64_t *table(hal_physaddr_t physical);
static void test_start(void);
static void test_scanout_preserved(void);
static void test_allocator(void);
static void test_insert_clear(void);
static void test_probe_failures(void);
static void test_ppgtt_create(void);
static void test_ppgtt_walk(void);
static void test_ppgtt_va(void);

int
main(void)
{
	test_start();
	test_scanout_preserved();
	test_allocator();
	test_insert_clear();
	test_probe_failures();
	test_ppgtt_create();
	test_ppgtt_walk();
	test_ppgtt_va();
	printf("i915 gtt host test PASS\n");
	return 0;
}

/* Views one table page through the fake direct map. */
static uint64_t *
table(
	hal_physaddr_t physical)
{
	uint64_t *page;

	page = kern_pmem_to_kernel(physical);
	assert(page != NULL);
	return page;
}

/* A new space is a scratch chain: every level names the level below. */
static void
test_ppgtt_create(void)
{
	struct i915_ppgtt vm;
	uint64_t *page;
	unsigned index;
	unsigned live;
	int error;

	fixture_reset();
	memset(&vm, 0, sizeof(vm));
	live = fixture_pool_live;
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	assert(vm.created == 1U);
	assert(fixture_pool_live == live + 5U);

	/* The data page is zero and its entry is a writable present PTE with PAT index 0. */
	page = table(vm.scratch[0].paddr);
	for (index = 0U; index < I915_PPGTT_ENTRIES; index++)
		assert(page[index] == 0U);
	assert(vm.scratch_entry[0] == ((uint64_t)vm.scratch[0].paddr | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));

	/* Each scratch table is full of the entry below it and is named uncached (PAT index 3). */
	for (index = 1U; index < I915_PPGTT_LEVELS; index++) {
		page = table(vm.scratch[index].paddr);
		assert(page[0] == vm.scratch_entry[index - 1U]);
		assert(page[I915_PPGTT_ENTRIES - 1U] == vm.scratch_entry[index - 1U]);
		assert(vm.scratch_entry[index] == ((uint64_t)vm.scratch[index].paddr | GEN8_PAGE_PRESENT | GEN8_PAGE_RW | GEN12_PPGTT_PTE_PAT0 | GEN12_PPGTT_PTE_PAT1));
	}

	/* The top table names the scratch directory pointer everywhere. */
	page = table(vm.pml4.paddr);
	assert(page[0] == vm.scratch_entry[3]);
	assert(page[511] == vm.scratch_entry[3]);
	assert(drv_i915_ppgtt_lookup(&vm, 0U) == vm.scratch_entry[0]);
	assert(drv_i915_ppgtt_lookup(&vm, (1ULL << 47)) == vm.scratch_entry[0]);

	/* A second creation is refused; destroy returns every page. */
	error = drv_i915_ppgtt_create(&vm);
	assert(error == EBUSY);
	drv_i915_ppgtt_destroy(&vm);
	assert(vm.created == 0U);
	assert(fixture_pool_live == live);
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
	uint64_t va;
	unsigned live;
	int error;

	fixture_reset();
	memset(&vm, 0, sizeof(vm));
	memset(&other, 0, sizeof(other));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);
	error = drv_i915_ppgtt_create(&other);
	assert(error == 0);
	live = fixture_pool_live;

	/* An address with all four indices distinct exercises every shift. */
	va = ((uint64_t)3U << 39) | ((uint64_t)17U << 30) | ((uint64_t)200U << 21) | ((uint64_t)511U << 12);
	error = drv_i915_ppgtt_insert(&vm, va, FIXTURE_POOL_BASE + 0x40000U, 2U);
	assert(error == 0);

	/* Four tables were allocated: a directory pointer, a directory and two tables. */
	assert(fixture_pool_live == live + 4U);
	assert(vm.page_count == 4U);
	pml4 = table(vm.pml4.paddr);
	assert(pml4[3] != vm.scratch_entry[3]);
	assert((pml4[3] & 0xfffU) == (GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	pdp = table((hal_physaddr_t)(pml4[3] & I915_PPGTT_ADDRESS_MASK));
	assert(pdp[17] != vm.scratch_entry[2]);
	assert(pdp[16] == vm.scratch_entry[2]);
	pd = table((hal_physaddr_t)(pdp[17] & I915_PPGTT_ADDRESS_MASK));
	assert(pd[200] != vm.scratch_entry[1]);
	assert(pd[199] == vm.scratch_entry[1]);
	pt = table((hal_physaddr_t)(pd[200] & I915_PPGTT_ADDRESS_MASK));
	assert(pt[511] == ((FIXTURE_POOL_BASE + 0x40000U) | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	assert(pt[510] == vm.scratch_entry[0]);

	/* The second page crossed into the next table, so a fourth page was allocated. */
	assert(fixture_pool_live == live + 4U);
	assert(pd[201] != vm.scratch_entry[1]);
	pt = table((hal_physaddr_t)(pd[201] & I915_PPGTT_ADDRESS_MASK));
	assert(pt[0] == ((FIXTURE_POOL_BASE + 0x41000U) | GEN8_PAGE_PRESENT | GEN8_PAGE_RW));
	assert(drv_i915_ppgtt_lookup(&vm, va + I915_PAGE_BYTES) == pt[0]);

	/* The other space is untouched by this space's tables. */
	assert(drv_i915_ppgtt_lookup(&other, va) == other.scratch_entry[0]);
	assert(other.page_count == 0U);

	/* Clearing restores scratch leaves but keeps the tables allocated. */
	drv_i915_ppgtt_clear(&vm, va, 2U);
	assert(drv_i915_ppgtt_lookup(&vm, va) == vm.scratch_entry[0]);
	assert(pt[0] == vm.scratch_entry[0]);
	assert(fixture_pool_live == live + 4U);

	/* Misaligned or out-of-range inserts are refused. */
	error = drv_i915_ppgtt_insert(&vm, va + 1U, FIXTURE_POOL_BASE, 1U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, (1ULL << 48) - I915_PAGE_BYTES, FIXTURE_POOL_BASE, 2U);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_insert(&vm, va, I915_DMA_MAX_ADDRESS + 1U, 1U);
	assert(error == EINVAL);

	drv_i915_ppgtt_destroy(&vm);
	drv_i915_ppgtt_destroy(&other);
	assert(fixture_pool_live == 0U);
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

	fixture_reset();
	memset(&vm, 0, sizeof(vm));
	error = drv_i915_ppgtt_create(&vm);
	assert(error == 0);

	error = drv_i915_ppgtt_va_alloc(&vm, 1U, &first);
	assert(error == 0);
	assert(first == I915_PPGTT_VA_START);
	error = drv_i915_ppgtt_va_alloc(&vm, I915_PPGTT_VA_ALIGN + 1U, &second);
	assert(error == 0);
	assert(second == first + I915_PPGTT_VA_ALIGN);
	error = drv_i915_ppgtt_va_alloc(&vm, 4096U, &third);
	assert(error == 0);
	assert(third == second + 2U * I915_PPGTT_VA_ALIGN);

	/* Zero and exhaustion are refused. */
	error = drv_i915_ppgtt_va_alloc(&vm, 0U, &third);
	assert(error == EINVAL);
	error = drv_i915_ppgtt_va_alloc(&vm, 1ULL << 48, &third);
	assert(error == ENOSPC);
	assert(third == 0U);

	drv_i915_ppgtt_destroy(&vm);
}

/* Reads one entry of the synthetic table. */
static uint64_t
entry(
	unsigned index)
{
	return kern_mmio_read64(fixture_gtt + (size_t)index * 8U);
}

/* Start sizes the table from the GMCH field, maps the window and fills scratch. */
static void
test_start(void)
{
	struct i915_device device;
	unsigned index;
	uint8_t *scratch;
	int error;

	fixture_reset();
	fixture_device(&device);

	/* GGMS=3 selects an 8 MiB table of one million entries. */
	error = drv_i915_ggtt_start(&device);
	assert(error == 0);
	assert(device.ggtt.entries == FIXTURE_REGS_BYTES / 8U);
	assert(device.gtt.address == fixture_gtt);
	assert(fixture_maps == 1U);

	/* The scratch page is zero and every entry names it with the present bit. */
	scratch = kern_pmem_to_kernel(device.ggtt.scratch.paddr);
	assert(scratch != NULL);
	for (index = 0U; index < I915_PAGE_BYTES; index++)
		assert(scratch[index] == 0U);
	assert(device.ggtt.scratch_pte == ((uint64_t)device.ggtt.scratch.paddr | GEN8_PAGE_PRESENT));
	for (index = 0U; index < device.ggtt.entries; index += 4093U)
		assert(entry(index) == device.ggtt.scratch_pte);
	assert(entry(device.ggtt.entries - 1U) == device.ggtt.scratch_pte);
	assert(fixture_flushes == 1U);

	/* Stop releases the bitmap and the scratch page. */
	drv_i915_ggtt_stop(&device);
	assert(device.ggtt.bitmap == NULL);
	assert(fixture_pool_live == 0U);
	assert(device.ggtt.entries == 0U);
}

/* The firmware framebuffer's GGTT entries survive the fill and are reserved from allocation. */
static void
test_scanout_preserved(void)
{
	struct i915_device device;
	uint64_t aperture_base;
	uint64_t fb_offset;
	uint64_t fb_size;
	uint64_t sentinel;
	unsigned fb_start;
	unsigned fb_pages;
	unsigned fb_end;
	unsigned index;
	uint32_t run;
	uint32_t after;
	int error;

	fixture_reset();

	/* A 256 MiB aperture at a plausible bus address holds the framebuffer 8 MiB in. */
	aperture_base = 0x4000000000ULL;
	fb_offset = 0x800000ULL;
	fb_size = 0x400000ULL;
	fixture_gmadr_base = aperture_base;
	fixture_gmadr_size = 256ULL * 1024ULL * 1024ULL;
	fixture_framebuffer.physical_base = aperture_base + fb_offset;
	fixture_framebuffer.size = fb_size;
	fixture_framebuffer.width = 1024U;
	fixture_framebuffer.height = 768U;
	fixture_framebuffer.stride = 4096U;
	fixture_framebuffer_present = 1U;

	fixture_device(&device);

	/* The firmware leaves a recognizable present PTE in each framebuffer page entry. */
	fb_start = (unsigned)(fb_offset / I915_PAGE_BYTES);
	fb_pages = (unsigned)(fb_size / I915_PAGE_BYTES);
	fb_end = fb_start + fb_pages;
	sentinel = 0x123456000ULL | GEN8_PAGE_PRESENT;
	for (index = fb_start; index < fb_end; index++)
		kern_mmio_write64(fixture_gtt + (size_t)index * 8U, sentinel);

	error = drv_i915_ggtt_start(&device);
	assert(error == 0);

	/* The framebuffer's own entries are left exactly as the firmware wrote them. */
	assert(entry(fb_start) == sentinel);
	assert(entry(fb_start + fb_pages - 1U) == sentinel);

	/* The entries bracketing the range are scratch, so only the framebuffer was spared. */
	assert(entry(fb_start - 1U) == device.ggtt.scratch_pte);
	assert(entry(fb_end) == device.ggtt.scratch_pte);

	/* Filling the space up to the framebuffer forces the next page past the reserved range. */
	error = drv_i915_ggtt_alloc(&device, fb_start - I915_GGTT_RESERVED_PAGES, &run);
	assert(error == 0);
	assert(run == I915_GGTT_RESERVED_PAGES * I915_PAGE_BYTES);
	error = drv_i915_ggtt_alloc(&device, 1U, &after);
	assert(error == 0);
	assert(after == fb_end * I915_PAGE_BYTES);

	/* The allocations are returned so stop finds no live mapping. */
	drv_i915_ggtt_free(&device, run, fb_start - I915_GGTT_RESERVED_PAGES);
	drv_i915_ggtt_free(&device, after, 1U);
	drv_i915_ggtt_stop(&device);
	assert(device.ggtt.allocated_pages == 0U);
}

/* The allocator skips the reserved pages, packs runs and refuses what does not fit. */
static void
test_allocator(void)
{
	struct i915_device device;
	uint32_t first;
	uint32_t second;
	uint32_t third;
	int error;

	fixture_reset();
	fixture_device(&device);
	error = drv_i915_ggtt_start(&device);
	assert(error == 0);

	/* The first run starts right after the reserved megabyte. */
	error = drv_i915_ggtt_alloc(&device, 4U, &first);
	assert(error == 0);
	assert(first == I915_GGTT_RESERVED_PAGES * I915_PAGE_BYTES);

	/* The next run follows without a gap. */
	error = drv_i915_ggtt_alloc(&device, 16U, &second);
	assert(error == 0);
	assert(second == first + 4U * I915_PAGE_BYTES);
	assert(device.ggtt.allocated_pages == 20U);

	/* Freeing the first run lets a same-sized run reuse it but not a larger one. */
	drv_i915_ggtt_free(&device, first, 4U);
	assert(device.ggtt.allocated_pages == 16U);
	error = drv_i915_ggtt_alloc(&device, 5U, &third);
	assert(error == 0);
	assert(third == second + 16U * I915_PAGE_BYTES);
	error = drv_i915_ggtt_alloc(&device, 4U, &third);
	assert(error == 0);
	assert(third == first);

	/* A run larger than the table and a zero-length run are refused. */
	error = drv_i915_ggtt_alloc(&device, device.ggtt.entries + 1U, &third);
	assert(error == EINVAL);
	error = drv_i915_ggtt_alloc(&device, 0U, &third);
	assert(error == EINVAL);

	/* A run longer than the free space reports exhaustion. */
	error = drv_i915_ggtt_alloc(&device, device.ggtt.entries - I915_GGTT_RESERVED_PAGES, &third);
	assert(error == ENOSPC);

	/* A misaligned free is reported and ignored. */
	drv_i915_ggtt_free(&device, first + 1U, 1U);
	assert(fixture_log_lines >= 1U);
	assert(device.ggtt.allocated_pages == 25U);

	drv_i915_ggtt_free(&device, first, 4U);
	drv_i915_ggtt_free(&device, second, 16U);
	drv_i915_ggtt_free(&device, second + 16U * I915_PAGE_BYTES, 5U);
	assert(device.ggtt.allocated_pages == 0U);
	drv_i915_ggtt_stop(&device);
}

/* Insert writes present entries and a flush; clear restores scratch. */
static void
test_insert_clear(void)
{
	struct i915_device device;
	uint32_t offset;
	unsigned start;
	unsigned flushes;
	int error;

	fixture_reset();
	fixture_device(&device);
	error = drv_i915_ggtt_start(&device);
	assert(error == 0);
	error = drv_i915_ggtt_alloc(&device, 3U, &offset);
	assert(error == 0);
	start = offset / I915_PAGE_BYTES;

	/* Each page maps to its own entry with the present bit set. */
	flushes = fixture_flushes;
	error = drv_i915_ggtt_insert(&device, offset, FIXTURE_POOL_BASE + 0x10000U, 3U);
	assert(error == 0);
	assert(entry(start) == ((FIXTURE_POOL_BASE + 0x10000U) | GEN8_PAGE_PRESENT));
	assert(entry(start + 1U) == ((FIXTURE_POOL_BASE + 0x11000U) | GEN8_PAGE_PRESENT));
	assert(entry(start + 2U) == ((FIXTURE_POOL_BASE + 0x12000U) | GEN8_PAGE_PRESENT));
	assert(entry(start + 3U) == device.ggtt.scratch_pte);
	assert(fixture_flushes == flushes + 1U);

	/* Misaligned, oversized and out-of-range inserts are refused. */
	error = drv_i915_ggtt_insert(&device, offset + 8U, FIXTURE_POOL_BASE, 1U);
	assert(error == EINVAL);
	error = drv_i915_ggtt_insert(&device, offset, FIXTURE_POOL_BASE + 8U, 1U);
	assert(error == EINVAL);
	error = drv_i915_ggtt_insert(&device, offset, I915_DMA_MAX_ADDRESS - 0xfffU, 1U);
	assert(error == EINVAL);
	error = drv_i915_ggtt_insert(&device, (device.ggtt.entries - 1U) * I915_PAGE_BYTES, FIXTURE_POOL_BASE, 2U);
	assert(error == EINVAL);

	/* Clearing points the run back at scratch and flushes again. */
	drv_i915_ggtt_clear(&device, offset, 3U);
	assert(entry(start) == device.ggtt.scratch_pte);
	assert(entry(start + 2U) == device.ggtt.scratch_pte);
	assert(fixture_flushes == flushes + 2U);

	/* Freeing a mapped run clears it before releasing the pages. */
	error = drv_i915_ggtt_insert(&device, offset, FIXTURE_POOL_BASE, 3U);
	assert(error == 0);
	drv_i915_ggtt_free(&device, offset, 3U);
	assert(entry(start + 1U) == device.ggtt.scratch_pte);
	drv_i915_ggtt_stop(&device);
}

/* A disabled GTT field stops attach before any mapping is taken. */
static void
test_probe_failures(void)
{
	struct i915_device device;
	int error;

	fixture_reset();
	fixture_device(&device);
	fixture_gmch_control = 0U;

	/* GGMS=0 means no table; nothing is mapped or allocated. */
	error = drv_i915_ggtt_start(&device);
	assert(error == ENODEV);
	assert(fixture_maps == 0U);
	assert(fixture_pool_live == 0U);
	assert(device.ggtt.entries == 0U);
}
