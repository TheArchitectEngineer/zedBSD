/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of a rotated display's arithmetic (ws217-p002a,
 * userland/desktop/wayland/rotate.c): the points, the 24.8 fixed-point
 * places, the rectangles, the directions and the sizes at every angle on
 * panels that are not square, every pixel and the way back; the angles a
 * display may take and why not; how an angle is shown; and the steps of a
 * change of an output's state, the sixteen pairs of angles of the design's
 * table (plan/ws217/phase001/phase.md, section 4.6); the turn pass's
 * texture coordinates against the points (ws217-p002b) and what a user's
 * choice of an angle does; the inline way back of the absolute devices
 * against the points and the fixed-point places (ws217-p003); a floating
 * window's place fitted to an output turned and back (BUG-306); and the
 * wait for the windows' images of the new size after a turn (BUG-306:
 * the serials acknowledged and the end of the wait).
 */

#include "rotate.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The panels the points are walked on: wide, tall, one pixel, a thin strip. */
#define TEST_PANELS	4U

/* The most pixels a panel of the walk has. */
#define TEST_PIXELS	64U

/* The four angles, in order. */
static const unsigned test_angles[4] = { KWL_ROTATE_0, KWL_ROTATE_90, KWL_ROTATE_180, KWL_ROTATE_270 };

/* The physical sizes of the panels walked. */
static const uint32_t test_panels[TEST_PANELS][2] = { { 5U, 3U }, { 3U, 5U }, { 1U, 1U }, { 8U, 2U } };

/* The number of failed checks. */
static unsigned failures;

static void check(int condition, const char *what);
static void test_sizes(void);
static void test_points(void);
static void test_fixed(void);
static void test_rects(void);
static void test_vectors(void);
static void test_mask(void);
static void test_report(void);
static void test_plan(void);
static void test_steps(void);
static void test_coefficients(void);
static void test_select(void);
static void test_pointer(void);
static void test_pixel_back(void);
static void test_fit_window(void);
static void test_hold(void);
static void test_state(struct kwl_rotate_state *state, uint32_t width, uint32_t height, unsigned rotation, unsigned hw180, unsigned policy_any);

