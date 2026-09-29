/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's glass in zdesktop (Files' glass.c): the editor's card floats
 * on zdesktop's frosted glass, and the desktop shows around it.  zdesktop
 * draws the glass, its rim and the card's shadow (keiland_glass_v1 through
 * libkeiland); the frame leaves its ground clear around the card.
 *
 * The window is glass when its swapchain is see-through and zdesktop has
 * glass; otherwise it keeps its own opaque ground.  The card is sent
 * before the frame is shown, only when it changed.
 */

#include "window.h"

#include <errno.h>
#include <string.h>

/*
 * Makes the window glass when it can be: returns 1 when it is, 0 when it
 * keeps its opaque ground (a swapchain that is not see-through, or a
 * compositor without glass).
 */
int
te_glass_open(
	struct te_glass *glass,
	struct te_window *window,
	int see_through)
{
	/* Nothing sent yet. */
	memset(glass, 0, sizeof(*glass));

	/* A frame that zdesktop does not blend cannot let the desktop through. */
	if (!see_through) {
		te_log("GLASS off reason=opaque");
		return 0;
	}

	/* zdesktop's glass for the window's surface. */
	glass->glass = keiland_glass_create(kui_window_display(window->kui), kui_window_surface(window->kui));
	if (glass->glass == NULL) {
		te_log("GLASS off reason=compositor errno=%d", errno);
		return 0;
	}

	/* Succeeded: the window is glass. */
	te_log("GLASS on");
	return 1;
}

/*
 * Sends the card of the frame just drawn, when it differs from the one
 * sent last; it takes effect with the frame's present.
 */
void
te_glass_refresh(
	struct te_glass *glass,
	const struct te_app *app)
{
	struct keiland_glass_panel panel;
	struct te_rect card;
	int same;
	int error;

	/* A window that is not glass has no card. */
	if (glass->glass == NULL)
		return;

	/* The frame's card, unless it is the one zdesktop has. */
	te_app_card(app, &card);
	same = memcmp(&card, &glass->shown, sizeof(card));
	if (glass->sent && same == 0)
		return;

	/* The card in zdesktop's terms. */
	memset(&panel, 0, sizeof(panel));
	panel.x = card.x;
	panel.y = card.y;
	panel.width = card.width;
	panel.height = card.height;
	panel.radius = TE_CARD_RADIUS;
	panel.kind = KEILAND_GLASS_CARD;

	/* Sent with the frame; a refused card is logged and the old one stays. */
	error = keiland_glass_set_panels(glass->glass, &panel, 1U);
	if (error != 0) {
		te_log("GLASS refused errno=%d", error);
		return;
	}

	/* Remembered, to send again only what changes. */
	glass->shown = card;
	glass->sent = 1;
	te_log("GLASS card width=%d height=%d", card.width, card.height);
}

/*
 * Takes the window's glass away.
 */
void
te_glass_close(
	struct te_glass *glass)
{
	/* The glass object, when there is one. */
	keiland_glass_destroy(glass->glass);
	glass->glass = NULL;
}
