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
 * Checks: no application, no switcher; one, it is selected; more, the one
 * used before the current one is selected (D2, the latest use first);
 * steps around at both ends, also by many; the pad's travel a step each
 * ZWL_SWITCHER_STEP_UM, the rest kept, both ways; the order kept while it
 * is on; nothing while off.
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
	return same == 0;
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

	/* 1. No application: it does not open. */
	zwl_apps_build(windows, 0, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == -1 && !switcher.on, "no application: off");
	check(zwl_switcher_selected(&switcher) == NULL, "no application: nothing selected");

	/* 2. One application: it is selected. */
	windows[0].app_id = "files";
	windows[0].client = 1;
	windows[0].open_order = 1;
	windows[0].map_order = 1;
	zwl_apps_build(windows, 1, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, ZWL_SWITCHER_VIA_PAD, ZWL_SWITCHER_CENTER);
	check(error == 0 && switcher.on && switcher.index == 0U, "one application: on, it selected");
	check(switcher.via == ZWL_SWITCHER_VIA_PAD && switcher.placement == ZWL_SWITCHER_CENTER, "how and where kept");
	zwl_switcher_step(&switcher, 1);
	check(switcher.index == 0U, "one application: a step stays");

	/* 3. Three: files (raised last), terminal (before), notes (first): terminal selected. */
	windows[0].map_order = 9;
	windows[1].app_id = "terminal";
	windows[1].client = 2;
	windows[1].open_order = 2;
	windows[1].map_order = 7;
	windows[2].app_id = "notes";
	windows[2].client = 3;
	windows[2].open_order = 3;
	windows[2].map_order = 3;
	zwl_apps_build(windows, 3, &order, &apps);
	error = zwl_switcher_open(&switcher, &apps, ZWL_SWITCHER_VIA_KEYS, ZWL_SWITCHER_BAR);
	check(error == 0 && switcher.count == 3U, "three applications");
	check(selected_is(&switcher, "terminal"), "the one used before the current one is selected");
	check(strcmp(switcher.keys[0], "files") == 0 && strcmp(switcher.keys[2], "notes") == 0, "the latest use first");

	/* 4. Steps, around at both ends. */
	zwl_switcher_step(&switcher, 1);
	check(selected_is(&switcher, "notes"), "a step on");
	zwl_switcher_step(&switcher, 1);
	check(selected_is(&switcher, "files"), "around past the end");
	zwl_switcher_step(&switcher, -1);
	check(selected_is(&switcher, "notes"), "around back past the start");
	zwl_switcher_step(&switcher, -7);
	check(selected_is(&switcher, "terminal"), "many steps back");
	zwl_switcher_step(&switcher, 8);
	check(selected_is(&switcher, "files"), "many steps on");

	/* 5. The pad's travel: a step each 12 mm, the rest kept, both ways. */
	steps = zwl_switcher_travel(&switcher, 6000);
	check(steps == 0 && selected_is(&switcher, "files"), "6 mm: no step yet");
	steps = zwl_switcher_travel(&switcher, 6000);
	check(steps == 1 && selected_is(&switcher, "terminal"), "12 mm in all: a step");
	steps = zwl_switcher_travel(&switcher, -25000);
	check(steps == -2 && selected_is(&switcher, "notes"), "25 mm left: two steps back");
	steps = zwl_switcher_travel(&switcher, 2000);
	check(steps == 0 && selected_is(&switcher, "notes"), "the rest and a little: no step");

	/* 6. The order kept while on, whatever the applications do. */
	windows[2].map_order = 20;
	zwl_apps_build(windows, 3, &order, &apps);
	check(strcmp(switcher.keys[0], "files") == 0, "the order taken at the opening stays");

	/* 7. Closed: nothing selected, no step, no travel. */
	zwl_switcher_close(&switcher);
	check(!switcher.on && zwl_switcher_selected(&switcher) == NULL, "closed: off");
	zwl_switcher_step(&switcher, 1);
	steps = zwl_switcher_travel(&switcher, 24000);
	check(steps == 0 && !switcher.on, "closed: no step, no travel");

	/* The result. */
	if (failures != 0) {
		printf("host-switcher: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-switcher: ok (%d checks)\n", checks);
	return 0;
}
