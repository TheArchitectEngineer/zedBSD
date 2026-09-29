/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The theme of the library: the Kei look's colours and sizes, the values
 * the file manager's specification set (plan/ws071/spec.md, Files'
 * files.h), so that every widget drawn with it matches Files.
 */

#include <keiui.h>

/*
 * The light theme, the only one so far.
 *
 * It is constant for the life of the program; kui_theme_default hands out
 * a pointer to it and nobody writes it.
 */
static const struct kui_theme theme_light = {
	KUI_RGB(0xeef2f7),		/* ground_top */
	KUI_RGB(0xe6ebf3),		/* ground_bottom */
	KUI_RGB(0xffffff),		/* panel */
	KUI_RGB(0xe2e7ef),		/* panel_edge */
	KUI_RGBA(0x1f3a66, 34),		/* shadow */
	KUI_RGBA(0xffffff, 40),		/* glass_sidebar */
	KUI_RGBA(0xffffff, 60),		/* glass_content */
	KUI_RGBA(0xffffff, 120),	/* sidebar */
	KUI_RGB(0x1e2632),		/* text */
	KUI_RGB(0x6b7585),		/* text_secondary */
	KUI_RGB(0xa3abb8),		/* text_faint */
	KUI_RGB(0x46526a),		/* icon */
	KUI_RGB(0x2f7cf6),		/* accent */
	KUI_RGBA(0x2f7cf6, 40),		/* selection */
	KUI_RGBA(0x7a8699, 38),		/* selection_inactive */
	KUI_RGBA(0x5a6b85, 18),		/* hover */
	KUI_RGB(0xe8ecf2),		/* separator */
	KUI_RGB(0x5aa2f5),		/* folder */
	KUI_RGB(0xe5484d),		/* danger */
	16.0f,				/* card_radius */
	8.0f,				/* control_radius */
	28,				/* row_height */
	13U,				/* text_body */
	12U,				/* text_small */
	15U				/* text_title */
};

/*
 * Reports the theme applications draw with (the light one).
 */
const struct kui_theme *
kui_theme_default(void)
{
	/* Succeeded: the light theme. */
	return &theme_light;
}
