/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's titlebar in zdesktop (WS070's CONTROLS presentation, the
 * file manager's way, ws071-p014): Back, Forward and Home, the breadcrumb
 * (Settings and the page), the search field (ws089-p008) and the list of
 * pages' switch, drawn by zdesktop
 * in the floating titlebar, or in the system bar while the window is
 * docked.  What the user does with them is queued for the main loop.
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * One control of the model: its ID, role, priority, segmented group and
 * label.
 */
struct titlebar_control {
	uint32_t id;
	unsigned role;
	unsigned priority;
	unsigned group;
	const char *label;
};

/* The controls, in their order. */
static const struct titlebar_control titlebar_controls[] = {
	{ SE_CONTROL_BACK, KEILAND_CONTROL_BACK, KEILAND_PRIORITY_PRIMARY, 0U, "Back" },
	{ SE_CONTROL_FORWARD, KEILAND_CONTROL_FORWARD, KEILAND_PRIORITY_PRIMARY, 0U, "Forward" },
	{ SE_CONTROL_HOME, KEILAND_CONTROL_HOME, KEILAND_PRIORITY_PRIMARY, 0U, "Home" },
	{ SE_CONTROL_PATH, KEILAND_CONTROL_BREADCRUMB, KEILAND_PRIORITY_NORMAL, 0U, "Location" },
	{ SE_CONTROL_SEARCH, KEILAND_CONTROL_SEARCH, KEILAND_PRIORITY_NORMAL, 0U, "Search" },
	{ SE_CONTROL_SIDEBAR, KEILAND_CONTROL_SIDEBAR, KEILAND_PRIORITY_SECONDARY, 0U, "Sidebar" }
};

