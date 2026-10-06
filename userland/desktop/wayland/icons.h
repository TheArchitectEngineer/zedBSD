/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The icons of the titlebar's controls (icons.c, WS070 p009) and of App
 * Home's applications (ws035-p123): which there are, the call that draws
 * one into a square of coverage, and the call that draws an application's
 * whole tile in colour with its picture cut out (ws128-p012).  The header
 * needs nothing of the compositor, so the host's tests build icons.c alone.
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
	/* The system bar's volume (ws100-p004): a speaker with no, one, two or three waves, and muted. */
	GLASS_ICON_VOLUME_0,
	GLASS_ICON_VOLUME_1,
	GLASS_ICON_VOLUME_2,
	GLASS_ICON_VOLUME_3,
	GLASS_ICON_VOLUME_MUTED,
	/* The system bar's removable media (ws132-p005; the USB trident, the 2026-10-05 user decision). */
	GLASS_ICON_USB,
	/*
	 * The system bar's Wi-Fi (ws099-p034, the 2026-10-06 user decision: the
	 * fan): the dot alone, then with one, two and three arcs over it.
	 */
	GLASS_ICON_WIFI_1,
	GLASS_ICON_WIFI_2,
	GLASS_ICON_WIFI_3,
	GLASS_ICON_WIFI_4,
	GLASS_ICON_APP_FILES,
	GLASS_ICON_APP_NOTES,
	GLASS_ICON_APP_TERMINAL,
	GLASS_ICON_APP_PDF,
	GLASS_ICON_APP_IMAGE,
	GLASS_ICON_APP_BROWSER,
	GLASS_ICON_APP_MODEL,
	GLASS_ICON_APP_GEARS,
	GLASS_ICON_APP_XTERM,
	GLASS_ICON_APP_LOCK,
	GLASS_ICON_APP_POWER,
	GLASS_ICON_APP_TEXT,
	GLASS_ICON_APP_SETTINGS,
	/* ws128-p012: the standard applications that had only their first letter. */
	GLASS_ICON_APP_VIDEO,
	GLASS_ICON_APP_PHONE,
	GLASS_ICON_APP_CALENDAR,
	GLASS_ICON_APP_MAIL,
	GLASS_ICON_APP_MONITOR,
	GLASS_ICON_COUNT
};

/* The first of App Home's pictures, and how many there are. */
#define GLASS_ICON_FIRST_APP	GLASS_ICON_APP_FILES
#define GLASS_ICON_APPS		(GLASS_ICON_COUNT - GLASS_ICON_FIRST_APP)

/* The largest application tile zwl_icon_tile draws, in pixels a side. */
#define GLASS_ICON_TILE_MOST	256U

/* The corner radius of an application's tile, a part of its side (glass.c fits the see-through window under it). */
#define GLASS_ICON_TILE_RADIUS	0.24f

void zwl_icon_raster(unsigned icon, unsigned pixels, uint8_t *coverage, size_t stride);
void zwl_icon_tile(unsigned icon, unsigned pixels, uint32_t *argb, size_t stride);
int zwl_icon_named(const char *name);
int zwl_icon_for_app_id(const char *app_id);

#endif