/* Runs every check. */
int
main(void)
{
	/* Each part. */
	test_sizes();
	test_points();
	test_fixed();
	test_rects();
	test_vectors();
	test_mask();
	test_report();
	test_plan();
	test_steps();
	test_coefficients();
	test_select();
	test_pointer();
	test_pixel_back();
	test_fit_window();
	test_hold();

	/* The verdict. */
	if (failures != 0U) {
		printf("host-rotate: %u failures\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-rotate: PASS\n");
	return 0;
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

/* The angles' bits and sizes: a quarter turn exchanges the sides, a half turn keeps them. */
static void
test_sizes(void)
{
	uint32_t width;
	uint32_t height;
	int valid;

	/* The four angles are angles; others are not. */
	valid = kwl_rotate_valid(KWL_ROTATE_270);
	check(valid, "270 is an angle");
	valid = kwl_rotate_valid(45U);
	check(!valid, "45 is not an angle");
	valid = kwl_rotate_valid(360U);
	check(!valid, "360 is not an angle");
	check(kwl_rotate_bit(KWL_ROTATE_180) == KWL_ROTATION_180, "180's bit");

	/* 1280x800 on its side is 800x1280, upside down 1280x800. */
	kwl_rotate_size(KWL_ROTATE_90, 1280U, 800U, &width, &height);
	check(width == 800U && height == 1280U, "90 swaps the sides");
	kwl_rotate_size(KWL_ROTATE_270, 1280U, 800U, &width, &height);
	check(width == 800U && height == 1280U, "270 swaps the sides");
	kwl_rotate_size(KWL_ROTATE_180, 1280U, 800U, &width, &height);
	check(width == 1280U && height == 800U, "180 keeps the sides");
	kwl_rotate_size(KWL_ROTATE_0, 1280U, 800U, &width, &height);
	check(width == 1280U && height == 800U, "0 keeps the sides");
}

/*
 * Every logical pixel of every panel at every angle: inside the panel, a
 * different physical pixel for each (every one of them reached), and back
 * where it came from; and the design's corners of a 1280x800 panel.
 */
static void
test_points(void)
{
	unsigned hits[TEST_PIXELS];
	uint32_t physical_width;
	uint32_t physical_height;
	uint32_t width;
	uint32_t height;
	int32_t x;
	int32_t y;
	int32_t physical_x;
	int32_t physical_y;
	int32_t back_x;
	int32_t back_y;
	unsigned panel;
	unsigned angle;
	unsigned index;
	int inside;
	int each_once;

	/* Each panel at each angle. */
	for (panel = 0U; panel < TEST_PANELS; panel++) {
		physical_width = test_panels[panel][0];
		physical_height = test_panels[panel][1];
		for (angle = 0U; angle < 4U; angle++) {
			/* The logical size, and no physical pixel reached yet. */
			kwl_rotate_size(test_angles[angle], physical_width, physical_height, &width, &height);
			memset(hits, 0, sizeof(hits));
			inside = 1;

			/* Each logical pixel. */
			for (y = 0; y < (int32_t)height; y++) {
				for (x = 0; x < (int32_t)width; x++) {
					/* Its physical pixel, inside the panel, and the way back. */
					kwl_rotate_point(test_angles[angle], physical_width, physical_height, x, y, &physical_x, &physical_y);
					if (physical_x < 0 ||
					    physical_y < 0 ||
					    physical_x >= (int32_t)physical_width ||
					    physical_y >= (int32_t)physical_height) {
						inside = 0;
						continue;
					}

					/* Counted once, and back where it came from. */
					hits[(unsigned)physical_y * physical_width + (unsigned)physical_x]++;
					kwl_rotate_point_back(test_angles[angle], physical_width, physical_height, physical_x, physical_y, &back_x, &back_y);
					check(back_x == x && back_y == y, "a pixel comes back where it came from");
				}
			}

			/* Every physical pixel reached once. */
			check(inside, "every logical pixel is shown inside the panel");
			each_once = 1;
			for (index = 0U; index < physical_width * physical_height; index++) {
				if (hits[index] != 1U)
					each_once = 0;
			}

			/* None missed, none twice. */
			check(each_once, "every physical pixel shows one logical pixel");
		}
	}

	/* The design's corners: the logical top left on a 1280x800 panel. */
	kwl_rotate_point(KWL_ROTATE_90, 1280U, 800U, 0, 0, &physical_x, &physical_y);
	check(physical_x == 1279 && physical_y == 0, "90 shows the top left at the panel's top right");
	kwl_rotate_point(KWL_ROTATE_180, 1280U, 800U, 0, 0, &physical_x, &physical_y);
	check(physical_x == 1279 && physical_y == 799, "180 shows the top left at the panel's bottom right");
	kwl_rotate_point(KWL_ROTATE_270, 1280U, 800U, 0, 0, &physical_x, &physical_y);
	check(physical_x == 0 && physical_y == 799, "270 shows the top left at the panel's bottom left");
	kwl_rotate_point(KWL_ROTATE_90, 1280U, 800U, 799, 1279, &physical_x, &physical_y);
	check(physical_x == 0 && physical_y == 799, "90 shows the bottom right at the panel's bottom left");
}

/*
 * The 24.8 fixed-point places: every sub-pixel of a small panel's logical
 * picture is in the physical pixel of its pixel, and comes back exactly.
 */
static void
test_fixed(void)
{
	uint32_t physical_width;
	uint32_t physical_height;
	uint32_t width;
	uint32_t height;
	int32_t x;
	int32_t y;
	int32_t physical_x;
	int32_t physical_y;
	int32_t pixel_x;
	int32_t pixel_y;
	int32_t back_x;
	int32_t back_y;
	unsigned angle;
	int matches;
	int exact;

	/* A 5x3 panel at each angle, every 7th sub-pixel each way (all of them meet every pixel). */
	physical_width = 5U;
	physical_height = 3U;
	for (angle = 0U; angle < 4U; angle++) {
		kwl_rotate_size(test_angles[angle], physical_width, physical_height, &width, &height);
		matches = 1;
		exact = 1;
		for (y = 0; y < (int32_t)(height * 256U); y += 7) {
			for (x = 0; x < (int32_t)(width * 256U); x += 7) {
				/* The place, the pixel its pixel is shown at, and the way back. */
				kwl_rotate_fixed(test_angles[angle], physical_width, physical_height, x, y, &physical_x, &physical_y);
				kwl_rotate_point(test_angles[angle], physical_width, physical_height, x / 256, y / 256, &pixel_x, &pixel_y);
				if (physical_x / 256 != pixel_x || physical_y / 256 != pixel_y)
					matches = 0;
				if (physical_x < 0 || physical_y < 0)
					matches = 0;
				kwl_rotate_fixed_back(test_angles[angle], physical_width, physical_height, physical_x, physical_y, &back_x, &back_y);
				if (back_x != x || back_y != y)
					exact = 0;
			}
		}

		/* Every place in its pixel, every one back. */
		check(matches, "a fixed-point place is in the physical pixel of its pixel");
		check(exact, "a fixed-point place comes back exactly");
	}
}

/*
 * Every rectangle of a 5x3 panel's logical picture at every angle: the
 * physical rectangle holds exactly the pixels the rectangle's pixels are
 * shown at (the same area, each inside), and comes back.
 */
static void
test_rects(void)
{
	uint32_t physical_width;
	uint32_t physical_height;
	uint32_t width;
	uint32_t height;
	int32_t box[4];
	int32_t physical_box[4];
	int32_t back[4];
	int32_t x;
	int32_t y;
	int32_t physical_x;
	int32_t physical_y;
	int64_t area;
	int64_t physical_area;
	unsigned angle;
	int inside;
	int same;
	int differs;

	/* Each angle, each rectangle (left, top, right, bottom) of at least one pixel. */
	physical_width = 5U;
	physical_height = 3U;
	for (angle = 0U; angle < 4U; angle++) {
		kwl_rotate_size(test_angles[angle], physical_width, physical_height, &width, &height);
		inside = 1;
		same = 1;
		for (box[0] = 0; box[0] < (int32_t)width; box[0]++) {
			for (box[2] = box[0] + 1; box[2] <= (int32_t)width; box[2]++) {
				for (box[1] = 0; box[1] < (int32_t)height; box[1]++) {
					for (box[3] = box[1] + 1; box[3] <= (int32_t)height; box[3]++) {
						/* The physical rectangle, its area, and the way back. */
						kwl_rotate_rect(test_angles[angle], physical_width, physical_height, box, physical_box);
						area = (int64_t)(box[2] - box[0]) * (box[3] - box[1]);
						physical_area = (int64_t)(physical_box[2] - physical_box[0]) * (physical_box[3] - physical_box[1]);
						if (area != physical_area)
							same = 0;
						kwl_rotate_rect_back(test_angles[angle], physical_width, physical_height, physical_box, back);
						differs = memcmp(back, box, sizeof(back));
						if (differs != 0)
							same = 0;

						/* Each of its pixels inside the physical rectangle. */
						for (y = box[1]; y < box[3]; y++) {
							for (x = box[0]; x < box[2]; x++) {
								kwl_rotate_point(test_angles[angle], physical_width, physical_height, x, y, &physical_x, &physical_y);
								if (physical_x < physical_box[0] ||
								    physical_x >= physical_box[2] ||
								    physical_y < physical_box[1] ||
								    physical_y >= physical_box[3])
									inside = 0;
							}
						}
					}
				}
			}
		}

		/* Every rectangle the same area and back, every pixel inside. */
		check(same, "a rectangle keeps its area and comes back");
		check(inside, "a rectangle's pixels are inside its physical rectangle");
	}

	/* The design's example: the logical top strip of 800x1280 at 90 is the panel's right column. */
	box[0] = 0;
	box[1] = 0;
	box[2] = 800;
	box[3] = 10;
	kwl_rotate_rect(KWL_ROTATE_90, 1280U, 800U, box, physical_box);
	check(physical_box[0] == 1270 && physical_box[1] == 0 && physical_box[2] == 1280 && physical_box[3] == 800, "90's top strip is the right column");
}

/*
 * A pen's tilt: the logical direction from one point to another is the
 * physical direction between where they are shown, turned back.
 */
static void
test_vectors(void)
{
	int32_t from_x;
	int32_t from_y;
	int32_t to_x;
	int32_t to_y;
	int32_t x;
	int32_t y;
	unsigned angle;

	/* Each angle: from (1, 1) to (4, 2) on a 1280x800 panel's logical picture. */
	for (angle = 0U; angle < 4U; angle++) {
		kwl_rotate_point(test_angles[angle], 1280U, 800U, 1, 1, &from_x, &from_y);
		kwl_rotate_point(test_angles[angle], 1280U, 800U, 4, 2, &to_x, &to_y);
		kwl_rotate_vector(test_angles[angle], to_x - from_x, to_y - from_y, &x, &y);
		check(x == 3 && y == 1, "the tilt turns back with the picture");
	}
}

/*
 * The angles a display may take and why not: the hardware's 180, whether
 * the turn pass is offered and built, and the test policy.
 */
static void
test_mask(void)
{
	unsigned scanout;
	unsigned turn;
	unsigned mask;
	unsigned why;
	int error;

	/* A display whose hardware cannot turn and no turn pass offered (the Pi 4 before WS141 p015): nothing. */
	mask = kwl_rotate_mask(0U, 0U, 0U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == KWL_ROTATION_0 && why == KWL_ROTATE_WHY_SCANOUT, "no hardware, no turn: 0 only, scanout");
	mask = kwl_rotate_mask(0U, 0U, 1U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == KWL_ROTATION_0 && why == KWL_ROTATE_WHY_SCANOUT, "no hardware, the turn built but not offered (rpi4): 0 only, scanout");

	/* The turn pass offered and built turns every angle, 180 too where the hardware cannot (ws217-p004, before ws217-p010). */
	mask = kwl_rotate_mask(0U, 1U, 1U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == 15U && why == 0U, "no hardware, the turn offered: all four");

	/* The hardware turns by 180: the quarter turns where the pass is offered and built. */
	mask = kwl_rotate_mask(1U, 0U, 0U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == (KWL_ROTATION_0 | KWL_ROTATION_180) && why == KWL_ROTATE_WHY_TURN, "the hardware, no turn: 0 and 180, turn");
	mask = kwl_rotate_mask(1U, 1U, 1U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == 15U && why == 0U, "the hardware and the turn: all four");
	mask = kwl_rotate_mask(1U, 0U, 1U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == (KWL_ROTATION_0 | KWL_ROTATION_180) && why == KWL_ROTATE_WHY_TURN, "the hardware, the turn built but not offered (rpi4): 0 and 180, turn");
	mask = kwl_rotate_mask(1U, 1U, 0U, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == (KWL_ROTATION_0 | KWL_ROTATION_180) && why == KWL_ROTATE_WHY_TURN, "the hardware, the turn offered but not built: 0 and 180");

	/* The test policy: all four where the build has the pass, offered by default or not. */
	mask = kwl_rotate_mask(0U, 1U, 1U, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == 15U && why == 0U, "any, no hardware, the turn built: all four");
	mask = kwl_rotate_mask(1U, 1U, 1U, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == 15U && why == 0U, "any, the hardware and the turn: all four");
	mask = kwl_rotate_mask(0U, 0U, 1U, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == 15U && why == 0U, "any, the turn built but not offered (rpi4): all four, to measure");
	mask = kwl_rotate_mask(0U, 1U, 0U, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == KWL_ROTATION_0 && why == KWL_ROTATE_WHY_SCANOUT, "any without the turn and the hardware: 0 only");
	mask = kwl_rotate_mask(1U, 1U, 0U, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == (KWL_ROTATION_0 | KWL_ROTATION_180) && why == KWL_ROTATE_WHY_TURN, "any without the turn: 0 and 180");

	/* This build has the turn pass (ws217-p002b); an Intel GPU without the hardware's 180 offers all four by the turn (ws217-p004). */
	check(KWL_TURN_BUILD == 1U, "this build has the turn pass");
	mask = kwl_rotate_mask(0U, 1U, KWL_TURN_BUILD, KWL_ROTATE_POLICY_DEFAULT, &why);
	check(mask == 15U, "this build, no hardware, the turn offered: all four");
	mask = kwl_rotate_mask(0U, 0U, KWL_TURN_BUILD, KWL_ROTATE_POLICY_ANY, &why);
	check(mask == 15U, "this build, the test policy, a GPU not offered the turn: all four");

	/* Without the hardware's 180, 180 is the turn's. */
	error = kwl_rotate_plan(KWL_ROTATE_180, 15U, 0U, &scanout, &turn);
	check(error == 0 && scanout == 0U && turn == KWL_ROTATE_180, "180 without the hardware: the turn");
}

/*
 * What Settings is told of a display's angles (ws217-p004): its own alone,
 * the angle chosen only beside another display, with the reason.
 */
static void
test_report(void)
{
	unsigned mask;
	unsigned why;

	/* Alone: the display's angles and reasons as they are. */
	kwl_rotate_report(KWL_ROTATE_90, 15U, 0U, 1U, &mask, &why);
	check(mask == 15U && why == 0U, "alone: all four, no reason");
	kwl_rotate_report(KWL_ROTATE_0, KWL_ROTATION_0, KWL_ROTATE_WHY_SCANOUT, 1U, &mask, &why);
	check(mask == KWL_ROTATION_0 && why == KWL_ROTATE_WHY_SCANOUT, "alone, fixed: 0, scanout");

	/* Beside another display: the angle chosen only, and why. */
	kwl_rotate_report(KWL_ROTATE_90, 15U, 0U, 0U, &mask, &why);
	check(mask == KWL_ROTATION_90 && why == KWL_ROTATE_WHY_ALONE, "beside a head: 90 alone, alone");
	kwl_rotate_report(KWL_ROTATE_0, 15U, KWL_ROTATE_WHY_RESOURCE, 0U, &mask, &why);
	check(mask == KWL_ROTATION_0 && why == (KWL_ROTATE_WHY_RESOURCE | KWL_ROTATE_WHY_ALONE), "beside a head: the reasons kept");
	kwl_rotate_report(45U, 15U, 0U, 0U, &mask, &why);
	check(mask == KWL_ROTATION_0 && why == KWL_ROTATE_WHY_ALONE, "beside a head, not an angle: 0");

	/* The bits are the wire's (libkeiland's KL_DISPLAY_ROTATE_WHY_ALONE). */
	check(KWL_ROTATE_WHY_ALONE == 16U, "the reason beside another display is 16");
}

/*
 * The turn pass's texture coordinates: at the middle of every physical
 * pixel of every panel and angle, the coordinates times the logical size
 * fall inside the logical pixel kwl_rotate_point_back gives, a quarter of
 * a pixel away from its sides at least.
 */
static void
test_coefficients(void)
{
	uint32_t physical_width;
	uint32_t physical_height;
	uint32_t logical_width;
	uint32_t logical_height;
	unsigned panel;
	unsigned angle;
	int32_t physical_x;
	int32_t physical_y;
	int32_t x;
	int32_t y;
	float u[3];
	float v[3];
	float corner_x;
	float corner_y;
	float texel_x;
	float texel_y;
	int inside;

	/* Each panel and angle. */
	for (panel = 0U; panel < TEST_PANELS; panel++) {
		for (angle = 0U; angle < 4U; angle++) {
			/* The sizes and the coefficients. */
			physical_width = test_panels[panel][0];
			physical_height = test_panels[panel][1];
			kwl_rotate_size(test_angles[angle], physical_width, physical_height, &logical_width, &logical_height);
			kwl_turn_coefficients(test_angles[angle], u, v);

			/* Each physical pixel's middle. */
			for (physical_y = 0; physical_y < (int32_t)physical_height; physical_y++) {
				for (physical_x = 0; physical_x < (int32_t)physical_width; physical_x++) {
					/* The point of the whole output, 0 to 1, and the logical texel there. */
					corner_x = ((float)physical_x + 0.5f) / (float)physical_width;
					corner_y = ((float)physical_y + 0.5f) / (float)physical_height;
					texel_x = (u[0] * corner_x + u[1] * corner_y + u[2]) * (float)logical_width;
					texel_y = (v[0] * corner_x + v[1] * corner_y + v[2]) * (float)logical_height;

					/* The logical pixel the point maps back to holds it, well inside. */
					kwl_rotate_point_back(test_angles[angle], physical_width, physical_height, physical_x, physical_y, &x, &y);
					inside = 0;
					if (texel_x > (float)x + 0.25f &&
					    texel_x < (float)x + 0.75f &&
					    texel_y > (float)y + 0.25f &&
					    texel_y < (float)y + 0.75f)
						inside = 1;
					check(inside, "the turn's texture coordinates fall on the logical pixel");
				}
			}
		}
	}
}

/*
 * What a user's choice of an angle does: refused outside the mask, nothing
 * for the same choice shown, tried again for the same choice not shown,
 * saved for a closed output, the direct scanout left, else applied.
 */
static void
test_select(void)
{
	unsigned outcome;

	/* An angle the display may not take, and a number that is no angle. */
	outcome = kwl_rotate_select(KWL_ROTATE_90, KWL_ROTATION_0, 0U, 0U, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_REFUSE, "an angle outside the mask is refused");
	outcome = kwl_rotate_select(45U, 15U, 0U, 0U, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_REFUSE, "a number that is no angle is refused");

	/* The same choice, shown: nothing; not shown (the turn's images could not be made): again. */
	outcome = kwl_rotate_select(KWL_ROTATE_90, 15U, KWL_ROTATE_90, KWL_ROTATE_90, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_NOTHING, "the same choice shown changes nothing");
	outcome = kwl_rotate_select(KWL_ROTATE_90, 15U, KWL_ROTATE_90, KWL_ROTATE_0, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_APPLY, "the same choice not shown is tried again");

	/* A closed output, a direct scanout (the output closed meanwhile, review M1), an open output. */
	outcome = kwl_rotate_select(KWL_ROTATE_270, 15U, KWL_ROTATE_0, KWL_ROTATE_0, 0U, 0U);
	check(outcome == KWL_ROTATE_SELECT_SAVE, "a closed output saves the choice");
	outcome = kwl_rotate_select(KWL_ROTATE_270, 15U, KWL_ROTATE_0, KWL_ROTATE_0, 0U, 1U);
	check(outcome == KWL_ROTATE_SELECT_LEAVE, "a direct scanout (output closed) is left first");
	outcome = kwl_rotate_select(KWL_ROTATE_270, 15U, KWL_ROTATE_0, KWL_ROTATE_0, 1U, 1U);
	check(outcome == KWL_ROTATE_SELECT_LEAVE, "a direct scanout is left first");
	outcome = kwl_rotate_select(KWL_ROTATE_270, 15U, KWL_ROTATE_0, KWL_ROTATE_0, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_APPLY, "an open output shows the choice now");
	outcome = kwl_rotate_select(KWL_ROTATE_0, KWL_ROTATION_0, KWL_ROTATE_90, KWL_ROTATE_0, 1U, 0U);
	check(outcome == KWL_ROTATE_SELECT_APPLY, "back to 0 from a choice not shown");
}

/* How each angle is shown, for the masks the policy gives. */
static void
test_plan(void)
{
	unsigned scanout;
	unsigned turn;
	int error;

	/* 0 is always shown as it is. */
	error = kwl_rotate_plan(KWL_ROTATE_0, KWL_ROTATION_0, 0U, &scanout, &turn);
	check(error == 0 && scanout == 0U && turn == 0U, "0 needs nothing");

	/* An angle the mask does not allow is refused; a number that is no angle is not one. */
	error = kwl_rotate_plan(KWL_ROTATE_90, KWL_ROTATION_0, 0U, &scanout, &turn);
	check(error == ENOTSUP, "90 outside the mask is unsupported");
	error = kwl_rotate_plan(KWL_ROTATE_180, KWL_ROTATION_0, 0U, &scanout, &turn);
	check(error == ENOTSUP, "180 outside the mask is unsupported");
	error = kwl_rotate_plan(45U, 15U, 1U, &scanout, &turn);
	check(error == EINVAL, "45 is not an angle");

	/* The hardware's 180, the turn's quarter turns. */
	error = kwl_rotate_plan(KWL_ROTATE_180, 15U, 1U, &scanout, &turn);
	check(error == 0 && scanout == 180U && turn == 0U, "180 by the hardware");
	error = kwl_rotate_plan(KWL_ROTATE_90, 15U, 1U, &scanout, &turn);
	check(error == 0 && scanout == 0U && turn == 90U, "90 by the turn");
	error = kwl_rotate_plan(KWL_ROTATE_270, 15U, 1U, &scanout, &turn);
	check(error == 0 && scanout == 0U && turn == 270U, "270 by the turn");

	/* The test policy on a display without the hardware: 180 by the turn. */
	error = kwl_rotate_plan(KWL_ROTATE_180, 15U, 0U, &scanout, &turn);
	check(error == 0 && scanout == 0U && turn == 180U, "180 by the turn without the hardware");
}

/* Makes an output's state on a 1280x800 panel at an angle, shown as the plan says. */
static void
test_state(
	struct kwl_rotate_state *state,
	uint32_t width,
	uint32_t height,
	unsigned rotation,
	unsigned hw180,
	unsigned policy_any)
{
	unsigned policy;
	unsigned mask;
	unsigned why;
	int error;

	/* The logical size of the angle. */
	memset(state, 0, sizeof(*state));
	kwl_rotate_size(rotation, width, height, &state->width, &state->height);
	state->selected = rotation;
	state->rotation = rotation;

	/* How it is shown, with the turn pass built. */
	policy = KWL_ROTATE_POLICY_DEFAULT;
	if (policy_any)
		policy = KWL_ROTATE_POLICY_ANY;
	mask = kwl_rotate_mask(hw180, 1U, 1U, policy, &why);
	error = kwl_rotate_plan(rotation, mask, hw180, &state->scanout, &state->turn);
	check(error == 0, "the steps' angle is shown");
}

/*
 * The steps of the design's sixteen pairs (a display whose hardware turns
 * by 180, the turn pass built); 0 and 180 on a display without the
 * hardware under the test policy; an opening's steps; a game's pause and
 * end; and the choice saved only when the user changes it.
 */
static void
test_steps(void)
{
	/* The design's table (section 4.6): rows are the angle before, columns the angle after. */
	static const unsigned expected[4][4] = {
		{ 0U, KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED,
		  KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED },
		{ KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, 0U,
		  KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED },
		{ KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED, KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, 0U,
		  KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED },
		{ KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED,
		  KWL_ROTATE_STEP_REOPEN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED, 0U }
	};
	struct kwl_rotate_state before;
	struct kwl_rotate_state after;
	char what[96];
	unsigned row;
	unsigned column;
	unsigned steps;

	/* Each pair, without a choice to keep (a game's end takes every step but the saving). */
	for (row = 0U; row < 4U; row++) {
		for (column = 0U; column < 4U; column++) {
			test_state(&before, 1280U, 800U, test_angles[row], 1U, 0U);
			test_state(&after, 1280U, 800U, test_angles[column], 1U, 0U);
			steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_SESSION_RESUME);
			(void)snprintf(what, sizeof(what), "steps from %u to %u", test_angles[row], test_angles[column]);
			check(steps == expected[row][column], what);
		}
	}

	/* The same pairs kept by the user (set_rotation): saved exactly when the angle changes. */
	for (row = 0U; row < 4U; row++) {
		for (column = 0U; column < 4U; column++) {
			test_state(&before, 1280U, 800U, test_angles[row], 1U, 0U);
			test_state(&after, 1280U, 800U, test_angles[column], 1U, 0U);
			steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_USER);
			(void)snprintf(what, sizeof(what), "kept steps from %u to %u", test_angles[row], test_angles[column]);
			if (row == column) {
				check(steps == 0U, what);
			} else {
				check(steps == (expected[row][column] | KWL_ROTATE_STEP_SAVE), what);
			}
		}
	}

	/* 0 and 180 by the turn under the test policy, without the hardware: the same size, still rotated. */
	test_state(&before, 1280U, 800U, KWL_ROTATE_0, 0U, 1U);
	test_state(&after, 1280U, 800U, KWL_ROTATE_180, 0U, 1U);
	steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_SESSION_RESUME);
	check(steps == (KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED), "0 to 180 by the turn");
	steps = kwl_rotate_steps(&after, &before, KWL_ROTATE_MODE_SESSION_RESUME);
	check(steps == (KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED), "180 to 0 by the turn");

	/* An output opened on a display of another size at 0 (an output switch, review-4 C7): resized alone. */
	test_state(&before, 1280U, 800U, KWL_ROTATE_0, 0U, 0U);
	test_state(&after, 1920U, 1080U, KWL_ROTATE_0, 0U, 0U);
	steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_OPEN);
	check(steps == KWL_ROTATE_STEP_RESIZED, "another size at 0 is resized alone");

	/* The same display opened again at 0 (a game's end, a hotplug's reopen): nothing. */
	steps = kwl_rotate_steps(&before, &before, KWL_ROTATE_MODE_OPEN);
	check(steps == 0U, "the same opening takes no step");

	/* A switch between two displays of the same mode, 90 kept for one and 270 for the other: rotated, not resized. */
	test_state(&before, 1280U, 800U, KWL_ROTATE_90, 1U, 0U);
	test_state(&after, 1280U, 800U, KWL_ROTATE_270, 1U, 0U);
	steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_OPEN);
	check(steps == KWL_ROTATE_STEP_ROTATED, "90 to 270 on the same mode is rotated by the opening");

	/* Every pair at an opening: the turn and the reopening are the opening's own, nothing saved (review-4 L49). */
	for (row = 0U; row < 4U; row++) {
		for (column = 0U; column < 4U; column++) {
			test_state(&before, 1280U, 800U, test_angles[row], 1U, 0U);
			test_state(&after, 1280U, 800U, test_angles[column], 1U, 0U);
			steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_OPEN);
			(void)snprintf(what, sizeof(what), "opening steps from %u to %u", test_angles[row], test_angles[column]);
			check(steps == (expected[row][column] & (KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED)), what);
		}
	}

	/* The game's pause and its end: the angle shown goes to 0 and back, the choice stays (nothing saved). */
	test_state(&before, 1280U, 800U, KWL_ROTATE_90, 1U, 0U);
	test_state(&after, 1280U, 800U, KWL_ROTATE_0, 1U, 0U);
	after.selected = KWL_ROTATE_90;
	steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_SESSION_PAUSE);
	check(steps == (KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED), "a pause saves nothing");
	steps = kwl_rotate_steps(&after, &before, KWL_ROTATE_MODE_SESSION_RESUME);
	check(steps == (KWL_ROTATE_STEP_TURN | KWL_ROTATE_STEP_ROTATED | KWL_ROTATE_STEP_RESIZED), "an end saves nothing");

	/* The user's choice of the angle shown already (the game paused it): saved, nothing else. */
	test_state(&before, 1280U, 800U, KWL_ROTATE_0, 1U, 0U);
	before.selected = KWL_ROTATE_90;
	test_state(&after, 1280U, 800U, KWL_ROTATE_0, 1U, 0U);
	steps = kwl_rotate_steps(&before, &after, KWL_ROTATE_MODE_USER);
	check(steps == KWL_ROTATE_STEP_SAVE, "a choice alone is saved alone");
}

