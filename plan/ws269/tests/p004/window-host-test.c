/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the device window's page leaves: only the pages of the
 * range get a leaf, each pointing at its own page with the given flags,
 * and a range that crosses a megapage is split between the two tables.
 */

#include <stdio.h>
#include <string.h>

#include "defs.h"
#include "window.h"

/* How many checks failed. */
static int failures;

/* Prints one check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

/* Counts the valid entries of a table. */
static unsigned
count_valid(
	const uint64_t *table)
{
	unsigned index;
	unsigned count;

	count = 0;
	for (index = 0; index < RISCV64_WINDOW_TABLE_ENTRIES; index++) {
		if ((table[index] & RISCV64_PTE_V) != 0)
			count++;
	}
	return count;
}

int
main(
	void)
{
	static uint64_t first[RISCV64_WINDOW_TABLE_ENTRIES];
	static uint64_t second[RISCV64_WINDOW_TABLE_ENTRIES];
	uint64_t flags;
	uint64_t block;
	uint64_t alias;
	unsigned written;

	/*
	 * The alias of RAM at 0x4020_1000 (two pages above the megapage
	 * 0x4020_0000's start plus one), 0x4_0000_0000 higher.
	 */
	flags = RISCV64_PTE_V | RISCV64_PTE_R | RISCV64_PTE_W | RISCV64_PTE_G | RISCV64_PTE_A | RISCV64_PTE_D;
	block = 0x440200000ULL;
	alias = 0x440201000ULL;
	written = riscv64_window_fill_pages(first, block, alias, 0x3000, flags);
	check(written == 3U, "three pages written");
	check(first[0] == 0, "the megapage's first page stays unreachable");
	check(first[1] == ((alias >> 12) << 10 | flags), "page 1 points at its own alias page");
	check(first[3] == (((alias + 0x2000) >> 12) << 10 | flags), "page 3 points at its own alias page");
	check(first[4] == 0, "the page after the range stays unreachable");
	check(count_valid(first) == 3U, "no other leaf");

	/* A range that crosses into the next megapage is split. */
	memset(first, 0, sizeof(first));
	alias = 0x440200000ULL + 0x200000ULL - 0x2000ULL;
	written = riscv64_window_fill_pages(first, block, alias, 0x4000, flags);
	check(written == 2U, "two pages in the first megapage");
	check(first[510] != 0 && first[511] != 0, "the last two pages of the first megapage");
	written = riscv64_window_fill_pages(second, block + 0x200000ULL, alias, 0x4000, flags);
	check(written == 2U, "two pages in the second megapage");
	check(second[0] == ((0x440400000ULL >> 12) << 10 | flags), "the second megapage's first page");
	check(count_valid(second) == 2U, "nothing else in the second megapage");

	/* An unaligned start covers the page that holds it. */
	memset(first, 0, sizeof(first));
	written = riscv64_window_fill_pages(first, block, block + 0x10, 0x10, flags);
	check(written == 1U && first[0] != 0, "an unaligned range covers its page");

	/* An empty or wrapping range writes nothing. */
	written = riscv64_window_fill_pages(first, block, block, 0, flags);
	check(written == 0U, "an empty range writes nothing");
	written = riscv64_window_fill_pages(first, block, ~0ULL - 0xfff, 0x2000, flags);
	check(written == 0U, "a wrapping range writes nothing");

	printf("%s window-host-test (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
	return failures == 0 ? 0 : 1;
}
