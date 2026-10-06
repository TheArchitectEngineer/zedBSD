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
 * The controls are a table given to libkeiland (WS131 p016:
 * kl_window_set_controls); their state is their actions' (menu.c).
 * zdesktop draws the controls and makes them give way when the room runs
 * short; a control chosen comes back as a KL_WINDOW_ACTION input among the
 * window's, and the find field's text as KL_WINDOW_CONTROL_TEXT and
 * KL_WINDOW_CONTROL_DONE inputs.  A compositor without the titlebar leaves
 * the editor with its menus and its keys (and F3 for finding again).
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

/* The controls, in their order. */
static const struct kl_control_entry titlebar_controls[] = {
	{ CONTROL_OPEN, KL_CONTROL_GENERIC, KL_PRIORITY_NORMAL, 0U, "Open", TE_ACTION_OPEN },
	{ CONTROL_SAVE, KL_CONTROL_GENERIC, KL_PRIORITY_PRIMARY, 0U, "Save", TE_ACTION_SAVE },
	{ CONTROL_UNDO, KL_CONTROL_GENERIC, KL_PRIORITY_SECONDARY, 0U, "Undo", TE_ACTION_UNDO },
	{ CONTROL_REDO, KL_CONTROL_GENERIC, KL_PRIORITY_SECONDARY, 0U, "Redo", TE_ACTION_REDO },
	{ CONTROL_FIND, KL_CONTROL_SEARCH, KL_PRIORITY_NORMAL, 0U, "Find", TE_ACTION_NONE }
};

/*
 * Gives zdesktop the window's titlebar controls and the find field's
 * placeholder.
 *
 * Returns 0, also when the compositor has no titlebar presentation, or an
 * errno value when the controls could not be made.
 */
int
te_titlebar_open(
	struct te_titlebar *titlebar,
	struct te_window *window)
{
	int error;

	/* Nothing yet but the window. */
	memset(titlebar, 0, sizeof(*titlebar));
	titlebar->window = window;

	/* The controls; a compositor without the titlebar leaves the menus and the keys. */
	error = kl_window_set_controls(window->kui, titlebar_controls, sizeof(titlebar_controls) / sizeof(titlebar_controls[0]));
	if (error == ENOTSUP) {
		te_log("TITLEBAR none errno=%d", error);
		return 0;
	}
	if (error != 0)
		return error;
	titlebar->shown = 1;

	/* The find field's placeholder. */
	error = kl_window_set_control_text(window->kui, CONTROL_FIND, "", "Find");
	if (error != 0)
		return error;

	/* Succeeded: the titlebar is zdesktop's to show. */
	te_log("TITLEBAR ready controls=%u", (unsigned)(sizeof(titlebar_controls) / sizeof(titlebar_controls[0])));
	return 0;
}

/*
 * Gives the find field the keyboard when the editor asked.
 */
void
te_titlebar_refresh(
	struct te_titlebar *titlebar)
{
	int error;

	/* Nothing asked, or no titlebar. */
	if (!titlebar->want_focus || !titlebar->shown)
		return;

	/* The find field takes the keyboard. */
	titlebar->want_focus = 0;
	error = kl_window_focus_control(titlebar->window->kui, CONTROL_FIND);
	if (error != 0)
		te_log("TITLEBAR focus-failed errno=%d", error);
}

/*
 * Takes an input of the titlebar's: a control chosen (logged; its action
 * is the main loop's), or the find field's text as it is typed and when
 * its editing ends, as the editor's inputs.
 */
void
te_titlebar_input(
	struct te_titlebar *titlebar,
	const struct kl_window_event *input)
{
	struct te_event *event;

	/* A control chosen. */
	if (input->kind == KL_WINDOW_ACTION) {
		te_log("TITLEBAR control=%d", (int)input->id);
		return;
	}

	/* Only the find field's text. */
	if (input->id != (int32_t)CONTROL_FIND)
		return;

	/* As it is typed. */
	if (input->kind == KL_WINDOW_CONTROL_TEXT) {
		event = te_window_push(titlebar->window, TE_EVENT_FIND_TEXT);
		if (event != NULL)
			snprintf(event->text, sizeof(event->text), "%s", input->text);
		return;
	}

	/* How its editing ended, and its text. */
	if (input->kind == KL_WINDOW_CONTROL_DONE) {
		event = te_window_push(titlebar->window, TE_EVENT_FIND_DONE);
		if (event == NULL)
			return;
		event->how = input->code;
		snprintf(event->text, sizeof(event->text), "%s", input->text);
	}
}

/*
 * Takes the titlebar away from zdesktop (before the window goes).
 */
void
te_titlebar_close(
	struct te_titlebar *titlebar)
{
	/* No controls on the window, and nothing kept. */
	if (titlebar->window != NULL && titlebar->shown)
		(void)kl_window_set_controls(titlebar->window->kui, NULL, 0U);
	memset(titlebar, 0, sizeof(*titlebar));
}
