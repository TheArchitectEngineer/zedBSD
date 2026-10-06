/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p024: the host test of a mouse's pointer acceleration
 * (userland/desktop/wayland/pointer-accel.c compiled unchanged).
 *   - No acceleration at 100%: a count is a pixel at every speed.
 *   - The gain: 1 when slow, the level's most when fast, between in
 *     proportion, and stronger levels give more.
 *   - The same hand motion moves the pointer further when it is fast, and
 *     further at a stronger level; slow motion is the speed alone.
 *   - 150% of slow motion: three pixels for two counts, the half carried.
 *   - The first report after a pause is measured over 50 ms.
 * Prints one line a check and "host-pointer-accel: PASS" or FAIL.
 *
 *   sh plan/ws089/tests/run-host-pointer-accel.sh
 */

#include "userland/desktop/wayland/pointer-accel.h"

#include <stdio.h>

static int failures;

static void check(const char *what, int passed);
static int64_t travel(int32_t speed, int32_t level, int64_t counts, unsigned reports, uint64_t gap_us);

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* The line, and the count of failures. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/* Moves a mouse by counts in each of some reports gap_us apart; returns the pixels moved across. */
static int64_t
travel(
	int32_t speed,
	int32_t level,
	int64_t counts,
	unsigned reports,
	uint64_t gap_us)
{
	struct kwl_pointer_accel accel;
	uint64_t time_us;
	int64_t total;
	int64_t x;
	int64_t y;
	unsigned index;

	/* Each report, after a first that starts the clock. */
	kwl_pointer_accel_init(&accel);
	time_us = 1000000;
	kwl_pointer_accel_move(&accel, 0, 0, time_us, speed, level, &x, &y);
	total = 0;
	for (index = 0; index < reports; index++) {
		time_us += gap_us;
		kwl_pointer_accel_move(&accel, counts, 0, time_us, speed, level, &x, &y);
		total += x;
	}

	/* The pixels across. */
	return total;
}

/*
 * Runs the checks.
 */
int
main(void)
{
	struct kwl_pointer_accel accel;
	int64_t slow;
	int64_t fast;
	int64_t x;
	int64_t y;

	/* No acceleration at 100%: one pixel a count, slow or fast. */
	check("none 100% slow", travel(100, KWL_ACCEL_NONE, 2, 50, 8000) == 100);
	check("none 100% fast", travel(100, KWL_ACCEL_NONE, 40, 50, 8000) == 2000);

	/* The gain curve. */
	check("gain 1 when slow", kwl_pointer_accel_gain(300, KWL_ACCEL_STRONG) == 256);
	check("gain strong when fast", kwl_pointer_accel_gain(10000, KWL_ACCEL_STRONG) == 768);
	check("gain between", kwl_pointer_accel_gain(2200, KWL_ACCEL_STRONG) == 512);
	check("stronger gives more", kwl_pointer_accel_gain(3000, KWL_ACCEL_MILD) < kwl_pointer_accel_gain(3000, KWL_ACCEL_MEDIUM) && kwl_pointer_accel_gain(3000, KWL_ACCEL_MEDIUM) < kwl_pointer_accel_gain(3000, KWL_ACCEL_STRONG));
	check("level out of range is kept", kwl_pointer_accel_gain(10000, 9) == 768 && kwl_pointer_accel_gain(10000, -1) == 256);

	/* The same 2000 counts slow (2 a report, 250 a second) and fast (40 a report, 5000 a second). */
	slow = travel(100, KWL_ACCEL_STRONG, 2, 1000, 8000);
	fast = travel(100, KWL_ACCEL_STRONG, 40, 50, 8000);
	printf("strong: slow %lld px, fast %lld px for 2000 counts\n", (long long)slow, (long long)fast);
	check("slow is the speed alone", slow == 2000);
	check("fast goes about three times as far (the first report slow)", fast > 5900 && fast <= 6000);
	check("a stronger level goes further", travel(100, KWL_ACCEL_MILD, 40, 50, 8000) < travel(100, KWL_ACCEL_STRONG, 40, 50, 8000));

	/* 150% when slow: two counts are three pixels; one count a pixel and a half carried. */
	check("150% slow", travel(150, KWL_ACCEL_STRONG, 2, 10, 8000) == 30);
	kwl_pointer_accel_init(&accel);
	kwl_pointer_accel_move(&accel, 1, 0, 1000000, 150, KWL_ACCEL_NONE, &x, &y);
	check("half carried", x == 1);
	kwl_pointer_accel_move(&accel, 1, 0, 1100000, 150, KWL_ACCEL_NONE, &x, &y);
	check("half added", x == 2);
	kwl_pointer_accel_move(&accel, -1, -1, 1200000, 150, KWL_ACCEL_NONE, &x, &y);
	check("backwards", x == -1 && y == -1);

	/* The first report after a pause is measured over 50 ms: 40 counts are 800 a second (48 px), not 5000 (120 px). */
	kwl_pointer_accel_init(&accel);
	kwl_pointer_accel_move(&accel, 40, 0, 5000000, 100, KWL_ACCEL_STRONG, &x, &y);
	check("first report slow", x == 48);
	kwl_pointer_accel_move(&accel, 40, 0, 5008000, 100, KWL_ACCEL_STRONG, &x, &y);
	check("the next report fast", x == 120);

	/* The verdict. */
	if (failures != 0) {
		printf("host-pointer-accel: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-pointer-accel: PASS\n");
	return 0;
}
