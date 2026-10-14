/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU's page tables (Turing, version 2), and where they go in the VRAM.
 *
 * A GPU virtual address is translated through five levels: a root of 4
 * entries (bits 48:47), two directories of 512 entries (bits 46:38 and
 * 37:29), a directory of 256 double entries (bits 28:21) whose upper half
 * points at the 4 KiB-page table, and that table of 512 entries (bits
 * 20:12).  The driver's address space is small: one table covers 2 MiB, so
 * the five pages are built in memory here and copied into the VRAM by the
 * caller.  The instance block of a channel names the root; this file writes
 * that part of it too.  Nothing here touches the hardware.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

/* The directory entries: valid targets (in bits 2:1), the volatile bit, and the address shift. */
#define MMU_PDE_VRAM			(1ULL << 1)
#define MMU_PDE_SYSTEM			(2ULL << 1)
#define MMU_PDE_VOLATILE		(1ULL << 3)

/* The page entries: valid, the target (in bits 2:1, VRAM 0, coherent system 2), volatile, kind 0 (pitch). */
#define MMU_PTE_VALID			(1ULL << 0)
#define MMU_PTE_SYSTEM			(2ULL << 1)
#define MMU_PTE_VOLATILE		(1ULL << 3)

/* A page only privileged work (the context switch) may use, and one it may only read. */
#define MMU_PTE_PRIVILEGED		(1ULL << 5)
#define MMU_PTE_READ_ONLY		(1ULL << 6)

/* The graphics context's tables: the four directories, then one page table per 2 MiB of range. */
#define MMU_GR_TABLES_FIRST_AT		0x4000U
#define MMU_GR_DIRECTORY1_SPAN		0x20000000ULL

/* The address field of every entry: the address shifted right by 4 (page number at bit 8). */
#define MMU_ADDRESS_SHIFT		4U

/* The five pages of the tables, in the order they lie in the block. */
#define MMU_ROOT_AT			0x0000U
#define MMU_DIR2_AT			0x1000U
#define MMU_DIR1_AT			0x2000U
#define MMU_DIR0_AT			0x3000U
#define MMU_TABLE_AT			0x4000U

/* The instance block's fields for the page tables. */
#define MMU_INST_ROOT_AT		0x200U
#define MMU_INST_LIMIT_AT		0x208U
#define MMU_INST_SUBCONTEXT_CLEAR_AT	0x21cU
#define MMU_INST_SUBCONTEXT_MASK_AT	0x298U
#define MMU_INST_SUBCONTEXTS_AT		0x2a0U
#define MMU_INST_SUBCONTEXT_COUNT	64U
#define MMU_INST_ROOT_VERSION2		(1ULL << 10)
#define MMU_INST_ROOT_BIG_64K		(1ULL << 11)
#define MMU_INST_INVALID		0x1U

/* The FB region table in the static information: count, then 16 regions of 48 bytes. */
#define MMU_REGION_COUNT_AT		(NVRTX_STATIC_REGIONS_AT + 0U)
#define MMU_REGION_FIRST_AT		(NVRTX_STATIC_REGIONS_AT + 8U)
#define MMU_REGION_BYTES		48U
#define MMU_REGION_MAX			16U
#define MMU_REGION_BASE_AT		0U
#define MMU_REGION_LIMIT_AT		8U
#define MMU_REGION_RESERVED_AT		16U
#define MMU_REGION_COMPRESSED_AT	28U
#define MMU_REGION_ISO_AT		29U
#define MMU_REGION_PROTECTED_AT		30U

/* The block of VRAM the driver takes is aligned so one window position covers it. */
#define MMU_BLOCK_ALIGN			0x100000ULL

static void mmu_clear(uint8_t *bytes, size_t length);
static int mmu_region_usable(const uint8_t *static_info, size_t at);
static int mmu_gr_range_valid(uint64_t va_base, uint64_t va_bytes);

/*
 * Builds the five pages of page tables for one 2 MiB range of addresses.
 *
 * The tables lie in the VRAM at block (the root first, then each lower
 * level a page apart); the image is NVRTX_MMU_TABLES_BYTES long.  Each page
 * of the range at va gets the system-memory page of the same index, up to
 * the count given.  Returns EINVAL for a range that is not 2 MiB aligned or
 * holds more than 512 pages.
 */
