/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the desktop's applications (ws142-p004,
 * userland/desktop/wayland/apps.c, compiled unchanged).
 *
 * Checks: windows of one application ID are one application, a window
 * without one is its client's (D7); the bar's order is the opening order,
 * kept by the desktop's order, which a drag changes; a new application
 * comes on the right, one that has gone leaves the order; each desktop's
 * order is its own; the most recently used order (D2); an application's
 * windows the most recently raised first; an application is minimized only
 * when all its windows are (D11); the limits.
 *
 *   plan/ws142/tests/run-host-apps.sh
 */

#include "apps.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);
static void window(struct zwl_apps_window *windows, unsigned index, const char *app_id, uint64_t client, uint64_t open_order, uint64_t map_order, unsigned minimized);
static int bar_is(const struct zwl_apps *apps, const char *expected);

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

/* Describes one window. */
static void
window(
	struct zwl_apps_window *windows,
	unsigned index,
	const char *app_id,
	uint64_t client,
	uint64_t open_order,
	uint64_t map_order,
	unsigned minimized)
{
	/* Each field. */
	windows[index].app_id = app_id;
	windows[index].client = client;
	windows[index].open_order = open_order;
	windows[index].map_order = map_order;
	windows[index].minimized = minimized;
}

/* Tells whether the bar's applications are, from the left, the keys of a comma-separated list. */
static int
bar_is(
	const struct zwl_apps *apps,
	const char *expected)
{
	char line[512];
	size_t used;
	unsigned index;
	int same;

	/* The keys joined by commas. */
	line[0] = '\0';
	used = 0;
	for (index = 0; index < apps->count; index++) {
		if (index > 0U && used + 1U < sizeof(line)) {
			line[used] = ',';
			used++;
			line[used] = '\0';
		}

		/* The key after it. */
		(void)snprintf(line + used, sizeof(line) - used, "%s", apps->apps[index].key);
		used = strlen(line);
	}

	/* The same text. */
	same = strcmp(line, expected);
	if (same != 0)
		printf("  bar: %s (expected %s)\n", line, expected);
	return same == 0;
}

