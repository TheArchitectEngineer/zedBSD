/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The edges' gestures of the screen (edge.c, WS181, the 2026-10-07 UAT):
 * the swipe up from the bottom edge that opens App Home, the swipe down
 * from the top edge's band that opens Wiseview (a touch only, the band
 * holding its press until it knows), a drag on Home that is mostly down
 * (closing Home) or sideways (the pages), and how far a docked title has
 * been pulled out of the system bar.
 *
 * It knows nothing of the server: the caller gives points and distances,
 * and acts on what the rules say (shell.c, home.c).  So the host tests
 * run it alone.
 */

#ifndef KWL_EDGE_H
#define KWL_EDGE_H

#include <stdint.h>

/*
 * The top edge's band: its height, and the launcher's width at the left
 * (the band starts after it; the top-left corner of App Home is inside it).
 * The band ends before the top-right corner of Notes (CORNER_ZONE).
 */
#define KWL_EDGE_BAND			10
#define KWL_EDGE_LAUNCHER_WIDTH		40
#define KWL_EDGE_CORNER			28

/* The bottom edge's strip, where the swipe up to App Home starts. */
#define KWL_EDGE_BOTTOM_HEIGHT		20

/*
 * How far a press in the band moves before it is something: down by
 * KWL_EDGE_BAND_START (more down than across) is Wiseview's swipe, across
 * or up by KWL_EDGE_BAND_SLIP is a press of what is under it after all.
 */
#define KWL_EDGE_BAND_START		12
#define KWL_EDGE_BAND_SLIP		8

/* Where a press is: nowhere special, the bottom edge's strip, or the top edge's band. */
#define KWL_EDGE_NONE			0U
#define KWL_EDGE_BOTTOM_STRIP		1U
#define KWL_EDGE_TOP_BAND		2U

/* What a press held in the band is after a motion: still waiting, Wiseview's swipe, or the press of what is under it. */
#define KWL_EDGE_BAND_WAIT		0U
#define KWL_EDGE_BAND_WISEVIEW		1U
#define KWL_EDGE_BAND_REPLAY		2U

/* What a drag on Home is: the pages (sideways), closing Home (down), or nothing (up). */
#define KWL_EDGE_DRAG_PAGES		0U
#define KWL_EDGE_DRAG_CLOSE		1U
#define KWL_EDGE_DRAG_NONE		2U

unsigned kwl_edge_classify(int32_t x, int32_t y, int32_t width, int32_t height, int touch);
unsigned kwl_edge_band_motion(int32_t dx, int32_t dy);
unsigned kwl_edge_drag_axis(int32_t dx, int32_t dy);
int32_t kwl_edge_distance(int32_t dx, int32_t dy);

#endif
