/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Display page's arrangement (ws113-p006,
 * userland/desktop/settings/arrange.c): the plane fitted into a box and
 * back, and a dragged display snapped next to the others.  Each snapped
 * arrangement is also checked with the compositor's own check of the
 * extended places (userland/desktop/wayland/displays.c), so that what the
 * page sends is what the compositor takes.
 */

#include "arrange.h"
#include "displays.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

static void check(int condition, const char *what);
static int compositor_takes(const struct se_arrange_rect *rects, unsigned count);
static void test_fit(void);
static void test_snap_sides(void);
static void test_snap_alone(void);
static void test_snap_three(void);
static void test_snap_random(void);

int
main(void)
{
	/* Each part. */
	test_fit();
	test_snap_sides();
	test_snap_alone();
	test_snap_three();
	test_snap_random();
	if (failures != 0U) {
		fprintf(stderr, "%u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("arrange host test PASS\n");
	return 0;
}

/* Counts a failed condition. */
static void
check(
	int condition,
	const char *what)
{
	/* Told and counted. */
	if (!condition) {
		fprintf(stderr, "FAIL %s\n", what);
		failures++;
	}
}

/* Tells whether the compositor's check takes the arrangement. */
static int
compositor_takes(
	const struct se_arrange_rect *rects,
	unsigned count)
{
	struct kwl_display_rect places[SE_ARRANGE_MAX];
	unsigned index;
	int error;

	/* The same rectangles in the compositor's type. */
	for (index = 0U; index < count; index++) {
		places[index].x = rects[index].x;
		places[index].y = rects[index].y;
		places[index].width = rects[index].width;
		places[index].height = rects[index].height;
	}

	/* The compositor's check. */
	error = kwl_displays_validate(places, count);

	/* Taken when no error. */
	return error == 0;
}

/* The plane fitted into a box: centred, inside it, and a box's point back to the plane's. */
static void
test_fit(void)
{
	struct se_arrange_rect rects[2];
	struct se_arrange_view view;
	int x0;
	int y0;
	int w0;
	int h0;
	int x1;
	int y1;
	int w1;
	int h1;
	int32_t px;
	int32_t py;

	/* Two displays side by side, into a 600 x 200 box at (10, 20). */
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].width = 1920;
	rects[0].height = 1080;
	rects[1].x = 1920;
	rects[1].y = 0;
	rects[1].width = 1280;
	rects[1].height = 1024;
	se_arrange_fit(rects, 2U, 10, 20, 600, 200, &view);
	se_arrange_to_box(&view, &rects[0], &x0, &y0, &w0, &h0);
	se_arrange_to_box(&view, &rects[1], &x1, &y1, &w1, &h1);
	check(view.scale > 0.0f, "fit scale");
	check(x0 >= 10 && y0 >= 20, "fit inside top left");
	check(x1 + w1 <= 610 && y0 + h0 <= 220, "fit inside bottom right");
	check(h0 >= 136 && h0 <= 142, "fit fills the share of the height");
	check(abs(x1 - (x0 + w0)) <= 1, "fit keeps the shared edge");
	check(abs((x0 - 10) - (610 - (x1 + w1))) <= 1, "fit centred across");

	/* Back from the box: the second display's corner. */
	se_arrange_from_box(&view, x1, y1, &px, &py);
	check(abs(px - 1920) <= 10 && abs(py) <= 10, "from box");

	/* No display: the scale of one. */
	se_arrange_fit(rects, 0U, 0, 0, 600, 200, &view);
	check(view.scale == 1.0f, "fit of none");
}

/* A second display dragged around the first: each side, level when near, never overlapping. */
static void
test_snap_sides(void)
{
	struct se_arrange_rect rects[2];
	int32_t x;
	int32_t y;

	/* The first at the origin, the second right of it. */
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].width = 1920;
	rects[0].height = 1080;
	rects[1].x = 1920;
	rects[1].y = 0;
	rects[1].width = 1920;
	rects[1].height = 1080;

	/* Dragged to the left, a little low: left of it and level with its top. */
	se_arrange_snap(rects, 2U, 1U, -1900, 60, &x, &y);
	check(x == -1920 && y == 0, "snap left level");
	rects[1].x = x;
	rects[1].y = y;
	check(compositor_takes(rects, 2U), "snap left taken");

	/* Dragged below, off to the right: below it, sharing a length, not level. */
	se_arrange_snap(rects, 2U, 1U, 500, 1000, &x, &y);
	check(x == 500 && y == 1080, "snap below");
	rects[1].x = x;
	rects[1].y = y;
	check(compositor_takes(rects, 2U), "snap below taken");

	/* Dragged far above: above it, sharing a length, not level. */
	se_arrange_snap(rects, 2U, 1U, 1500, -3000, &x, &y);
	check(x == 1500 && y == -1080, "snap above");
	rects[1].x = x;
	rects[1].y = y;
	check(compositor_takes(rects, 2U), "snap above taken");

	/* Dragged onto the first: the nearest side that overlaps nothing. */
	se_arrange_snap(rects, 2U, 1U, 100, 100, &x, &y);
	rects[1].x = x;
	rects[1].y = y;
	check(compositor_takes(rects, 2U), "snap from inside taken");
	check(x == 0 && y == 1080, "snap from inside below level");
}

