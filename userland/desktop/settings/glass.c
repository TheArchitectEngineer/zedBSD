/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's glass in zdesktop (the file manager's, ws071-p015): the list
 * of pages and the page float as cards on zdesktop's frosted glass, and the
 * desktop shows between them.  zdesktop draws the glass, its rim
 * and the cards' shadows (keiland_glass_v1 through libkeiland); the frame
 * leaves its ground clear and only tints the cards.
 *
 * The window is glass when its swapchain is see-through and zdesktop has
 * glass; otherwise it keeps its own opaque ground.  The panels are worked
 * out from each frame's layout and sent before the frame is shown, only
 * when they changed, so that they take effect with that frame.
 */

#include "window.h"

#include <errno.h>
#include <string.h>

static int glass_same(const struct se_glass *glass, const struct se_panel *panels, size_t count);

/*
 * Makes the window glass when it can be: returns 1 when it is, 0 when it
 * keeps its opaque ground (a swapchain that is not see-through, or a
 * compositor without glass).
 */
int
se_glass_open(
	struct se_glass *glass,
	struct se_window *window,
	const struct se_present *present)
{
	int error;

	/* Nothing sent yet. */
	memset(glass, 0, sizeof(*glass));

	/* A frame that zdesktop does not blend cannot let the desktop through. */
	if (present->premultiplied == 0) {
		se_log("GLASS off reason=opaque");
		return 0;
	}

	/* zdesktop's glass for the window's surface. */
	glass->glass = keiland_glass_create(window->display, window->surface);
	if (glass->glass == NULL) {
		se_log("GLASS off reason=compositor errno=%d", errno);
		return 0;
	}

	/*
	 * Settings is not open all the time, so its glass shows the windows
	 * under it blurred (the user's choice of 2026-09-30, ws075-p029); a
	 * compositor whose glass has no such choice shows the blurred wallpaper.
	 */
	error = keiland_glass_set_blur(glass->glass, 1);
	se_log("GLASS blur=%d", error == 0);

	/* Succeeded: the window is glass. */
	se_log("GLASS on");
	return 1;
}

/*
 * Sends the panels of the frame just drawn, when they differ from those
 * sent last; they take effect with the frame's present.
 */
void
se_glass_refresh(
	struct se_glass *glass,
	struct se_app *app)
{
	struct keiland_glass_panel sent[SE_PANELS];
	struct se_panel panels[SE_PANELS];
	size_t count;
	size_t index;
	int same;
	int error;

	/* A window that is not glass has no panels. */
	if (glass->glass == NULL)
		return;

	/* The frame's panels, unless they are the ones zdesktop has. */
	count = se_ui_panels(app, panels, SE_PANELS);
	same = glass_same(glass, panels, count);
	if (same != 0)
		return;

	/* Each panel in zdesktop's terms. */
	for (index = 0; index < count; index++) {
		sent[index].x = panels[index].rect.x;
		sent[index].y = panels[index].rect.y;
		sent[index].width = panels[index].rect.width;
		sent[index].height = panels[index].rect.height;
		sent[index].radius = panels[index].radius;
		sent[index].kind = KEILAND_GLASS_CARD;
	}

	/* Sent with the frame; a refused list is logged and the old panels stay. */
	error = keiland_glass_set_panels(glass->glass, sent, count);
	if (error != 0) {
		se_log("GLASS refused errno=%d count=%lu", error, (unsigned long)count);
		return;
	}

	/* Remembered, to send again only what changes. */
	memcpy(glass->shown, panels, sizeof(panels[0]) * count);
	glass->shown_count = count;
	glass->sent = 1;
	se_log("GLASS panels count=%lu", (unsigned long)count);
}

/*
 * Takes the window's glass away.
 */
void
se_glass_close(
	struct se_glass *glass)
{
	/* The glass object, when there is one. */
	keiland_glass_destroy(glass->glass);
	glass->glass = NULL;
}

/* Tells whether a list of panels is the one sent last. */
static int
glass_same(
	const struct se_glass *glass,
	const struct se_panel *panels,
	size_t count)
{
	size_t index;

	/* Nothing sent yet, or another count. */
	if (glass->sent == 0)
		return 0;
	if (glass->shown_count != count)
		return 0;

	/* Any panel with another place, radius or kind. */
	for (index = 0; index < count; index++) {
		if (panels[index].rect.x != glass->shown[index].rect.x ||
		    panels[index].rect.y != glass->shown[index].rect.y ||
		    panels[index].rect.width != glass->shown[index].rect.width ||
		    panels[index].rect.height != glass->shown[index].rect.height)
			return 0;
		if (panels[index].radius != glass->shown[index].radius)
			return 0;
		if (panels[index].kind != glass->shown[index].kind)
			return 0;
	}

	/* The same list. */
	return 1;
}