int
nvrtx_mmu_tables(
	uint8_t *image,
	uint64_t block,
	uint64_t va,
	const uint64_t *pages,
	size_t count)
{
	uint64_t entry;
	size_t index;

	/* The range must be one table's worth, on its boundary. */
	if ((va & (NVRTX_MMU_RANGE_BYTES - 1U)) != 0U)
		return EINVAL;
	if (count > NVRTX_MMU_RANGE_BYTES / NVRTX_GSP_PAGE_SIZE)
		return EINVAL;

	/* Starts with every entry invalid. */
	mmu_clear(image, NVRTX_MMU_TABLES_BYTES);

	/* The root's entry for the range points at the second directory, in the VRAM. */
	entry = ((block + MMU_DIR2_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_ROOT_AT + ((va >> 47) & 0x3U) * 8U, entry);

	/* The second directory points at the first. */
	entry = ((block + MMU_DIR1_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_DIR2_AT + ((va >> 38) & 0x1ffU) * 8U, entry);

	/* The first points at the double-entry directory. */
	entry = ((block + MMU_DIR0_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_DIR1_AT + ((va >> 29) & 0x1ffU) * 8U, entry);

	/* The double entry's upper half points at the 4 KiB-page table; its lower half (64 KiB pages) stays invalid. */
	entry = ((block + MMU_TABLE_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_DIR0_AT + ((va >> 21) & 0xffU) * 16U + 8U, entry);

	/* Each page entry maps one coherent system-memory page. */
	for (index = 0U; index < count; index++) {
		entry = (pages[index] >> MMU_ADDRESS_SHIFT) | MMU_PTE_VALID | MMU_PTE_SYSTEM | MMU_PTE_VOLATILE;
		nvrtx_put64(image, MMU_TABLE_AT + index * 8U, entry);
	}

	/* Succeeded: the tables map the range. */
	return 0;
}

/*
 * Writes a channel's instance block fields that name its page tables.
 *
 * The root is in the VRAM; the tables use version 2 with 64 KiB big pages.
 * Subcontext 0 uses the same root and the other 63 are invalid.  The image
 * is the whole 4 KiB instance block.
 */
void
nvrtx_mmu_instance(
	uint8_t *instance,
	uint64_t root,
	uint64_t limit)
{
	uint64_t pointer;
	unsigned index;
	size_t at;

	/* Names the root, its format and the address limit. */
	pointer = root | MMU_INST_ROOT_VERSION2 | MMU_INST_ROOT_BIG_64K;
	nvrtx_put64(instance, MMU_INST_ROOT_AT, pointer);
	nvrtx_put64(instance, MMU_INST_LIMIT_AT, limit - 1U);

	/* Gives subcontext 0 the same root and leaves the others invalid. */
	nvrtx_put32(instance, MMU_INST_SUBCONTEXT_CLEAR_AT, 0U);
	for (index = 0U; index < MMU_INST_SUBCONTEXT_COUNT; index++) {
		at = MMU_INST_SUBCONTEXTS_AT + (size_t)index * 16U;
		if (index == 0U) {
			nvrtx_put32(instance, at + 0U, (uint32_t)(pointer & 0xffffffffU));
			nvrtx_put32(instance, at + 4U, (uint32_t)(pointer >> 32));
		} else {
			nvrtx_put32(instance, at + 0U, MMU_INST_INVALID);
			nvrtx_put32(instance, at + 4U, MMU_INST_INVALID);
		}
		nvrtx_put32(instance, at + 8U, 0U);
	}

	/* Only subcontext 0 is in use. */
	nvrtx_put32(instance, MMU_INST_SUBCONTEXT_MASK_AT, 1U);
	nvrtx_put32(instance, MMU_INST_SUBCONTEXT_MASK_AT + 4U, 0U);
}

/*
 * Chooses the VRAM block the driver uses, from the static information's FB regions.
 *
 * A region is usable when it is neither reserved nor protected and supports
 * both compression and ISO.  The block is taken at the top of the last usable
 * region, aligned down to 1 MiB.  Returns ENOSPC when no usable region holds it.
 */
int
nvrtx_mmu_vram_block(
	const uint8_t *static_info,
	uint64_t size,
	uint64_t *block,
	uint32_t *region)
{
	uint32_t count;
	uint32_t index;
	size_t at;
	uint64_t base;
	uint64_t top;
	uint64_t candidate;
	int usable;
	int found;

	/* Walks the regions; the last usable one that holds the block wins. */
	count = nvrtx_get32(static_info, MMU_REGION_COUNT_AT);
	if (count > MMU_REGION_MAX)
		count = MMU_REGION_MAX;
	found = 0;
	for (index = 0U; index < count; index++) {
		at = MMU_REGION_FIRST_AT + (size_t)index * MMU_REGION_BYTES;

		/* Reserved, protected, or without compression and ISO: not for the driver. */
		usable = mmu_region_usable(static_info, at);
		if (!usable)
			continue;

		/* Takes the block from the region's top, if the region holds it. */
		base = nvrtx_get64(static_info, at + MMU_REGION_BASE_AT);
		top = nvrtx_get64(static_info, at + MMU_REGION_LIMIT_AT) + 1U;
		if (top < size || top - size < base)
			continue;
		candidate = (top - size) & ~(MMU_BLOCK_ALIGN - 1U);
		if (candidate < base)
			continue;

		*block = candidate;
		*region = index;
		found = 1;
	}

	/* No usable region holds the block. */
	if (found == 0)
		return ENOSPC;

	/* Succeeded: the block is chosen. */
	return 0;
}

/*
 * Finds the last usable VRAM region of the static information's FB regions.
 *
 * Usable is what nvrtx_mmu_vram_block() takes its blocks from: neither
 * reserved nor protected, with compression and ISO.  The region is
 * [*base, *top).  Returns ENOSPC when no region is usable.
 */
int
nvrtx_mmu_vram_region(
	const uint8_t *static_info,
	uint64_t *base,
	uint64_t *top,
	uint32_t *region)
{
	uint32_t count;
	uint32_t index;
	size_t at;
	int usable;
	int found;

	/* Walks the regions; the last usable one wins. */
	count = nvrtx_get32(static_info, MMU_REGION_COUNT_AT);
	if (count > MMU_REGION_MAX)
		count = MMU_REGION_MAX;
	found = 0;
	for (index = 0U; index < count; index++) {
		/* Skips a region the driver may not use. */
		at = MMU_REGION_FIRST_AT + (size_t)index * MMU_REGION_BYTES;
		usable = mmu_region_usable(static_info, at);
		if (!usable)
			continue;

		/* Records the region's bounds. */
		*base = nvrtx_get64(static_info, at + MMU_REGION_BASE_AT);
		*top = nvrtx_get64(static_info, at + MMU_REGION_LIMIT_AT) + 1U;
		*region = index;
		found = 1;
	}

	/* No region is usable. */
	if (found == 0)
		return ENOSPC;

	/* Succeeded: the region's bounds are in *base and *top. */
	return 0;
}

/*
 * Encodes the page entry that maps one 4 KiB page of the VRAM for work that
 * is not privileged: valid, the VRAM target, not volatile, kind 0 (pitch).
 */
uint64_t
nvrtx_mmu_vram_pte(
	uint64_t physical)
{
	uint64_t entry;

	/* The page's address in the entry's address field, and the valid bit. */
	entry = (physical >> MMU_ADDRESS_SHIFT) | MMU_PTE_VALID;

	/* Succeeded: the entry. */
	return entry;
}

/*
 * Reports how many bytes the graphics context's tables take for a range.
 */
size_t
nvrtx_mmu_gr_tables_bytes(
	uint64_t va_bytes)
{
	size_t tables;

	/* One page table per 2 MiB, after the four directories. */
	tables = (size_t)(va_bytes / NVRTX_MMU_RANGE_BYTES);

	/* Reports the length of the image. */
	return MMU_GR_TABLES_FIRST_AT + tables * NVRTX_GSP_PAGE_SIZE;
}

/*
 * Builds the directories of an address space the graphics context uses.
 *
 * The range at va_base of va_bytes lies under one entry of the first-level
 * directory (512 MiB); each 2 MiB of it has its own 4 KiB-page table, in
 * order after the four directories.  The tables lie in the VRAM at block.
 * The page entries start invalid; nvrtx_mmu_gr_map() fills them.  The root
 * and the two upper directories are along the path of index 0, the one the
 * resource manager shares for its own range at 4 GiB, which it writes into
 * the same first-level directory.  Returns EINVAL for a range the tables
 * cannot hold (see mmu_gr_range_valid()).
 */
int
nvrtx_mmu_gr_tables(
	uint8_t *image,
	uint64_t block,
	uint64_t va_base,
	uint64_t va_bytes)
{
	uint64_t entry;
	uint64_t tables;
	uint64_t index;
	size_t at;
	int valid;

	/* The range must suit the tables. */
	valid = mmu_gr_range_valid(va_base, va_bytes);
	if (!valid)
		return EINVAL;

	/* Starts with every entry invalid. */
	mmu_clear(image, nvrtx_mmu_gr_tables_bytes(va_bytes));

	/* The root points at the second directory, which points at the first. */
	entry = ((block + MMU_DIR2_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_ROOT_AT + ((va_base >> 47) & 0x3U) * 8U, entry);
	entry = ((block + MMU_DIR1_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_DIR2_AT + ((va_base >> 38) & 0x1ffU) * 8U, entry);

	/* The first directory points at the double-entry directory of the range. */
	entry = ((block + MMU_DIR0_AT) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
	nvrtx_put64(image, MMU_DIR1_AT + ((va_base >> 29) & 0x1ffU) * 8U, entry);

	/* Each double entry's upper half points at its 2 MiB's page table. */
	tables = va_bytes / NVRTX_MMU_RANGE_BYTES;
	for (index = 0U; index < tables; index++) {
		at = MMU_DIR0_AT + (size_t)(((va_base >> 21) & 0xffU) + index) * 16U + 8U;
		entry = ((block + MMU_GR_TABLES_FIRST_AT + index * NVRTX_GSP_PAGE_SIZE) >> MMU_ADDRESS_SHIFT) | MMU_PDE_VRAM;
		nvrtx_put64(image, at, entry);
	}

	/* Succeeded: the directories cover the range. */
	return 0;
}

/*
 * Maps a run of VRAM into the graphics context's tables, privileged.
 *
 * Every page of the run gets a privileged, non-volatile VRAM entry, read-only
 * when asked.  Returns EINVAL for a range the tables cannot hold or a run
 * outside it, or one not on page boundaries.
 */
int
nvrtx_mmu_gr_map(
	uint8_t *image,
	uint64_t va_base,
	uint64_t va_bytes,
	uint64_t va,
	uint64_t physical,
	uint64_t size,
	int read_only)
{
	uint64_t flags;
	uint64_t entry;
	uint64_t page;
	uint64_t pages;
	size_t at;
	int valid;

	/* The tables must be ones nvrtx_mmu_gr_tables() can build. */
	valid = mmu_gr_range_valid(va_base, va_bytes);
	if (!valid)
		return EINVAL;

	/* The run must be whole pages inside the range. */
	if ((va & 0xfffU) != 0U || (physical & 0xfffU) != 0U || (size & 0xfffU) != 0U)
		return EINVAL;
	if (va < va_base || size > va_bytes || va - va_base > va_bytes - size)
		return EINVAL;

	/* A privileged VRAM page, read-only when asked. */
	flags = MMU_PTE_VALID | MMU_PTE_PRIVILEGED;
	if (read_only)
		flags |= MMU_PTE_READ_ONLY;

	/* Writes each page's entry in its table. */
	pages = size / NVRTX_GSP_PAGE_SIZE;
	for (page = 0U; page < pages; page++) {
		at = MMU_GR_TABLES_FIRST_AT + (size_t)((va - va_base) / NVRTX_GSP_PAGE_SIZE + page) * 8U;
		entry = ((physical + page * NVRTX_GSP_PAGE_SIZE) >> MMU_ADDRESS_SHIFT) | flags;
		nvrtx_put64(image, at, entry);
	}

	/* Succeeded: the run is mapped. */
	return 0;
}

/*
 * Maps system-memory pages into the graphics context's tables, for work.
 *
 * Each page at va onward gets a coherent, volatile system-memory entry
 * that is not privileged (the compute launch's ring, push buffer, program
 * and data, ws037-p008).  Returns EINVAL for a range the tables cannot hold
 * or pages outside it.
 */
int
nvrtx_mmu_gr_map_system(
	uint8_t *image,
	uint64_t va_base,
	uint64_t va_bytes,
	uint64_t va,
	const uint64_t *pages,
	size_t count)
{
	uint64_t entry;
	uint64_t size;
	size_t index;
	size_t at;
	int valid;

	/* The tables must be ones nvrtx_mmu_gr_tables() can build. */
	valid = mmu_gr_range_valid(va_base, va_bytes);
	if (!valid)
		return EINVAL;

	/* The pages must lie inside the range, from a page boundary. */
	size = (uint64_t)count * NVRTX_GSP_PAGE_SIZE;
	if ((va & 0xfffU) != 0U)
		return EINVAL;
	if (va < va_base || size > va_bytes || va - va_base > va_bytes - size)
		return EINVAL;

	/* Writes each page's entry in its table. */
	for (index = 0U; index < count; index++) {
		at = MMU_GR_TABLES_FIRST_AT + (size_t)((va - va_base) / NVRTX_GSP_PAGE_SIZE + index) * 8U;
		entry = (pages[index] >> MMU_ADDRESS_SHIFT) | MMU_PTE_VALID | MMU_PTE_SYSTEM | MMU_PTE_VOLATILE;
		nvrtx_put64(image, at, entry);
	}

	/* Succeeded: the pages are mapped. */
	return 0;
}

/* Clears a range of bytes. */
static void
mmu_clear(
	uint8_t *bytes,
	size_t length)
{
	size_t at;

	/* Zeroes each byte. */
	for (at = 0U; at < length; at++)
		bytes[at] = 0U;
}

/*
 * Reports whether a range suits the graphics context's tables.
 *
 * It starts on a 2 MiB boundary, is a whole number of 2 MiB, and lies under
 * one first-level entry below the resource manager's range at 4 GiB.
 */
static int
mmu_gr_range_valid(
	uint64_t va_base,
	uint64_t va_bytes)
{
	/* Whole page tables. */
	if ((va_base & (NVRTX_MMU_RANGE_BYTES - 1U)) != 0U)
		return 0;
	if (va_bytes == 0U || (va_bytes & (NVRTX_MMU_RANGE_BYTES - 1U)) != 0U)
		return 0;

	/* One first-level entry: the double-entry directory covers 512 MiB. */
	if ((va_base & (MMU_GR_DIRECTORY1_SPAN - 1U)) + va_bytes > MMU_GR_DIRECTORY1_SPAN)
		return 0;

	/* Below the resource manager's range, along its path. */
	if (va_base + va_bytes > NVRTX_SERVER_VA)
		return 0;

	/* The range suits the tables. */
	return 1;
}

/* Reports whether the FB region at an offset of the static information is one the driver may use. */
static int
mmu_region_usable(
	const uint8_t *static_info,
	size_t at)
{
	/* A reserved region is the firmware's. */
	if (nvrtx_get64(static_info, at + MMU_REGION_RESERVED_AT) != 0U)
		return 0;

	/* A protected region is not readable by the driver. */
	if (static_info[at + MMU_REGION_PROTECTED_AT] != 0U)
		return 0;

	/* The ordinary VRAM supports both compression and ISO (the display's reads). */
	if (static_info[at + MMU_REGION_COMPRESSED_AT] == 0U)
		return 0;
	if (static_info[at + MMU_REGION_ISO_AT] == 0U)
		return 0;

	/* The region is usable. */
	return 1;
}
