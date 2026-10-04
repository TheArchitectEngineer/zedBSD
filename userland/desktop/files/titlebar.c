/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's titlebar in zdesktop (WS070's CONTROLS presentation,
 * plan/ws070/titlebar-design.md §11): back, forward, home, the path, the
 * search field, the view (icons or list), the preview and, while
 * operations run, their progress.
 *
 * zdesktop draws the controls in the window's floating titlebar (in the
 * system bar while the window is maximized), makes them give way when the
 * room runs short (into its "..." popup, which also holds the menus), and
 * edits the text fields.  This file gives it the model and the window's
 * state (made by fm_ui_titlebar_state) in transactions, sending the state
 * only when it changed, and queues what zdesktop tells the window for the
 * main loop (fm_ui_titlebar).  The file manager needs zdesktop's titlebar:
 * without it the window does not start.
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

/* The controls always there, in their order (the progress is added while operations run). */
static const struct titlebar_control titlebar_controls[] = {
	{ FM_CONTROL_BACK, KEILAND_CONTROL_BACK, KEILAND_PRIORITY_PRIMARY, 0U, "Back" },
	{ FM_CONTROL_FORWARD, KEILAND_CONTROL_FORWARD, KEILAND_PRIORITY_PRIMARY, 0U, "Forward" },
	{ FM_CONTROL_HOME, KEILAND_CONTROL_HOME, KEILAND_PRIORITY_PRIMARY, 0U, "Home" },
	{ FM_CONTROL_PATH, KEILAND_CONTROL_BREADCRUMB, KEILAND_PRIORITY_NORMAL, 0U, "Location" },
	{ FM_CONTROL_SEARCH, KEILAND_CONTROL_SEARCH, KEILAND_PRIORITY_NORMAL, 0U, "Search" },
	{ FM_CONTROL_ICONS, KEILAND_CONTROL_VIEW_GRID, KEILAND_PRIORITY_SECONDARY, 1U, "Icons" },
	{ FM_CONTROL_LIST, KEILAND_CONTROL_VIEW_LIST, KEILAND_PRIORITY_SECONDARY, 1U, "List" },
	{ FM_CONTROL_PREVIEW, KEILAND_CONTROL_PREVIEW, KEILAND_PRIORITY_SECONDARY, 0U, "Preview" }
};

static void titlebar_activated(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static void titlebar_changed(void *data, struct keiland_titlebar *object, uint32_t id, const char *text);
static void titlebar_done(void *data, struct keiland_titlebar *object, uint32_t id, const char *text, unsigned how);
static void titlebar_drop_target(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail);
static void titlebar_queue(struct fm_titlebar *titlebar, unsigned kind, uint32_t id, uint32_t detail, const char *text);
static int titlebar_build(struct fm_titlebar *titlebar);
static int titlebar_state(struct fm_titlebar *titlebar, const struct fm_titlebar_state *state);
static int titlebar_state_controls(struct keiland_titlebar *object, const struct fm_titlebar_state *state);
static int titlebar_state_progress(struct fm_titlebar *titlebar, const struct fm_titlebar_state *state);
static void titlebar_suggest(struct fm_titlebar *titlebar, const struct fm_titlebar_state *state);

/* What the titlebar tells the window: the controls chosen, the text fields' typing, and the part of the path a drag is over. */
static const struct keiland_titlebar_listener titlebar_listener = {
	titlebar_activated, titlebar_changed, titlebar_done, NULL, NULL, NULL, NULL, titlebar_drop_target
};

/*
 * Gives zdesktop the window's titlebar, showing a state.
 *
 * Returns 0, or an errno value (ENOTSUP for a compositor without the
 * titlebar) when the titlebar could not be made.
 */
int
fm_titlebar_open(
	struct fm_titlebar *titlebar,
	struct fm_window *window,
	const struct fm_titlebar_state *state)
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
	fm_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Tells the titlebar the window's state when it differs from what it
 * shows, and gives a text field the keyboard when the window asked for it
 * since.
 */
void
fm_titlebar_refresh(
	struct fm_titlebar *titlebar,
	const struct fm_titlebar_state *state)
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
		fm_log("TITLEBAR update-failed errno=%d", error);
}