/* Runs every case. */
int
main(void)
{
	static struct zwl_apps_window windows[64];
	static struct zwl_apps apps;
	static struct zwl_apps_order first;
	static struct zwl_apps_order second;
	char key[ZWL_APPS_KEY];
	char name[ZWL_APPS_MAX + 8U][16];
	unsigned index;
	int found;
	int moved;

	/* 1. Two windows of Files (two clients), one without an ID, one minimized Terminal. */
	window(windows, 0, "files", 1, 1, 5, 0);
	window(windows, 1, "files", 2, 3, 2, 0);
	window(windows, 2, "", 3, 2, 7, 0);
	window(windows, 3, "terminal", 4, 4, 6, 1);
	zwl_apps_build(windows, 4, &first, &apps);
	check(apps.count == 3U, "three applications");
	check(bar_is(&apps, "files,client:3,terminal"), "the bar in the opening order");
	check(apps.apps[0].window_count == 2U, "files has both windows");
	check(apps.apps[0].windows[0] == 0U && apps.apps[0].windows[1] == 1U, "files' windows the latest raised first");
	check(apps.recent[0] == 1U && apps.recent[1] == 2U && apps.recent[2] == 0U, "the most recently used order");
	check(apps.apps[2].minimized == 1U, "terminal, all minimized, is minimized");
	check(apps.apps[0].minimized == 0U, "files is not");
	check(first.count == 3U, "the order holds the three");

	/* 2. A drag of terminal to the left end: kept by the next build. */
	moved = zwl_apps_move(&first, 2, 0);
	check(moved == 0, "the move is done");
	zwl_apps_build(windows, 4, &first, &apps);
	check(bar_is(&apps, "terminal,files,client:3"), "the dragged order is kept");
	moved = zwl_apps_move(&first, 0, 2);
	zwl_apps_build(windows, 4, &first, &apps);
	check(bar_is(&apps, "files,client:3,terminal"), "a drag to the right end");
	(void)zwl_apps_move(&first, 2, 0);
	zwl_apps_build(windows, 4, &first, &apps);

	/* 3. A new application comes on the right, whatever its raise. */
	window(windows, 4, "notes", 5, 9, 9, 0);
	zwl_apps_build(windows, 5, &first, &apps);
	check(bar_is(&apps, "terminal,files,client:3,notes"), "a new application on the right");
	check(apps.recent[0] == 3U, "the new one is the most recent");

	/* 4. Files gone: it leaves the order; back again, it comes on the right. */
	window(windows, 0, "terminal", 4, 4, 6, 1);
	window(windows, 1, "notes", 5, 9, 9, 0);
	zwl_apps_build(windows, 5, &first, &apps);
	check(bar_is(&apps, "terminal,client:3,notes"), "a gone application leaves the bar");
	check(first.count == 3U, "and the order");
	window(windows, 5, "files", 1, 10, 10, 0);
	zwl_apps_build(windows, 6, &first, &apps);
	check(bar_is(&apps, "terminal,client:3,notes,files"), "back, on the right");

	/* 5. Another desktop's order is its own. */
	window(windows, 0, "files", 1, 1, 5, 0);
	window(windows, 1, "files", 2, 3, 2, 0);
	zwl_apps_build(windows, 4, &second, &apps);
	check(bar_is(&apps, "files,client:3,terminal"), "the second desktop by opening");
	(void)zwl_apps_move(&second, 0, 1);
	zwl_apps_build(windows, 4, &second, &apps);
	check(bar_is(&apps, "client:3,files,terminal"), "the second desktop's drag");
	check(strcmp(first.keys[0], "terminal") == 0, "the first desktop's order unchanged");

	/* 6. A minimized window and a shown one: not minimized. */
	window(windows, 0, "files", 1, 1, 5, 1);
	zwl_apps_build(windows, 2, &second, &apps);
	check(apps.count == 1U && apps.apps[0].minimized == 0U, "one shown window keeps an application shown");

	/* 7. Moves outside the order are refused. */
	moved = zwl_apps_move(&second, 0, 5);
	check(moved == -1, "a move past the end is refused");
	moved = zwl_apps_move(&second, 7, 0);
	check(moved == -1, "a move from past the end is refused");

	/* 8. The keys, and finding by key. */
	zwl_apps_key("", 42, key, sizeof(key));
	check(strcmp(key, "client:42") == 0, "a window without an ID is its client's");
	zwl_apps_key(NULL, 7, key, sizeof(key));
	check(strcmp(key, "client:7") == 0, "no ID at all, the same");
	zwl_apps_key("org.example.Viewer", 7, key, sizeof(key));
	check(strcmp(key, "org.example.Viewer") == 0, "an ID is its own key");
	window(windows, 0, "files", 1, 1, 5, 0);
	zwl_apps_build(windows, 4, &second, &apps);
	found = zwl_apps_find(&apps, "terminal");
	check(found >= 0 && strcmp(apps.apps[found].key, "terminal") == 0, "found by key");
	found = zwl_apps_find(&apps, "nothing");
	check(found == -1, "an unknown key is not found");

	/* 9. More applications than the limit, and more windows of one than the limit. */
	memset(&second, 0, sizeof(second));
	for (index = 0; index < ZWL_APPS_MAX + 8U; index++) {
		(void)snprintf(name[index], sizeof(name[index]), "app%u", index);
		window(windows, index, name[index], index, index, index, 0);
	}

	/* Built. */
	zwl_apps_build(windows, ZWL_APPS_MAX + 8U, &second, &apps);
	check(apps.count == ZWL_APPS_MAX, "at most ZWL_APPS_MAX applications");
	for (index = 0; index < 20U; index++)
		window(windows, index, "many", 1, index, index, 0);
	zwl_apps_build(windows, 20, &second, &apps);
	check(apps.count == 1U && apps.apps[0].window_count == ZWL_APPS_WINDOWS, "at most ZWL_APPS_WINDOWS windows of one");
	check(apps.apps[0].windows[0] == 19U, "the latest raised among them first");
	check(apps.apps[0].map_order == 19U, "the latest raise counts the left-out ones too");

	/* 10. No windows: no applications, an empty order. */
	zwl_apps_build(windows, 0, &second, &apps);
	check(apps.count == 0U && second.count == 0U, "nothing");

	/* The result. */
	if (failures != 0) {
		printf("host-apps: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-apps: ok (%d checks)\n", checks);
	return 0;
}
