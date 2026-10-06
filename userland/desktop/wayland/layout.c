/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the session's layout mode (ws142-p008, BUG-217; layout.h
 * says what the mode is).  Each rule answers one question the shell asks
 * at one moment: a switch to a window, a new window's first configure, a
 * window leaving fullscreen, a window to draw.
 */

#include "layout.h"

/*
 * Names a layout mode for the log (KWL LAYOUT).
 */
const char *
kwl_layout_name(
	unsigned mode)
{
	/* The docked mode is the tablet's. */
	if (mode == KWL_LAYOUT_DOCKED)
		return "docked";

	/* Every other value is the windowed mode, the session's first. */
	return "windowed";
}

/*
 * Says what a window switched to does to follow the mode (an application
 * switch: Alt+Tab, Wiseview, the bar, an activation): in the docked mode
 * it is docked, in the windowed mode it floats again at its place before.
 * A fullscreen window stays fullscreen (it comes back to the mode when it
 * leaves fullscreen), and a window with a parent floats over its parent,
 * which the caller makes follow the mode instead.
 */
unsigned
kwl_layout_switch_action(
	unsigned mode,
	const struct kwl_layout_window *window)
{
	/* A fullscreen window keeps covering the output. */
	if (window->fullscreen)
		return KWL_LAYOUT_KEEP;

	/* A dialog or a sheet is never docked; its parent follows the mode. */
	if (window->child)
		return KWL_LAYOUT_KEEP;

	/* The docked mode docks a floating window. */
	if (mode == KWL_LAYOUT_DOCKED && !window->docked)
		return KWL_LAYOUT_DOCK;

	/* The windowed mode brings a docked window back to floating. */
	if (mode != KWL_LAYOUT_DOCKED && window->docked)
		return KWL_LAYOUT_FLOAT;

	/* The window already is as the mode wants it. */
	return KWL_LAYOUT_KEEP;
}

/*
 * Tells whether a new window opens docked (its first configure is the
 * docked space): in the docked mode every window but a dialog, a sheet, a
 * fullscreen one and one docked already, a window of one size too (the
 * 2026-10-06 user decision: there is no window that cannot be docked).
 * Returns 1 when it opens docked.
 */
int
kwl_layout_opens_docked(
	unsigned mode,
	const struct kwl_layout_window *window)
{
	/* The windowed mode opens windows floating. */
	if (mode != KWL_LAYOUT_DOCKED)
		return 0;

	/* A dialog or a sheet floats over its parent. */
	if (window->child)
		return 0;

	/* A fullscreen window covers the output, and a docked one is docked already. */
	if (window->fullscreen)
		return 0;
	if (window->docked)
		return 0;

	/* Succeeded: the window opens docked. */
	return 1;
}

/*
 * Tells whether a window leaving fullscreen is docked (it comes back to
 * the session's mode, whatever it was when it went fullscreen; this
 * replaces BUG-208's own memory of it).  A dialog or a sheet floats.
 * Returns 1 when it is docked.
 */
int
kwl_layout_unfullscreen_docked(
	unsigned mode,
	const struct kwl_layout_window *window)
{
	/* The windowed mode brings it back floating. */
	if (mode != KWL_LAYOUT_DOCKED)
		return 0;

	/* A dialog or a sheet floats over its parent. */
	if (window->child)
		return 0;

	/* Succeeded: the window is docked. */
	return 1;
}

/*
 * Tells whether a window is drawn at its own size in the middle of the
 * docked space: a docked window of one size (its client does not draw the
 * docked size; the rest of the space is dark), and in the docked mode a
 * dialog or a sheet (the File Chooser among them, the 2026-10-06 user
 * decision).  Returns 1 when it is centred.
 */
int
kwl_layout_centred(
	unsigned mode,
	const struct kwl_layout_window *window)
{
	/* A docked window of one size keeps its size in the middle. */
	if (window->docked && window->fixed)
		return 1;

	/* In the docked mode, a dialog or a sheet shows in the middle of the screen. */
	if (mode == KWL_LAYOUT_DOCKED && window->child)
		return 1;

	/* Any other window is drawn where its place is. */
	return 0;
}

/*
 * Tells whether a window is left out of the scene: in the docked mode only
 * the current application's windows show (the 2026-10-06 user decision:
 * no other application is stacked under a docked one), except while an
 * overview shows them all (App Home, Wiseview, the switcher).  Returns 1
 * when the window is not drawn and takes no press.
 */
int
kwl_layout_hidden(
	unsigned mode,
	int same_application,
	int overview)
{
	/* The windowed mode shows every window. */
	if (mode != KWL_LAYOUT_DOCKED)
		return 0;

	/* The current application's windows show. */
	if (same_application)
		return 0;

	/* An overview shows every application. */
	if (overview)
		return 0;

	/* Succeeded: another application's window is hidden. */
	return 1;
}

/*
 * Places a body of a size in the middle of a space; one larger than the
 * space starts at the space's top-left corner.
 */
void
kwl_layout_centre(
	int32_t space_x,
	int32_t space_y,
	int32_t space_width,
	int32_t space_height,
	int32_t width,
	int32_t height,
	int32_t *x,
	int32_t *y)
{
	/* Across: the middle, never left of the space. */
	*x = space_x + (space_width - width) / 2;
	if (*x < space_x)
		*x = space_x;

	/* Down: the middle, never above the space. */
	*y = space_y + (space_height - height) / 2;
	if (*y < space_y)
		*y = space_y;
}
