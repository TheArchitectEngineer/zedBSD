/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's titlebar in zdesktop (WS070's CONTROLS presentation): back,
 * forward, reload, and the location of the page (the parts of its path,
 * which zdesktop turns into a field for the whole URL when it is edited).
 *
 * zdesktop draws the controls and edits the field; this file gives it the
 * model and the browser's state in transactions, the same way as
 * zdesktop-files' titlebar, and queues what zdesktop tells the window for
 * the main loop.  A compositor without the titlebar leaves the window with
 * zdesktop's plain titlebar and the keyboard's shortcuts.
 */

#include "shell/internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The most parts of a path the location shows. */
#define TITLEBAR_PARTS		32U

/* The scheme the location's text starts with. */
#define TITLEBAR_FILE_SCHEME	"file://"

/*
 * One control of the model: its ID, role, priority and label.
 */
struct titlebar_control {
	uint32_t id;
	unsigned role;
	unsigned priority;
	const char *label;
};

/* The controls, in their order. */
static const struct titlebar_control titlebar_controls[] = {
	{ SHELL_CONTROL_BACK, ZDESKTOP_CONTROL_BACK, ZDESKTOP_PRIORITY_PRIMARY, "Back" },
	{ SHELL_CONTROL_FORWARD, ZDESKTOP_CONTROL_FORWARD, ZDESKTOP_PRIORITY_PRIMARY, "Forward" },
	{ SHELL_CONTROL_RELOAD, ZDESKTOP_CONTROL_GENERIC, ZDESKTOP_PRIORITY_NORMAL, "Reload" },
	{ SHELL_CONTROL_LOCATION, ZDESKTOP_CONTROL_BREADCRUMB, ZDESKTOP_PRIORITY_NORMAL, "Location" }
};