/*
 * A pointer's move when an output turns on the same panel (review S1):
 * every logical pixel of every panel, for every pair of angles, ends where
 * kwl_rotate_point and kwl_rotate_point_back take it; a place outside the
 * old picture is kept to its edge first; the 1280x800 case of the review.
 */
static void
test_pointer(void)
{
	uint32_t physical_width;
	uint32_t physical_height;
	uint32_t logical_width;
	uint32_t logical_height;
	unsigned panel;
	unsigned before;
	unsigned after;
	int32_t x;
	int32_t y;
	int32_t moved_x;
	int32_t moved_y;
	int32_t physical_x;
	int32_t physical_y;
	int32_t expected_x;
	int32_t expected_y;

	/* Each panel, each pair of angles, each logical pixel. */
	for (panel = 0U; panel < TEST_PANELS; panel++) {
		physical_width = test_panels[panel][0];
		physical_height = test_panels[panel][1];
		for (before = 0U; before < 4U; before++) {
			for (after = 0U; after < 4U; after++) {
				kwl_rotate_size(test_angles[before], physical_width, physical_height, &logical_width, &logical_height);
				for (y = 0; y < (int32_t)logical_height; y++) {
					for (x = 0; x < (int32_t)logical_width; x++) {
						/* The same physical point at the new angle. */
						moved_x = x;
						moved_y = y;
						kwl_rotate_pointer_place(test_angles[before], test_angles[after], physical_width, physical_height, &moved_x, &moved_y);
						kwl_rotate_point(test_angles[before], physical_width, physical_height, x, y, &physical_x, &physical_y);
						kwl_rotate_point_back(test_angles[after], physical_width, physical_height, physical_x, physical_y, &expected_x, &expected_y);
						check(moved_x == expected_x && moved_y == expected_y, "the pointer keeps its physical point");
					}
				}
			}
		}
	}

	/* The review's case: 1280x800, (1000, 400) at 0 is (400, 279) at 90; the last column's edge point. */
	x = 1000;
	y = 400;
	kwl_rotate_pointer_place(KWL_ROTATE_0, KWL_ROTATE_90, 1280U, 800U, &x, &y);
	check(x == 400 && y == 279, "1280x800: (1000, 400) at 0 is (400, 279) at 90");
	x = 1279;
	y = 0;
	kwl_rotate_pointer_place(KWL_ROTATE_0, KWL_ROTATE_90, 1280U, 800U, &x, &y);
	check(x == 0 && y == 0, "1280x800: the top right corner at 0 is the top left at 90");
	x = 799;
	y = 1279;
	kwl_rotate_pointer_place(KWL_ROTATE_90, KWL_ROTATE_270, 1280U, 800U, &x, &y);
	check(x == 0 && y == 0, "the bottom right at 90 is the top left at 270");

	/* A place outside the old picture is kept to its edge first. */
	x = 5000;
	y = -3;
	kwl_rotate_pointer_place(KWL_ROTATE_0, KWL_ROTATE_0, 1280U, 800U, &x, &y);
	check(x == 1279 && y == 0, "a place outside is kept to the edge");
}

