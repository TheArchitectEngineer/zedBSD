/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws299-p003 host test of drv_dma_range_bus_address().
 *
 * The boundaries Q1 asked for (2026-10-27): physical_limit, address_bits and
 * segment_boundary, each just inside and just outside, with and without a
 * bus offset, and the refusals of an empty, an oversized and a wrapping range.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -I include \
 *       -o build/tmp/dma-range-test plan/ws299/tests/host/dma-range-test.c
 */

#include <drivers/generic/dma-range.h>

#include <stdio.h>

#define TEST_PAGE	4096U
#define TEST_SEGMENT	(64U * 1024U)

/* How many checks failed; the test passes only when it stays zero. */
static unsigned failures;

static void check(int condition, const char *what);

/* Runs every case and reports PASS or the failures. */
int
main(void)
{
	uint64_t bus;
	int fits;

	/* A plain 64-bit device with no limit maps a page onto itself. */
	bus = 0;
	fits = drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0U, 0U, 0x123456000ULL, TEST_PAGE, &bus);
	check(fits && bus == 0x123456000ULL, "identity page");

	/* physical_limit is inclusive: the last byte may be the limit, not past it. */
	fits = drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0U, 0xffffffffULL, 0xfffff000ULL, TEST_PAGE, &bus);
	check(fits, "a page ending at the physical limit");
	fits = drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0U, 0xfffffffeULL, 0xfffff000ULL, TEST_PAGE, &bus);
	check(!fits, "a page one byte past the physical limit");

	/* address_bits applies to the bus alias, offset included. */
	fits = drv_dma_range_bus_address(32U, TEST_SEGMENT, 0U, 0U, 0U, 0xfffff000ULL, TEST_PAGE, &bus);
	check(fits, "a page ending at the 32-bit bus limit");
	fits = drv_dma_range_bus_address(32U, TEST_SEGMENT, 0U, 0U, 0U, 0x100000000ULL, TEST_PAGE, &bus);
	check(!fits, "a page past the 32-bit bus limit");
	fits = drv_dma_range_bus_address(32U, TEST_SEGMENT, 0U, 0x80000000ULL, 0U, 0x7ffff000ULL, TEST_PAGE, &bus);
	check(fits && bus == 0xfffff000ULL, "an offset alias ending at the bus limit");
	fits = drv_dma_range_bus_address(32U, TEST_SEGMENT, 0U, 0x80000000ULL, 0U, 0x80000000ULL, TEST_PAGE, &bus);
	check(!fits, "an offset alias past the bus limit");

	/* segment_boundary: a range may end at a boundary but not cross it. */
	fits = drv_dma_range_bus_address(64U, TEST_SEGMENT, TEST_SEGMENT, 0U, 0U, 0x1f000ULL, TEST_PAGE, &bus);
	check(fits, "a page ending at a segment boundary");
	fits = drv_dma_range_bus_address(64U, TEST_SEGMENT, TEST_SEGMENT, 0U, 0U, 0x1f800ULL, TEST_PAGE, &bus);
	check(!fits, "a page crossing a segment boundary");

	/* Empty, oversized and wrapping ranges, and a bad width, are refused. */
	check(!drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0U, 0U, 0x1000ULL, 0U, &bus), "an empty range");
	check(!drv_dma_range_bus_address(64U, TEST_PAGE, 0U, 0U, 0U, 0x1000ULL, TEST_PAGE + 1U, &bus), "a range over max_segment_size");
	check(!drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0U, 0U, UINT64_MAX - 100U, TEST_PAGE, &bus), "a wrapping range");
	check(!drv_dma_range_bus_address(64U, TEST_SEGMENT, 0U, 0x2000ULL, 0U, UINT64_MAX - 0x1fffULL, TEST_PAGE, &bus), "a wrapping alias");
	check(!drv_dma_range_bus_address(0U, TEST_SEGMENT, 0U, 0U, 0U, 0x1000ULL, TEST_PAGE, &bus), "zero address bits");

	/* Reports the outcome. */
	if (failures != 0) {
		printf("dma-range-test: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("dma-range-test: PASS\n");
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
