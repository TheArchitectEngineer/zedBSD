/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The theme of the library: the Kei look's colours and sizes, the values
 * the file manager's specification set (plan/ws071/spec.md, Files'
 * files.h) and Settings' (plan/ws089, settings.h and widgets.c: the cards,
 * the controls, the switches), so that every widget drawn with it matches
 * Files and Settings.
 */

#include <keiland.h>

/*
 * The light theme, the only one so far.
 *
 * It is constant for the life of the program; kl_theme_default hands out
 * a pointer to it and nobody writes it.
 */
static const struct kl_theme theme_light = {
	KL_RGB(0xeef2f7),		/* ground_top */
	KL_RGB(0xe6ebf3),		/* ground_bottom */
	KL_RGB(0xffffff),		/* panel */
	KL_RGB(0xe2e7ef),		/* panel_edge */
	KL_RGBA(0x1f3a66, 34),		/* shadow */
	KL_RGBA(0xffffff, 40),		/* glass_sidebar */
	KL_RGBA(0xffffff, 60),		/* glass_content */
	KL_RGBA(0xffffff, 120),	/* sidebar */
	KL_RGB(0x1e2632),		/* text */
	KL_RGB(0x56606f),		/* text_secondary */
	KL_RGB(0xa3abb8),		/* text_faint */
	KL_RGB(0x46526a),		/* icon */
	KL_RGB(0x2f7cf6),		/* accent */
	KL_RGBA(0x2f7cf6, 40),		/* selection */
	KL_RGBA(0x7a8699, 38),		/* selection_inactive */
	KL_RGBA(0x5a6b85, 18),		/* hover */
	KL_RGB(0xe8ecf2),		/* separator */
	KL_RGB(0x5aa2f5),		/* folder */
	KL_RGB(0xe5484d),		/* danger */
	16.0f,				/* card_radius */
	8.0f,				/* control_radius */
	28,				/* row_height */
	13U,				/* text_body */
	12U,				/* text_small */
	15U,				/* text_title */
	KL_RGBA(0xffffff, 150),	/* card */
	KL_RGBA(0xffffff, 190),	/* card_edge */
	KL_RGBA(0x8a96aa, 60),		/* row_separator */
	KL_RGBA(0xffffff, 225),	/* control */
	KL_RGBA(0x8a96aa, 70),		/* control_edge */
	KL_RGB(0xc9d1dc),		/* track */
	KL_RGB(0x2fb45a),		/* good */
	KL_RGB(0xe0533d),		/* bad */
	32,				/* control_height */
	44,				/* switch_width */
	24				/* switch_height */
};

/*
 * Reports the theme applications draw with (the light one).
 */
const struct kl_theme *
kl_theme_default(void)
{
	/* Succeeded: the light theme. */
	return &theme_light;
}
