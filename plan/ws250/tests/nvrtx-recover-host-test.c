/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of src/drivers/gpu/nvrtx/recover.c: the sessions' channel
 * numbers are handed out after the last one, wrapping, and none is handed
 * out while all are in use.
 */

#include <stdint.h>
#include <stdio.h>

#include "drivers/gpu/nvrtx/nvrtx-work.h"

/* The checks run and the ones that failed. */
static unsigned checks;
static unsigned failures;

static void check(int condition, const char *what);
static void test_chid(void);

int
main(void)
{
	/* Each group of checks. */
	test_chid();

	/* The summary line. */
	printf("nvrtx-recover host test: %u checks, %u failed\n", checks, failures);
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts one check and tells a failed one. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;

	/* A failed check is told. */
	failures++;
	printf("FAIL %s\n", what);
}

/* The next-fit numbers of the sessions' channels. */
static void
test_chid(void)
{
	uint32_t used;
	uint32_t last;
	int number;
	int index;

	/* The first number after the last one, from an empty set. */
	check(nvrtx_recover_chid_next(0U, NVRTX_WORK_SESSION_CHANNELS - 1U, NVRTX_WORK_SESSION_CHANNELS) == 0, "first is 0");
	check(nvrtx_recover_chid_next(0U, 0U, NVRTX_WORK_SESSION_CHANNELS) == 1, "after 0 comes 1");

	/* A number freed now is taken again only after every other free one. */
	used = 0U;
	last = NVRTX_WORK_SESSION_CHANNELS - 1U;
	for (index = 0; index < 5; index++) {
		number = nvrtx_recover_chid_next(used, last, NVRTX_WORK_SESSION_CHANNELS);
		check(number == index, "numbers in order");
		used |= 1U << (uint32_t)number;
		last = (uint32_t)number;
	}
	used &= ~(1U << 4);
	number = nvrtx_recover_chid_next(used, last, NVRTX_WORK_SESSION_CHANNELS);
	check(number == 5, "the number just freed waits");

	/* Wrapping past the end to a free number at the start. */
	used = 0xfffffffeU;
	check(nvrtx_recover_chid_next(used, 31U, 32U) == 0, "wraps to 0");
	used = 0x7fffffffU;
	check(nvrtx_recover_chid_next(used, 5U, 32U) == 31, "finds the last free");

	/* None while all are in use, and none for a count the bits cannot hold. */
	check(nvrtx_recover_chid_next(0xffffffffU, 3U, 32U) == -1, "all in use");
	check(nvrtx_recover_chid_next(0U, 0U, 0U) == -1, "no numbers");
	check(nvrtx_recover_chid_next(0U, 0U, 33U) == -1, "too many numbers");

	/* The last one itself when it is the only free one. */
	used = 0xffffffffU & ~(1U << 7);
	check(nvrtx_recover_chid_next(used, 7U, 32U) == 7, "the last one when it alone is free");
}
