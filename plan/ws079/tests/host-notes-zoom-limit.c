/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-296: Notes on the Latitude 5330 quits when two fingers zoom far in.
 * The i915 executor's allocations are at most 16 MiB (I915_MAX_RESOURCE_BYTES,
 * a contiguous session object), and the page's picture at four times the
 * whole page's scale needs more, so vkAllocateMemory fails and Notes used
 * to end.  This test drives Notes' touch screen (touch.c, the production
 * code) on the 5330's window and models the device the way the executor
 * lays the picture out (src/drivers/gpu/i915/render/image.c: the colour
 * image is linear rows of 4 bytes, the D32_SFLOAT_S8_UINT stencil buffer
 * a Y-tiled depth plane and, on the next page, a Y-tiled stencil plane),
 * and main.c's app_picture as it meets a refusal: the zoom is held to three
 * quarters of the refused picture's pixels (notes_touch_picture_limit)
 * until a picture fits, at most eight times.
 *
 * It checks that the far pinch asks for a picture the device refuses (the
 * bug's condition), that the limit then gives a picture the device holds
 * within the tries with the page still zoomed and inside its edges, that a
 * later pinch stops at the limit while the fingers move, that a limit is
 * only lowered, and that a limit below the whole page leaves the zoom at
 * the whole page (where app_picture gives up as before).
 */

#include "../../../userland/desktop/notes/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The 5330's window (1920 x 1080 less the panel's band), Notes' toolbar band and margin, and the page (A4 in points). */
#define TEST_WIDTH	1920U
#define TEST_HEIGHT	1048U
#define TEST_TOP	68.0f
#define TEST_MARGIN	16.0f
#define TEST_PAGE_W	595.0f
#define TEST_PAGE_H	842.0f

/* The finger's report interval and the frame interval, in microseconds. */
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The most bytes one allocation of the i915 executor may have (I915_MAX_RESOURCE_BYTES). */
#define TEST_DEVICE_BYTES	(16ULL * 1024ULL * 1024ULL)

/* main.c's MAIN_PICTURE_TRIES and MAIN_PICTURE_SHRINK. */
#define TEST_TRIES	8U
#define TEST_SHRINK	0.75

/* The touch screen under test. */
static struct notes_touch touch;

/* The test's clock, microseconds. */
static uint64_t clock_us;

/* The number of checks run, and of those that failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *format, ...);
static float fit_scale(void);
static void layout(void);
static void frame_tick(void);
static void rest(void);
static void finger(unsigned type, int32_t id, float x, float y);
static void pinch(float x, float y, float from, float to, int steps);
static void picture_size(uint32_t *width, uint32_t *height);
static int device_holds(uint32_t width, uint32_t height);
static unsigned picture_make(int *made);
static int page_inside(void);

int
main(void)
{
	uint32_t width;
	uint32_t height;
	unsigned tries;
	float held_zoom;
	float zoom;
	int made;
	int error;

	/* The touch screen at the whole page. */
	error = notes_touch_open(&touch);
	check(error == 0, "the touch screen opens: %d", error);
	if (error != 0)
		return 1;
	clock_us = 1000000U;
	layout();

	/* The far pinch zooms four times, and its picture is one the device refuses (the bug). */
	pinch(960.0f, 500.0f, 100.0f, 900.0f, 30);
	rest();
	picture_size(&width, &height);
	check(touch.zoom == 4.0f, "the far pinch zooms four times: %.3f", (double)touch.zoom);
	check(!device_holds(width, height), "its picture %ux%u is more than the device holds", width, height);

	/* The refusals hold the zoom until a picture fits, within the tries, still zoomed and inside the page's edges. */
	tries = picture_make(&made);
	picture_size(&width, &height);
	check(made, "a picture is made after %u refusals: %ux%u zoom=%.3f", tries, width, height, (double)touch.zoom);
	check(tries >= 1U && tries < TEST_TRIES, "within the tries: %u", tries);
	check(touch.zoom > 2.0f && touch.zoom < 4.0f, "the page stays zoomed: %.3f", (double)touch.zoom);
	check(page_inside(), "the page covers the room: %.1f,%.1f %.4f", (double)touch.view_x, (double)touch.view_y, (double)touch.view_scale);
	held_zoom = touch.zoom;

	/* Back to the whole page, then far apart again: the zoom stops at the limit while the fingers move. */
	pinch(960.0f, 500.0f, 800.0f, 50.0f, 30);
	rest();
	check(touch.zoom == 1.0f, "the fingers zoom back to the whole page: %.3f", (double)touch.zoom);
	pinch(960.0f, 500.0f, 100.0f, 900.0f, 30);
	rest();
	picture_size(&width, &height);
	check(fabsf(touch.zoom - held_zoom) < 0.001f, "a later pinch stops at the limit: %.3f (%.3f)", (double)touch.zoom, (double)held_zoom);
	check(device_holds(width, height), "and its picture %ux%u is one the device holds", width, height);
	tries = picture_make(&made);
	check(made && tries == 0U, "with no refusal: %u", tries);
	check(page_inside(), "the page covers the room again");

	/* A limit is only lowered. */
	zoom = touch.zoom;
	notes_touch_picture_limit(&touch, 1.0e9);
	check(touch.zoom == zoom && touch.picture_pixels < 1.0e9, "a larger limit changes nothing: %.3f", (double)touch.zoom);

	/* A limit below the whole page leaves the whole page, which app_picture cannot lower (it gives up as before). */
	notes_touch_picture_limit(&touch, 1000.0);
	check(touch.zoom == 1.0f, "a limit below the whole page leaves the whole page: %.3f", (double)touch.zoom);
	check(touch.view_scale == fit_scale(), "at the whole page's scale: %.4f", (double)touch.view_scale);
	notes_touch_close(&touch);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-notes-zoom-limit: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-notes-zoom-limit: ok (%u checks)\n", checks);
	return 0;
}

