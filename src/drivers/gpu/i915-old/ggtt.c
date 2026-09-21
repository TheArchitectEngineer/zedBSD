/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The global GTT: probe, scratch fill, page allocation and PTE
 * updates.
 *
 * The page table itself lives in the upper half of BAR0 and is
 * written with 64-bit uncached stores. References: Linux intel_ggtt.c
 * gen8_gmch_probe, gen8_get_total_gtt_size, gen8_ggtt_pte_encode,
 * gen8_ggtt_insert_page, gen8_ggtt_clear_range and
 * gen8_ggtt_invalidate, re-expressed for this driver.
 */

#include "internal.h"

#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/platform.h>

#include <errno.h>
#include <string.h>

#include "bootloader/include/amd64-handoff.h"

static int i915_ggtt_probe(struct i915_device *device);
static int i915_ggtt_scratch_start(struct i915_device *device);
static void i915_ggtt_boot_scanout(struct i915_device *device, unsigned *first_page, unsigned *page_count);
static void i915_ggtt_write_pte(struct i915_device *device, unsigned index, uint64_t pte);
static void i915_ggtt_flush(struct i915_device *device);
static unsigned i915_ggtt_bit_test(const struct i915_ggtt *ggtt, unsigned index);
static void i915_ggtt_bit_set(struct i915_ggtt *ggtt, unsigned index, unsigned value);

/*
 * Sizes the global GTT, maps its page table window and points every entry at scratch.
 */
int
drv_i915_ggtt_start(
	struct i915_device *device)
{
	unsigned scanout_start;
	unsigned scanout_pages;
	unsigned scanout_end;
	unsigned index;
	int error;

	/* The entry count and the mapped table window come from PCI configuration and BAR0. */
	device->stage = "ggtt-probe";
	error = i915_ggtt_probe(device);
	if (error != 0)
		return error;

	/* A zeroed scratch page backs every entry that no allocation owns. */
	device->stage = "ggtt-scratch";
	error = i915_ggtt_scratch_start(device);
	if (error != 0)
		return error;

	/* Allocation state starts empty; reserved pages are marked used below. */
	device->stage = "ggtt-bitmap";
	device->ggtt.bitmap_words = (device->ggtt.entries + 31U) / 32U;
	device->ggtt.bitmap = kern_calloc(device->ggtt.bitmap_words, sizeof(uint32_t));
	if (device->ggtt.bitmap == NULL)
		return ENOMEM;

	/* The lowest pages stay unmapped so a null GPU address never resolves. */
	for (index = 0U; index < I915_GGTT_RESERVED_PAGES; index++)
		i915_ggtt_bit_set(&device->ggtt, index, 1U);

	/*
	 * On a native boot the firmware is still scanning the display out of the
	 * GGTT.  Those entries are held back from the scratch fill and reserved so
	 * the console stays readable and no allocation overwrites the framebuffer.
	 */
	device->stage = "ggtt-scanout";
	i915_ggtt_boot_scanout(device, &scanout_start, &scanout_pages);
	scanout_end = scanout_start + scanout_pages;
	for (index = scanout_start; index < scanout_end; index++)
		i915_ggtt_bit_set(&device->ggtt, index, 1U);
	if (scanout_pages != 0U)
		kern_logf("i915: preserving %u GGTT scanout pages from page %u\n",
			scanout_pages, scanout_start);

	/* Every entry outside the preserved scanout points at scratch before any engine reads the table. */
	device->stage = "ggtt-fill";
	for (index = 0U; index < device->ggtt.entries; index++) {
		/* The firmware framebuffer's own entries are left untouched so scanout survives. */
		if (index >= scanout_start &&
		    index < scanout_end)
			continue;

		i915_ggtt_write_pte(device, index, device->ggtt.scratch_pte);
	}

	/* The flush makes the whole table visible to the GPU translation caches. */
	i915_ggtt_flush(device);

	/* Succeeded: pages can be allocated and mapped for GPU use. */
	return 0;
}

/*
 * Releases the allocator and the scratch page after every mapping retired.
 */
void
drv_i915_ggtt_stop(
	struct i915_device *device)
{
	/* An allocation still live here is a driver bug; it is reported, not hidden. */
	if (device->ggtt.allocated_pages != 0U)
		kern_logf("i915: %u GGTT pages still allocated at stop\n", device->ggtt.allocated_pages);