/*
 * The inline way back (ws217-p003, the cursor's box): every physical pixel
 * of each panel at each angle gives what kwl_rotate_point_back gives, and
 * every 5th sub-pixel of a 5x3 panel, with the sizes in sub-pixels, what
 * kwl_rotate_fixed_back gives.
 */
static void
test_pixel_back(void)
{
	uint32_t physical_width;
	uint32_t physical_height;
	int32_t physical_x;
	int32_t physical_y;
	int32_t x;
	int32_t y;
	int32_t expected_x;
	int32_t expected_y;
	unsigned panel;
	unsigned angle;
	int same;

	/* Each panel at each angle, every pixel. */
	for (panel = 0U; panel < TEST_PANELS; panel++) {
		physical_width = test_panels[panel][0];
		physical_height = test_panels[panel][1];
		for (angle = 0U; angle < 4U; angle++) {
			same = 1;
			for (physical_y = 0; physical_y < (int32_t)physical_height; physical_y++) {
				for (physical_x = 0; physical_x < (int32_t)physical_width; physical_x++) {
					/* The inline way back against the function's. */
					x = physical_x;
					y = physical_y;
					kwl_rotate_pixel_back(test_angles[angle], physical_width, physical_height, &x, &y);
					kwl_rotate_point_back(test_angles[angle], physical_width, physical_height, physical_x, physical_y, &expected_x, &expected_y);
					if (x != expected_x || y != expected_y)
						same = 0;
				}
			}

			/* Every pixel the same. */
			check(same, "the inline way back is kwl_rotate_point_back's");
		}
	}

	/* The 5x3 panel's sub-pixels at each angle. */
	physical_width = 5U;
	physical_height = 3U;
	for (angle = 0U; angle < 4U; angle++) {
		same = 1;
		for (physical_y = 0; physical_y < (int32_t)(physical_height * 256U); physical_y += 5) {
			for (physical_x = 0; physical_x < (int32_t)(physical_width * 256U); physical_x += 5) {
				/* In sub-pixels, against the fixed-point way back. */
				x = physical_x;
				y = physical_y;
				kwl_rotate_pixel_back(test_angles[angle], physical_width * 256U, physical_height * 256U, &x, &y);
				kwl_rotate_fixed_back(test_angles[angle], physical_width, physical_height, physical_x, physical_y, &expected_x, &expected_y);
				if (x != expected_x || y != expected_y)
					same = 0;
			}
		}

		/* Every sub-pixel the same. */
		check(same, "the inline way back in sub-pixels is kwl_rotate_fixed_back's");
	}
}

