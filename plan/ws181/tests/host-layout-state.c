/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the windows' states, the end of the docked mode and the
 * check of a desktop's docked owner (WS181 p002, the 2026-10-07 UAT;
 * userland/desktop/wayland/layout.c compiled unchanged).
 *
 * Checks: the state of each kind of window in each mode, in front or
 * behind, minimized or not (docking's hiding is not a minimize); what the
 * end of the docked mode does to each kind (no docked window is left); and
 * what the owner's check finds for an owner gone, unmapped, minimized,
 * floating, carried along or sent away, on the desktop shown or not.
 *
 *   plan/ws181/tests/run-host-layout-state.sh
 */

#include "layout.h"

#include <stdio.h>
#include <string.h>

/* The kinds of window the tables name. */
#define FLOATING	0
#define DOCKED		1
#define FULLSCREEN	2
#define CHILD		3

/*
 * One row of the state table: a kind of window in a mode, minimized or
 * not, of the application in front or not, and the state it must be in.
 */
struct state_case {
	unsigned kind;
	unsigned mode;
	int minimized;
	int front;
	unsigned state;
	const char *what;
};

/*
 * One row of the owner table: the owner as the check sees it, its desktop
 * and the desktop shown, and what the check must find (and its reason).
 */
struct owner_case {
	struct kwl_layout_owner owner;
	unsigned desktop;
	unsigned shown;
	unsigned found;
	const char *reason;
	const char *what;
};

/* The state table (the 2026-10-07 UAT: docking's hiding and a minimize are two states). */
static const struct state_case state_cases[] = {
	{ FLOATING, KWL_LAYOUT_WINDOWED, 0, 1, KWL_LAYOUT_STATE_FLOATING, "windowed: a floating window in front floats" },
	{ FLOATING, KWL_LAYOUT_WINDOWED, 0, 0, KWL_LAYOUT_STATE_FLOATING, "windowed: another application's window floats" },
	{ FLOATING, KWL_LAYOUT_WINDOWED, 1, 0, KWL_LAYOUT_STATE_MINIMIZED, "windowed: a minimized window is minimized" },
	{ FULLSCREEN, KWL_LAYOUT_WINDOWED, 0, 1, KWL_LAYOUT_STATE_FULLSCREEN, "windowed: a fullscreen window" },
	{ DOCKED, KWL_LAYOUT_DOCKED, 0, 1, KWL_LAYOUT_STATE_DOCKED, "docked: the docked window in front" },
	{ DOCKED, KWL_LAYOUT_DOCKED, 0, 0, KWL_LAYOUT_STATE_DOCK_HIDDEN, "docked: another application's docked window is hidden by docking" },
	{ FLOATING, KWL_LAYOUT_DOCKED, 0, 0, KWL_LAYOUT_STATE_DOCK_HIDDEN, "docked: another application's floating window is hidden by docking" },
	{ FLOATING, KWL_LAYOUT_DOCKED, 0, 1, KWL_LAYOUT_STATE_FLOATING, "docked: the front application's other window floats under it" },
	{ FLOATING, KWL_LAYOUT_DOCKED, 1, 0, KWL_LAYOUT_STATE_MINIMIZED, "docked: a minimized window stays minimized, not dock-hidden" },
	{ DOCKED, KWL_LAYOUT_DOCKED, 1, 1, KWL_LAYOUT_STATE_MINIMIZED, "docked: a minimized docked window is minimized" },
	{ FULLSCREEN, KWL_LAYOUT_DOCKED, 0, 0, KWL_LAYOUT_STATE_FULLSCREEN, "docked: a fullscreen window" },
};

/* The owner table (WS181 design §1.4, step A). */
static const struct owner_case owner_cases[] = {
	{ { 0U, 1U, 0U, 1U, 0U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_KEEP, NULL, "a docked owner on the desktop shown stays" },
	{ { 0U, 1U, 0U, 0U, 1U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_KEEP, NULL, "a docked owner gone fullscreen stays" },
	{ { 1U, 0U, 0U, 0U, 0U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_LEAVE, "closed", "an owner destroyed on the desktop shown ends the mode" },
	{ { 0U, 0U, 0U, 1U, 0U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_LEAVE, "closed", "an owner unmapped on the desktop shown ends the mode" },
	{ { 0U, 1U, 1U, 1U, 0U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_LEAVE, "minimized", "an owner minimized on the desktop shown ends the mode" },
	{ { 0U, 1U, 0U, 0U, 0U, 0U }, 0U, 0U, KWL_LAYOUT_OWNER_LEAVE, "floated", "an owner floating again ends the mode" },
	{ { 1U, 0U, 0U, 0U, 0U, 1U }, 1U, 0U, KWL_LAYOUT_OWNER_FORGET, "closed", "an owner destroyed on a desktop not shown is forgotten" },
	{ { 0U, 1U, 1U, 1U, 0U, 1U }, 1U, 0U, KWL_LAYOUT_OWNER_FORGET, "minimized", "an owner minimized on a desktop not shown is forgotten" },
	{ { 0U, 1U, 0U, 1U, 0U, 1U }, 0U, 1U, KWL_LAYOUT_OWNER_MOVED, NULL, "an owner carried along to the desktop shown moves" },
	{ { 0U, 1U, 0U, 1U, 0U, 2U }, 0U, 0U, KWL_LAYOUT_OWNER_LEAVE, "moved", "an owner sent from the desktop shown ends the mode" },
	{ { 0U, 1U, 0U, 1U, 0U, 2U }, 1U, 0U, KWL_LAYOUT_OWNER_FORGET, NULL, "an owner moved between desktops not shown is forgotten" },
	{ { 0U, 1U, 0U, 1U, 0U, 1U }, 1U, 0U, KWL_LAYOUT_OWNER_KEEP, NULL, "a docked owner on a desktop not shown stays" },
};

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what, const char *rule);
static void describe(unsigned kind, struct kwl_layout_window *window);
static int same_reason(const char *found, const char *expected);

/* Runs every table. */
int
main(void)
{
	struct kwl_layout_window window;
	const char *reason;
	unsigned index;
	unsigned answer;
	int same;

	/* 1. The state of each kind of window. */
	for (index = 0U; index < sizeof(state_cases) / sizeof(state_cases[0]); index++) {
		describe(state_cases[index].kind, &window);
		answer = kwl_layout_state(state_cases[index].mode, &window, state_cases[index].minimized, state_cases[index].front);
		check(answer == state_cases[index].state, state_cases[index].what, "state");
	}

	/* 2. The names the log gives the states. */
	check(strcmp(kwl_layout_state_name(KWL_LAYOUT_STATE_DOCK_HIDDEN), "dock-hidden") == 0, "the name of docking's hiding", "state name");
	check(strcmp(kwl_layout_state_name(KWL_LAYOUT_STATE_MINIMIZED), "minimized") == 0, "the name of a minimize", "state name");
	check(strcmp(kwl_layout_state_name(KWL_LAYOUT_STATE_FLOATING), "floating") == 0, "the name of floating", "state name");

	/* 3. The end of the docked mode: no docked window is left (I1). */
	describe(DOCKED, &window);
	check(kwl_layout_leave_action(&window, 1) == KWL_LAYOUT_FLOAT, "the docked window brought back is animated", "leave");
	check(kwl_layout_leave_action(&window, 0) == KWL_LAYOUT_QUIET, "another docked window floats at once", "leave");
	describe(FLOATING, &window);
	check(kwl_layout_leave_action(&window, 0) == KWL_LAYOUT_KEEP, "a floating window stays", "leave");
	describe(FULLSCREEN, &window);
	check(kwl_layout_leave_action(&window, 0) == KWL_LAYOUT_KEEP, "a fullscreen window stays", "leave");
	describe(CHILD, &window);
	check(kwl_layout_leave_action(&window, 0) == KWL_LAYOUT_KEEP, "a dialog follows its parent", "leave");

	/* 4. The check of a desktop's docked owner. */
	for (index = 0U; index < sizeof(owner_cases) / sizeof(owner_cases[0]); index++) {
		answer = kwl_layout_owner_check(&owner_cases[index].owner, owner_cases[index].desktop, owner_cases[index].shown, &reason);
		check(answer == owner_cases[index].found, owner_cases[index].what, "owner");
		same = same_reason(reason, owner_cases[index].reason);
		check(same, owner_cases[index].what, "owner reason");
	}

	/* The result. */
	printf("WS181 host-layout-state checks=%d failures=%d\n", checks, failures);
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what,
	const char *rule)
{
	/* One more check ran. */
	checks++;

	/* A failed check is named. */
	if (!condition) {
		failures++;
		printf("FAIL %s: %s\n", rule, what);
	}
}

/* Describes a kind of window of the tables as the rules see it. */
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
	if (kind == DOCKED)
		window->docked = 1U;
	if (kind == FULLSCREEN)
		window->fullscreen = 1U;
	if (kind == CHILD)
		window->child = 1U;
}

/* Tells whether the reason the check gave is the one expected (both absent counts). */
static int
same_reason(
	const char *found,
	const char *expected)
{
	/* No reason expected: none given. */
	if (expected == NULL) {
		if (found == NULL)
			return 1;
		return 0;
	}

	/* A reason expected: one given. */
	if (found == NULL)
		return 0;

	/* Succeeded: whether the two words are the same. */
	if (strcmp(found, expected) == 0)
		return 1;
	return 0;
}
