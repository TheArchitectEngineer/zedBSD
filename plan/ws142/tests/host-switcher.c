/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the application switcher's state (ws142-p005,
 * userland/desktop/wayland/switcher.c with apps.c, compiled unchanged).
 *
 * Checks: no application, no switcher; one, it is selected; more, they are
 * in the bar's order and the current one is selected (BUG-209), the
 * leftmost when none is current or the index is out of range; steps one
 * icon to the right (or the left), around at both ends, also by many; the
 * pad's travel a step each ZWL_SWITCHER_STEP_UM, the rest kept, both ways;
 * the order kept while it is on, also when the bar's order changes; nothing
 * while off; letting Alt go after a quick Alt+Tab leaves it open and sticky
 * (BUG-209, 2026-10-06), after a slow one or a step brings.
 *
 *   plan/ws142/tests/run-host-switcher.sh
 */

#include "switcher.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);
static int selected_is(const struct zwl_switcher *switcher, const char *key);

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

/* Tells whether the selection is an application. */
static int
selected_is(
	const struct zwl_switcher *switcher,
	const char *key)
{
	const char *selected;
	int same;

	/* None selected matches no key. */
	selected = zwl_switcher_selected(switcher);
	if (selected == NULL)
		return 0;

	/* The same key. */
	same = strcmp(selected, key);
	if (same != 0)
		return 0;

	/* Succeeded: it is. */
	return 1;
}