/*
 * A floating window fitted to an output turned and turned back (BUG-306,
 * T1-611): Settings 1180 wide at x=50 on 1280x800 (the glass look's space
 * from x=12, 1256 wide) is moved to x=12 at 90 (776 wide), shrinks to the
 * new bounds, and goes back to x=50 at 0, the second time too; a window the
 * user moved after a fit starts from where it was put; one that fits is not
 * moved and keeps nothing.
 */
static void
test_fit_window(void)
{
	struct kwl_fit_home fit;
	struct kwl_fit_home plain;
	int32_t x;
	int32_t y;
	unsigned round;

	/* Nothing moved yet. */
	memset(&fit, 0, sizeof(fit));
	x = 50;
	y = 100;

	/* Twice: 90 (the window still 1180 wide), then 0 (the client shrank to the bounds, 776). */
	for (round = 0U; round < 2U; round++) {
		kwl_rotate_fit_window(&fit, &x, &y, 1180, 680, 12, 100, 776, 1168);
		check(x == 12 && y == 100, "at 90 the window is moved to the space's left edge");
		check(fit.moved == 1U && fit.home_x == 50 && fit.home_y == 100, "the place it had is kept");
		kwl_rotate_fit_window(&fit, &x, &y, 776, 680, 12, 100, 1256, 688);
		check(x == 50 && y == 100, "back at 0 the window is where it was");
		check(fit.moved == 0U, "a window that fits keeps nothing");
	}

	/* The old fitting (nothing kept) leaves the window at x=12 at 0: the bug. */
	memset(&plain, 0, sizeof(plain));
	x = 50;
	y = 100;
	kwl_rotate_fit_window(&plain, &x, &y, 1180, 680, 12, 100, 776, 1168);
	plain.moved = 0U;
	kwl_rotate_fit_window(&plain, &x, &y, 776, 680, 12, 100, 1256, 688);
	check(x == 12, "without the kept place the window stays at the left edge");

	/* A window the user moved after the fit starts from its new place. */
	memset(&fit, 0, sizeof(fit));
	x = 50;
	y = 100;
	kwl_rotate_fit_window(&fit, &x, &y, 1180, 680, 12, 100, 776, 1168);
	x = 30;
	kwl_rotate_fit_window(&fit, &x, &y, 776, 680, 12, 100, 1256, 688);
	check(x == 30 && fit.moved == 0U, "a window moved by the user stays where it was put");

	/* Its bottom edge too: a tall window at 0 is moved up inside the space, and back at 90. */
	memset(&fit, 0, sizeof(fit));
	x = 12;
	y = 600;
	kwl_rotate_fit_window(&fit, &x, &y, 700, 400, 12, 100, 1256, 688);
	check(y == 388 && fit.moved == 1U && fit.home_y == 600, "the bottom edge inside the space");
	kwl_rotate_fit_window(&fit, &x, &y, 700, 400, 12, 100, 776, 1168);
	check(y == 600 && fit.moved == 0U, "back where it was when the space is tall again");
}

