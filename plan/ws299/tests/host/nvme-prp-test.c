/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws299-p003 host test of the NVMe PRP construction.
 *
 * Checks drv_nvme_prp_fill() of include/drivers/pci/pci-nvme-protocol.h:
 * one page in PRP1 alone, two in PRP1 and PRP2, three or more through a
 * list whose address is PRP2, a full list without a chain, and the inputs
 * it refuses.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -I include \
 *       -o build/tmp/nvme-prp-test plan/ws299/tests/host/nvme-prp-test.c
 */

#include <drivers/pci/pci-nvme-protocol.h>

#include <stdio.h>
#include <string.h>

#define TEST_PAGE	4096U
#define TEST_LIST	0x7f000000ULL
#define TEST_ENTRIES	(TEST_PAGE / 8U)

/* How many checks failed; the test passes only when it stays zero. */
static unsigned failures;

static void check(int condition, const char *what);
static void fill_pages(uint64_t *pages, uint32_t count, uint64_t first);

/* Runs every case and reports PASS or the failures. */
int
main(void)
{
	static uint64_t pages[TEST_ENTRIES + 2U];
	static uint64_t list[TEST_ENTRIES];
	struct drv_nvme_command command;
	int filled;
	uint32_t index;
	int list_ok;

	/* One page: PRP1 only, PRP2 zero, the list untouched. */
	fill_pages(pages, 1U, 0x100000ULL);
	memset(&command, 0, sizeof(command));
	memset(list, 0xa5, sizeof(list));
	filled = drv_nvme_prp_fill(&command, pages, 1U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES);
	check(filled, "one page is accepted");
	check(command.prp1 == 0x100000ULL, "one page is PRP1");
	check(command.prp2 == 0U, "one page leaves PRP2 zero");
	check(list[0] == 0xa5a5a5a5a5a5a5a5ULL, "one page leaves the list alone");

	/* Two pages: PRP2 is the second page itself. */
	fill_pages(pages, 2U, 0x200000ULL);
	memset(&command, 0, sizeof(command));
	filled = drv_nvme_prp_fill(&command, pages, 2U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES);
	check(filled, "two pages are accepted");
	check(command.prp1 == 0x200000ULL, "two pages: PRP1 is the first");
	check(command.prp2 == 0x201000ULL, "two pages: PRP2 is the second page");

	/* Three pages: PRP2 is the list, which holds the second and third. */
	fill_pages(pages, 3U, 0x300000ULL);
	pages[2] = 0x900000ULL;
	memset(&command, 0, sizeof(command));
	filled = drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES);
	check(filled, "three pages are accepted");
	check(command.prp1 == 0x300000ULL, "three pages: PRP1 is the first");
	check(command.prp2 == TEST_LIST, "three pages: PRP2 is the list");
	check(list[0] == 0x301000ULL, "three pages: list entry 0 is the second page");
	check(list[1] == 0x900000ULL, "three pages: list entry 1 is the third page, wherever it is");

	/* A full list: PRP1 and 512 entries, the last one data, not a chain. */
	fill_pages(pages, TEST_ENTRIES + 1U, 0x10000000ULL);
	memset(&command, 0, sizeof(command));
	filled = drv_nvme_prp_fill(&command, pages, TEST_ENTRIES + 1U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES);
	check(filled, "PRP1 and a full list are accepted");
	list_ok = 1;
	for (index = 0; index < TEST_ENTRIES; index++) {
		if (list[index] != pages[index + 1U])
			list_ok = 0;
	}
	check(list_ok, "a full list holds pages 1 to 512 in order");

	/* One page more than the list holds would need a chain, which is refused. */
	fill_pages(pages, TEST_ENTRIES + 2U, 0x20000000ULL);
	filled = drv_nvme_prp_fill(&command, pages, TEST_ENTRIES + 2U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES);
	check(!filled, "a list that would chain is refused");

	/* Inputs that are not whole aligned pages are refused. */
	fill_pages(pages, 3U, 0x400000ULL);
	pages[1] = 0x401200ULL;
	check(!drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES),
	    "an unaligned later page is refused");
	pages[1] = 0x401000ULL;
	pages[0] = 0x400200ULL;
	check(!drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES),
	    "an unaligned first page is refused");
	pages[0] = 0U;
	check(!drv_nvme_prp_fill(&command, pages, 1U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES),
	    "a zero address is refused");
	fill_pages(pages, 3U, 0x400000ULL);
	check(!drv_nvme_prp_fill(&command, pages, 0U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES),
	    "no pages is refused");
	check(!drv_nvme_prp_fill(&command, pages, 3U, 3000U, list, TEST_LIST, TEST_ENTRIES),
	    "a page size that is not a power of two is refused");
	check(!drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, list, TEST_LIST + 8U, TEST_ENTRIES),
	    "a list that is not page aligned is refused");
	check(!drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, NULL, TEST_LIST, TEST_ENTRIES),
	    "three pages without a list are refused");
	check(drv_nvme_prp_fill(&command, pages, 2U, TEST_PAGE, NULL, 0U, 0U),
	    "two pages need no list");
	check(!drv_nvme_prp_fill(&command, pages, 3U, TEST_PAGE, list, TEST_LIST, TEST_ENTRIES + 1U),
	    "a list capacity beyond one page is refused");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("nvme-prp-test: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("nvme-prp-test: PASS\n");
	return 0;
}

/* Counts and names a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A true condition passes silently. */
	if (condition)
		return;

	printf("FAIL: %s\n", what);
	failures++;
}

/* Fills consecutive page addresses starting at one address. */
static void
fill_pages(
	uint64_t *pages,
	uint32_t count,
	uint64_t first)
{
	uint32_t index;

	/* Gives each page the address one page after the one before. */
	for (index = 0; index < count; index++)
		pages[index] = first + (uint64_t)index * TEST_PAGE;
}