/* Runs every case. */
int
main(void)
{
	static struct zwl_apps_window windows[8];
	static struct zwl_apps apps;
	static struct zwl_apps_order order;
	static struct zwl_switcher switcher;
	int error;
	int steps;
	int brings;

	/* 1. No application: it does not open. */
	zwl_apps_build(windows, 0, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, -1, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == -1 && !switcher.on, "no application: off");
	check(zwl_switcher_selected(&switcher) == NULL, "no application: nothing selected");

	/* 2. One application: it is selected. */
	windows[0].app_id = "files";
	windows[0].client = 1;
	windows[0].open_order = 1;
	windows[0].map_order = 1;
	zwl_apps_build(windows, 1, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_PAD, ZWL_SWITCHER_CENTER);
	check(error == 0 && switcher.on && switcher.index == 0U, "one application: on, it selected");
	check(switcher.via == ZWL_SWITCHER_VIA_PAD && switcher.placement == ZWL_SWITCHER_CENTER, "how and where kept");
	zwl_switcher_step(&switcher, 1);
	check(switcher.index == 0U, "one application: a step stays");

	/*
	 * 3. Three in the bar by their opening: files, terminal, notes.  The
	 * latest raised (notes, then files) must not decide the order: the
	 * current one (terminal, the middle icon) is selected.
	 */
	windows[0].map_order = 9;
	windows[1].app_id = "terminal";
	windows[1].client = 2;
	windows[1].open_order = 2;
	windows[1].map_order = 7;
	windows[2].app_id = "notes";
	windows[2].client = 3;
	windows[2].open_order = 3;
	windows[2].map_order = 11;
	zwl_apps_build(windows, 3, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, 1, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == 0 && switcher.count == 3U, "three applications");
	check(selected_is(&switcher, "terminal"), "the current application is selected");
	check(strcmp(switcher.keys[0], "files") == 0 &&
	      strcmp(switcher.keys[1], "terminal") == 0 &&
	      strcmp(switcher.keys[2], "notes") == 0, "the bar's order, not the latest use");

	/* 4. Steps one icon to the right, around at both ends. */
	zwl_switcher_step(&switcher, 1);
	check(selected_is(&switcher, "notes"), "a step to the right");
	zwl_switcher_step(&switcher, 1);
	check(selected_is(&switcher, "files"), "around past the right end to the left end");
	zwl_switcher_step(&switcher, -1);
	check(selected_is(&switcher, "notes"), "a step to the left around past the left end");
	zwl_switcher_step(&switcher, -7);
	check(selected_is(&switcher, "terminal"), "many steps to the left");
	zwl_switcher_step(&switcher, 8);
	check(selected_is(&switcher, "files"), "many steps to the right");

	/* 5. The pad's travel: a step each 12 mm, the rest kept, both ways. */
	steps = zwl_switcher_travel(&switcher, 6000);
	check(steps == 0 && selected_is(&switcher, "files"), "6 mm: no step yet");
	steps = zwl_switcher_travel(&switcher, 6000);
	check(steps == 1 && selected_is(&switcher, "terminal"), "12 mm in all: a step");
	steps = zwl_switcher_travel(&switcher, -25000);
	check(steps == -2 && selected_is(&switcher, "notes"), "25 mm left: two steps back");
	steps = zwl_switcher_travel(&switcher, 2000);
	check(steps == 0 && selected_is(&switcher, "notes"), "the rest and a little: no step");

	/* 6. The order kept while on, whatever the bar does (an icon dragged to the left end). */
	error = zwl_apps_move(&order, 2, 0);
	zwl_apps_build(windows, 3, &order, &apps);
	check(error == 0 && strcmp(apps.apps[0].key, "notes") == 0, "the bar's order changed");
	check(strcmp(switcher.keys[0], "files") == 0, "the order taken at the opening stays");

	/* 6b. Opened again: the new bar order; no current or one out of range is the leftmost. */
	error = zwl_switcher_open(&switcher, &apps, -1, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == 0 && selected_is(&switcher, "notes"), "no current application: the leftmost");
	error = zwl_switcher_open(&switcher, &apps, 3, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == 0 && selected_is(&switcher, "notes"), "out of range: the leftmost");
	error = zwl_switcher_open(&switcher, &apps, 2, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == 0 && selected_is(&switcher, "terminal"), "the current one at its new place");
	zwl_switcher_step(&switcher, 1);
	check(selected_is(&switcher, "notes"), "right of the right end: the left end");

	/* 7. Closed: nothing selected, no step, no travel. */
	zwl_switcher_close(&switcher);
	check(!switcher.on && zwl_switcher_selected(&switcher) == NULL, "closed: off");
	zwl_switcher_step(&switcher, 1);
	steps = zwl_switcher_travel(&switcher, 24000);
	check(steps == 0 && !switcher.on, "closed: no step, no travel");

	/*
	 * 8. Letting Alt go (BUG-209, the 2026-10-06 user instruction): a quick
	 * Alt+Tab without a step leaves the switcher open and sticky, and every
	 * later release brings nothing; a slow one, or one after a step, brings.
	 */
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	switcher.opened_ms = 1000U;
	brings = zwl_switcher_alt_released(&switcher, 1200U);
	check(error == 0 && brings == 0 && switcher.on && switcher.sticky, "a quick Alt+Tab: open, sticky");
	zwl_switcher_step(&switcher, 1);
	brings = zwl_switcher_alt_released(&switcher, 9000U);
	check(brings == 0 && switcher.on, "sticky: a later release (after a step) brings nothing");
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	switcher.opened_ms = 1000U;
	brings = zwl_switcher_alt_released(&switcher, 1000U + ZWL_SWITCHER_QUICK_MS);
	check(error == 0 && brings == 1 && !switcher.sticky, "Alt held the quick time or longer: brings");
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	switcher.opened_ms = 1000U;
	zwl_switcher_step(&switcher, 1);
	brings = zwl_switcher_alt_released(&switcher, 1100U);
	check(error == 0 && brings == 1 && switcher.steps == 1U, "a quick Alt+Tab+Tab: brings");
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_PAD, ZWL_SWITCHER_BAR);
	switcher.opened_ms = 1000U;
	steps = zwl_switcher_travel(&switcher, 13000);
	brings = zwl_switcher_alt_released(&switcher, 1100U);
	check(error == 0 && steps == 1 && brings == 1, "the pad's travel counts as a step");
	error = zwl_switcher_open(&switcher, &apps, 0, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	switcher.opened_ms = 5000U;
	brings = zwl_switcher_alt_released(&switcher, 4000U);
	check(error == 0 && brings == 0 && switcher.sticky, "a clock before the opening counts as quick");
	zwl_switcher_close(&switcher);
	brings = zwl_switcher_alt_released(&switcher, 9000U);
	check(brings == 0 && !switcher.on && !switcher.sticky, "closed: a release brings nothing");

	/* The result. */
	if (failures != 0) {
		printf("host-switcher: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-switcher: ok (%d checks)\n", checks);
	return 0;
}