/*
 * The wait after a turn to another size: a configure acknowledged after
 * the one sent before the turn ends a window's wait (over the serials'
 * wrap too), the same or an older one does not; the old frame stays while
 * a window is waited for, until KWL_ROTATE_HOLD_MS; a window that no longer
 * fits must also draw an image of another size.
 */
static void
test_hold(void)
{
	/* Newer, the same, and older serials, near the wrap too. */
	check(kwl_rotate_serial_newer(11U, 10U) == 1, "hold: serial 11 after 10");
	check(kwl_rotate_serial_newer(10U, 10U) == 0, "hold: serial 10 is not after 10");
	check(kwl_rotate_serial_newer(9U, 10U) == 0, "hold: serial 9 is not after 10");
	check(kwl_rotate_serial_newer(1U, 0xffffffffU) == 1, "hold: serial 1 after the wrap");
	check(kwl_rotate_serial_newer(0xfffffffeU, 2U) == 0, "hold: serial before the wrap is older");
	check(kwl_rotate_serial_newer(5U, 0U) == 1, "hold: any serial after none sent");

	/* The old frame stays while a window is waited for, until the wait is over. */
	check(kwl_rotate_holding(0U, 1000U, 1000U) == 0, "hold: no window waited for");
	check(kwl_rotate_holding(2U, 1000U, 1000U) == 1, "hold: windows waited for at the start");
	check(kwl_rotate_holding(1U, 1000U, 1000U + KWL_ROTATE_HOLD_MS - 1U) == 1, "hold: just before the end");
	check(kwl_rotate_holding(1U, 1000U, 1000U + KWL_ROTATE_HOLD_MS) == 0, "hold: at the end");
	check(kwl_rotate_holding(1U, 1000U, 1000U + 10U * KWL_ROTATE_HOLD_MS) == 0, "hold: long after the end");

	/*
	 * Whether a window must draw another size (T1-614: Settings 1180x680
	 * turned to bounds 776x1160, and 776x800 turned back to 1256x680): a
	 * fullscreen or docked one always, a floating one when it no longer fits.
	 */
	check(kwl_rotate_size_needed(1U, 800U, 600U, 2000, 2000) == 1, "hold: a fullscreen or docked window is given a size");
	check(kwl_rotate_size_needed(0U, 1180U, 680U, 776, 1160) == 1, "hold: a window wider than the new bounds");
	check(kwl_rotate_size_needed(0U, 776U, 800U, 1256, 680) == 1, "hold: a window taller than the new bounds");
	check(kwl_rotate_size_needed(0U, 600U, 500U, 776, 1160) == 0, "hold: a window that fits keeps its size");
	check(kwl_rotate_size_needed(0U, 776U, 1160U, 776, 1160) == 0, "hold: a window of the bounds' size fits");
	check(kwl_rotate_size_needed(0U, 10U, 10U, 0, 0) == 1, "hold: no space at all");

	/* The image drawn: after a newer acknowledgement, and of another size when one is needed. */
	check(kwl_rotate_drawn(20U, 19U, 1U, 1180U, 680U, 776U, 800U) == 1, "hold: a new size after the new configure");
	check(kwl_rotate_drawn(20U, 19U, 1U, 1180U, 680U, 1180U, 680U) == 0, "hold: the old size after the new configure (T1-614 rotation 5)");
	check(kwl_rotate_drawn(20U, 19U, 1U, 1180U, 680U, 1180U, 600U) == 1, "hold: only the height changed");
	check(kwl_rotate_drawn(19U, 19U, 1U, 1180U, 680U, 776U, 800U) == 0, "hold: a new size before the new configure");
	check(kwl_rotate_drawn(20U, 19U, 0U, 600U, 500U, 600U, 500U) == 1, "hold: a window that fits, after the new configure");
	check(kwl_rotate_drawn(19U, 19U, 0U, 600U, 500U, 600U, 500U) == 0, "hold: a window that fits, before the new configure");
	check(kwl_rotate_drawn(1U, 0xffffffffU, 1U, 1180U, 680U, 776U, 800U) == 1, "hold: a new size after the serials' wrap");
}
