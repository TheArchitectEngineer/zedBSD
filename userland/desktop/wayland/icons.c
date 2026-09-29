/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The icons the compositor draws for the titlebar's controls (WS070 p009,
 * plan/ws070/titlebar-design.md section 7): back, forward, home, search,
 * the views, and the other roles; and the pictures on App Home's tiles
 * (ws035-p123): Files, Notes, Terminal, PDF Viewer, Browser, the model
 * viewer, Gears, the X terminal, Lock Screen, Log Out, Text Editor and
 * Settings.  They are drawn
 * here as line art, not taken from any icon set or font.
 *
 * Each icon is a few strokes, arcs, rings, dots and rounded boxes (filled
 * or outlined) on a grid of 24 units, and a hole may be cut out of what
 * they cover.  zwl_icon_raster turns one into a square of coverage at a
 * size in pixels by measuring, for each pixel's centre, how far it is from
 * the nearest part: a stroke covers the pixel as far as the pixel lies
 * within half its width, which gives smooth edges without an outline
 * rasterizer.  glass.c places the results in the glyph atlas when the look
 * opens.
 */

#include "icons.h"

#include <math.h>
#include <string.h>

/* The grid the icons are drawn on, and the width of their strokes, in units. */
#define ICON_GRID		24.0f
#define ICON_STROKE		1.8f

/* The narrowest line of an application's picture, in pixels. */
#define ICON_APP_MIN_STROKE	1.6f

/* The most parts one icon has. */
#define ICON_PARTS		12

/* Degrees in a turn, and radians in a degree. */
#define ICON_TURN		360.0f
#define ICON_RADIANS		0.017453293f

/*
 * The kinds of part an icon is made of: a stroke between two points, a
 * ring (a stroked circle), a dot (a filled circle), a filled box with
 * rounded corners, the outline of such a box (a frame), a stroked arc of
 * a circle, and a hole: a filled circle cut out of everything else the
 * icon covers.
 */
enum icon_kind {
	ICON_END,
	ICON_SEGMENT,
	ICON_RING,
	ICON_DOT,
	ICON_BOX,
	ICON_FRAME,
	ICON_ARC,
	ICON_HOLE
};

/*
 * One part of an icon, in grid units: a segment's two ends (a, b, c, d)
 * and its own width (e, or the icons' stroke when 0); a ring's, a dot's or
 * a hole's centre and radius (a, b, c); a box's or a frame's corners and
 * corner radius (a, b, c, d, e); an arc's centre and radius (a, b, c), the
 * angle it starts at and the angle it sweeps through (d, e), in degrees
 * clockwise from the right (y grows downwards).
 */
struct icon_part {
	unsigned kind;
	float a;
	float b;
	float c;
	float d;
	float e;
};

/*
 * The application IDs of the windows whose mark is a picture (ws035-p124):
 * the ID each program gives its windows (an X11 window's is its class),
 * the picture, and the colour of its square, the same as App Home's.
 */
struct icon_app_id {
	const char *app_id;
	unsigned icon;
	uint32_t rgb;
};