static void titlebar_activated(void *data, struct zdesktop_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void titlebar_done(void *data, struct zdesktop_titlebar *object, uint32_t id, const char *text, unsigned how);
static void titlebar_queue(struct shell_titlebar *titlebar, int kind, uint32_t id, uint32_t detail, const char *text);
static int titlebar_build(struct shell_titlebar *titlebar);
static int titlebar_state(struct shell_titlebar *titlebar, int can_back, int can_forward, const char *path);

/* What the titlebar tells the window: the controls chosen and the end of the location's editing. */
static const struct zdesktop_titlebar_listener titlebar_listener = {
	titlebar_activated, NULL, titlebar_done, NULL, NULL, NULL, NULL
};

/*
 * Gives zdesktop the window's titlebar with its controls.
 *
 * Returns 0, or an errno value (ENOTSUP for a compositor without the
 * titlebar).
 */
int
shell_titlebar_open(
	struct shell_titlebar *titlebar,
	struct shell_window *window)
{
	int error;

	/* Nothing yet. */
	memset(titlebar, 0, sizeof(*titlebar));

	/* The window's titlebar object. */
	titlebar->titlebar = zdesktop_titlebar_create(window->display, window->toplevel, &titlebar_listener, titlebar);
	if (titlebar->titlebar == NULL)
		return errno;

	/* The controls in one transaction. */
	error = titlebar_build(titlebar);
	if (error != 0)
		return error;

	/* Succeeded: the titlebar is zdesktop's to show. */
	return 0;
}

/*
 * Shows the browser's state: whether the history goes back and forward,
 * and the path of the page shown.
 */
int
shell_titlebar_show(
	struct shell_titlebar *titlebar,
	int can_back,
	int can_forward,
	const char *path)
{
	int error;

	/* Without a titlebar nothing is shown. */
	if (titlebar->titlebar == NULL)
		return 0;

	/* The state in one transaction. */
	error = titlebar_state(titlebar, can_back, can_forward, path);
	if (error != 0)
		return error;

	/* The line the tests read. */
	printf("ZBROWSER TITLEBAR back=%d forward=%d path=%s\n", can_back, can_forward, path);
	fflush(stdout);

	/* Succeeded: the titlebar shows the state. */
	return 0;
}

/*
 * Turns the location into a field for editing the URL, and gives it the
 * keyboard.
 */
int
shell_titlebar_edit_location(
	struct shell_titlebar *titlebar)
{
	int error;

	/* Without a titlebar there is no field. */
	if (titlebar->titlebar == NULL)
		return ENOTSUP;

	/* The field takes the keyboard. */
	error = zdesktop_titlebar_focus_control(titlebar->titlebar, SHELL_CONTROL_LOCATION, ZDESKTOP_FOCUS_EDIT);
	if (error != 0)
		return error;

	/* Succeeded: the URL can be edited. */
	return 0;
}

/*
 * Takes the oldest thing done with the titlebar and not yet carried out;
 * returns 1, or 0 when nothing waits.
 */
int
shell_titlebar_take(
	struct shell_titlebar *titlebar,
	struct shell_titlebar_event *event)
{
	/* Nothing waits. */
	if (titlebar->event_count == 0U)
		return 0;

	/* The oldest leaves the queue. */
	*event = titlebar->events[0];
	titlebar->event_count--;
	memmove(titlebar->events, titlebar->events + 1, titlebar->event_count * sizeof(titlebar->events[0]));

	/* Succeeded: an event to carry out. */
	return 1;
}

/*
 * Takes the titlebar away from zdesktop (before the window goes).
 */
void
shell_titlebar_close(
	struct shell_titlebar *titlebar)
{
	/* The titlebar object. */
	if (titlebar->titlebar != NULL)
		zdesktop_titlebar_destroy(titlebar->titlebar);

	/* Nothing is left. */
	memset(titlebar, 0, sizeof(*titlebar));
}

/* Queues a control chosen. */
static void
titlebar_activated(
	void *data,
	struct zdesktop_titlebar *object,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(seat);
	UNUSED_PARAMETER(serial);

	/* The event, for the main loop. */
	titlebar_queue(data, SHELL_TITLEBAR_ACTIVATED, id, detail, "");
}

/* Queues the end of the location's editing. */
static void
titlebar_done(
	void *data,
	struct zdesktop_titlebar *object,
	uint32_t id,
	const char *text,
	unsigned how)
{
	UNUSED_PARAMETER(object);

	/* The event, for the main loop. */
	titlebar_queue(data, SHELL_TITLEBAR_DONE, id, how, text);
}

/* Puts an event at the end of the queue; a full queue drops it. */
static void
titlebar_queue(
	struct shell_titlebar *titlebar,
	int kind,
	uint32_t id,
	uint32_t detail,
	const char *text)
{
	struct shell_titlebar_event *event;

	/* A full queue drops the event (sixteen in one round). */
	if (titlebar->event_count == SHELL_TITLEBAR_EVENTS)
		return;

	/* The event. */
	event = &titlebar->events[titlebar->event_count];
	event->kind = kind;
	event->id = id;
	event->detail = detail;
	snprintf(event->text, sizeof(event->text), "%s", text);
	titlebar->event_count++;
}

/* Gives zdesktop the mode and every control in one transaction; returns 0 or an errno value. */
static int
titlebar_build(
	struct shell_titlebar *titlebar)
{
	const struct titlebar_control *control;
	size_t index;
	int error;

	/* The transaction. */
	error = zdesktop_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The controls' presentation. */
	error = zdesktop_titlebar_set_mode(titlebar->titlebar, ZDESKTOP_TITLEBAR_CONTROLS);

	/* Each control in its order. */
	for (index = 0; error == 0 && index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		control = &titlebar_controls[index];
		error = zdesktop_titlebar_add_control(titlebar->titlebar, control->id, control->role, control->priority, 0U, control->label);
	}

	/* A refused change still ends the transaction, which is reported. */
	if (error != 0) {
		(void)zdesktop_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The controls are shown together. */
	error = zdesktop_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* Succeeded: the controls are there. */
	return 0;
}

/* Shows the history's steps and the page's location in one transaction; returns 0 or an errno value. */
static int
titlebar_state(
	struct shell_titlebar *titlebar,
	int can_back,
	int can_forward,
	const char *path)
{
	const char *parts[TITLEBAR_PARTS];
	char copy[SHELL_TITLEBAR_TEXT];
	char url[SHELL_TITLEBAR_TEXT + 8U];
	size_t count;
	char *part;
	char *next;
	int error;

	/* The path's parts, between its slashes (the first ones give way when there are too many). */
	snprintf(copy, sizeof(copy), "%s", path);
	count = 0;
	part = copy;
	while (part != NULL && *part != '\0') {
		next = strchr(part, '/');
		if (next != NULL) {
			*next = '\0';
			next++;
		}

		/* A part that is not empty is shown, the first one giving way when the list is full. */
		if (*part != '\0') {
			if (count == TITLEBAR_PARTS) {
				memmove(parts, parts + 1, (TITLEBAR_PARTS - 1U) * sizeof(parts[0]));
				count--;
			}

			/* The part goes at the end. */
			parts[count] = part;
			count++;
		}

		/* The part after it. */
		part = next;
	}

	/* The URL the field edits. */
	snprintf(url, sizeof(url), "%s%s", TITLEBAR_FILE_SCHEME, path);

	/* The transaction. */
	error = zdesktop_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The history's steps, the location's parts and its URL. */
	error = zdesktop_titlebar_set_control_state(titlebar->titlebar, SHELL_CONTROL_BACK, can_back, 0);
	if (error == 0)
		error = zdesktop_titlebar_set_control_state(titlebar->titlebar, SHELL_CONTROL_FORWARD, can_forward, 0);
	if (error == 0)
		error = zdesktop_titlebar_set_breadcrumb(titlebar->titlebar, SHELL_CONTROL_LOCATION, parts, count);
	if (error == 0)
		error = zdesktop_titlebar_set_control_text(titlebar->titlebar, SHELL_CONTROL_LOCATION, url, "File path or file: URL");

	/* A refused change still ends the transaction, which is reported. */
	if (error != 0) {
		(void)zdesktop_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The state is shown together. */
	error = zdesktop_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* Succeeded: the titlebar shows the state. */
	return 0;
}