/*
 * Takes the oldest thing done with the titlebar and not yet carried out;
 * returns 1, or 0 when nothing waits.
 */
int
fm_titlebar_take(
	struct fm_titlebar *titlebar,
	struct fm_titlebar_event *event)
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
fm_titlebar_close(
	struct fm_titlebar *titlebar)
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
	titlebar_queue(data, FM_TITLEBAR_ACTIVATED, id, detail, "");
}

/*
 * Queues the part of the path a drag and drop is over (id 0: none) with the
 * window's input, where the drag's own events are (dnd.c), so that the
 * interface knows the part before the drag's next enter or motion.
 */
static void
titlebar_drop_target(
	void *data,
	struct keiland_titlebar *object,
	uint32_t id,
	uint32_t detail)
{
	struct fm_titlebar *titlebar;
	struct fm_event *event;

	/* The event, with the window's input. */
	(void)object;
	titlebar = data;
	event = fm_window_push(titlebar->window, FM_EVENT_DROP_PART);
	if (event == NULL)
		return;
	event->action = id;
	event->button = detail;
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
	titlebar_queue(data, FM_TITLEBAR_CHANGED, id, 0U, text);
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
	titlebar_queue(data, FM_TITLEBAR_DONE, id, how, text);
}

/* Puts an event at the end of the queue; a full queue keeps only the newest text of a field. */
static void
titlebar_queue(
	struct fm_titlebar *titlebar,
	unsigned kind,
	uint32_t id,
	uint32_t detail,
	const char *text)
{
	struct fm_titlebar_event *event;

	/* A text typed replaces the text of the same field waiting at the end. */
	if (kind == FM_TITLEBAR_CHANGED && titlebar->event_count > 0U) {
		event = &titlebar->events[titlebar->event_count - 1U];
		if (event->kind == FM_TITLEBAR_CHANGED && event->id == id) {
			snprintf(event->text, sizeof(event->text), "%s", text);
			return;
		}
	}

	/* A full queue drops the event (sixteen in one round). */
	if (titlebar->event_count == FM_TITLEBAR_EVENTS)
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
	struct fm_titlebar *titlebar)
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

/* Shows a state in the titlebar in one transaction, then gives a text field the keyboard when asked; returns 0 or an errno value. */
static int
titlebar_state(
	struct fm_titlebar *titlebar,
	const struct fm_titlebar_state *state)
{
	const char *last;
	unsigned focused;
	unsigned mode;
	int asked;
	int error;

	/* The transaction. */
	error = keiland_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The controls' state, then the progress. */
	error = titlebar_state_controls(titlebar->titlebar, state);
	if (error == 0)
		error = titlebar_state_progress(titlebar, state);

	/* A refused change still ends the transaction, which is reported. */
	if (error != 0) {
		(void)keiland_titlebar_commit(titlebar->titlebar);
		return error;
	}

	/* The state is shown together. */
	error = keiland_titlebar_commit(titlebar->titlebar);
	if (error != 0)
		return error;

	/* A text field the window asked the keyboard for since the last state (the path is edited as text). */
	asked = 0;
	if (state->focus != FM_CONTROL_NONE && state->focus_serial != titlebar->shown.focus_serial)
		asked = 1;
	if (asked != 0) {
		mode = KEILAND_FOCUS_FIELD;
		if (state->focus == FM_CONTROL_PATH)
			mode = KEILAND_FOCUS_EDIT;
		error = keiland_titlebar_focus_control(titlebar->titlebar, state->focus, mode);
		if (error != 0)
			fm_log("TITLEBAR focus-failed id=%u errno=%d", state->focus, error);
	}

	/* The path's field's suggestions, once for each list made (ws127-p010). */
	if (state->suggest_serial != titlebar->shown.suggest_serial)
		titlebar_suggest(titlebar, state);

	/* The log line the tests read: the last part, and the field given the keyboard now (0 for none). */
	last = "-";
	if (state->part_count > 0)
		last = state->parts[state->part_count - 1];
	focused = FM_CONTROL_NONE;
	if (asked != 0)
		focused = state->focus;
	fm_log("TITLEBAR state back=%d forward=%d parts=%d last=%s view=%u preview=%d progress=%d query=%s focus=%u",
	       state->can_back, state->can_forward, state->part_count, last, state->view, state->preview, state->progress, state->query, focused);

	/* Succeeded: the titlebar shows the state. */
	titlebar->shown = *state;
	titlebar->sent = 1;
	return 0;
}

