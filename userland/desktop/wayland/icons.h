/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The icons of the titlebar's controls (icons.c, WS070 p009) and of App
 * Home's applications (ws035-p123): which there are, and the call that
 * draws one into a square of coverage.  The header needs nothing of the
 * compositor, so the host's tests build icons.c alone.
 */

#ifndef ZWL_ICONS_H
#define ZWL_ICONS_H

#include <stddef.h>
#include <stdint.h>

/*
 * The icons, in the order icons.c draws them: the titlebar's controls,
 * then from GLASS_ICON_FIRST_APP the pictures on App Home's tiles, which
 * are drawn larger (glass.c renders them at a size of their own).
 */
enum glass_icon {
	GLASS_ICON_BACK,
	GLASS_ICON_FORWARD,
	GLASS_ICON_UP,
	GLASS_ICON_HOME,
	GLASS_ICON_SEARCH,
	GLASS_ICON_GRID,
	GLASS_ICON_LIST,
	GLASS_ICON_COLUMNS,
	GLASS_ICON_SORT,
	GLASS_ICON_FILTER,
	GLASS_ICON_SIDEBAR,
	GLASS_ICON_PREVIEW,
	GLASS_ICON_PLUS,
	GLASS_ICON_CLOSE,
	GLASS_ICON_OVERFLOW,
	GLASS_ICON_APP_FILES,
	GLASS_ICON_APP_NOTES,
	GLASS_ICON_APP_TERMINAL,
	GLASS_ICON_APP_PDF,
	GLASS_ICON_APP_BROWSER,
	GLASS_ICON_APP_MODEL,
	GLASS_ICON_APP_GEARS,
	GLASS_ICON_APP_XTERM,
	GLASS_ICON_APP_LOCK,
	GLASS_ICON_APP_LOGOUT,
	GLASS_ICON_COUNT
};

/* The first of App Home's pictures, and how many there are. */
#define GLASS_ICON_FIRST_APP	GLASS_ICON_APP_FILES
#define GLASS_ICON_APPS		(GLASS_ICON_COUNT - GLASS_ICON_FIRST_APP)

void zwl_icon_raster(unsigned icon, unsigned pixels, uint8_t *coverage, size_t stride);
int zwl_icon_named(const char *name);

#endif