	/* The bitmap is pure bookkeeping and can go first. */
	if (device->ggtt.bitmap != NULL) {
		kern_free(device->ggtt.bitmap);
		device->ggtt.bitmap = NULL;
	}

	/* The scratch page is released only after no table entry can name it. */
	if (device->ggtt.scratch.size != 0U) {
		(void)kern_pmem_free(&device->ggtt.scratch);
		device->ggtt.scratch.size = 0U;
		device->ggtt.scratch.paddr = 0U;
	}

	device->ggtt.entries = 0U;
}

/*
 * Allocates a contiguous run of GTT pages and reports its byte offset.
 */
int
drv_i915_ggtt_alloc(
	struct i915_device *device,
	unsigned pages,
	uint32_t *offset)
{
	struct i915_ggtt *ggtt;
	unsigned start;
	unsigned index;
	unsigned run;

	/* No caller receives an offset for a zero-length or failed allocation. */
	*offset = 0U;
	ggtt = &device->ggtt;
	if (pages == 0U || pages > ggtt->entries)
		return EINVAL;

	/* First fit: scans for a run of free entries after the reserved pages. */
	start = I915_GGTT_RESERVED_PAGES;
	run = 0U;
	for (index = I915_GGTT_RESERVED_PAGES; index < ggtt->entries; index++) {
		/* A used entry ends the current candidate run. */
		if (i915_ggtt_bit_test(ggtt, index) != 0U) {
			run = 0U;
			start = index + 1U;
			continue;
		}

		/* The run is long enough once it spans the requested page count. */
		run++;
		if (run == pages)
			break;
	}

	/* The table has no free run of the requested length. */
	if (run != pages)
		return ENOSPC;

	/* Marks the run used so a later allocation cannot overlap it. */
	for (index = start; index < start + pages; index++)
		i915_ggtt_bit_set(ggtt, index, 1U);

	/* The page count keeps stop from silently discarding live mappings. */
	ggtt->allocated_pages += pages;
	*offset = start * I915_PAGE_BYTES;

	/* Succeeded: the caller owns the run until it frees it. */
	return 0;
}

/*
 * Returns a run of GTT pages after its entries were pointed back at scratch.
 */
void
drv_i915_ggtt_free(
	struct i915_device *device,
	uint32_t offset,
	unsigned pages)
{
	struct i915_ggtt *ggtt;
	unsigned start;
	unsigned index;

	/* A misaligned or out-of-range release names no allocation. */
	ggtt = &device->ggtt;
	start = offset / I915_PAGE_BYTES;
	if ((offset % I915_PAGE_BYTES) != 0U ||
	    pages == 0U ||
	    start + pages > ggtt->entries) {
		kern_logf("i915: bad GGTT free offset=0x%x pages=%u\n", offset, pages);
		return;
	}

	/* Scratch entries replace the mapping so the GPU cannot reuse it. */
	drv_i915_ggtt_clear(device, offset, pages);

	/* Marks the run free for the next allocation. */
	for (index = start; index < start + pages; index++)
		i915_ggtt_bit_set(ggtt, index, 0U);

	ggtt->allocated_pages -= pages;
}

/*
 * Maps physically contiguous pages at a GTT offset and flushes the table.
 */
int
drv_i915_ggtt_insert(
	struct i915_device *device,
	uint32_t offset,
	uint64_t physical,
	unsigned pages)
{
	unsigned start;
	unsigned index;
	uint64_t pte;

	/* The run must lie inside the table and start on a page boundary. */
	start = offset / I915_PAGE_BYTES;
	if ((offset % I915_PAGE_BYTES) != 0U ||
	    pages == 0U ||
	    start + pages > device->ggtt.entries)
		return EINVAL;

	/* Physical pages must be page aligned and within the address bits the PTE holds. */
	if ((physical % I915_PAGE_BYTES) != 0U ||
	    physical + (uint64_t)pages * I915_PAGE_BYTES > I915_DMA_MAX_ADDRESS)
		return EINVAL;

	/* Each entry names one page; the present bit is the only attribute the GGTT needs. */
	for (index = 0U; index < pages; index++) {
		pte = (physical + (uint64_t)index * I915_PAGE_BYTES) | GEN8_PAGE_PRESENT;
		i915_ggtt_write_pte(device, start + index, pte);
	}

	/* The flush invalidates translations the GPU cached for these entries. */
	i915_ggtt_flush(device);

	/* Succeeded: the GPU can address the pages at this GTT offset. */
	return 0;
}