/* Sets the controls that are always there; returns 0 or the first refusal. */
static int
titlebar_state_controls(
	struct keiland_titlebar *object,
	const struct fm_titlebar_state *state)
{
	const char *parts[FM_CRUMBS];
	int index;
	int error;

	/* The history's steps; Home always works. */
	error = keiland_titlebar_set_control_state(object, FM_CONTROL_BACK, state->can_back, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, FM_CONTROL_FORWARD, state->can_forward, 0);

	/* The path's parts, and the folder its field starts from. */
	for (index = 0; index < state->part_count; index++)
		parts[index] = state->parts[index];
	if (error == 0)
		error = keiland_titlebar_set_breadcrumb(object, FM_CONTROL_PATH, parts, (size_t)state->part_count);
	if (error == 0)
		error = keiland_titlebar_set_control_text(object, FM_CONTROL_PATH, state->path, "Go to folder");

	/* The search's query. */
	if (error == 0)
		error = keiland_titlebar_set_control_text(object, FM_CONTROL_SEARCH, state->query, "Search");

	/* The view shown and the preview, checked. */
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, FM_CONTROL_ICONS, 1, state->view == FM_VIEW_ICONS);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, FM_CONTROL_LIST, 1, state->view == FM_VIEW_LIST);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, FM_CONTROL_PREVIEW, 1, state->preview);

	/* Reports the first refusal, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the controls show the state. */
	return 0;
}

/* Adds the progress when operations start, sets its share while they run, and removes it when they end. */
static int
titlebar_state_progress(
	struct fm_titlebar *titlebar,
	const struct fm_titlebar_state *state)
{
	int shown;
	int error;

	/* Whether the titlebar has it now. */
	shown = 0;
	if (titlebar->sent != 0 && titlebar->shown.progress != FM_TITLEBAR_NO_PROGRESS)
		shown = 1;

	/* Nothing runs: it goes, when it was there. */
	if (state->progress == FM_TITLEBAR_NO_PROGRESS) {
		if (shown == 0)
			return 0;
		error = keiland_titlebar_remove_control(titlebar->titlebar, FM_CONTROL_PROGRESS);
		return error;
	}

	/* Operations run: it comes, when it was not there. */
	if (shown == 0) {
		error = keiland_titlebar_add_control(titlebar->titlebar, FM_CONTROL_PROGRESS, KEILAND_CONTROL_PROGRESS, KEILAND_PRIORITY_NORMAL, 0U, "Operations");
		if (error != 0)
			return error;
	}

	/* Their share done. */
	error = keiland_titlebar_set_control_value(titlebar->titlebar, FM_CONTROL_PROGRESS, (unsigned)state->progress);
	if (error != 0)
		return error;

	/* Succeeded: the progress shows. */
	return 0;
}

/* Gives the path's field the folders it suggests (none takes the list away); a refusal is only logged. */
static void
titlebar_suggest(
	struct fm_titlebar *titlebar,
	const struct fm_titlebar_state *state)
{
	const char *labels[FM_SUGGESTIONS];
	const char *texts[FM_SUGGESTIONS];
	int index;
	int error;

	/* The labels and the texts as the library takes them. */
	for (index = 0; index < state->suggest_count; index++) {
		labels[index] = state->suggest_labels[index];
		texts[index] = state->suggest_texts[index];
	}

	/* The request; a compositor without suggestions (ENOTSUP) leaves the field as it is. */
	error = keiland_titlebar_set_suggestions(titlebar->titlebar, FM_CONTROL_PATH, labels, texts, (size_t)state->suggest_count);
	if (error != 0)
		fm_log("TITLEBAR suggest-failed errno=%d", error);
}
