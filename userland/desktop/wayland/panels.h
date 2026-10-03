/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A window's glass panels (keiland_glass_v1, panels.c): the parts of a
 * surface that stand on the system's frosted glass, shared with the
 * drawing of windows (shell.c).
 */

#ifndef ZWL_PANELS_H
#define ZWL_PANELS_H

#include "glass.h"

/* The most panels a surface may have. */
#define ZWL_PANELS_MAX		32U

/* The kind of panel (the only one so far): a card floating in the window. */
#define ZWL_PANEL_CARD		0U

/* The largest corner radius a panel may ask for. */
#define ZWL_PANEL_RADIUS_MAX	64

/*
 * One glass panel, in surface coordinates: its rectangle, its corners'
 * radius and its kind.
 */
struct zwl_panel {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t radius;
	uint32_t kind;
};

/*
 * A surface's glass panels: those set for its next commit and those its
 * last commit applied.  The surface owns the record from its first
 * keiland_glass_v1 to its own end.
 */
struct zwl_panels {
	struct zwl_panel pending[ZWL_PANELS_MAX];
	unsigned pending_count;
	unsigned changed;
	struct zwl_panel current[ZWL_PANELS_MAX];
	unsigned count;
	/*
	 * Whether the surface's glass shows the windows under it blurred
	 * (keiland_glass_v1.set_blur, version 2, ws075-p029): pending, and
	 * committed.  0, the default, is the blurred wallpaper alone.
	 */
	unsigned pending_blur;
	unsigned blur;
};

int zwl_panels_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_panels_commit(struct zwl_object *surface);
void zwl_panels_object_gone(struct zwl_object *object);
unsigned zwl_panels_count(const struct zwl_object *surface);
unsigned zwl_panels_blur(const struct zwl_object *surface);
void zwl_panels_draw(struct zwl_server *server, VkCommandBuffer command, const struct zwl_object *surface, const float *place, float opacity, unsigned shadows);

#endif
