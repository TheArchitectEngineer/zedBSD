/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the riscv64 loader's list of the RAM the firmware keeps
 * (bootloader/uefi/riscv64-reserved.c) on fake memory maps: the types
 * kept, descriptors farther apart than their size, joining, the overflow
 * flag, and maps that cannot be read.
 *
 *   reserved-host-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootloader/uefi/riscv64-reserved.h"

/* The descriptor stride of the fake maps, larger than the descriptor as firmware may make it. */
#define TEST_STRIDE	48U

/* The most descriptors one fake map holds. */
#define TEST_MAX_DESCRIPTORS	40U

/* How many checks failed. */
static int failures;

/* The fake map and how many descriptors it holds. */
static uint8_t map_bytes[TEST_STRIDE * TEST_MAX_DESCRIPTORS];
static unsigned map_count;

/* Prints one check's outcome and counts a failure. */
static void
check(
	const char *name,
	int condition)
{
	/* One line per check. */
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

/* Appends one descriptor to the fake map. */
static void
describe(
	UINT32 type,
	uint64_t start,
	uint64_t pages)
{
	EFI_MEMORY_DESCRIPTOR descriptor;

	/* Fills the descriptor and copies it into its slot. */
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.Type = type;
	descriptor.PhysicalStart = start;
	descriptor.NumberOfPages = pages;
	memset(map_bytes + (size_t)map_count * TEST_STRIDE, 0x5a, TEST_STRIDE);
	memcpy(map_bytes + (size_t)map_count * TEST_STRIDE, &descriptor, sizeof(descriptor));
	map_count++;
}

/* Starts an empty handoff list and an empty map. */
static void
reset(
	struct zbl6_riscv64_handoff *handoff)
{
	/* Nothing listed, nothing described. */
	memset(handoff, 0, sizeof(*handoff));
	map_count = 0;
}

/* Reports whether the list holds a range exactly. */
static int
listed(
	const struct zbl6_riscv64_handoff *handoff,
	uint64_t phys,
	uint64_t size)
{
	uint32_t index;

	/* Looks through the list. */
	for (index = 0; index < handoff->reserved_count; index++) {
		if (handoff->reserved[index].phys == phys && handoff->reserved[index].size == size)
			return 1;
	}
	return 0;
}

/* Checks which types are kept. */
static void
test_types(void)
{
	/* The firmware's types. */
	check("reserved kept", zbl_riscv64_firmware_keeps(EfiReservedMemoryType));
	check("runtime code kept", zbl_riscv64_firmware_keeps(EfiRuntimeServicesCode));
	check("runtime data kept", zbl_riscv64_firmware_keeps(EfiRuntimeServicesData));
	check("unusable kept", zbl_riscv64_firmware_keeps(EfiUnusableMemory));
	check("ACPI reclaim kept", zbl_riscv64_firmware_keeps(EfiACPIReclaimMemory));
	check("ACPI NVS kept", zbl_riscv64_firmware_keeps(EfiACPIMemoryNVS));
	check("PAL code kept", zbl_riscv64_firmware_keeps(EfiPalCode));
	check("persistent kept", zbl_riscv64_firmware_keeps(EfiPersistentMemory));

	/* The kernel's types, and what is not RAM. */
	check("loader code not kept", !zbl_riscv64_firmware_keeps(EfiLoaderCode));
	check("loader data not kept", !zbl_riscv64_firmware_keeps(EfiLoaderData));
	check("boot code not kept", !zbl_riscv64_firmware_keeps(EfiBootServicesCode));
	check("boot data not kept", !zbl_riscv64_firmware_keeps(EfiBootServicesData));
	check("conventional not kept", !zbl_riscv64_firmware_keeps(EfiConventionalMemory));
	check("MMIO not kept", !zbl_riscv64_firmware_keeps(EfiMemoryMappedIO));
	check("unknown type not kept", !zbl_riscv64_firmware_keeps(0x80000000U));
}

/* Checks a map like U-Boot's: the screen, OpenSBI's reservation, runtime pages, the rest free. */
static void
test_map(void)
{
	struct zbl6_riscv64_handoff handoff;

	/* The screen first, as the loader lists it. */
	reset(&handoff);
	zbl_riscv64_reserved_add(&handoff, 0xfe000000ULL, 0x7e9000ULL);
	describe(EfiReservedMemoryType, 0x40000000ULL, 0x80);
	describe(EfiConventionalMemory, 0x40080000ULL, 0x1000);
	describe(EfiRuntimeServicesCode, 0xff700000ULL, 0x10);
	describe(EfiRuntimeServicesData, 0xff710000ULL, 0x8);
	describe(EfiBootServicesData, 0xff718000ULL, 0x100);
	describe(EfiMemoryMappedIO, 0x10000000ULL, 0x1);
	describe(EfiLoaderData, 0x80000000ULL, 0x100);
	zbl_riscv64_reserved_collect(&handoff, map_bytes, (UINTN)map_count * TEST_STRIDE, TEST_STRIDE);
	check("map: three ranges listed", handoff.reserved_count == 3U);
	check("map: the screen", listed(&handoff, 0xfe000000ULL, 0x7e9000ULL));
	check("map: OpenSBI's reservation", listed(&handoff, 0x40000000ULL, 0x80000ULL));
	check("map: runtime code and data joined", listed(&handoff, 0xff700000ULL, 0x18000ULL));
	check("map: no overflow", handoff.reserved_flags == 0U);

	/* A trailing part descriptor is not read. */
	reset(&handoff);
	describe(EfiReservedMemoryType, 0x40000000ULL, 0x80);
	describe(EfiReservedMemoryType, 0x50000000ULL, 0x80);
	zbl_riscv64_reserved_collect(&handoff, map_bytes, (UINTN)map_count * TEST_STRIDE - 1U, TEST_STRIDE);
	check("map: a cut last descriptor is not read", handoff.reserved_count == 1U);

	/* A descriptor size smaller than the descriptor reads nothing, nor does no map. */
	reset(&handoff);
	describe(EfiReservedMemoryType, 0x40000000ULL, 0x80);
	zbl_riscv64_reserved_collect(&handoff, map_bytes, 32U, 32U);
	check("map: a short descriptor size reads nothing", handoff.reserved_count == 0U);
	zbl_riscv64_reserved_collect(&handoff, 0, 480U, TEST_STRIDE);
	check("map: no map reads nothing", handoff.reserved_count == 0U);

	/* A page count whose bytes do not fit is cut at the top. */
	reset(&handoff);
	describe(EfiReservedMemoryType, 0x40000000ULL, 0x8000000000000000ULL);
	zbl_riscv64_reserved_collect(&handoff, map_bytes, (UINTN)map_count * TEST_STRIDE, TEST_STRIDE);
	check("map: a huge page count is cut at the top",
	      handoff.reserved_count == 1U && listed(&handoff, 0x40000000ULL, ~0ULL - 0x40000000ULL));
}

/* Checks the joining and the overflow flag. */
static void
test_list(void)
{
	struct zbl6_riscv64_handoff handoff;
	unsigned index;

	/* Empty ranges add nothing; a bridge joins two listed ranges and an inner one. */
	reset(&handoff);
	zbl_riscv64_reserved_add(&handoff, 0x1000ULL, 0);
	zbl_riscv64_reserved_add(&handoff, 0x80000000ULL, 0x1000ULL);
	zbl_riscv64_reserved_add(&handoff, 0x80003000ULL, 0x1000ULL);
	zbl_riscv64_reserved_add(&handoff, 0x80010000ULL, 0x1000ULL);
	zbl_riscv64_reserved_add(&handoff, 0x80001000ULL, 0x2000ULL);
	check("list: bridge joins, the apart one stays", handoff.reserved_count == 2U);
	check("list: 0x80000000 + 0x4000", listed(&handoff, 0x80000000ULL, 0x4000ULL));
	check("list: 0x80010000 + 0x1000", listed(&handoff, 0x80010000ULL, 0x1000ULL));

	/* Sixteen apart ranges fill the list without the flag. */
	reset(&handoff);
	for (index = 0; index < 16U; index++)
		zbl_riscv64_reserved_add(&handoff, 0x80000000ULL + (uint64_t)index * 0x10000ULL, 0x1000ULL);
	check("list: 16 apart ranges listed", handoff.reserved_count == 16U && handoff.reserved_flags == 0U);

	/* A seventeenth that touches a listed one still joins. */
	zbl_riscv64_reserved_add(&handoff, 0x80001000ULL, 0x1000ULL);
	check("list: a touching seventeenth joins", handoff.reserved_count == 16U && handoff.reserved_flags == 0U);

	/* A seventeenth apart one sets the flag and is not listed. */
	zbl_riscv64_reserved_add(&handoff, 0x90000000ULL, 0x1000ULL);
	check("list: an apart seventeenth flags overflow",
	      handoff.reserved_count == 16U && handoff.reserved_flags == ZBL6_RISCV64_RESERVED_OVERFLOW);
	check("list: the apart seventeenth is not listed", !listed(&handoff, 0x90000000ULL, 0x1000ULL));

	/* A wrapping range is cut at the top. */
	reset(&handoff);
	zbl_riscv64_reserved_add(&handoff, 0xfffffffffffff000ULL, 0x2000ULL);
	check("list: a wrapping range is cut", listed(&handoff, 0xfffffffffffff000ULL, 0xfffULL));
}

/* Runs every check. */
int
main(void)
{
	/* The checks. */
	test_types();
	test_map();
	test_list();

	/* Reports. */
	printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
