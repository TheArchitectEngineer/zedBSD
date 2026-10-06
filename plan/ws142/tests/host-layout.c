/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the session's layout mode (ws142-p008, BUG-217,
 * userland/desktop/wayland/layout.c compiled unchanged).
 *
 * Checks, for each kind of window (floating, docked, fullscreen, a dialog
 * or a sheet, one of one size) in each mode (windowed, docked): what a
 * switch to it does (dock, float, keep), whether a new one opens docked,
 * whether it is docked on leaving fullscreen, whether it is drawn in the
 * middle of the docked space; which windows the docked mode hides (another
 * application's, except under an overview); the middle of a space.
 *
 *   plan/ws142/tests/run-host-layout.sh
 */

#include "layout.h"

#include <stdio.h>

/* The kinds of window the table names. */
#define FLOATING	0
#define DOCKED		1
#define FULLSCREEN	2
#define CHILD		3
#define FIXED		4
#define FIXED_DOCKED	5

/*
 * One row of the table: a kind of window in a mode, and what each rule
 * must say of it (the switch's action, opens docked, docked after
 * fullscreen, centred).
 */
struct layout_case {
	unsigned kind;
	unsigned mode;
	unsigned action;
	int opens_docked;
	int unfullscreen_docked;
	int centred;
	const char *what;
};

/* The table: every kind in both modes (the 2026-10-06 user decisions of ws142-p007). */
static const struct layout_case cases[] = {
	{ FLOATING, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_KEEP, 0, 0, 0, "windowed: a floating window stays" },
	{ DOCKED, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_FLOAT, 0, 0, 0, "windowed: a docked window floats again" },
	{ FULLSCREEN, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_KEEP, 0, 0, 0, "windowed: a fullscreen window stays, floats after" },
	{ CHILD, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_KEEP, 0, 0, 0, "windowed: a dialog stays over its parent" },
	{ FIXED, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_KEEP, 0, 0, 0, "windowed: a window of one size floats" },
	{ FIXED_DOCKED, KWL_LAYOUT_WINDOWED, KWL_LAYOUT_FLOAT, 0, 0, 1, "windowed: a docked window of one size floats again" },
	{ FLOATING, KWL_LAYOUT_DOCKED, KWL_LAYOUT_DOCK, 1, 1, 0, "docked: a floating window docks" },
	{ DOCKED, KWL_LAYOUT_DOCKED, KWL_LAYOUT_KEEP, 0, 1, 0, "docked: a docked window stays" },
	{ FULLSCREEN, KWL_LAYOUT_DOCKED, KWL_LAYOUT_KEEP, 0, 1, 0, "docked: a fullscreen window stays, docks after" },
	{ CHILD, KWL_LAYOUT_DOCKED, KWL_LAYOUT_KEEP, 0, 0, 1, "docked: a dialog is never docked, in the middle" },
	{ FIXED, KWL_LAYOUT_DOCKED, KWL_LAYOUT_DOCK, 1, 1, 0, "docked: a window of one size docks too" },
	{ FIXED_DOCKED, KWL_LAYOUT_DOCKED, KWL_LAYOUT_KEEP, 0, 1, 1, "docked: a docked window of one size is in the middle" },
};

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what, const char *rule);
static void describe(unsigned kind, struct kwl_layout_window *window);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what,
	const char *rule)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s (%s)\n", what, rule);
		failures++;
	}
}

/* Describes a kind of window of the table as the rules see it. */
static void
describe(
	unsigned kind,
	struct kwl_layout_window *window)
{
	/* Nothing set: a floating window of many sizes without a parent. */
	window->docked = 0U;
	window->fullscreen = 0U;
	window->child = 0U;
	window->fixed = 0U;

	/* The kind's own state. */
	if (kind == DOCKED || kind == FIXED_DOCKED)
		window->docked = 1U;
	if (kind == FULLSCREEN)
		window->fullscreen = 1U;
	if (kind == CHILD)
		window->child = 1U;
	if (kind == FIXED || kind == FIXED_DOCKED)
		window->fixed = 1U;
}

/* Runs every case. */
int
main(void)
{
	struct kwl_layout_window window;
	unsigned index;
	unsigned action;
	int answer;
	int32_t x;
	int32_t y;

	/* 1. The table: each rule for each kind in each mode. */
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		describe(cases[index].kind, &window);
		action = kwl_layout_switch_action(cases[index].mode, &window);
		check(action == cases[index].action, cases[index].what, "switch");
		answer = kwl_layout_opens_docked(cases[index].mode, &window);
		check(answer == cases[index].opens_docked, cases[index].what, "opens docked");
		answer = kwl_layout_unfullscreen_docked(cases[index].mode, &window);
		check(answer == cases[index].unfullscreen_docked, cases[index].what, "after fullscreen");
		answer = kwl_layout_centred(cases[index].mode, &window);
		check(answer == cases[index].centred, cases[index].what, "centred");
	}

	/* 2. Hidden: only another application's window in the docked mode, without an overview. */
	check(!kwl_layout_hidden(KWL_LAYOUT_WINDOWED, 0, 0), "windowed: another application shows", "hidden");
	check(!kwl_layout_hidden(KWL_LAYOUT_WINDOWED, 1, 0), "windowed: the current application shows", "hidden");
	check(kwl_layout_hidden(KWL_LAYOUT_DOCKED, 0, 0), "docked: another application is hidden", "hidden");
	check(!kwl_layout_hidden(KWL_LAYOUT_DOCKED, 1, 0), "docked: the current application's other windows show", "hidden");
	check(!kwl_layout_hidden(KWL_LAYOUT_DOCKED, 0, 1), "docked: an overview shows every application", "hidden");

	/* 3. The middle of a space; a body larger than it starts at its corner. */
	kwl_layout_centre(0, 48, 1280, 752, 640, 400, &x, &y);
	check(x == 320 && y == 224, "a 640x400 body in the 1280x752 space under the bar", "centre");
	kwl_layout_centre(0, 48, 1280, 752, 1400, 900, &x, &y);
	check(x == 0 && y == 48, "a body larger than the space", "centre");

	/* 4. The names the log uses. */
	check(kwl_layout_name(KWL_LAYOUT_DOCKED)[0] == 'd', "the docked mode's name", "name");
	check(kwl_layout_name(KWL_LAYOUT_WINDOWED)[0] == 'w', "the windowed mode's name", "name");

	/* The result. */
	if (failures != 0) {
		printf("host-layout: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-layout: ok (%d checks)\n", checks);
	return 0;
}
