/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The titlebar of Text Editor in zdesktop (WS070's CONTROLS presentation;
 * plan/ws092/design.md sections 11 and 13): Open, Save, Undo, Redo and the
 * find field.
 *
 * zdesktop draws the controls and makes them give way when the room runs
 * short; a control chosen comes back as an action queued among the
 * window's inputs, and the find field's text as it is typed and when its
 * editing ends.  A compositor without the titlebar leaves the editor with
 * its menus and its keys (and F3 for finding again).
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The controls. */
#define CONTROL_OPEN		1U
#define CONTROL_SAVE		2U
#define CONTROL_UNDO		3U
#define CONTROL_REDO		4U
#define CONTROL_FIND		5U

/*
 * One control of the model: its ID, role, priority, label, and the action
 * it asks for.
 */
struct titlebar_control {
	uint32_t id;
	unsigned role;
	unsigned priority;
	const char *label;
	uint32_t action;
};

/* The controls, in their order. */
static const struct titlebar_control titlebar_controls[] = {
	{ CONTROL_OPEN, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_NORMAL, "Open", TE_ACTION_OPEN },
	{ CONTROL_SAVE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_PRIMARY, "Save", TE_ACTION_SAVE },
	{ CONTROL_UNDO, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, "Undo", TE_ACTION_UNDO },
	{ CONTROL_REDO, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, "Redo", TE_ACTION_REDO },
	{ CONTROL_FIND, KEILAND_CONTROL_SEARCH, KEILAND_PRIORITY_NORMAL, "Find", TE_ACTION_NONE }
};

