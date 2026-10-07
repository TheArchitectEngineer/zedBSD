/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The plane of the displays shown at once in the extended mode
 * (plane.c, ws113-p007; the design is plan/ws113/phase007/phase.md).
 *
 * The windows and the pointer have their places in one plane whose origin
 * is the anchor's top left corner (the anchor is the display the system
 * bar and App Home are on; its rectangle is 0, 0, width, height as before
 * there was more than one display).  Each other display shown (a head) is
 * a rectangle of the same plane, beside the anchor or another head
 * (displays.h checks that they join).  An output is named by its slot: 0
 * the anchor, 1 + N the head N.  A slot whose rectangle has no size is
 * not shown (a closed head, or every head in the mirror mode).
 *
 * Each window belongs to one output (D-ATOMIC): it is drawn by that
 * output alone, also where it reaches past its edge.  The pointer moves
 * over the outputs: a relative move into another output crosses to it, a
 * move past an edge no output shares stops at the edge.  Nothing here
 * knows the server, so the host tests run it alone.
 */

#ifndef KWL_PLANE_H
#define KWL_PLANE_H

#include <stdint.h>

/* The slots: the anchor's, and how many there are with the heads'. */
#define KWL_PLANE_ANCHOR	0U
#define KWL_PLANE_SLOTS		8U

/* An output's rectangle of the plane (no size: not shown). */
struct kwl_plane_rect {
	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
};

int kwl_plane_at(const struct kwl_plane_rect *outputs, unsigned count, int32_t x, int32_t y);
unsigned kwl_plane_move(const struct kwl_plane_rect *outputs, unsigned count, unsigned current, int32_t x, int32_t y, int32_t dx, int32_t dy, int32_t *moved_x, int32_t *moved_y);
int kwl_plane_neighbour(const struct kwl_plane_rect *outputs, unsigned count, unsigned current, int direction);
void kwl_plane_carry(const struct kwl_plane_rect *from, const struct kwl_plane_rect *to, int32_t x, int32_t y, uint32_t width, uint32_t height, int32_t top, int32_t *carried_x, int32_t *carried_y);

#endif