/* Counts one check and prints it when it fails. */
static void
check(
	int passed,
	const char *format,
	...)
{
	va_list arguments;

	/* Counts the check; a passed one prints nothing. */
	checks++;
	if (passed)
		return;

	/* Prints the failure. */
	failures++;
	printf("FAIL: ");
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	printf("\n");
}

/* The scale at which the whole page fits, as notes_view_layout finds it. */
static float
fit_scale(void)
{
	float room_width;
	float room_height;
	float scale;

	/* The room below the toolbar, inside the margin, and the larger scale at which both sides fit. */
	room_width = (float)TEST_WIDTH - 2.0f * TEST_MARGIN;
	room_height = (float)TEST_HEIGHT - TEST_TOP - 2.0f * TEST_MARGIN;
	scale = room_width / TEST_PAGE_W;
	if (room_height / TEST_PAGE_H < scale)
		scale = room_height / TEST_PAGE_H;
	return scale;
}

/* Gives the touch screen the layout, as each frame does. */
static void
layout(void)
{
	/* The layout of the page in the window. */
	notes_touch_layout(&touch, TEST_WIDTH, TEST_HEIGHT, TEST_TOP, TEST_MARGIN, TEST_PAGE_W, TEST_PAGE_H, fit_scale());
}

/* Moves the clock on by a frame: the fingers' tick and the layout, as the main loop does. */
static void
frame_tick(void)
{
	/* The next frame. */
	clock_us += TEST_FRAME_US;
	(void)notes_touch_tick(&touch, clock_us);
	layout();
}

/* Lets everything rest (any glide or spring). */
static void
rest(void)
{
	uint64_t until;

	/* Frames for three seconds. */
	until = clock_us + 3000000U;
	while (clock_us < until)
		frame_tick();
}

