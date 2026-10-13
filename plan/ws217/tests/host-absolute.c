/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the absolute devices' places on a rotated display
 * (ws217-p003, userland/desktop/wayland/cursor-box.c): the pure mapping
 * kwl_cursor_geometry_place at every angle on panels that are not square,
 * for a device on the display (direct) and one beside it, in pixels and in
 * 24.8 fixed point, over every value of a small range and past its ends,
 * against the arithmetic it replaces and rotate.c's way back; that a
 * direct device's whole range reaches every logical pixel once; and the
 * cursor's box: an absolute pointer's place at each angle, its relative
 * motion kept on the output, and the geometry written whole by a warp and
 * by kwl_cursor_box_outputs.
 */

#include "cursor-box.h"
#include "rotate.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* The panels walked: wide, tall, one pixel, a thin strip, and the design's 1280x800. */
#define TEST_PANELS	5U

/* The most pixels a small panel of the coverage walk has. */
#define TEST_PIXELS	64U

/* The four angles, in order. */
static const unsigned test_angles[4] = { KWL_ROTATE_0, KWL_ROTATE_90, KWL_ROTATE_180, KWL_ROTATE_270 };

/* The physical sizes of the panels walked. */
static const uint32_t test_panels[TEST_PANELS][2] = { { 5U, 3U }, { 3U, 5U }, { 1U, 1U }, { 8U, 2U }, { 1280U, 800U } };

/* The number of failed checks. */
static unsigned failures;

uint64_t kwl_microseconds(void);
static void check(int condition, const char *what);
static int32_t reference_scale(int32_t value, int32_t minimum, int32_t maximum, uint32_t size, unsigned unit);
static void reference_place(unsigned rotation, uint32_t physical_width, uint32_t physical_height, unsigned direct, unsigned unit, const struct kwl_cursor_axis *across, const struct kwl_cursor_axis *down, int32_t corner_x, int32_t corner_y, int32_t *x, int32_t *y);
static void geometry_one(struct kwl_cursor_geometry *geometry, unsigned slot, int32_t corner_x, int32_t corner_y, uint32_t physical_width, uint32_t physical_height, unsigned rotation);
static void test_place(void);
static void test_coverage(void);
static void test_box(void);
static void test_keep(const void *context, const struct kwl_plane_rect *outputs, int32_t *x, int32_t *y, unsigned *output);

