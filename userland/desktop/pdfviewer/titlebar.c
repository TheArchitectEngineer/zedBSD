/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The titlebar of PDF Viewer in zdesktop (WS070's CONTROLS presentation):
 * the previous and the next page, where the view is ("Page 3 of 10"), the
 * two modes, the zoom, the two fits, and "Annotate in Notes".
 *
 * zdesktop draws the controls and makes them give way when the room runs
 * short (into its "..." popup, which also holds the menus); a control
 * chosen comes back as an action queued among the window's inputs.  A
 * compositor without the titlebar leaves the viewer with its menus and
 * its keys.
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The controls. */
#define CONTROL_PREVIOUS	1U
#define CONTROL_NEXT		2U
#define CONTROL_PAGE		3U
#define CONTROL_SCROLL		4U
#define CONTROL_PAGES		5U
#define CONTROL_ZOOM_OUT	6U
#define CONTROL_ZOOM_IN		7U
#define CONTROL_FIT_WIDTH	8U
#define CONTROL_FIT_PAGE	9U
#define CONTROL_ANNOTATE	10U

/*
 * One control of the model: its ID, role, priority, segmented group,
 * label, and the action it asks for.
 */
struct titlebar_control {
	uint32_t id;
	unsigned role;
	unsigned priority;
	unsigned group;
	const char *label;
	uint32_t action;
};

/*
 * The controls, in their order.  zdesktop draws a generic control outside a
 * segmented group as a pill with its label, so the zoom, the fits and
 * Annotate are ungrouped generic controls; the modes are the view pair.
 */
static const struct titlebar_control titlebar_controls[] = {
	{ CONTROL_PREVIOUS, KEILAND_CONTROL_BACK, KEILAND_PRIORITY_PRIMARY, 0U, "Previous Page", PV_ACTION_PREVIOUS },
	{ CONTROL_NEXT, KEILAND_CONTROL_FORWARD, KEILAND_PRIORITY_PRIMARY, 0U, "Next Page", PV_ACTION_NEXT },
	{ CONTROL_PAGE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_NORMAL, 0U, "No document", PV_ACTION_NONE },
	{ CONTROL_SCROLL, KEILAND_CONTROL_VIEW_LIST, KEILAND_PRIORITY_NORMAL, 1U, "Continuous Scroll", PV_ACTION_MODE_SCROLL },
	{ CONTROL_PAGES, KEILAND_CONTROL_VIEW_COLUMNS, KEILAND_PRIORITY_NORMAL, 1U, "Single Page", PV_ACTION_MODE_PAGE },
	{ CONTROL_ZOOM_OUT, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "\xe2\x88\x92", PV_ACTION_ZOOM_OUT },
	{ CONTROL_ZOOM_IN, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "+", PV_ACTION_ZOOM_IN },
	{ CONTROL_FIT_WIDTH, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "Fit Width", PV_ACTION_FIT_WIDTH },
	{ CONTROL_FIT_PAGE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "Fit Page", PV_ACTION_FIT_PAGE },
	{ CONTROL_ANNOTATE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_PRIMARY, 0U, "Annotate in Notes", PV_ACTION_ANNOTATE }
};

static void titlebar_activated(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static int titlebar_build(struct pv_titlebar *titlebar);
static int titlebar_state(struct pv_titlebar *titlebar, const struct pv_state *state);

/* What the titlebar tells the viewer: the controls chosen. */
static const struct keiland_titlebar_listener titlebar_listener = {
	titlebar_activated, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

/*
 * Gives zdesktop the window's titlebar controls, showing a state.
 *
 * Returns 0, also when the compositor has no titlebar presentation, or an
 * errno value when the controls could not be made.
 */
int
pv_titlebar_open(
	struct pv_titlebar *titlebar,
	struct pv_window *window,
	const struct pv_state *state)
{
	int error;

	/* Nothing yet but the window. */
	memset(titlebar, 0, sizeof(*titlebar));
	titlebar->window = window;

	/* The window's titlebar object; a compositor without one leaves the menus and the keys. */
	titlebar->titlebar = keiland_titlebar_create(window->display, window->toplevel, &titlebar_listener, titlebar);
	if (titlebar->titlebar == NULL) {
		pv_log("TITLEBAR none errno=%d", errno);
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
	pv_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Tells the titlebar the viewer's state when it differs from what it shows.
 */
void
pv_titlebar_refresh(
	struct pv_titlebar *titlebar,
	const struct pv_state *state)
{
	int same;
	int error;

	/* Without a titlebar nothing is sent. */
	if (titlebar->titlebar == NULL)
		return;

	/* Nor when the state is the one shown. */
	same = memcmp(state, &titlebar->shown, sizeof(*state));
	if (same == 0 && titlebar->sent)
		return;

	/* The new state; a refusal is logged. */
	error = titlebar_state(titlebar, state);
	if (error != 0)
		pv_log("TITLEBAR update-failed errno=%d", error);
}

/*
 * Takes the titlebar away from zdesktop (before the window goes).
 */
void
pv_titlebar_close(
	struct pv_titlebar *titlebar)
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
	struct pv_titlebar *titlebar;
	size_t index;

	/* The titlebar whose control was chosen. */
	(void)object;
	(void)detail;
	(void)seat;
	(void)serial;
	titlebar = data;
	pv_log("TITLEBAR control=%u", id);

	/* The control's action, when it has one. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		if (titlebar_controls[index].id != id)
			continue;
		if (titlebar_controls[index].action != PV_ACTION_NONE)
			pv_window_action(titlebar->window, titlebar_controls[index].action);
		return;
	}
}

/* Gives zdesktop the controls, in the controls presentation, in one transaction. */
static int
titlebar_build(
	struct pv_titlebar *titlebar)
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
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]) && error == 0; index++) {
		control = &titlebar_controls[index];
		error = keiland_titlebar_add_control(titlebar->titlebar, control->id, control->role, control->priority, control->group, control->label);
	}

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

/*
 * Shows a state in one transaction: the page's text, the controls that
 * need a document or another page enabled, the mode's and the fit's
 * controls checked.
 */
static int
titlebar_state(
	struct pv_titlebar *titlebar,
	const struct pv_state *state)
{
	struct keiland_titlebar *object;
	char label[64];
	int error;

	/* Where the view is. */
	snprintf(label, sizeof(label), "No document");
	if (state->has_document)
		snprintf(label, sizeof(label), "Page %lu of %lu", (unsigned long)(state->page + 1), (unsigned long)state->count);

	/* The transaction. */
	object = titlebar->titlebar;
	error = keiland_titlebar_begin(object);
	if (error != 0)
		return error;

	/* The page's text and the controls' states. */
	error = keiland_titlebar_set_control_label(object, CONTROL_PAGE, label);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_PREVIOUS, state->has_document && state->page > 0, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_NEXT, state->has_document && state->page + 1 < state->count, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_PAGE, state->has_document, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_SCROLL, 1, state->mode == PV_MODE_SCROLL);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_PAGES, 1, state->mode == PV_MODE_PAGE);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_OUT, state->has_document, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_IN, state->has_document, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_FIT_WIDTH, state->has_document, state->fit == PV_FIT_WIDTH);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_FIT_PAGE, state->has_document, state->fit == PV_FIT_PAGE);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ANNOTATE, state->has_document, 0);

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
