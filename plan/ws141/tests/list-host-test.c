/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the BCM2711 display list decoding (ws141-p003, stage N0).
 *
 * It decodes a one-plane unscaled list, a scaled plane, a list without an
 * end word and a word that is not a plane, from ordinary arrays.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/bcm2711/bcm2711-private.h"

/* The list memory the test fills. */
static uint32_t test_memory[BCM2711_LIST_WORDS];

/* The number of failed checks. */
static unsigned test_failures;

static void check(bool condition, const char *what);

/*
 * Runs the checks and reports PASS or FAIL.
 */
int
main(
	void)
{
	struct bcm2711_list list;

	/* One unscaled 1920x1080 plane at word 40, then the end word. */
	memset(test_memory, 0, sizeof(test_memory));
	test_memory[40] = 0x4800D807U;
	test_memory[41] = 0x00000000U;
	test_memory[42] = 0x4000FFF0U;
	test_memory[43] = 0x04380780U;
	test_memory[44] = 0xC0C0C0C0U;
	test_memory[45] = 0xC0000000U + 0x3e8fa000U;
	test_memory[46] = 0xC0C0C0C0U;
	test_memory[47] = 0x00001E00U;
	test_memory[48] = 0x80000000U;
	bcm2711_list_decode(test_memory, 40, &list);
	check(list.valid, "the one-plane list is valid");
	check(list.end == 48, "the end word is at 48");
	check(list.plane_count == 1, "one plane");
	check(!list.planes[0].scaled, "the plane is unscaled");
	check(list.planes[0].format == 7, "format 7");
	check(list.planes[0].order == 2, "order 2");
	check(list.planes[0].width == 1920 && list.planes[0].height == 1080, "1920x1080");
	check(list.planes[0].pointer == 0xfe8fa000U, "the pointer is the bus address");
	check(list.planes[0].pitch == 7680, "pitch 7680");

	/* A scaled plane of nine words: the size and later words move by one. */
	memset(test_memory, 0, sizeof(test_memory));
	test_memory[0] = 0x49005807U;
	test_memory[1] = (5U << 16) | 7U;
	test_memory[3] = (540U << 16) | 960U;
	test_memory[4] = (1080U << 16) | 1920U;
	test_memory[6] = 0xC0001000U;
	test_memory[8] = 3840U;
	test_memory[9] = 0x80000000U;
	bcm2711_list_decode(test_memory, 0, &list);
	check(list.valid && list.plane_count == 1, "the scaled list is valid");
	check(list.planes[0].scaled, "the plane is scaled");
	check(list.planes[0].x == 7 && list.planes[0].y == 5, "the position");
	check(list.planes[0].width == 1920 && list.planes[0].height == 1080, "the source size");
	check(list.planes[0].pointer == 0xC0001000U && list.planes[0].pitch == 3840, "pointer and pitch");

	/* A word that neither starts a plane nor ends the list. */
	memset(test_memory, 0, sizeof(test_memory));
	test_memory[10] = 0x12345678U;
	bcm2711_list_decode(test_memory, 10, &list);
	check(!list.valid, "a stray word is malformed");

	/* A plane that runs past the end of the memory. */
	memset(test_memory, 0, sizeof(test_memory));
	test_memory[BCM2711_LIST_WORDS - 2U] = 0x48008007U;
	bcm2711_list_decode(test_memory, BCM2711_LIST_WORDS - 2U, &list);
	check(!list.valid, "a plane past the memory is malformed");

	/* Reports the verdict. */
	if (test_failures != 0) {
		printf("list-host-test: FAIL (%u)\n", test_failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("list-host-test: PASS\n");
	return 0;
}

/* Counts and shows one failed check. */
static void
check(
	bool condition,
	const char *what)
{
	/* A held check says nothing. */
	if (condition)
		return;

	/* Shows the failed check. */
	printf("FAIL: %s\n", what);
	test_failures++;
}
