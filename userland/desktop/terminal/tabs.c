/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The terminal's tabs in zdesktop's titlebar (the Titlebar Presentation's
 * TABS mode, plan/ws070/titlebar-design.md, ws035-p086).
 *
 * Each tab is a shell of its own (main.c).  With one tab the titlebar
 * shows the menus (MENU mode); with two or more it shows the tabs, with
 * "+" for a new one and the menus under "..." (TABS mode).  zdesktop draws
 * them and tells the terminal a tab chosen, a tab's close button and "+";
 * those are queued here for the main loop.  The tabs are sent in one
 * transaction, and only when they changed.  Without zdesktop's titlebar
 * the terminal still has its tabs, switched from the Shell menu's keys.
 */

#include "terminal.h"

#include <stdio.h>
#include <string.h>

static void tabs_activated(void *data, struct keiland_titlebar *titlebar, uint32_t id, uint32_t serial);
static void tabs_close(void *data, struct keiland_titlebar *titlebar, uint32_t id);
static void tabs_new(void *data, struct keiland_titlebar *titlebar, uint32_t serial);
static void tabs_queue(struct terminal_window *window, unsigned kind, uint32_t id);
static int tabs_same(const struct terminal_window *window, const struct terminal_tab_view *tabs, unsigned count, uint32_t active);

/* What the titlebar tells the terminal: a tab chosen, a tab to close, "+". */
static const struct keiland_titlebar_listener tabs_listener = {
	NULL, NULL, NULL, tabs_activated, tabs_close, tabs_new, NULL, NULL
};

/*
 * Gives the window a titlebar presentation of its own, in menu mode until
 * it has two tabs.  A compositor without it leaves the terminal without
 * tabs in the titlebar.
 */
void
terminal_tabs_open(
	struct terminal_window *window)
{
	/* zdesktop's titlebar for the window (NULL without it). */
	window->titlebar = keiland_titlebar_create(window->display, window->toplevel, &tabs_listener, window);
	window->tabs_sent = 0;
	window->tab_request_count = 0;
}

/*
 * Shows the tabs and the active one: the menus while there is one tab, the
 * tabs (each closable, "+" after them) from two.  Only a change is sent.
 */
void
terminal_tabs_show(
	struct terminal_window *window,
	const struct terminal_tab_view *tabs,
	unsigned count,
	uint32_t active)
{
	struct keiland_titlebar *titlebar;
	unsigned index;
	unsigned shown;
	unsigned flags;
	unsigned mode;
	int found;
	int same;
	int error;

	/* Only with a titlebar, and only a change. */
	titlebar = window->titlebar;
	if (titlebar == NULL)
		return;
	same = tabs_same(window, tabs, count, active);
	if (same)
		return;

	/* One transaction. */
	error = keiland_titlebar_begin(titlebar);
	if (error != 0)
		return;

	/* The tabs shown before that are gone. */
	for (shown = 0; shown < window->tabs_shown_count; shown++) {
		/* Still there, by its ID. */
		found = 0;
		for (index = 0; index < count; index++) {
			/* The same tab. */
			if (tabs[index].id == window->tabs_shown[shown].id)
				found = 1;
		}

		/* Gone: removed. */
		if (!found)
			(void)keiland_titlebar_remove_tab(titlebar, window->tabs_shown[shown].id);
	}

	/* Each tab now, added when it is new, with its title and whether it is active. */
	for (index = 0; index < count; index++) {
		/* New since the last time. */
		found = 0;
		for (shown = 0; shown < window->tabs_shown_count; shown++) {
			/* The same tab. */
			if (window->tabs_shown[shown].id == tabs[index].id)
				found = 1;
		}

		/* A new one is added. */
		if (!found)
			(void)keiland_titlebar_add_tab(titlebar, tabs[index].id, tabs[index].title);

		/* Its title and flags. */
		flags = KEILAND_TAB_CLOSABLE;
		if (tabs[index].id == active)
			flags |= KEILAND_TAB_ACTIVE;
		(void)keiland_titlebar_set_tab(titlebar, tabs[index].id, tabs[index].title, flags);
	}

	/* "+", and the mode: the menus with one tab, the tabs from two. */
	(void)keiland_titlebar_set_tabs_options(titlebar, KEILAND_TABS_NEW_BUTTON);
	mode = KEILAND_TITLEBAR_MENU;
	if (count >= 2U)
		mode = KEILAND_TITLEBAR_TABS;
	(void)keiland_titlebar_set_mode(titlebar, mode);
	(void)keiland_titlebar_commit(titlebar);

	/* What was shown, to send only changes. */
	for (index = 0; index < count && index < TERMINAL_TABS; index++)
		window->tabs_shown[index] = tabs[index];
	window->tabs_shown_count = count;
	window->tabs_shown_active = active;
	window->tabs_sent = 1;
	printf("ZTERM TABS count=%u active=%u mode=%u\n", count, active, mode);
	fflush(stdout);
}