/* Sends one touch input at the clock's time. */
static void
finger(
	unsigned type,
	int32_t id,
	float x,
	float y)
{
	struct notes_touch_event event;

	/* The input, read as it is made. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time = (uint32_t)(clock_us / 1000U);
	event.arrival = clock_us;
	notes_touch_event(&touch, &event);
}

/* Two fingers either side of a place, from a distance to another in 60 Hz reports; they hold still, then lift. */
static void
pinch(
	float x,
	float y,
	float from,
	float to,
	int steps)
{
	float half;
	int reached;
	int k;

	/* Down either side. */
	finger(NOTES_TOUCH_DOWN, 1, x - from / 2.0f, y);
	finger(NOTES_TOUCH_DOWN, 2, x + from / 2.0f, y);

	/* Apart (or together), then still. */
	for (k = 1; k <= steps + 6; k++) {
		clock_us += TEST_REPORT_US;
		reached = k;
		if (reached > steps)
			reached = steps;
		half = (from + (to - from) * (float)reached / (float)steps) / 2.0f;
		finger(NOTES_TOUCH_MOTION, 1, x - half, y);
		finger(NOTES_TOUCH_MOTION, 2, x + half, y);
		frame_tick();
	}

	/* The lift. */
	clock_us += 4000U;
	finger(NOTES_TOUCH_UP, 1, 0.0f, 0.0f);
	finger(NOTES_TOUCH_UP, 2, 0.0f, 0.0f);
}

/* The page's picture at the place's scale, in whole pixels, as main.c's app_picture asks for it. */
static void
picture_size(
	uint32_t *width,
	uint32_t *height)
{
	float x;
	float y;
	float scale;

	/* The place's scale, and the page at it. */
	notes_touch_view(&touch, &x, &y, &scale);
	*width = (uint32_t)ceil((double)(TEST_PAGE_W * scale));
	*height = (uint32_t)ceil((double)(TEST_PAGE_H * scale));
}

/* Tells whether the i915 executor would hold the picture's colour image and its D32_SFLOAT_S8_UINT stencil buffer. */
static int
device_holds(
	uint32_t width,
	uint32_t height)
{
	uint64_t color;
	uint64_t depth;
	uint64_t stencil;

	/* The colour image: linear rows of 4 bytes. */
	color = (uint64_t)width * 4U * height;
	if (color > TEST_DEVICE_BYTES)
		return 0;

	/* The stencil buffer: the Y-tiled depth plane, then on its next page the Y-tiled stencil plane. */
	depth = (uint64_t)((width * 4U + 127U) & ~127U) * ((height + 31U) & ~31U);
	stencil = ((depth + 4095U) & ~(uint64_t)4095U) + (uint64_t)((width + 127U) & ~127U) * ((height + 31U) & ~31U);
	if (stencil > TEST_DEVICE_BYTES)
		return 0;

	/* The device holds both. */
	return 1;
}

/*
 * Makes the picture as main.c's app_picture does with a device that refuses
 * what it cannot hold, and reports the refusals and whether one was made.
 */
static unsigned
picture_make(
	int *made)
{
	uint32_t width;
	uint32_t height;
	unsigned tries;
	float scale;
	int held;

	/* Pictures at the scale, smaller each time the device refuses one. */
	*made = 0;
	tries = 0U;
	for (;;) {
		picture_size(&width, &height);
		held = device_holds(width, height);
		if (held) {
			*made = 1;
			break;
		}

		/* Only so often. */
		tries++;
		if (tries >= TEST_TRIES)
			break;

		/* The zoom within a share of the refused picture's pixels; a zoom that cannot be lowered gives up. */
		scale = touch.view_scale;
		notes_touch_picture_limit(&touch, (double)width * (double)height * TEST_SHRINK);
		if (touch.view_scale >= scale)
			break;
	}

	/* The refusals met. */
	return tries;
}

/*
 * Tells whether the zoomed page is placed within its edges: on an axis where
 * it is larger than the room inside the margin it covers that room (it
 * scrolled no further than its edges), on one where it is smaller it is
 * centred as the whole page is.
 */
static int
page_inside(void)
{
	float width;
	float height;
	float room_height;
	float centre_x;
	float centre_y;

	/* The page's size at the place, the room below the toolbar, and where the whole page's formula centres the page. */
	width = TEST_PAGE_W * touch.view_scale;
	height = TEST_PAGE_H * touch.view_scale;
	room_height = (float)TEST_HEIGHT - TEST_TOP;
	centre_x = (float)floor(((double)TEST_WIDTH - (double)width) / 2.0);
	centre_y = (float)floor((double)TEST_TOP + ((double)room_height - (double)height) / 2.0);

	/* Across: covering the room, or centred. */
	if (width > (float)TEST_WIDTH - 2.0f * TEST_MARGIN) {
		if (touch.view_x > TEST_MARGIN + 0.5f)
			return 0;
		if (touch.view_x + width < (float)TEST_WIDTH - TEST_MARGIN - 0.5f)
			return 0;
	} else {
		if (touch.view_x != centre_x)
			return 0;
	}

	/* Down: the same below the toolbar. */
	if (height > room_height - 2.0f * TEST_MARGIN) {
		if (touch.view_y > TEST_TOP + TEST_MARGIN + 0.5f)
			return 0;
		if (touch.view_y + height < (float)TEST_HEIGHT - TEST_MARGIN - 0.5f)
			return 0;
	} else {
		if (touch.view_y != centre_y)
			return 0;
	}

	/* The page is within its edges. */
	return 1;
}
