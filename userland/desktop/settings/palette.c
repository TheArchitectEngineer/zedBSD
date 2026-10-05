/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The colours of Settings in the light appearance and in the dark one
 * (ws089-p017).  The light set is the colours Settings always had; the
 * dark set turns the greys over -- a dark ground and cards, light text --
 * and keeps the accent and the news.  Its text on its cards and controls
 * keeps a contrast of 4.5 or more (plan/ws089/tests/host-dark.c).
 */

#include "settings.h"

/* The light appearance's colours. */
static const struct se_palette palette_light = {
	FM_RGB(0xeef2f7),		/* background_top */
	FM_RGB(0xe6ebf3),		/* background_bottom */
	FM_RGBA(0xffffff, 150),		/* panel */
	FM_RGBA(0xffffff, 170),		/* panel_edge */
	FM_RGBA(0xffffff, 40),		/* glass_sidebar */
	FM_RGBA(0xffffff, 60),		/* glass_page */
	FM_RGBA(0xffffff, 150),		/* card */
	FM_RGBA(0xffffff, 190),		/* card_edge */
	FM_RGBA(0xf4f7fb, 190),		/* tile */
	FM_RGBA(0xdfe7f3, 230),		/* tile_hover */
	FM_RGB(0x1e2632),		/* text */
	FM_RGB(0x56606f),		/* text_secondary */
	FM_RGB(0xa3abb8),		/* text_faint */
	FM_RGB(0x2e3a4c),		/* title */
	FM_RGB(0x46526a),		/* icon */
	FM_RGB(0x2f7cf6),		/* accent */
	FM_RGBA(0x2f7cf6, 40),		/* selection */
	FM_RGBA(0x7a8699, 38),		/* selection_inactive */
	FM_RGBA(0x5a6b85, 18),		/* hover */
	FM_RGBA(0x8a96aa, 60),		/* separator */
	FM_RGB(0x2fb45a),		/* good */
	FM_RGB(0xe0533d),		/* bad */
	FM_RGBA(0xffffff, 225),		/* control */
	FM_RGBA(0x8a96aa, 70),		/* control_edge */
	FM_RGB(0xffffff),		/* field */
	FM_RGB(0xc9d1dc),		/* track */
	FM_RGB(0xd3d9e2),		/* rail */
	FM_RGB(0xeef1f5),		/* faded */
	FM_RGB(0x1e2632),		/* pressed */
};

/* The dark appearance's colours. */
static const struct se_palette palette_dark = {
	FM_RGB(0x1b1f26),		/* background_top */
	FM_RGB(0x16191f),		/* background_bottom */
	FM_RGBA(0x262b34, 150),		/* panel */
	FM_RGBA(0x3a404b, 170),		/* panel_edge */
	FM_RGBA(0x000000, 40),		/* glass_sidebar */
	FM_RGBA(0x000000, 60),		/* glass_page */
	FM_RGBA(0x2a2f38, 150),		/* card */
	FM_RGBA(0x3a404b, 190),		/* card_edge */
	FM_RGBA(0x2a2f38, 190),		/* tile */
	FM_RGBA(0x353c48, 230),		/* tile_hover */
	FM_RGB(0xe9edf3),		/* text */
	FM_RGB(0xa9b2bf),		/* text_secondary */
	FM_RGB(0x646d7a),		/* text_faint */
	FM_RGB(0xdde3ec),		/* title */
	FM_RGB(0xc1c9d6),		/* icon */
	FM_RGB(0x2f7cf6),		/* accent */
	FM_RGBA(0x2f7cf6, 70),		/* selection */
	FM_RGBA(0x8a96aa, 50),		/* selection_inactive */
	FM_RGBA(0xffffff, 18),		/* hover */
	FM_RGBA(0x8a96aa, 50),		/* separator */
	FM_RGB(0x2fb45a),		/* good */
	FM_RGB(0xe0533d),		/* bad */
	FM_RGBA(0x2c313b, 225),		/* control */
	FM_RGBA(0x8a96aa, 80),		/* control_edge */
	FM_RGB(0x1f232a),		/* field */
	FM_RGB(0x4a515d),		/* track */
	FM_RGB(0x3f4652),		/* rail */
	FM_RGB(0x2a2e36),		/* faded */
	FM_RGB(0xffffff),		/* pressed */
};

/* The set in use: the light one until the compositor tells the dark appearance. */
const struct se_palette *se_palette = &palette_light;

/*
 * Uses the set of an appearance (KL_APPEARANCE_*; any other value is
 * light).
 */
void
se_palette_set(
	unsigned appearance)
{
	/* The appearance's set. */
	se_palette = se_palette_of(appearance);
}

/*
 * Reports the set of an appearance (KL_APPEARANCE_*; any other value is
 * light).
 */
const struct se_palette *
se_palette_of(
	unsigned appearance)
{
	/* The dark set. */
	if (appearance == KL_APPEARANCE_DARK)
		return &palette_dark;

	/* The light set. */
	return &palette_light;
}
