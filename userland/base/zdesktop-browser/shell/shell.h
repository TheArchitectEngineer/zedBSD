/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The shell of zdesktop-browser: the zdesktop window, its titlebar and
 * toolbar, the tabs and the input.
 *
 * main.c hands the window mode here.  The host tests build the engine
 * without this directory and supply their own shell_run that refuses.
 */

#ifndef ZDESKTOP_BROWSER_SHELL_H
#define ZDESKTOP_BROWSER_SHELL_H

#include "text/text.h"

/*
 * What the command line asked of the window: the display, the page to
 * open, the window's size and the fonts.
 */
struct shell_options {
	const char *display;
	const char *start;
	unsigned width;
	unsigned height;
	const struct text_font_paths *fonts;
};

int shell_run(const struct shell_options *options);

#endif