static void titlebar_activated(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void titlebar_text_changed(void *data, struct keiland_titlebar *object, uint32_t id, const char *text);
static void titlebar_text_done(void *data, struct keiland_titlebar *object, uint32_t id, const char *text, unsigned how);
static int titlebar_build(struct te_titlebar *titlebar);
static int titlebar_state(struct te_titlebar *titlebar, const struct te_state *state);

/* What the titlebar tells the editor: the controls chosen, and the find field's text. */
static const struct keiland_titlebar_listener titlebar_listener = {
	titlebar_activated, titlebar_text_changed, titlebar_text_done, NULL, NULL, NULL, NULL, NULL
};

/*
 * Gives zdesktop the window's titlebar controls, showing a state.
 *
 * Returns 0, also when the compositor has no titlebar presentation, or an
 * errno value when the controls could not be made.
 */
int
te_titlebar_open(
	struct te_titlebar *titlebar,
	struct te_window *window,
	const struct te_state *state)
{
	int error;

	/* Nothing yet but the window. */
	memset(titlebar, 0, sizeof(*titlebar));
	titlebar->window = window;

	/* The window's titlebar object; a compositor without one leaves the menus and the keys. */
	titlebar->titlebar = keiland_titlebar_create(kui_window_display(window->kui), kui_window_toplevel(window->kui), &titlebar_listener, titlebar);
	if (titlebar->titlebar == NULL) {
		te_log("TITLEBAR none errno=%d", errno);
		return 0;
	}

	/* The controls in one transaction. */
	error = titlebar_build(titlebar);
	if (error != 0)
		return error;

	/* The state they show. */
	error = titlebar_state(titlebar, state);
	if (error != 0)
		return error;

	/* Succeeded: the titlebar is zdesktop's to show. */
	te_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Tells the titlebar the editor's state when it differs from what it
 * shows, and gives the find field the keyboard when the editor asked.
 */
void
te_titlebar_refresh(
	struct te_titlebar *titlebar,
	const struct te_state *state)
{
	int same;
	int error;

	/* Without a titlebar nothing is sent. */
	if (titlebar->titlebar == NULL)
		return;

	/* The find field takes the keyboard when asked. */
	if (titlebar->want_focus) {
		titlebar->want_focus = 0;
		error = keiland_titlebar_focus_control(titlebar->titlebar, CONTROL_FIND, KEILAND_FOCUS_FIELD);
		if (error != 0)
			te_log("TITLEBAR focus-failed errno=%d", error);
	}

	/* Nothing else when the state is the one shown. */
	same = memcmp(state, &titlebar->shown, sizeof(*state));
	if (same == 0 && titlebar->sent)
		return;

	/* The new state; a refusal is logged. */
	error = titlebar_state(titlebar, state);
	if (error != 0)
		te_log("TITLEBAR update-failed errno=%d", error);
}

/*
 * Takes the titlebar away from zdesktop (before the window goes).
 */
void
te_titlebar_close(
	struct te_titlebar *titlebar)
{
	/* The titlebar object, when there is one. */
	if (titlebar->titlebar != NULL)
		keiland_titlebar_destroy(titlebar->titlebar);
	memset(titlebar, 0, sizeof(*titlebar));
}

/* Queues the action of a chosen control. */
static void
titlebar_activated(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	uint32_t detail,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct te_titlebar *titlebar;
	size_t index;

	/* The titlebar whose control was chosen. */
	(void)object;
	(void)detail;
	(void)seat;
	titlebar = data;
	kui_window_set_serial(titlebar->window->kui, serial);
	te_log("TITLEBAR control=%u", id);

	/* The control's action, when it has one. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		if (titlebar_controls[index].id != id)
			continue;
		if (titlebar_controls[index].action != TE_ACTION_NONE)
			te_window_action(titlebar->window, titlebar_controls[index].action);
		return;
	}
}

/* Queues the find field's text as it is typed. */
static void
titlebar_text_changed(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	const char *text)
{
	struct te_titlebar *titlebar;
	struct te_event *event;

	/* Only the find field. */
	(void)object;
	titlebar = data;
	if (id != CONTROL_FIND)
		return;

	/* The text, as an input. */
	event = te_window_push(titlebar->window, TE_EVENT_FIND_TEXT);
	if (event == NULL)
		return;
	snprintf(event->text, sizeof(event->text), "%s", text);
}

/* Queues the end of the find field's editing: Enter finds the next place. */
static void
titlebar_text_done(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	const char *text,
	unsigned how)
{
	struct te_titlebar *titlebar;
	struct te_event *event;

	/* Only the find field. */
	(void)object;
	titlebar = data;
	if (id != CONTROL_FIND)
		return;

	/* How it ended, and its text. */
	event = te_window_push(titlebar->window, TE_EVENT_FIND_DONE);
	if (event == NULL)
		return;
	event->how = how;
	snprintf(event->text, sizeof(event->text), "%s", text);
}

/* Gives zdesktop the controls, in the controls presentation, in one transaction. */
static int
titlebar_build(
	struct te_titlebar *titlebar)
{
	const struct titlebar_control *control;
	size_t index;
	int error;

	/* The transaction and the presentation. */
	error = keiland_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;
	error = keiland_titlebar_set_mode(titlebar->titlebar, KEILAND_TITLEBAR_CONTROLS);

	/* Each control. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		if (error != 0)
			break;
		control = &titlebar_controls[index];
		error = keiland_titlebar_add_control(titlebar->titlebar, control->id, control->role, control->priority, 0U, control->label);
	}

	/* The find field's placeholder. */
	if (error == 0)
		error = keiland_titlebar_set_control_text(titlebar->titlebar, CONTROL_FIND, "", "Find");

	/* A refused control still ends the transaction. */
	if (error != 0) {
		(void)keiland_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The controls are shown together. */
	error = keiland_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* Succeeded: the controls are built. */
	return 0;
}

/* Shows a state in one transaction: Save enabled with unsaved changes, Undo and Redo when they can act. */
static int
titlebar_state(
	struct te_titlebar *titlebar,
	const struct te_state *state)
{
	struct keiland_titlebar *object;
	int error;

	/* The transaction. */
	object = titlebar->titlebar;
	error = keiland_titlebar_begin(object);
	if (error != 0)
		return error;

	/* The controls' states. */
	error = keiland_titlebar_set_control_state(object, CONTROL_SAVE, state->modified, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_UNDO, state->can_undo, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_REDO, state->can_redo, 0);

	/* A refused change still ends the transaction. */
	if (error != 0) {
		(void)keiland_titlebar_commit(object);
		return error;
	}

	/* The state is shown together. */
	error = keiland_titlebar_commit(object);
	if (error != 0)
		return error;

	/* Succeeded: the titlebar shows the state. */
	titlebar->shown = *state;
	titlebar->sent = 1;
	return 0;
}
