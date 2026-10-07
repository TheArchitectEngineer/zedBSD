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
 * (closing Home) or sideways (the pages), how far a docked title has
 * been pulled out of the system bar, and how deep the desktop and Home's
 * content are while Home opens or closes (ws181-p008).
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

/*
 * App Home's way in and out (ws181-p008, the 2026-10-07 UAT: as on iOS).
 * The desktop layer goes back into the distance: it shrinks about the
 * output's middle to KWL_EDGE_HOME_DESKTOP_DEPTH of its size as Home
 * opens, and fades out between KWL_EDGE_HOME_FADE_START and
 * KWL_EDGE_HOME_FADE_END of the way.  Home's content comes forward from
 * the distance: it grows about the middle from KWL_EDGE_HOME_CONTENT_DEPTH
 * of its size to its own.  Closing is the same way back.
 */
#define KWL_EDGE_HOME_DESKTOP_DEPTH	0.75f
#define KWL_EDGE_HOME_CONTENT_DEPTH	0.85f
#define KWL_EDGE_HOME_FADE_START	0.20f
#define KWL_EDGE_HOME_FADE_END		0.90f

/*
 * Where a layer is drawn at one moment of Home's way in or out: its
 * top-left corner on the output and its scale (a point (px, py) of the
 * layer is drawn at (x + px * scale, y + py * scale)), and its opacity.
 * The caller owns it.
 */
struct kwl_edge_depth {
	float x;
	float y;
	float scale;
	float opacity;
};

unsigned kwl_edge_classify(int32_t x, int32_t y, int32_t width, int32_t height, int touch);
unsigned kwl_edge_band_motion(int32_t dx, int32_t dy);
unsigned kwl_edge_drag_axis(int32_t dx, int32_t dy);
int32_t kwl_edge_distance(int32_t dx, int32_t dy);
void kwl_edge_home_desktop(float progress, int32_t width, int32_t height, struct kwl_edge_depth *depth);
void kwl_edge_home_content(float progress, int32_t width, int32_t height, struct kwl_edge_depth *depth);

#endif