/* The parts of each icon, in the order of enum glass_icon, each list ended by ICON_END. */
static const struct icon_part icon_parts[GLASS_ICON_COUNT][ICON_PARTS] = {
	/* Back: a chevron pointing left. */
	{
		{ ICON_SEGMENT, 15.0f, 5.0f, 8.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 8.0f, 12.0f, 15.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Forward: a chevron pointing right. */
	{
		{ ICON_SEGMENT, 9.0f, 5.0f, 16.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 16.0f, 12.0f, 9.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Up: a chevron pointing up. */
	{
		{ ICON_SEGMENT, 5.0f, 15.0f, 12.0f, 8.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 8.0f, 19.0f, 15.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Home: a roof over a house. */
	{
		{ ICON_SEGMENT, 3.5f, 11.5f, 12.0f, 4.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 4.0f, 20.5f, 11.5f, 0.0f },
		{ ICON_SEGMENT, 6.0f, 9.5f, 6.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 6.0f, 20.0f, 18.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 18.0f, 20.0f, 18.0f, 9.5f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 20.0f, 10.5f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 15.0f, 13.5f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 15.0f, 13.5f, 20.0f, 0.0f }
	},
	/* Search: a magnifier. */
	{
		{ ICON_RING, 10.0f, 10.0f, 6.0f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 14.5f, 14.5f, 20.0f, 20.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Icons (grid): four rounded squares. */
	{
		{ ICON_BOX, 4.0f, 4.0f, 10.8f, 10.8f, 1.6f },
		{ ICON_BOX, 13.2f, 4.0f, 20.0f, 10.8f, 1.6f },
		{ ICON_BOX, 4.0f, 13.2f, 10.8f, 20.0f, 1.6f },
		{ ICON_BOX, 13.2f, 13.2f, 20.0f, 20.0f, 1.6f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* List: three dots and three lines. */
	{
		{ ICON_DOT, 5.0f, 6.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_DOT, 5.0f, 12.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 12.0f, 20.0f, 12.0f, 0.0f },
		{ ICON_DOT, 5.0f, 18.0f, 1.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 18.0f, 20.0f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Columns: a frame cut in three. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 9.2f, 5.0f, 9.2f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 14.8f, 5.0f, 14.8f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Sort: three lines, shorter and shorter. */
	{
		{ ICON_SEGMENT, 4.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 4.0f, 12.0f, 15.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 4.0f, 18.0f, 10.0f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Filter: three centred lines, narrower and narrower. */
	{
		{ ICON_SEGMENT, 4.0f, 6.0f, 20.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 7.5f, 12.0f, 16.5f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 10.5f, 18.0f, 13.5f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Sidebar: a frame with a panel on the left. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 5.0f, 9.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Preview: a frame with a panel on the right. */
	{
		{ ICON_SEGMENT, 3.5f, 5.0f, 20.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 5.0f, 20.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 19.0f, 3.5f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 19.0f, 3.5f, 5.0f, 0.0f },
		{ ICON_SEGMENT, 15.0f, 5.0f, 15.0f, 19.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Plus. */
	{
		{ ICON_SEGMENT, 12.0f, 5.0f, 12.0f, 19.0f, 0.0f },
		{ ICON_SEGMENT, 5.0f, 12.0f, 19.0f, 12.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Close: a cross. */
	{
		{ ICON_SEGMENT, 7.0f, 7.0f, 17.0f, 17.0f, 0.0f },
		{ ICON_SEGMENT, 17.0f, 7.0f, 7.0f, 17.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Overflow: three dots. */
	{
		{ ICON_DOT, 6.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_DOT, 12.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_DOT, 18.0f, 12.0f, 1.9f, 0.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Files: a folder, its tab on the upper left. */
	{
		{ ICON_FRAME, 3.0f, 8.5f, 21.0f, 19.5f, 2.2f },
		{ ICON_SEGMENT, 3.0f, 10.0f, 3.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 3.0f, 6.0f, 9.0f, 6.0f, 0.0f },
		{ ICON_SEGMENT, 9.0f, 6.0f, 11.0f, 8.5f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Notes: a page with a written line and a pen writing across it. */
	{
		{ ICON_FRAME, 4.0f, 3.5f, 16.0f, 20.5f, 2.2f },
		{ ICON_SEGMENT, 7.0f, 8.0f, 12.5f, 8.0f, 0.0f },
		{ ICON_SEGMENT, 7.0f, 11.5f, 10.5f, 11.5f, 0.0f },
		{ ICON_SEGMENT, 20.5f, 6.0f, 12.5f, 14.0f, 3.4f },
		{ ICON_SEGMENT, 12.0f, 14.5f, 10.5f, 17.0f, 1.6f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Terminal: a screen with a prompt and a cursor. */
	{
		{ ICON_FRAME, 3.0f, 4.5f, 21.0f, 19.5f, 2.5f },
		{ ICON_SEGMENT, 7.0f, 9.0f, 10.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 10.0f, 12.0f, 7.0f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 12.5f, 15.0f, 17.0f, 15.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* PDF Viewer: a document with a folded corner and two lines of text. */
	{
		{ ICON_SEGMENT, 5.5f, 3.5f, 13.5f, 3.5f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 3.5f, 18.5f, 8.5f, 0.0f },
		{ ICON_SEGMENT, 18.5f, 8.5f, 18.5f, 20.5f, 0.0f },
		{ ICON_SEGMENT, 18.5f, 20.5f, 5.5f, 20.5f, 0.0f },
		{ ICON_SEGMENT, 5.5f, 20.5f, 5.5f, 3.5f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 3.5f, 13.5f, 8.5f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 8.5f, 18.5f, 8.5f, 0.0f },
		{ ICON_SEGMENT, 8.5f, 13.0f, 15.5f, 13.0f, 0.0f },
		{ ICON_SEGMENT, 8.5f, 16.5f, 15.5f, 16.5f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Image Viewer (ws091): a picture in a frame, a sun over two hills. */
	{
		{ ICON_FRAME, 3.0f, 4.5f, 21.0f, 19.5f, 2.5f },
		{ ICON_DOT, 16.5f, 9.0f, 1.8f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 5.5f, 17.0f, 10.0f, 11.5f, 0.0f },
		{ ICON_SEGMENT, 10.0f, 11.5f, 14.5f, 17.0f, 0.0f },
		{ ICON_SEGMENT, 12.8f, 15.0f, 15.5f, 12.5f, 0.0f },
		{ ICON_SEGMENT, 15.5f, 12.5f, 18.5f, 17.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Browser: a globe, its equator and one meridian seen from the side. */
	{
		{ ICON_RING, 12.0f, 12.0f, 8.5f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 3.5f, 12.0f, 20.5f, 12.0f, 0.0f },
		{ ICON_ARC, 4.97f, 12.0f, 11.03f, -50.4f, 100.8f },
		{ ICON_ARC, 19.03f, 12.0f, 11.03f, 129.6f, 100.8f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Model viewer: a cube seen from above a corner. */
	{
		{ ICON_SEGMENT, 12.0f, 3.0f, 19.8f, 7.5f, 0.0f },
		{ ICON_SEGMENT, 19.8f, 7.5f, 19.8f, 16.5f, 0.0f },
		{ ICON_SEGMENT, 19.8f, 16.5f, 12.0f, 21.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 21.0f, 4.2f, 16.5f, 0.0f },
		{ ICON_SEGMENT, 4.2f, 16.5f, 4.2f, 7.5f, 0.0f },
		{ ICON_SEGMENT, 4.2f, 7.5f, 12.0f, 3.0f, 0.0f },
		{ ICON_SEGMENT, 4.2f, 7.5f, 12.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 19.8f, 7.5f, 12.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 12.0f, 12.0f, 21.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Gears: a gear wheel, eight teeth round a disc with a hole in its middle. */
	{
		{ ICON_DOT, 12.0f, 12.0f, 6.4f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 17.5f, 12.0f, 19.8f, 12.0f, 3.6f },
		{ ICON_SEGMENT, 15.89f, 15.89f, 17.52f, 17.52f, 3.6f },
		{ ICON_SEGMENT, 12.0f, 17.5f, 12.0f, 19.8f, 3.6f },
		{ ICON_SEGMENT, 8.11f, 15.89f, 6.48f, 17.52f, 3.6f },
		{ ICON_SEGMENT, 6.5f, 12.0f, 4.2f, 12.0f, 3.6f },
		{ ICON_SEGMENT, 8.11f, 8.11f, 6.48f, 6.48f, 3.6f },
		{ ICON_SEGMENT, 12.0f, 6.5f, 12.0f, 4.2f, 3.6f },
		{ ICON_SEGMENT, 15.89f, 8.11f, 17.52f, 6.48f, 3.6f },
		{ ICON_HOLE, 12.0f, 12.0f, 2.7f, 0.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* X terminal: a screen with an X and a cursor. */
	{
		{ ICON_FRAME, 3.0f, 4.5f, 21.0f, 19.5f, 2.5f },
		{ ICON_SEGMENT, 7.0f, 9.0f, 11.0f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 11.0f, 9.0f, 7.0f, 15.0f, 0.0f },
		{ ICON_SEGMENT, 13.5f, 15.0f, 17.0f, 15.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Lock Screen: a padlock with its keyhole. */
	{
		{ ICON_FRAME, 5.0f, 10.5f, 19.0f, 20.5f, 2.5f },
		{ ICON_ARC, 12.0f, 8.0f, 4.0f, 180.0f, 180.0f },
		{ ICON_SEGMENT, 8.0f, 8.0f, 8.0f, 10.5f, 0.0f },
		{ ICON_SEGMENT, 16.0f, 8.0f, 16.0f, 10.5f, 0.0f },
		{ ICON_DOT, 12.0f, 14.7f, 1.5f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 12.0f, 14.7f, 12.0f, 17.2f, 1.6f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Log Out: an arrow leaving an open door. */
	{
		{ ICON_SEGMENT, 11.0f, 4.0f, 5.0f, 4.0f, 0.0f },
		{ ICON_SEGMENT, 5.0f, 4.0f, 5.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 5.0f, 20.0f, 11.0f, 20.0f, 0.0f },
		{ ICON_SEGMENT, 10.0f, 12.0f, 20.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 16.5f, 8.5f, 20.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 20.0f, 12.0f, 16.5f, 15.5f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/* Text Editor (WS092): a page with three lines of text, the cursor after the last. */
	{
		{ ICON_FRAME, 4.5f, 3.5f, 19.5f, 20.5f, 2.2f },
		{ ICON_SEGMENT, 8.0f, 8.0f, 16.0f, 8.0f, 0.0f },
		{ ICON_SEGMENT, 8.0f, 12.0f, 16.0f, 12.0f, 0.0f },
		{ ICON_SEGMENT, 8.0f, 16.0f, 11.5f, 16.0f, 0.0f },
		{ ICON_SEGMENT, 14.0f, 14.0f, 14.0f, 18.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	},
	/*
	 * Settings (WS089): the outline of a cog, eight teeth round a ring and a
	 * small ring at its hub, drawn in lines so that it is not taken for
	 * Gears' filled wheel.
	 */
	{
		{ ICON_RING, 12.0f, 12.0f, 6.0f, 0.0f, 0.0f },
		{ ICON_SEGMENT, 19.30f, 12.00f, 20.70f, 12.00f, 3.0f },
		{ ICON_SEGMENT, 17.16f, 17.16f, 18.15f, 18.15f, 3.0f },
		{ ICON_SEGMENT, 12.00f, 19.30f, 12.00f, 20.70f, 3.0f },
		{ ICON_SEGMENT, 6.84f, 17.16f, 5.85f, 18.15f, 3.0f },
		{ ICON_SEGMENT, 4.70f, 12.00f, 3.30f, 12.00f, 3.0f },
		{ ICON_SEGMENT, 6.84f, 6.84f, 5.85f, 5.85f, 3.0f },
		{ ICON_SEGMENT, 12.00f, 4.70f, 12.00f, 3.30f, 3.0f },
		{ ICON_SEGMENT, 17.16f, 6.84f, 18.15f, 5.85f, 3.0f },
		{ ICON_RING, 12.0f, 12.0f, 2.3f, 0.0f, 0.0f },
		{ ICON_END, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }
	}
};

/*
 * The names App Home's list (/etc/keiland/apps.conf) gives the pictures
 * by, in the order of enum glass_icon from GLASS_ICON_FIRST_APP.
 */
static const char *const icon_app_names[GLASS_ICON_APPS] = {
	"files",
	"notes",
	"terminal",
	"pdf",
	"image",
	"browser",
	"model",
	"gears",
	"xterm",
	"lock",
	"logout",
	"text",
	"settings"
};

/* The known programs' windows, found by their exact application ID. */
static const struct icon_app_id icon_app_ids[] = {
	{ "files", GLASS_ICON_APP_FILES, 0x2f7cf6U },
	{ "notes", GLASS_ICON_APP_NOTES, 0xe0a526U },
	{ "terminal", GLASS_ICON_APP_TERMINAL, 0x323a4eU },
	{ "pdfviewer", GLASS_ICON_APP_PDF, 0xd9534fU },
	{ "imageview", GLASS_ICON_APP_IMAGE, 0x3fa36bU },
	{ "browser", GLASS_ICON_APP_BROWSER, 0x3a8fd8U },
	{ "mview", GLASS_ICON_APP_MODEL, 0xe07a5aU },
	{ "Gears", GLASS_ICON_APP_GEARS, 0xd05a3aU },
	{ "XTerminal", GLASS_ICON_APP_XTERM, 0x4a4a78U },
	{ "textedit", GLASS_ICON_APP_TEXT, 0x1f9e9aU },
	{ "settings", GLASS_ICON_APP_SETTINGS, 0x6b7a8fU }
};

static float icon_distance(const struct icon_part *part, float x, float y);
static float icon_segment_distance(float x, float y, float x0, float y0, float x1, float y1);
static float icon_arc_distance(const struct icon_part *part, float x, float y);
static float icon_box_distance(float x, float y, float x0, float y0, float x1, float y1, float radius);
static float icon_clamp(float value);

/*
 * Draws an icon (GLASS_ICON_*) into a square of coverage of a size in
 * pixels, a byte a pixel (255 fully covered), rows stride bytes apart.
 */
void
zwl_icon_raster(
	unsigned icon,
	unsigned pixels,
	uint8_t *coverage,
	size_t stride)
{
	const struct icon_part *part;
	float scale;
	float half;
	float part_half;
	float distance;
	float amount;
	float best;
	float hole;
	float x;
	float y;
	unsigned column;
	unsigned row;
	unsigned index;

	/* An icon the table does not have is left empty. */
	for (row = 0; row < pixels; row++)
		memset(coverage + (size_t)row * stride, 0, pixels);
	if (icon >= GLASS_ICON_COUNT || pixels == 0U)
		return;

	/* Pixels a unit, and half a stroke's width in pixels. */
	scale = (float)pixels / ICON_GRID;
	half = ICON_STROKE * scale * 0.5f;

	/*
	 * An application's picture drawn small (a window's mark, ws035-p124)
	 * keeps its lines at least ICON_APP_MIN_STROKE pixels wide, so they
	 * stay white on the coloured square instead of fading to grey.
	 */
	if (icon >= GLASS_ICON_FIRST_APP && half < ICON_APP_MIN_STROKE * 0.5f)
		half = ICON_APP_MIN_STROKE * 0.5f;

	/* Each pixel: how much of it the nearest part covers. */
	for (row = 0; row < pixels; row++) {
		for (column = 0; column < pixels; column++) {
			x = ((float)column + 0.5f) / scale;
			y = ((float)row + 0.5f) / scale;
			best = 0.0f;
			hole = 0.0f;

			/* Each part of the icon. */
			for (index = 0; index < ICON_PARTS; index++) {
				part = &icon_parts[icon][index];
				if (part->kind == ICON_END)
					break;
				distance = icon_distance(part, x, y) * scale;

				/* A segment may be wider or narrower than the icons' stroke. */
				part_half = half;
				if (part->kind == ICON_SEGMENT && part->e > 0.0f)
					part_half = part->e * scale * 0.5f;

				/* A hole is cut out of the rest; it covers nothing itself. */
				if (part->kind == ICON_HOLE) {
					amount = icon_clamp(0.5f - distance);
					if (amount > hole)
						hole = amount;
					continue;
				}

				/* A stroke, a ring, a frame or an arc covers within half its width, a dot or a box inside it. */
				if (part->kind == ICON_SEGMENT ||
				    part->kind == ICON_RING ||
				    part->kind == ICON_FRAME ||
				    part->kind == ICON_ARC) {
					amount = icon_clamp(part_half - distance + 0.5f);
				} else {
					amount = icon_clamp(0.5f - distance);
				}

				/* The part that covers most wins. */
				if (amount > best)
					best = amount;
			}

			/* The pixel's coverage, less what a hole takes away. */
			best = best * (1.0f - hole);
			coverage[(size_t)row * stride + column] = (uint8_t)(best * 255.0f + 0.5f);
		}
	}
}

/*
 * Finds the App Home picture a name stands for ("files", "notes", ...;
 * see icon_app_names), for a line of App Home's list.
 *
 * Returns the icon (GLASS_ICON_APP_*), or -1 for a name no picture has.
 */
int
zwl_icon_named(
	const char *name)
{
	unsigned index;
	int differs;

	/* Each picture's name, until one is the name asked for. */
	for (index = 0; index < GLASS_ICON_APPS; index++) {
		differs = strcmp(name, icon_app_names[index]);
		if (differs == 0)
			return (int)(GLASS_ICON_FIRST_APP + index);
	}

	/* No picture has that name. */
	return -1;
}

/*
 * Finds the picture of a window's mark from its application ID, and the
 * colour (0xRRGGBB) of the square it is drawn on.
 *
 * Returns the icon (GLASS_ICON_APP_*), or -1 (and leaves rgb alone) for
 * an ID no picture belongs to.
 */
int
zwl_icon_for_app_id(
	const char *app_id,
	uint32_t *rgb)
{
	unsigned index;
	int differs;

	/* Each known ID, until one is the window's. */
	for (index = 0; index < sizeof(icon_app_ids) / sizeof(icon_app_ids[0]); index++) {
		differs = strcmp(app_id, icon_app_ids[index].app_id);
		if (differs != 0)
			continue;

		/* Succeeded: the picture, and its square's colour. */
		*rgb = icon_app_ids[index].rgb;
		return (int)icon_app_ids[index].icon;
	}

	/* No picture belongs to that ID. */
	return -1;
}

/* Measures how far a point (in units) is from a part: from a stroke's line, a ring's circle, a frame's outline or an arc, or outside a dot, a hole or a box (negative inside). */
static float
icon_distance(
	const struct icon_part *part,
	float x,
	float y)
{
	float distance;
	float dx;
	float dy;
	float from_centre;

	/* No part is as far as can be; the centre's distance serves the ring and the dot. */
	distance = 1000.0f;
	dx = x - part->a;
	dy = y - part->b;
	from_centre = sqrtf(dx * dx + dy * dy);

	/* Each kind of part. */
	switch (part->kind) {
	case ICON_SEGMENT:
		distance = icon_segment_distance(x, y, part->a, part->b, part->c, part->d);
		break;
	case ICON_RING:
		distance = fabsf(from_centre - part->c);
		break;
	case ICON_DOT:
	case ICON_HOLE:
		distance = from_centre - part->c;
		break;
	case ICON_BOX:
		distance = icon_box_distance(x, y, part->a, part->b, part->c, part->d, part->e);
		break;
	case ICON_FRAME:
		distance = fabsf(icon_box_distance(x, y, part->a, part->b, part->c, part->d, part->e));
		break;
	case ICON_ARC:
		distance = icon_arc_distance(part, x, y);
		break;
	default:
		break;
	}

	/* Reports the distance. */
	return distance;
}

/* Measures how far a point is from a segment. */
static float
icon_segment_distance(
	float x,
	float y,
	float x0,
	float y0,
	float x1,
	float y1)
{
	float distance;
	float along;
	float length;
	float dx;
	float dy;
	float px;
	float py;

	/* The segment's direction, and where along it the point falls (clamped to its ends). */
	dx = x1 - x0;
	dy = y1 - y0;
	length = dx * dx + dy * dy;
	along = 0.0f;
	if (length > 0.0f)
		along = ((x - x0) * dx + (y - y0) * dy) / length;
	if (along < 0.0f)
		along = 0.0f;
	if (along > 1.0f)
		along = 1.0f;

	/* The distance to that nearest point. */
	px = x - (x0 + along * dx);
	py = y - (y0 + along * dy);
	distance = sqrtf(px * px + py * py);

	/* Reports the distance. */
	return distance;
}

/*
 * Measures how far a point is from an arc: from its circle where the
 * point's angle falls within the arc's sweep, else from the nearer end.
 */
static float
icon_arc_distance(
	const struct icon_part *part,
	float x,
	float y)
{
	float angle;
	float along;
	float start;
	float end;
	float dx;
	float dy;
	float to_start;
	float to_end;

	/* The point's angle about the centre, in degrees, and how far past the arc's start it lies (0 up to a turn). */
	dx = x - part->a;
	dy = y - part->b;
	angle = atan2f(dy, dx) / ICON_RADIANS;
	along = fmodf(angle - part->d, ICON_TURN);
	if (along < 0.0f)
		along += ICON_TURN;

	/* Within the sweep the arc is as far as its circle. */
	if (along <= part->e)
		return fabsf(sqrtf(dx * dx + dy * dy) - part->c);

	/* Outside it, the nearer of the arc's two ends. */
	start = part->d * ICON_RADIANS;
	end = (part->d + part->e) * ICON_RADIANS;
	dx = x - (part->a + part->c * cosf(start));
	dy = y - (part->b + part->c * sinf(start));
	to_start = sqrtf(dx * dx + dy * dy);
	dx = x - (part->a + part->c * cosf(end));
	dy = y - (part->b + part->c * sinf(end));
	to_end = sqrtf(dx * dx + dy * dy);

	/* The start is nearer. */
	if (to_start < to_end)
		return to_start;

	/* The end is nearer, or as near. */
	return to_end;
}

/* Measures the signed distance from a point to a box with rounded corners (negative inside). */
static float
icon_box_distance(
	float x,
	float y,
	float x0,
	float y0,
	float x1,
	float y1,
	float radius)
{
	float cx;
	float cy;
	float qx;
	float qy;
	float outside;
	float inside;

	/* The point folded into the box's first quadrant, from the inner rectangle the corners round off. */
	cx = (x0 + x1) * 0.5f;
	cy = (y0 + y1) * 0.5f;
	qx = fabsf(x - cx) - ((x1 - x0) * 0.5f - radius);
	qy = fabsf(y - cy) - ((y1 - y0) * 0.5f - radius);

	/* Outside: the distance to the inner rectangle's corner; inside: how deep. */
	outside = sqrtf(fmaxf(qx, 0.0f) * fmaxf(qx, 0.0f) + fmaxf(qy, 0.0f) * fmaxf(qy, 0.0f));
	inside = fminf(fmaxf(qx, qy), 0.0f);

	/* Reports the distance, less the corners' radius. */
	return outside + inside - radius;
}

/* Keeps a coverage between 0 and 1. */
static float
icon_clamp(
	float value)
{
	/* Below and above the range. */
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;

	/* Inside it. */
	return value;
}
