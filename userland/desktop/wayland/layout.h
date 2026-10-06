/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The session's layout mode (layout.c, ws142-p008, BUG-217): whether the
 * person uses the desktop with windows, or docked (maximized) as a
 * tablet.  It is one state of the session, not of a window: docking a
 * window makes it docked, bringing one back makes it windowed, and every
 * window switched to, opened or leaving fullscreen follows it.
 *
 * It knows nothing of the server: the caller says what a window is (docked,
 * fullscreen, with a parent, of one size) and acts on what the rules say
 * (shell.c).  So the host tests run it alone.
 */

#ifndef ZWL_LAYOUT_H
#define ZWL_LAYOUT_H

#include <stdint.h>

/* The session's layout mode: windows float, or every window switched to is docked under the system bar. */
#define ZWL_LAYOUT_WINDOWED		0U
#define ZWL_LAYOUT_DOCKED		1U

/* What a window switched to is to do: stay as it is, dock, or float again. */
#define ZWL_LAYOUT_KEEP			0U
#define ZWL_LAYOUT_DOCK			1U
#define ZWL_LAYOUT_FLOAT		2U

/*
 * A window as the layout's rules see it: whether it is docked, fullscreen,
 * has a parent (a dialog or a sheet, which floats over its parent and is
 * never docked), and has one size only (its smallest and largest sizes the
 * same: it is docked all the same, drawn at its size in the middle of the
 * docked space).
 */
struct zwl_layout_window {
	unsigned docked;
	unsigned fullscreen;
	unsigned child;
	unsigned fixed;
};

const char *zwl_layout_name(unsigned mode);
unsigned zwl_layout_switch_action(unsigned mode, const struct zwl_layout_window *window);
int zwl_layout_opens_docked(unsigned mode, const struct zwl_layout_window *window);
int zwl_layout_unfullscreen_docked(unsigned mode, const struct zwl_layout_window *window);
int zwl_layout_centred(unsigned mode, const struct zwl_layout_window *window);
int zwl_layout_hidden(unsigned mode, int same_application, int overview);
void zwl_layout_centre(int32_t space_x, int32_t space_y, int32_t space_width, int32_t space_height, int32_t width, int32_t height, int32_t *x, int32_t *y);

#endif
