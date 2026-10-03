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
static int titlebar_build_controls(struct keiland_titlebar *object);
static int titlebar_state(struct iv_titlebar *titlebar, const struct iv_state *state);
static int titlebar_state_controls(struct keiland_titlebar *object, const struct iv_state *state);

/* What the titlebar tells the viewer: the controls chosen. */
static const struct keiland_titlebar_listener titlebar_listener = {
	titlebar_activated,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
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
	titlebar->titlebar = keiland_titlebar_create(kui_window_display(window->kui), kui_window_toplevel(window->kui), &titlebar_listener, titlebar);
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

	/* Logs the titlebar for the tests. */
	iv_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));

	/* Succeeded: the titlebar is zdesktop's to show. */
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

	/* Nothing of the titlebar is left. */
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

	UNUSED_PARAMETER(object);
	UNUSED_PARAMETER(detail);
	UNUSED_PARAMETER(seat);
	UNUSED_PARAMETER(serial);

	/* The titlebar whose control was chosen. */
	titlebar = data;
	iv_log("TITLEBAR control=%u", id);

	/* The control's action, when it has one. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		/* Another control's entry. */
		if (titlebar_controls[index].id != id)
			continue;

		/* The chosen control's action, unless it has none (the place). */
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
	int error;

	/* The transaction. */
	error = keiland_titlebar_begin(titlebar->titlebar);
	if (error != 0)
		return error;

	/* The presentation and the controls; a refused one still ends the transaction. */
	error = titlebar_build_controls(titlebar->titlebar);
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

/* Sets the controls presentation and adds each control inside an open transaction; stops at the first refusal. */
static int
titlebar_build_controls(
	struct keiland_titlebar *object)
{
	const struct titlebar_control *control;
	size_t index;
	int error;

	/* The controls presentation, which zdesktop draws as pills that give way when the room runs short. */
	error = keiland_titlebar_set_mode(object, KEILAND_TITLEBAR_CONTROLS);
	if (error != 0)
		return error;

	/* Each control, in its order. */
	for (index = 0; index < sizeof(titlebar_controls) / sizeof(titlebar_controls[0]); index++) {
		/* The control with its role, priority, group and label. */
		control = &titlebar_controls[index];
		error = keiland_titlebar_add_control(object, control->id, control->role, control->priority, control->group, control->label);
		if (error != 0)
			return error;
	}

	/* Succeeded: every control is added. */
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
	int error;

	/* The transaction. */
	object = titlebar->titlebar;
	error = keiland_titlebar_begin(object);
	if (error != 0)
		return error;

	/* The controls' states; a refused change still ends the transaction. */
	error = titlebar_state_controls(object, state);
	if (error != 0) {
		(void)keiland_titlebar_commit(object);
		return error;
	}

	/* The state is shown together. */
	error = keiland_titlebar_commit(object);
	if (error != 0)
		return error;

	/* The titlebar shows this state from now on. */
	titlebar->shown = *state;
	titlebar->sent = 1;

	/* Succeeded: the titlebar shows the state. */
	return 0;
}

/* Sets the place's text and each control's state inside an open transaction; stops at the first refusal. */
static int
titlebar_state_controls(
	struct keiland_titlebar *object,
	const struct iv_state *state)
{
	char label[64];
	int can_previous;
	int can_next;
	int error;

	/* There is an image before the one shown. */
	can_previous = 0;
	if (state->has_image && state->index > 0)
		can_previous = 1;

	/* There is an image after it. */
	can_next = 0;
	if (state->has_image && state->index + 1 < state->count)
		can_next = 1;

	/* The image's place in its folder, or that there is none. */
	if (state->has_image) {
		snprintf(label, sizeof(label), "%lu / %lu", (unsigned long)(state->index + 1), (unsigned long)state->count);
	} else {
		snprintf(label, sizeof(label), "No image");
	}

	/* The place's text. */
	error = keiland_titlebar_set_control_label(object, CONTROL_PLACE, label);
	if (error != 0)
		return error;

	/* The previous image, when there is one. */
	error = keiland_titlebar_set_control_state(object, CONTROL_PREVIOUS, can_previous, 0);
	if (error != 0)
		return error;

	/* The next image, when there is one. */
	error = keiland_titlebar_set_control_state(object, CONTROL_NEXT, can_next, 0);
	if (error != 0)
		return error;

	/* The place, while an image is shown. */
	error = keiland_titlebar_set_control_state(object, CONTROL_PLACE, state->has_image, 0);
	if (error != 0)
		return error;

	/* Zooming out needs an image that can be shown. */
	error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_OUT, state->can_show, 0);
	if (error != 0)
		return error;

	/* So does zooming in. */
	error = keiland_titlebar_set_control_state(object, CONTROL_ZOOM_IN, state->can_show, 0);
	if (error != 0)
		return error;

	/* So does the fit, checked while the image follows the window. */
	error = keiland_titlebar_set_control_state(object, CONTROL_FIT, state->can_show, state->fit);
	if (error != 0)
		return error;

	/* So does a turn. */
	error = keiland_titlebar_set_control_state(object, CONTROL_ROTATE, state->can_show, 0);
	if (error != 0)
		return error;

	/* The full screen is always there, checked while the window fills it. */
	error = keiland_titlebar_set_control_state(object, CONTROL_FULLSCREEN, 1, state->fullscreen);
	if (error != 0)
		return error;

	/* Succeeded: every control shows the state. */
	return 0;
}