/*
 * Points a run of entries back at the scratch page and flushes the table.
 */
void
drv_i915_ggtt_clear(
	struct i915_device *device,
	uint32_t offset,
	unsigned pages)
{
	unsigned start;
	unsigned index;

	/* A run outside the table is ignored rather than corrupting neighbours. */
	start = offset / I915_PAGE_BYTES;
	if ((offset % I915_PAGE_BYTES) != 0U ||
	    start + pages > device->ggtt.entries)
		return;

	/* Scratch entries keep a stale GPU access harmless. */
	for (index = 0U; index < pages; index++)
		i915_ggtt_write_pte(device, start + index, device->ggtt.scratch_pte);

	/* The flush removes cached translations of the old pages. */
	i915_ggtt_flush(device);
}

/* Reads the GTT size from configuration space and maps the table window. */
static int
i915_ggtt_probe(
	struct i915_device *device)
{
	struct drv_pci_bar bar;
	uint16_t control;
	unsigned ggms;
	uint64_t table_bytes;
	uint64_t window_bytes;
	int error;

	/* The graphics memory size field encodes the table size as a power of two in MiB. */
	error = drv_pci_device_config_read16(device->pci, SNB_GMCH_CTRL, &control);
	if (error != 0)
		return error;

	/* A zero field means firmware left the GTT disabled; the driver cannot proceed. */
	ggms = ((unsigned)control >> BDW_GMCH_GGMS_SHIFT) & BDW_GMCH_GGMS_MASK;
	if (ggms == 0U)
		return ENODEV;

	/* The encoded value selects the table size: 1 << ggms MiB of 8-byte entries. */
	table_bytes = (uint64_t)(1U << ggms) << 20;

	/* BAR0 holds registers in its lower half and the table in its upper half. */
	error = drv_pci_device_bar(device->pci, GEN4_GTTMMADR_BAR, &bar);
	if (error != 0)
		return error;

	/* A BAR too small to hold both halves cannot belong to a supported device. */
	if (bar.size < 2U * I915_PAGE_BYTES || (bar.size & (bar.size - 1U)) != 0U)
		return ENODEV;

	/* The table never extends past its window; a larger field is clamped and reported. */
	window_bytes = bar.size / 2U;
	if (table_bytes > window_bytes) {
		kern_logf("i915: GGTT size %llu MiB exceeds BAR0 window, clamping\n",
			(unsigned long long)(table_bytes >> 20));
		table_bytes = window_bytes;
	}

	/* Maps only the table bytes the entries actually use. */
	error = drv_pci_device_map_bar_region(
		device->pci,
		GEN4_GTTMMADR_BAR,
		window_bytes,
		(size_t)table_bytes,
		DRV_PCI_MAP_READ | DRV_PCI_MAP_WRITE | DRV_PCI_MAP_NOCACHE,
		&device->gtt);
	if (error != 0)
		return error;

	/* Each entry is one 64-bit page table entry covering one page. */
	device->ggtt.entries = (unsigned)(table_bytes / 8U);
	kern_logf("i915: GGTT %u entries (%llu MiB of GPU address space)\n",
		device->ggtt.entries,
		(unsigned long long)((uint64_t)device->ggtt.entries * I915_PAGE_BYTES >> 20));

	/* Succeeded: the table window is addressable and its extent is known. */
	return 0;
}

/* Allocates the zeroed scratch page and encodes its entry. */
static int
i915_ggtt_scratch_start(
	struct i915_device *device)
{
	void *page;
	int error;

	/* The page must be reachable by the GPU's address bits. */
	error = kern_pmem_alloc_limited(I915_PAGE_BYTES, I915_PAGE_BYTES, I915_DMA_MAX_ADDRESS, 0U, &device->ggtt.scratch);
	if (error != 0)
		return error;

	/* Managed RAM is direct-mapped, so the page can be cleared through its kernel address. */
	page = kern_pmem_to_kernel(device->ggtt.scratch.paddr);
	if (page == NULL) {
		(void)kern_pmem_free(&device->ggtt.scratch);
		device->ggtt.scratch.size = 0U;
		return EFAULT;
	}

	/* Reads of an unmapped GPU address return zeros instead of stale memory. */
	memset(page, 0, I915_PAGE_BYTES);
	device->ggtt.scratch_pte = (uint64_t)device->ggtt.scratch.paddr | GEN8_PAGE_PRESENT;

