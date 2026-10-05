/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The colours of Files in the light appearance and in the dark one
 * (ws089-p017).  The light set is the colours Files always had; the dark
 * set turns the greys over -- a dark ground and panels, light text -- and
 * keeps the accent and the folders' blue.  Its text on its panels keeps a
 * contrast of 4.5 or more (plan/ws089/tests/host-dark.c).
 */

#include "files.h"

#include <keiland.h>

/* The light appearance's colours. */
static const struct fm_palette palette_light = {
	FM_RGB(0xeef2f7),		/* background_top */
	FM_RGB(0xe6ebf3),		/* background_bottom */
	FM_RGB(0xffffff),		/* panel */
	FM_RGB(0xe2e7ef),		/* panel_edge */
	FM_RGBA(0xffffff, 170),		/* panel_rim */
	FM_RGBA(0xffffff, 120),		/* sidebar */
	FM_RGBA(0xffffff, 40),		/* glass_sidebar */
	FM_RGBA(0xffffff, 60),		/* glass_content */
	FM_RGBA(0x1f3a66, 34),		/* shadow */
	FM_RGB(0x1e2632),		/* text */
	FM_RGB(0x56606f),		/* text_secondary */
	FM_RGB(0xa3abb8),		/* text_faint */
	FM_RGB(0x2e3a4c),		/* title */
	FM_RGB(0x46526a),		/* icon */
	FM_RGB(0x2f7cf6),		/* accent */
	FM_RGBA(0x2f7cf6, 40),		/* selection */
	FM_RGBA(0x7a8699, 38),		/* selection_inactive */
	FM_RGBA(0x5a6b85, 18),		/* hover */
	FM_RGB(0x5aa2f5),		/* folder */
	FM_RGB(0xe8ecf2),		/* separator */
	FM_RGB(0xeef1f6),		/* button */
	FM_RGB(0xe2e7ef),		/* button_lit */
	FM_RGB(0xf4f6fa),		/* inner */
	FM_RGBA(0xffffff, 150),		/* tile */
	FM_RGB(0xe6ebf2),		/* rail */
};

/* The dark appearance's colours. */
static const struct fm_palette palette_dark = {
	FM_RGB(0x1b1f26),		/* background_top */
	FM_RGB(0x16191f),		/* background_bottom */
	FM_RGB(0x23272f),		/* panel */
	FM_RGB(0x343a45),		/* panel_edge */
	FM_RGBA(0x3a404b, 170),		/* panel_rim */
	FM_RGBA(0x1b1f26, 120),		/* sidebar */
	FM_RGBA(0x000000, 40),		/* glass_sidebar */
	FM_RGBA(0x000000, 60),		/* glass_content */
	FM_RGBA(0x000000, 70),		/* shadow */
	FM_RGB(0xe9edf3),		/* text */
	FM_RGB(0xa9b2bf),		/* text_secondary */
	FM_RGB(0x646d7a),		/* text_faint */
	FM_RGB(0xdde3ec),		/* title */
	FM_RGB(0xc1c9d6),		/* icon */
	FM_RGB(0x2f7cf6),		/* accent */
	FM_RGBA(0x2f7cf6, 70),		/* selection */
	FM_RGBA(0x8a96aa, 50),		/* selection_inactive */
	FM_RGBA(0xffffff, 18),		/* hover */
	FM_RGB(0x5aa2f5),		/* folder */
	FM_RGB(0x2e333c),		/* separator */
	FM_RGB(0x2f3540),		/* button */
	FM_RGB(0x3a414d),		/* button_lit */
	FM_RGB(0x2a2f38),		/* inner */
	FM_RGBA(0x2a2f38, 150),		/* tile */
	FM_RGB(0x3f4652),		/* rail */
};

/* The set in use: the light one until the compositor tells the dark appearance. */
const struct fm_palette *fm_palette = &palette_light;

/*
 * Uses the set of an appearance (KL_APPEARANCE_*; any other value is
 * light).
 */
void
fm_palette_set(
	unsigned appearance)
{
	/* The appearance's set. */
	fm_palette = fm_palette_of(appearance);
}

/*
 * Reports the set of an appearance (KL_APPEARANCE_*; any other value is
 * light).
 */
const struct fm_palette *
fm_palette_of(
	unsigned appearance)
{
	/* The dark set. */
	if (appearance == KL_APPEARANCE_DARK)
		return &palette_dark;

	/* The light set. */
	return &palette_light;
}
