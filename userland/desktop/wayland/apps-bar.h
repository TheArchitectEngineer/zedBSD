/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What the bar's applications (apps-bar.c, ws142-p004) share with the
 * switcher (switcher-shell.c, ws142-p005): the desktop's applications
 * gathered from its windows, the icons' places, the previews' layout, and
 * the bar's previews shown and hidden.
 */

#ifndef ZWL_APPS_BAR_H
#define ZWL_APPS_BAR_H

#include "zwl.h"
#include "apps.h"

#include <stdint.h>

/*
 * An icon's place in the bar and its mark's size, and the padding of the
 * applications' pill at its ends (pixels; ws099-p034: 26-pixel tiles 8
 * apart in one pill).
 */
#define ICON_WIDTH		34
#define ICON_MARK		26
#define ICON_PILL_PAD		6

/* The panel of previews: its distance under the bar, its padding, the gap between previews and the room for a preview's label (pixels). */
#define PANEL_DROP		8
#define PANEL_PAD		16
#define PREVIEW_GAP		16
#define PREVIEW_LABEL		46

/* The most windows looked at on a desktop. */
#define VIEW_WINDOWS		64U

/* A rectangle on the output. */
struct apps_rect {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

/* The desktop's applications this moment, and (with room in the bar) where their icons go. */
struct apps_view {
	struct zwl_apps apps;
	struct zwl_apps_window described[VIEW_WINDOWS];
	struct zwl_object *surfaces[VIEW_WINDOWS];
	unsigned window_count;
	int32_t left;
	int32_t right;
	unsigned shown;
	unsigned hidden;
	int current;
};

/* The previews of one application. */
struct apps_panel {
	int app;
	unsigned count;
	struct zwl_object *surfaces[ZWL_APPS_WINDOWS];
	struct apps_rect tiles[ZWL_APPS_WINDOWS];
	struct apps_rect rect;
};

int zwl_apps_view_build(struct zwl_server *server, struct apps_view *view);
int zwl_apps_view_collect(struct zwl_server *server, struct apps_view *view);
void zwl_apps_tiles_layout(struct zwl_server *server, const struct apps_view *view, unsigned found, struct apps_panel *panel);
int zwl_apps_tile_at(const struct apps_rect *tiles, unsigned count, int32_t x, int32_t y);
int zwl_apps_inside(const struct apps_rect *rect, int32_t x, int32_t y);
void zwl_apps_bar_show(struct zwl_server *server, const struct apps_view *view, const char *key, unsigned via);
void zwl_apps_bar_hide(struct zwl_server *server, const char *why);
int zwl_apps_bar_panel(struct zwl_server *server, const struct apps_view *view, const char *key, struct apps_panel *panel);

#endif
