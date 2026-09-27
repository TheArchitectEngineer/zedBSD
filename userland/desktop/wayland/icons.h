/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The icons of the titlebar's controls (icons.c, WS070 p009): which there
 * are, and the call that draws one into a square of coverage.  The header
 * needs nothing of the compositor, so the host's tests build icons.c alone.
 */

#ifndef ZWL_ICONS_H
#define ZWL_ICONS_H

#include <stddef.h>
#include <stdint.h>

/*
 * The icons, in the order icons.c draws them.
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
	GLASS_ICON_COUNT
};

void zwl_icon_raster(unsigned icon, unsigned pixels, uint8_t *coverage, size_t stride);

#endif