/*
 * Takes the oldest thing the titlebar asked of the tabs.  Returns 1 with
 * it, or 0 when nothing waits.
 */
int
terminal_tabs_take(
	struct terminal_window *window,
	struct terminal_tab_request *request)
{
	/* Nothing waits. */
	if (window->tab_request_count == 0U)
		return 0;

	/* The oldest, and the others move up. */
	*request = window->tab_requests[0];
	window->tab_request_count--;
	memmove(&window->tab_requests[0], &window->tab_requests[1], window->tab_request_count * sizeof(window->tab_requests[0]));

	/* Succeeded: one request. */
	return 1;
}

/* Takes the tabs away from the titlebar. */
void
terminal_tabs_close(
	struct terminal_window *window)
{
	/* The titlebar goes (the window shows its menus again). */
	if (window->titlebar != NULL)
		keiland_titlebar_destroy(window->titlebar);
	window->titlebar = NULL;
}

/* Queues a tab chosen in the titlebar (or with zdesktop's tab keys). */
static void
tabs_activated(
	void *data,
	struct keiland_titlebar *titlebar,
	uint32_t id,
	uint32_t serial)
{
	/* The request. */
	(void)titlebar;
	(void)serial;
	tabs_queue(data, TERMINAL_TAB_ACTIVATE, id);
}

/* Queues a tab's close button. */
static void
tabs_close(
	void *data,
	struct keiland_titlebar *titlebar,
	uint32_t id)
{
	/* The request. */
	(void)titlebar;
	tabs_queue(data, TERMINAL_TAB_CLOSE, id);
}

/* Queues "+". */
static void
tabs_new(
	void *data,
	struct keiland_titlebar *titlebar,
	uint32_t serial)
{
	/* The request. */
	(void)titlebar;
	(void)serial;
	tabs_queue(data, TERMINAL_TAB_NEW, 0U);
}

/* Queues one request for the main loop (a full queue drops it). */
static void
tabs_queue(
	struct terminal_window *window,
	unsigned kind,
	uint32_t id)
{
	/* A full queue: the user is far ahead. */
	if (window->tab_request_count == TERMINAL_ACTIONS)
		return;

	/* The request at the end. */
	window->tab_requests[window->tab_request_count].kind = kind;
	window->tab_requests[window->tab_request_count].id = id;
	window->tab_request_count++;
}

/* Tells whether tabs are the ones last shown (IDs, titles and the active one). */
static int
tabs_same(
	const struct terminal_window *window,
	const struct terminal_tab_view *tabs,
	unsigned count,
	uint32_t active)
{
	unsigned index;
	int same;

	/* Never shown, another count or another active tab. */
	if (!window->tabs_sent || count != window->tabs_shown_count || active != window->tabs_shown_active)
		return 0;

	/* Each tab. */
	for (index = 0; index < count; index++) {
		/* Another ID. */
		if (tabs[index].id != window->tabs_shown[index].id)
			return 0;

		/* Another title. */
		same = strcmp(tabs[index].title, window->tabs_shown[index].title);
		if (same != 0)
			return 0;
	}

	/* The same. */
	return 1;
}