/* Runs every check. */
int
main(void)
{
	/* Each part. */
	test_place();
	test_coverage();
	test_box();

	/* The verdict. */
	if (failures != 0U) {
		printf("host-absolute: %u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-absolute: PASS\n");
	return 0;
}

/* Gives the monotonic clock in microseconds (the box's write times). */
uint64_t
kwl_microseconds(void)
{
	struct timespec now;

	/* The clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Succeeded: the time. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* One that fails is named. */
	printf("FAIL: %s\n", what);
	failures++;
}

/*
 * The arithmetic the mapping replaces: the absolute pointer's
 * (kwl_cursor_scale_absolute, unit 1) and the touch screen's and the pen's
 * (scale_fixed, unit 256) before ws217-p003.
 */
static int32_t
reference_scale(
	int32_t value,
	int32_t minimum,
	int32_t maximum,
	uint32_t size,
	unsigned unit)
{
	int64_t offset;
	int64_t range;

	/* The low end, and below. */
	if (size <= 1U || value <= minimum)
		return 0;

	/* The high end, and above. */
	if (value >= maximum)
		return ((int32_t)size - 1) * (int32_t)unit;

	/* Linearly between, rounding down. */
	offset = (int64_t)value - minimum;
	range = (int64_t)maximum - minimum;

	/* Succeeded: the place. */
	return (int32_t)((offset * ((int64_t)size - 1) * (int64_t)unit) / range);
}

/*
 * The expected place: a direct device's on the panel turned back by
 * rotate.c (pixels or 24.8), any other's on the logical picture.
 */
static void
reference_place(
	unsigned rotation,
	uint32_t physical_width,
	uint32_t physical_height,
	unsigned direct,
	unsigned unit,
	const struct kwl_cursor_axis *across,
	const struct kwl_cursor_axis *down,
	int32_t corner_x,
	int32_t corner_y,
	int32_t *x,
	int32_t *y)
{
	uint32_t width;
	uint32_t height;
	int32_t physical_x;
	int32_t physical_y;

	/* The logical size. */
	kwl_rotate_size(rotation, physical_width, physical_height, &width, &height);

	/* A device beside the display: the logical picture. */
	if (!direct) {
		*x = reference_scale(across->value, across->minimum, across->maximum, width, unit) + corner_x * (int32_t)unit;
		*y = reference_scale(down->value, down->minimum, down->maximum, height, unit) + corner_y * (int32_t)unit;
		return;
	}

	/* A device on it: the panel's point, turned back. */
	physical_x = reference_scale(across->value, across->minimum, across->maximum, physical_width, unit);
	physical_y = reference_scale(down->value, down->minimum, down->maximum, physical_height, unit);
	if (unit == 1U) {
		kwl_rotate_point_back(rotation, physical_width, physical_height, physical_x, physical_y, x, y);
	} else {
		kwl_rotate_fixed_back(rotation, physical_width, physical_height, physical_x, physical_y, x, y);
	}

	/* Succeeded: of the plane. */
	*x += corner_x * (int32_t)unit;
	*y += corner_y * (int32_t)unit;
}

/* Fills a geometry whose one output (slot) is a panel at a corner of the plane and an angle. */
static void
geometry_one(
	struct kwl_cursor_geometry *geometry,
	unsigned slot,
	int32_t corner_x,
	int32_t corner_y,
	uint32_t physical_width,
	uint32_t physical_height,
	unsigned rotation)
{
	/* The anchor is a 4x4 panel, the slot the one given. */
	memset(geometry, 0, sizeof(*geometry));
	geometry->output_count = KWL_PLANE_SLOTS;
	geometry->outputs[KWL_PLANE_ANCHOR].width = 4U;
	geometry->outputs[KWL_PLANE_ANCHOR].height = 4U;
	geometry->physical[KWL_PLANE_ANCHOR].width = 4U;
	geometry->physical[KWL_PLANE_ANCHOR].height = 4U;

	/* The slot's rectangle is its panel turned. */
	geometry->outputs[slot].x = corner_x;
	geometry->outputs[slot].y = corner_y;
	kwl_rotate_size(rotation, physical_width, physical_height, &geometry->outputs[slot].width, &geometry->outputs[slot].height);
	geometry->physical[slot].width = physical_width;
	geometry->physical[slot].height = physical_height;
	geometry->physical[slot].rotation = rotation;
}

/*
 * Every panel, angle, kind and unit over the values -5..25 of the range
 * -3..20 (both ends and past them), on the anchor and on a head at
 * (10, -4); and an output not shown maps onto the anchor.
 */
static void
test_place(void)
{
	struct kwl_cursor_geometry geometry;
	struct kwl_cursor_axis across;
	struct kwl_cursor_axis down;
	int32_t x;
	int32_t y;
	int32_t expected_x;
	int32_t expected_y;
	int32_t corner_x;
	int32_t corner_y;
	int32_t value_x;
	int32_t value_y;
	unsigned panel;
	unsigned angle;
	unsigned direct;
	unsigned unit;
	unsigned slot;
	int same;

	/* The range of both axes. */
	across.minimum = -3;
	across.maximum = 20;
	down.minimum = -3;
	down.maximum = 20;

	/* Each case. */
	for (panel = 0U; panel < TEST_PANELS; panel++) {
		for (angle = 0U; angle < 4U; angle++) {
			for (slot = 0U; slot < 2U; slot++) {
				for (direct = 0U; direct < 2U; direct++) {
					for (unit = 1U; unit <= 256U; unit *= 256U) {
						/* The output at its corner. */
						corner_x = 0;
						corner_y = 0;
						if (slot == 1U) {
							corner_x = 10;
							corner_y = -4;
						}

						/* Its geometry. */
						geometry_one(&geometry, slot, corner_x, corner_y, test_panels[panel][0], test_panels[panel][1], test_angles[angle]);

						/* Every value pair against the reference. */
						same = 1;
						for (value_y = -5; value_y <= 25; value_y++) {
							for (value_x = -5; value_x <= 25; value_x++) {
								across.value = value_x;
								down.value = value_y;
								kwl_cursor_geometry_place(&geometry, slot, direct, unit, &across, &down, &x, &y);
								reference_place(test_angles[angle], test_panels[panel][0], test_panels[panel][1], direct, unit, &across, &down, corner_x, corner_y, &expected_x, &expected_y);
								if (x != expected_x || y != expected_y)
									same = 0;
							}
						}

						/* The case's verdict. */
						check(same, "a report maps as the reference does");
					}
				}
			}
		}
	}

	/* At 0 a direct device maps as before (logical is physical): the old pixel and 24.8 arithmetic. */
	geometry_one(&geometry, KWL_PLANE_ANCHOR, 0, 0, 1280U, 800U, KWL_ROTATE_0);
	across.value = 7;
	down.value = 11;
	kwl_cursor_geometry_place(&geometry, KWL_PLANE_ANCHOR, 1U, 1U, &across, &down, &x, &y);
	check(x == reference_scale(7, -3, 20, 1280U, 1U) && y == reference_scale(11, -3, 20, 800U, 1U), "0 keeps the old pixel arithmetic");
	kwl_cursor_geometry_place(&geometry, KWL_PLANE_ANCHOR, 1U, 256U, &across, &down, &x, &y);
	check(x == reference_scale(7, -3, 20, 1280U, 256U) && y == reference_scale(11, -3, 20, 800U, 256U), "0 keeps the old 24.8 arithmetic");

	/* The design's example: 90 on 1280x800, a range of the panel's pixels. */
	geometry_one(&geometry, KWL_PLANE_ANCHOR, 0, 0, 1280U, 800U, KWL_ROTATE_90);
	across.minimum = 0;
	across.maximum = 1279;
	down.minimum = 0;
	down.maximum = 799;
	across.value = 1000;
	down.value = 400;
	kwl_cursor_geometry_place(&geometry, KWL_PLANE_ANCHOR, 1U, 1U, &across, &down, &x, &y);
	check(x == 400 && y == 279, "90: the panel's (1000, 400) is the logical (400, 279)");
	kwl_cursor_geometry_place(&geometry, KWL_PLANE_ANCHOR, 0U, 1U, &across, &down, &x, &y);
	check(x == 1000 * 799 / 1279 && y == 400 * 1279 / 799, "90: a pen tablet's (1000, 400) is not turned");

	/* An output not shown is the anchor (4x4, not turned). */
	geometry_one(&geometry, 1U, 10, -4, 5U, 3U, KWL_ROTATE_90);
	geometry.outputs[1].width = 0U;
	across.value = 1279;
	down.value = 0;
	kwl_cursor_geometry_place(&geometry, 1U, 1U, 1U, &across, &down, &x, &y);
	check(x == 3 && y == 0, "an output not shown is the anchor");
	kwl_cursor_geometry_place(&geometry, KWL_PLANE_SLOTS, 1U, 1U, &across, &down, &x, &y);
	check(x == 3 && y == 0, "a slot past the table is the anchor");
}

/*
 * A direct device whose range is the panel's pixels reaches every logical
 * pixel of each small panel at each angle once, inside the picture.
 */
static void
test_coverage(void)
{
	struct kwl_cursor_geometry geometry;
	struct kwl_cursor_axis across;
	struct kwl_cursor_axis down;
	unsigned hits[TEST_PIXELS];
	uint32_t width;
	uint32_t height;
	int32_t x;
	int32_t y;
	unsigned panel;
	unsigned angle;
	unsigned index;
	int inside;
	int each_once;

	/* Each small panel at each angle. */
	for (panel = 0U; panel + 1U < TEST_PANELS; panel++) {
		for (angle = 0U; angle < 4U; angle++) {
			geometry_one(&geometry, KWL_PLANE_ANCHOR, 0, 0, test_panels[panel][0], test_panels[panel][1], test_angles[angle]);
			width = geometry.outputs[KWL_PLANE_ANCHOR].width;
			height = geometry.outputs[KWL_PLANE_ANCHOR].height;
			across.minimum = 0;
			across.maximum = (int32_t)test_panels[panel][0] - 1;
			down.minimum = 0;
			down.maximum = (int32_t)test_panels[panel][1] - 1;
			memset(hits, 0, sizeof(hits));
			inside = 1;

			/* Each physical pixel the device can report. */
			for (down.value = 0; down.value <= down.maximum; down.value++) {
				for (across.value = 0; across.value <= across.maximum; across.value++) {
					kwl_cursor_geometry_place(&geometry, KWL_PLANE_ANCHOR, 1U, 1U, &across, &down, &x, &y);
					if (x < 0 || y < 0 || x >= (int32_t)width || y >= (int32_t)height) {
						inside = 0;
						continue;
					}

					/* Counted. */
					hits[(unsigned)y * width + (unsigned)x]++;
				}
			}

			/* Every logical pixel once. */
			check(inside, "a direct report lands inside the logical picture");
			each_once = 1;
			for (index = 0U; index < width * height; index++) {
				if (hits[index] != 1U)
					each_once = 0;
			}

			/* None missed, none twice. */
			check(each_once, "every logical pixel is reached once");
		}
	}
}

/* Does nothing to the place (a warp that only gives the geometry). */
static void
test_keep(
	const void *context,
	const struct kwl_plane_rect *outputs,
	int32_t *x,
	int32_t *y,
	unsigned *output)
{
	/* The place stays. */
	(void)context;
	(void)outputs;
	(void)x;
	(void)y;
	(void)output;
}

/*
 * The box: an absolute pointer's place over a 1280x800 anchor at each
 * angle (a range of the panel's pixels), its relative motion kept on the
 * logical picture, and the geometry written whole.
 */
static void
test_box(void)
{
	struct kwl_cursor_box box;
	struct kwl_cursor_geometry geometry;
	struct kwl_cursor_motion motion;
	int32_t x;
	int32_t y;
	int32_t expected_x;
	int32_t expected_y;
	unsigned output;
	unsigned angle;
	int error;
	int physical_differs;
	int outputs_differ;

	/* A box over the anchor. */
	error = kwl_cursor_box_init(&box, 0, 0, 1280U, 800U);
	check(error == 0, "the box is made");
	if (error != 0)
		return;
	check(box.physical[KWL_PLANE_ANCHOR].width == 1280U && box.physical[KWL_PLANE_ANCHOR].rotation == 0U, "the box starts with the anchor's panel at 0");

	/* Each angle: the geometry given, then reports at the panel's corners and a middle point. */
	for (angle = 0U; angle < 4U; angle++) {
		geometry_one(&geometry, KWL_PLANE_ANCHOR, 0, 0, 1280U, 800U, test_angles[angle]);
		kwl_cursor_box_outputs(&box, &geometry);
		physical_differs = memcmp(box.physical, geometry.physical, sizeof(box.physical));
		outputs_differ = memcmp(box.outputs, geometry.outputs, sizeof(box.outputs));
		check(physical_differs == 0 && outputs_differ == 0, "kwl_cursor_box_outputs writes the geometry whole");

		/* The panel's (1000, 400), and its last pixel. */
		kwl_cursor_box_place(&box, 1000, 0, 1279, 400, 0, 799, 0, 0, 1U, &motion);
		kwl_rotate_point_back(test_angles[angle], 1280U, 800U, 1000, 400, &expected_x, &expected_y);
		check(motion.x == expected_x && motion.y == expected_y && motion.output == KWL_PLANE_ANCHOR, "an absolute pointer's place is the panel's point turned back");
		kwl_cursor_box_place(&box, 1279, 0, 1279, 799, 0, 799, 0, 0, 1U, &motion);
		kwl_rotate_point_back(test_angles[angle], 1280U, 800U, 1279, 799, &expected_x, &expected_y);
		check(motion.x == expected_x && motion.y == expected_y, "the panel's last pixel is a corner of the picture");

		/* A relative motion with it is kept on the logical picture. */
		kwl_cursor_box_place(&box, 1279, 0, 1279, 799, 0, 799, 5000, 5000, 1U, &motion);
		check(motion.x == (int32_t)geometry.outputs[KWL_PLANE_ANCHOR].width - 1 &&
		      motion.y == (int32_t)geometry.outputs[KWL_PLANE_ANCHOR].height - 1,
		      "a relative motion stops at the logical picture's edge");
		kwl_cursor_box_place(&box, 0, 0, 1279, 0, 0, 799, -5000, -5000, 1U, &motion);
		check(motion.x == 0 && motion.y == 0, "a relative motion stops at the logical picture's top left");
	}

	/* A warp with a geometry writes it whole, with the absolute pointer's output; one without keeps it. */
	geometry_one(&geometry, 2U, 1280, 0, 800U, 600U, KWL_ROTATE_270);
	geometry.absolute_slot = 2U;
	kwl_cursor_box_warp(&box, &geometry, test_keep, NULL, &motion);
	physical_differs = memcmp(box.physical, geometry.physical, sizeof(box.physical));
	outputs_differ = memcmp(box.outputs, geometry.outputs, sizeof(box.outputs));
	check(physical_differs == 0 && outputs_differ == 0 && box.absolute_slot == 2U, "a warp writes the geometry whole");
	kwl_cursor_box_warp(&box, NULL, test_keep, NULL, &motion);
	check(box.absolute_slot == 2U && box.physical[2].rotation == KWL_ROTATE_270, "a warp without a geometry keeps it");

	/* The absolute pointer now maps onto that head (600x800 logical at 1280, 0). */
	kwl_cursor_box_place(&box, 0, 0, 799, 0, 0, 599, 0, 0, 1U, &motion);
	kwl_rotate_point_back(KWL_ROTATE_270, 800U, 600U, 0, 0, &expected_x, &expected_y);
	check(motion.output == 2U && motion.x == 1280 + expected_x && motion.y == expected_y, "the absolute pointer maps onto its output");
	kwl_cursor_box_read(&box, &x, &y, &output);
	check(x == motion.x && y == motion.y && output == 2U, "the box holds the place");

	/* The box goes. */
	kwl_cursor_box_destroy(&box);
}