static void titlebar_activated(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void titlebar_changed(void *data, struct keiland_titlebar *object, uint32_t id, const char *text);
static void titlebar_done(void *data, struct keiland_titlebar *object, uint32_t id, const char *text, unsigned how);
static void titlebar_queue(struct se_titlebar *titlebar, unsigned kind, uint32_t id, uint32_t detail, const char *text);
static int titlebar_build(struct se_titlebar *titlebar);
static int titlebar_state(struct se_titlebar *titlebar, const struct se_titlebar_state *state);
static int titlebar_state_controls(struct keiland_titlebar *object, const struct se_titlebar_state *state);

/* What the titlebar tells the window: the controls chosen and the text fields' typing. */
static const struct keiland_titlebar_listener titlebar_listener = {
	titlebar_activated, titlebar_changed, titlebar_done, NULL, NULL, NULL, NULL, NULL
};

/*
 * Gives zdesktop the window's titlebar, showing a state.
 *
 * Returns 0, or an errno value (ENOTSUP for a compositor without the
 * titlebar) when the titlebar could not be made.
 */
int
se_titlebar_open(
	struct se_titlebar *titlebar,
	struct se_window *window,
	const struct se_titlebar_state *state)
{
	int error;

	/* Nothing yet but the window. */
	memset(titlebar, 0, sizeof(*titlebar));
	titlebar->window = window;

	/* The window's titlebar object. */
	titlebar->titlebar = keiland_titlebar_create(window->display, window->toplevel, &titlebar_listener, titlebar);
	if (titlebar->titlebar == NULL)
		return errno;

	/* The controls in one transaction. */
	error = titlebar_build(titlebar);
	if (error != 0)
		return error;

	/* The state it shows. */
	error = titlebar_state(titlebar, state);
	if (error != 0)
		return error;

	/* Succeeded: the titlebar is zdesktop's to show. */
	se_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Tells the titlebar the window's state when it differs from what it
 * shows.
 */
void
se_titlebar_refresh(
	struct se_titlebar *titlebar,
	const struct se_titlebar_state *state)
{
	int same;
	int error;

	/* Without a titlebar nothing is sent. */
	if (titlebar->titlebar == NULL)
		return;

	/* Nor when the state is the one shown. */
	same = memcmp(state, &titlebar->shown, sizeof(*state));
	if (same == 0)
		return;

	/* The new state in one transaction; a refusal is reported and the titlebar stays as it was. */
	error = titlebar_state(titlebar, state);
	if (error != 0)
		se_log("TITLEBAR update-failed errno=%d", error);
}

/*
 * Takes the oldest thing done with the titlebar and not yet carried out;
 * returns 1, or 0 when nothing waits.
 */
int
se_titlebar_take(
	struct se_titlebar *titlebar,
	struct se_titlebar_event *event)
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
se_titlebar_close(
	struct se_titlebar *titlebar)
{
	/* The titlebar object. */
	if (titlebar->titlebar != NULL)
		keiland_titlebar_destroy(titlebar->titlebar);

	/* Nothing is left. */
	memset(titlebar, 0, sizeof(*titlebar));
}

/* Queues a control chosen. */
static void
titlebar_activated(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	/* The event, for the main loop. */
	(void)object;
	(void)seat;
	(void)serial;
	se_log("TITLEBAR activated id=%u detail=%u", id, detail);
	titlebar_queue(data, SE_TITLEBAR_ACTIVATED, id, detail, "");
}

/* Queues a text field's text as typed. */
static void
titlebar_changed(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	const char *text)
{
	/* The event, for the main loop. */
	(void)object;
	titlebar_queue(data, SE_TITLEBAR_CHANGED, id, 0U, text);
}

/* Queues the end of a text field's editing. */
static void
titlebar_done(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	const char *text,
	unsigned how)
{
	/* The event, for the main loop. */
	(void)object;
	titlebar_queue(data, SE_TITLEBAR_DONE, id, how, text);
}

/* Puts an event at the end of the queue; a full queue keeps only the newest text of a field. */
static void
titlebar_queue(
	struct se_titlebar *titlebar,
	unsigned kind,
	uint32_t id,
	uint32_t detail,
	const char *text)
{
	struct se_titlebar_event *event;

	/* A text typed replaces the text of the same field waiting at the end. */
	if (kind == SE_TITLEBAR_CHANGED && titlebar->event_count > 0U) {
		event = &titlebar->events[titlebar->event_count - 1U];
		if (event->kind == SE_TITLEBAR_CHANGED && event->id == id) {
			(void)snprintf(event->text, sizeof(event->text), "%s", text);
			return;
		}
	}

	/* A full queue drops the event (sixteen in one round). */
	if (titlebar->event_count == SE_TITLEBAR_EVENTS)
		return;

	/* The event. */
	event = &titlebar->events[titlebar->event_count];
	event->kind = kind;
	event->id = id;
	event->detail = detail;
	(void)snprintf(event->text, sizeof(event->text), "%s", text);
	titlebar->event_count++;
}

/* Gives zdesktop the mode and every control in one transaction; returns 0 or an errno value. */
static int
titlebar_build(
	struct se_titlebar *titlebar)
{
	const struct titlebar_control *control;
	size_t index;
	int error;

	/* The transaction. */
	error = keiland_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The controls' presentation. */
	error = keiland_titlebar_set_mode(titlebar->titlebar, KEILAND_TITLEBAR_CONTROLS);

	/* Each control in its order. */
	for (index = 0; error == 0 && index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		control = &titlebar_controls[index];
		error = keiland_titlebar_add_control(titlebar->titlebar, control->id, control->role, control->priority, control->group, control->label);
	}

	/* A refused change still ends the transaction, which is reported. */
	if (error != 0) {
		(void)keiland_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The controls are shown together. */
	error = keiland_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* Succeeded: the controls are there. */
	return 0;
}

/* Shows a state in the titlebar in one transaction, then gives the search field the keyboard when asked; returns 0 or an errno value. */
static int
titlebar_state(
	struct se_titlebar *titlebar,
	const struct se_titlebar_state *state)
{
	int asked;
	int error;

	/* The transaction. */
	error = keiland_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The controls' state. */
	error = titlebar_state_controls(titlebar->titlebar, state);

	/* A refused change still ends the transaction, which is reported. */
	if (error != 0) {
		(void)keiland_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The state is shown together. */
	error = keiland_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The search field, when the window asked for it to have the keyboard since the last state (Ctrl+F). */
	asked = 0;
	if (state->focus_serial != titlebar->shown.focus_serial)
		asked = 1;
	if (asked != 0) {
		error = keiland_titlebar_focus_control(titlebar->titlebar, SE_CONTROL_SEARCH, KEILAND_FOCUS_FIELD);
		if (error != 0)
			se_log("TITLEBAR focus-failed errno=%d", error);
	}

	/* The log line the tests read: the history's steps, the breadcrumb's last part, the query and the field given the keyboard now. */
	se_log("TITLEBAR state back=%d forward=%d parts=%d last=%s sidebar=%d query=%s focus=%d", state->can_back, state->can_forward, state->part_count, state->parts[state->part_count - 1], state->sidebar, state->query, asked);

	/* Succeeded: the titlebar shows the state. */
	titlebar->shown = *state;
	titlebar->sent = 1;
	return 0;
}

/* Sets the controls' state; returns 0 or the first refusal. */
static int
titlebar_state_controls(
	struct keiland_titlebar *object,
	const struct se_titlebar_state *state)
{
	const char *parts[SE_CRUMBS];
	int index;
	int error;

	/* The history's steps; Home always works. */
	error = keiland_titlebar_set_control_state(object, SE_CONTROL_BACK, state->can_back, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, SE_CONTROL_FORWARD, state->can_forward, 0);

	/* The breadcrumb's parts. */
	for (index = 0; index < state->part_count; index++)
		parts[index] = state->parts[index];
	if (error == 0)
		error = keiland_titlebar_set_breadcrumb(object, SE_CONTROL_PATH, parts, (size_t)state->part_count);

	/* The search's query, and what the field shows when empty. */
	if (error == 0)
		error = keiland_titlebar_set_control_text(object, SE_CONTROL_SEARCH, state->query, kl_tr("Search settings"));

	/* The list of pages, checked while it is shown. */
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, SE_CONTROL_SIDEBAR, 1, state->sidebar);

	/* Reports the first refusal, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the controls show the state. */
	return 0;
}
