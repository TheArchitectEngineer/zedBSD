/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The titlebar of Image Viewer in zdesktop (WS070's CONTROLS
 * presentation): the previous and the next image, where the image is in
 * its folder ("3 / 12"), the zoom, the fit, a turn and the full screen.
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
#define CONTROL_PLACE		3U
#define CONTROL_ZOOM_OUT	4U
#define CONTROL_ZOOM_IN		5U
#define CONTROL_FIT		6U
#define CONTROL_ROTATE		7U
#define CONTROL_FULLSCREEN	8U

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
 * segmented group as a pill with its label.
 */
static const struct titlebar_control titlebar_controls[] = {
	{ CONTROL_PREVIOUS, KEILAND_CONTROL_BACK, KEILAND_PRIORITY_PRIMARY, 0U, "Previous Image", IV_ACTION_PREVIOUS },
	{ CONTROL_NEXT, KEILAND_CONTROL_FORWARD, KEILAND_PRIORITY_PRIMARY, 0U, "Next Image", IV_ACTION_NEXT },
	{ CONTROL_PLACE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_NORMAL, 0U, "No image", IV_ACTION_NONE },
	{ CONTROL_ZOOM_OUT, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "\xe2\x88\x92", IV_ACTION_ZOOM_OUT },
	{ CONTROL_ZOOM_IN, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "+", IV_ACTION_ZOOM_IN },
	{ CONTROL_FIT, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "Fit", IV_ACTION_FIT },
	{ CONTROL_ROTATE, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_SECONDARY, 0U, "Rotate", IV_ACTION_ROTATE_RIGHT },
	{ CONTROL_FULLSCREEN, KEILAND_CONTROL_GENERIC, KEILAND_PRIORITY_PRIMARY, 0U, "Full Screen", IV_ACTION_FULLSCREEN }
};

static void titlebar_activated(void *data, struct keiland_titlebar *object, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
static int titlebar_build(struct iv_titlebar *titlebar);
static int titlebar_state(struct iv_titlebar *titlebar, const struct iv_state *state);

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
iv_titlebar_open(
	struct iv_titlebar *titlebar,
	struct iv_window *window,
	const struct iv_state *state)
{
	int error;

	/* Nothing yet but the window. */
	memset(titlebar, 0, sizeof(*titlebar));
	titlebar->window = window;

	/* The window's titlebar object; a compositor without one leaves the menus and the keys. */
	titlebar->titlebar = keiland_titlebar_create(window->display, window->toplevel, &titlebar_listener, titlebar);
	if (titlebar->titlebar == NULL) {
		iv_log("TITLEBAR none errno=%d", errno);
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
	iv_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Tells the titlebar the viewer's state when it differs from what it shows.
 */
void
iv_titlebar_refresh(
	struct iv_titlebar *titlebar,
	const struct iv_state *state)
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
		iv_log("TITLEBAR update-failed errno=%d", error);
}

/*
 * Takes the titlebar away from zdesktop (before the window goes).
 */
void
iv_titlebar_close(
	struct iv_titlebar *titlebar)
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
	struct iv_titlebar *titlebar;
	size_t index;

	/* The titlebar whose control was chosen. */
	(void)object;
	(void)detail;
	(void)seat;
	(void)serial;
	titlebar = data;
	iv_log("TITLEBAR control=%u", id);

	/* The control's action, when it has one. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		if (titlebar_controls[index].id != id)
			continue;
		if (titlebar_controls[index].action != IV_ACTION_NONE)
			iv_window_action(titlebar->window, titlebar_controls[index].action);
		return;
	}
}

/* Gives zdesktop the controls, in the controls presentation, in one transaction. */
static int
titlebar_build(
	struct iv_titlebar *titlebar)
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
 * Shows a state in one transaction: where the image is in its folder, the
 * controls that need an image or another image enabled, the fit and the
 * full screen checked.
 */
static int
titlebar_state(
	struct iv_titlebar *titlebar,
	const struct iv_state *state)
{
	struct keiland_titlebar *object;
	char label[64];
	int can_previous;
	int can_next;
	int error;

	/* Where the image is in the folder, as the controls show it. */
	can_previous = 0;
	if (state->has_image && state->index > 0)
		can_previous = 1;
	can_next = 0;
	if (state->has_image && state->index + 1 < state->count)
		can_next = 1;

	/* The image's place in its folder. */
	snprintf(label, sizeof(label), "No image");
	if (state->has_image)
		snprintf(label, sizeof(label), "%lu / %lu", (unsigned long)(state->index + 1), (unsigned long)state->count);

	/* The transaction. */
	object = titlebar->titlebar;
	error = keiland_titlebar_begin(object);
	if (error != 0)
		return error;

	/* The place's text and the controls' states. */
	error = keiland_titlebar_set_control_label(object, CONTROL_PLACE, label);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_PREVIOUS, can_previous, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_NEXT, can_next, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_PLACE, state->has_image, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_OUT, state->can_show, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_IN, state->can_show, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_FIT, state->can_show, state->fit);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_ROTATE, state->can_show, 0);
	if (error == 0)
		error = keiland_titlebar_set_control_state(object, CONTROL_FULLSCREEN, 1, state->fullscreen);

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
