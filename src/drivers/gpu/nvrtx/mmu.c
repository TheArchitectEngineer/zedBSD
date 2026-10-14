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
	int found;

	/* Walks the regions; the last usable one that holds the block wins. */
	count = nvrtx_get32(static_info, MMU_REGION_COUNT_AT);
	if (count > MMU_REGION_MAX)
		count = MMU_REGION_MAX;
	found = 0;
	for (index = 0U; index < count; index++) {
		at = MMU_REGION_FIRST_AT + (size_t)index * MMU_REGION_BYTES;

		/* Reserved, protected, or without compression and ISO: not for the driver. */
		if (nvrtx_get64(static_info, at + MMU_REGION_RESERVED_AT) != 0U)
			continue;
		if (static_info[at + MMU_REGION_PROTECTED_AT] != 0U)
			continue;
		if (static_info[at + MMU_REGION_COMPRESSED_AT] == 0U || static_info[at + MMU_REGION_ISO_AT] == 0U)
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
