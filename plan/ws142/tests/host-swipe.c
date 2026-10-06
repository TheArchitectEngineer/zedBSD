/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of one swipe as one step (ws142-p009, BUG-215 and
 * BUG-216, userland/desktop/wayland/swipe.c compiled unchanged).
 *
 * Checks: less than 8 mm decides nothing; 8 mm across decides right (or
 * left) once, and however far the fingers go on, nothing more until the
 * swipe ends; down and up the same; the way it went most decides; the
 * travel adds up over reports, both ways; after the end the next swipe
 * decides again; the names.
 *
 *   plan/ws142/tests/run-host-swipe.sh
 */

#include "swipe.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Runs every case. */
int
main(void)
{
	struct zwl_swipe swipe;
	unsigned decided;
	int index;
	int many;

	/* 1. Less than a step decides nothing. */
	memset(&swipe, 0, sizeof(swipe));
	decided = zwl_swipe_take(&swipe, 5000, 0);
	check(decided == ZWL_SWIPE_NONE, "5 mm across: nothing yet");

	/* 2. The rest of the step across to the right: right, once; on and on, nothing more. */
	decided = zwl_swipe_take(&swipe, 3000, 1000);
	check(decided == ZWL_SWIPE_RIGHT, "8 mm right in all: right");
	many = 0;
	for (index = 0; index < 20; index++) {
		decided = zwl_swipe_take(&swipe, 5000, 0);
		if (decided != ZWL_SWIPE_NONE)
			many++;
	}

	/* None of them decided anything. */
	check(many == 0, "100 mm more of the same swipe: nothing more (BUG-215)");

	/* 3. The swipe ends; the next, to the left, decides again. */
	zwl_swipe_end(&swipe);
	check(swipe.decided == 0U && swipe.across_um == 0 && swipe.down_um == 0, "ended: nothing kept");
	decided = zwl_swipe_take(&swipe, -8000, 0);
	check(decided == ZWL_SWIPE_LEFT, "8 mm left: left");

	/* 4. Down and up. */
	zwl_swipe_end(&swipe);
	decided = zwl_swipe_take(&swipe, 2000, 9000);
	check(decided == ZWL_SWIPE_DOWN, "9 mm down, 2 across: down");
	zwl_swipe_end(&swipe);
	decided = zwl_swipe_take(&swipe, 0, -8000);
	check(decided == ZWL_SWIPE_UP, "8 mm up: up");

	/* 5. The way it went most decides; a tie is across. */
	zwl_swipe_end(&swipe);
	decided = zwl_swipe_take(&swipe, 9000, -8500);
	check(decided == ZWL_SWIPE_RIGHT, "9 right, 8.5 up: right");
	zwl_swipe_end(&swipe);
	decided = zwl_swipe_take(&swipe, -8000, 8000);
	check(decided == ZWL_SWIPE_LEFT, "a tie: across");

	/* 6. Back and forth adds up: 6 right, 6 left is nothing; then 8 more left is left. */
	zwl_swipe_end(&swipe);
	decided = zwl_swipe_take(&swipe, 6000, 0);
	check(decided == ZWL_SWIPE_NONE, "6 right: nothing");
	decided = zwl_swipe_take(&swipe, -6000, 0);
	check(decided == ZWL_SWIPE_NONE, "and back: nothing");
	decided = zwl_swipe_take(&swipe, -8000, 0);
	check(decided == ZWL_SWIPE_LEFT, "then 8 left: left");

	/* 7. The names the log uses. */
	check(strcmp(zwl_swipe_name(ZWL_SWIPE_RIGHT), "right") == 0, "right's name");
	check(strcmp(zwl_swipe_name(ZWL_SWIPE_DOWN), "down") == 0, "down's name");
	check(strcmp(zwl_swipe_name(ZWL_SWIPE_NONE), "none") == 0, "none's name");

	/* The result. */
	if (failures != 0) {
		printf("host-swipe: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-swipe: ok (%d checks)\n", checks);
	return 0;
}