	/* Succeeded: every cleared entry can point at this page. */
	return 0;
}

/* Reports the GGTT page range the boot firmware is scanning the display out of. */
static void
i915_ggtt_boot_scanout(
	struct i915_device *device,
	unsigned *first_page,
	unsigned *page_count)
{
	const struct zbl6_framebuffer *framebuffer;
	struct drv_pci_bar aperture;
	uint64_t offset;
	uint64_t last;
	unsigned start;
	unsigned pages;
	int error;

	/* Nothing is preserved unless every check below identifies a mapped framebuffer. */
	*first_page = 0U;
	*page_count = 0U;

	/* The boot handoff names the framebuffer the firmware left the display scanning. */
	framebuffer = kern_boot_handoff("pcat.framebuffer");
	if (framebuffer == NULL || framebuffer->size == 0U)
		return;

	/* The display reads the framebuffer through the GMADR aperture that BAR2 exposes. */
	error = drv_pci_device_bar(device->pci, GEN4_GMADR_BAR, &aperture);
	if (error != 0)
		return;

	/* A framebuffer below the aperture is not addressed through the GGTT at all. */
	if (framebuffer->physical_base < aperture.bus_address) {
		kern_logf("i915: boot framebuffer 0x%llx below GMADR 0x%llx; scanout not preserved\n",
			(unsigned long long)framebuffer->physical_base,
			(unsigned long long)aperture.bus_address);
		return;
	}

	/* A framebuffer past the aperture end likewise has no GGTT entries to keep. */
	offset = framebuffer->physical_base - aperture.bus_address;
	if (offset >= aperture.size) {
		kern_logf("i915: boot framebuffer offset 0x%llx past GMADR size 0x%llx; scanout not preserved\n",
			(unsigned long long)offset,
			(unsigned long long)aperture.size);
		return;
	}

	/* The aperture offset equals the GGTT offset, so it selects the entries to keep. */
	last = offset + framebuffer->size - 1U;
	if (last >= aperture.size)
		last = aperture.size - 1U;
	start = (unsigned)(offset / I915_PAGE_BYTES);
	pages = (unsigned)(last / I915_PAGE_BYTES) - start + 1U;

	/* A firmware value is clamped to the table so the range can never walk past it. */
	if (start >= device->ggtt.entries)
		return;
	if (start + pages > device->ggtt.entries)
		pages = device->ggtt.entries - start;

	/* Succeeded: the caller keeps and reserves this range during the scratch fill. */
	*first_page = start;
	*page_count = pages;
}

/* Stores one entry into the mapped page table window. */
static void
i915_ggtt_write_pte(
	struct i915_device *device,
	unsigned index,
	uint64_t pte)
{
	volatile uint8_t *table;

	/* Entries are stored as little-endian 64-bit words in BAR0's upper half. */
	table = device->gtt.address;
	kern_mmio_write64(table + (size_t)index * 8U, pte);
}

/* Makes table updates visible to the GPU by writing the flush control register. */
static void
i915_ggtt_flush(
	struct i915_device *device)
{
	/* Posted table writes must land before the flush is requested. */
	kern_io_write_barrier();

	/* The enable bit both flushes write buffers and invalidates cached translations. */
	drv_i915_write32(device, GFX_FLSH_CNTL_GEN6, GFX_FLSH_CNTL_EN);

	/* Reading the register back completes the posted flush. */
	(void)drv_i915_read32(device, GFX_FLSH_CNTL_GEN6);
}

/* Reports whether one page index is allocated. */
static unsigned
i915_ggtt_bit_test(
	const struct i915_ggtt *ggtt,
	unsigned index)
{
	uint32_t word;

	/* Thirty-two page states share each bitmap word. */
	word = ggtt->bitmap[index / 32U];
	if ((word & (1U << (index % 32U))) != 0U)
		return 1U;

	return 0U;
}

/* Marks one page index allocated or free. */
static void
i915_ggtt_bit_set(
	struct i915_ggtt *ggtt,
	unsigned index,
	unsigned value)
{
	/* A set bit means the page belongs to a live allocation or the reserved range. */
	if (value != 0U) {
		ggtt->bitmap[index / 32U] |= 1U << (index % 32U);
	} else {
		ggtt->bitmap[index / 32U] &= ~(1U << (index % 32U));
	}
}
