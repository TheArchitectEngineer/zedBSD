/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host check of the MIPS HAL's page table entries
 * (src/hal/mips/pte.h): an entry is valid to the TLB only once it is
 * mapped, used and allows some access; writable to the TLB only once it
 * is also writable and written; PROT_NONE stays mapped but invalid; and
 * the refill's shift by six puts the frame where EntryLo has it and
 * drops the software bits.
 *
 * cc -m32 -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal/mips -DHAL_ARCH_MIPS32 \
 *    plan/ws271/tests/pte-host-test.c -o <out> && <out>
 */

#include <stdio.h>
#include "pte.h"

/* A frame with every frame bit that matters set, at a page boundary. */
#define FRAME		0x0abcd000U

/* The EntryLo bits: valid, dirty, the cache attribute and the frame number. */
#define LO_VALID	0x2U
#define LO_DIRTY	0x4U
#define LO_CACHE_MASK	0x38U

static int failures;

static void check(int condition, const char *what);

/*
 * Runs the checks and reports PASS or FAIL.
 */
int
main(
	void)
{
	uint32_t entry;
	uint32_t low;

	/* A writable page just mapped: neither valid nor dirty to the TLB. */
	entry = mips_pte_compose(FRAME | mips_pte_software_bits(HAL_SPACE_READ | HAL_SPACE_WRITE));
	low = entry >> MIPS_PTE_HARDWARE_SHIFT;
	check((low & LO_VALID) == 0, "new page invalid");
	check((low & LO_DIRTY) == 0, "new page clean");
	check((low & LO_CACHE_MASK) == (MIPS_PTE_CACHED << 3), "cache attribute");
	check((low >> 6) == (FRAME >> 12), "frame number in EntryLo");

	/* Used: valid but clean; written: dirty. */
	entry = mips_pte_compose(entry | MIPS_PTE_ACCESSED);
	low = entry >> MIPS_PTE_HARDWARE_SHIFT;
	check((low & LO_VALID) != 0, "used page valid");
	check((low & LO_DIRTY) == 0, "read page clean");
	entry = mips_pte_compose(entry | MIPS_PTE_DIRTY);
	low = entry >> MIPS_PTE_HARDWARE_SHIFT;
	check((low & LO_DIRTY) != 0, "written page dirty");

	/* Read-only and written records: never dirty to the TLB. */
	entry = mips_pte_compose(FRAME | mips_pte_software_bits(HAL_SPACE_READ) | MIPS_PTE_ACCESSED | MIPS_PTE_DIRTY);
	low = entry >> MIPS_PTE_HARDWARE_SHIFT;
	check((low & LO_VALID) != 0, "read-only page valid");
	check((low & LO_DIRTY) == 0, "read-only page never dirty");

	/* No access: mapped, used, but invalid. */
	entry = mips_pte_compose(FRAME | mips_pte_software_bits(HAL_SPACE_NONE) | MIPS_PTE_ACCESSED);
	check((entry & MIPS_PTE_PRESENT) != 0, "PROT_NONE page present");
	check(((entry >> MIPS_PTE_HARDWARE_SHIFT) & LO_VALID) == 0, "PROT_NONE page invalid");

	/* An entry that is not mapped is nothing to the TLB. */
	entry = mips_pte_compose(FRAME | MIPS_PTE_ACCESSED);
	check(((entry >> MIPS_PTE_HARDWARE_SHIFT) & LO_VALID) == 0, "unmapped entry invalid");

	/* Reports the result. */
	if (failures != 0) {
		printf("FAIL %d\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}

/* Counts and names a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A failed check is named on the output. */
	if (!condition) {
		printf("failed: %s\n", what);
		failures++;
	}
}