/* A display alone stays where it is. */
static void
test_snap_alone(void)
{
	struct se_arrange_rect rect;
	int32_t x;
	int32_t y;

	/* One display. */
	rect.x = 7;
	rect.y = 9;
	rect.width = 800;
	rect.height = 600;
	se_arrange_snap(&rect, 1U, 0U, 5000, 5000, &x, &y);
	check(x == 7 && y == 9, "alone");
}

/* Three displays: the dragged one goes next to the one nearest its wanted place, overlapping neither. */
static void
test_snap_three(void)
{
	struct se_arrange_rect rects[3];
	int32_t x;
	int32_t y;

	/* A in the middle, B right of it, C left of it. */
	rects[0].x = 0;
	rects[0].y = 0;
	rects[0].width = 1920;
	rects[0].height = 1080;
	rects[1].x = 1920;
	rects[1].y = 0;
	rects[1].width = 1280;
	rects[1].height = 1024;
	rects[2].x = -1280;
	rects[2].y = 0;
	rects[2].width = 1280;
	rects[2].height = 800;

	/* C dragged past B: right of B, level with its top. */
	se_arrange_snap(rects, 3U, 2U, 3000, 40, &x, &y);
	check(x == 3200 && y == 0, "three right of B");
	rects[2].x = x;
	rects[2].y = y;
	check(compositor_takes(rects, 3U), "three taken");

	/* C dragged into the gap below A and B: below one of them, overlapping neither. */
	se_arrange_snap(rects, 3U, 2U, 1500, 1050, &x, &y);
	rects[2].x = x;
	rects[2].y = y;
	check(compositor_takes(rects, 3U), "three below taken");
}

/* Dragged anywhere, the second of two is always taken by the compositor. */
static void
test_snap_random(void)
{
	struct se_arrange_rect rects[2];
	unsigned round;
	unsigned bad;
	int taken;
	int32_t x;
	int32_t y;

	/* Many sizes and wanted places, from a fixed seed. */
	srand(113U);
	bad = 0U;
	for (round = 0U; round < 20000U; round++) {
		rects[0].x = rand() % 4000 - 2000;
		rects[0].y = rand() % 4000 - 2000;
		rects[0].width = 640U + (uint32_t)(rand() % 3200);
		rects[0].height = 480U + (uint32_t)(rand() % 2000);
		rects[1].x = 0;
		rects[1].y = 0;
		rects[1].width = 640U + (uint32_t)(rand() % 3200);
		rects[1].height = 480U + (uint32_t)(rand() % 2000);
		se_arrange_snap(rects, 2U, 1U, rand() % 12000 - 6000, rand() % 12000 - 6000, &x, &y);
		rects[1].x = x;
		rects[1].y = y;
		taken = compositor_takes(rects, 2U);
		if (!taken)
			bad++;
	}

	/* None refused. */
	check(bad == 0U, "random snaps taken");
}
