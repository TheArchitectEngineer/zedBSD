/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's glass in zdesktop (ws091, after the file manager's
 * glass.c): the images float on one frosted card inside the window, and
 * the chip, while it is shown, on a small card of its own; the desktop
 * shows around the card.  zdesktop draws the glass, its rim and the
 * cards' shadows (keiland_glass_v1 through libkeiland); the frame leaves
 * its ground clear.
 *
 * The window is glass when its swapchain is see-through and zdesktop has
 * glass; otherwise it keeps an opaque ground.  A fullscreen window has no
 * glass (its ground is black).  The panels are worked out from each
 * frame's layout and sent before the frame is shown, only when they
 * changed, so that they take effect with that frame.
 */

#include "window.h"

#include <errno.h>
#include <string.h>

static size_t glass_panels(const struct iv_app *app, struct keiland_glass_panel *panels, size_t capacity);
static int glass_same(const struct iv_glass *glass, const struct keiland_glass_panel *panels, size_t count);

/*
 * Makes the window glass when it can be: returns 1 when it is, 0 when it
 * keeps its opaque ground (a swapchain that is not see-through, or a
 * compositor without glass).
 */
int
iv_glass_open(
	struct iv_glass *glass,
	struct iv_window *window,
	const struct iv_present *present)
{
	/* Nothing sent yet. */
	memset(glass, 0, sizeof(*glass));

	/* A frame that zdesktop does not blend cannot let the desktop through. */
	if (present->premultiplied == 0) {
		iv_log("GLASS off reason=opaque");
		return 0;
	}

	/* zdesktop's glass for the window's surface. */
	glass->glass = keiland_glass_create(window->display, window->surface);
	if (glass->glass == NULL) {
		iv_log("GLASS off reason=compositor errno=%d", errno);
		return 0;
	}

	/* Succeeded: the window is glass. */
	iv_log("GLASS on");
	return 1;
}

/*
 * Sends the panels of the frame about to be shown, when they differ from
 * those sent last; they take effect with the frame's present.
 */
void
iv_glass_update(
	struct iv_glass *glass,
	const struct iv_app *app)
{
	struct keiland_glass_panel panels[2];
	size_t count;
	int same;
	int error;

	/* A window that is not glass has no panels. */
	if (glass->glass == NULL)
		return;

	/* The frame's panels, unless they are the ones zdesktop has. */
	count = glass_panels(app, panels, 2U);
	same = glass_same(glass, panels, count);
	if (same != 0)
		return;

	/* Sent with the frame; a refused list is logged and the old panels stay. */
	error = keiland_glass_set_panels(glass->glass, panels, count);
	if (error != 0) {
		iv_log("GLASS refused errno=%d count=%lu", error, (unsigned long)count);
		return;
	}

	/* Remembered, to send again only what changes. */
	memcpy(glass->panels, panels, sizeof(panels[0]) * count);
	glass->count = count;
	glass->sent = 1;
	iv_log("GLASS panels count=%lu", (unsigned long)count);
}

/*
 * Takes the window's glass away.
 */
void
iv_glass_close(
	struct iv_glass *glass)
{
	/* The glass object, when there is one. */
	if (glass->glass != NULL)
		keiland_glass_destroy(glass->glass);
	glass->glass = NULL;
}

/* Works out the frame's panels: the card (none when fullscreen) and the chip while it is drawn; returns how many. */
static size_t
glass_panels(
	const struct iv_app *app,
	struct keiland_glass_panel *panels,
	size_t capacity)
{
	size_t count;

	/* A fullscreen window has no glass. */
	count = 0;
	if (app->fullscreen)
		return 0;

	/* The card, inset from the window's edge. */
	if (count < capacity) {
		panels[count].x = IV_CARD_INSET;
		panels[count].y = IV_CARD_INSET;
		panels[count].width = app->window_width - 2 * IV_CARD_INSET;
		panels[count].height = app->window_height - 2 * IV_CARD_INSET;
		panels[count].radius = IV_CARD_RADIUS;
		panels[count].kind = KEILAND_GLASS_CARD;
		if (panels[count].width > 0 && panels[count].height > 0)
			count++;
	}

	/* The chip, where the frame drew it. */
	if (count < capacity && app->chip_width > 0 && app->chip_height > 0) {
		panels[count].x = app->chip_x;
		panels[count].y = app->chip_y;
		panels[count].width = app->chip_width;
		panels[count].height = app->chip_height;
		panels[count].radius = app->chip_height / 2;
		panels[count].kind = KEILAND_GLASS_CARD;
		count++;
	}

	/* Reports how many panels the frame has. */
	return count;
}

/* Tells whether a list of panels is the one sent last. */
static int
glass_same(
	const struct iv_glass *glass,
	const struct keiland_glass_panel *panels,
	size_t count)
{
	size_t index;
	int differs;

	/* Nothing sent yet, or another count. */
	if (glass->sent == 0)
		return 0;
	if (glass->count != count)
		return 0;

	/* Any panel with another place, radius or kind. */
	for (index = 0; index < count; index++) {
		differs = memcmp(&panels[index], &glass->panels[index], sizeof(panels[index]));
		if (differs != 0)
			return 0;
	}

	/* The same list. */
	return 1;
}
