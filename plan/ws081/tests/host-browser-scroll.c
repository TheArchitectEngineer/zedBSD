/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws081-p006: the host test of the view's placed scroll (<browser/browser.h>
 * version 2) on plan/ws081/tests/pages/scroll.html: a green canvas, a red
 * block at the top, a 3000-pixel gap and a blue block at the end, and a
 * listener that writes each wheel event to the console.
 *
 *   host-browser-scroll PAGES SANS MONO FALLBACK
 *
 * It checks: the range is the document less the view; the scroll is
 * placed where asked, kept inside the document, drawn again, and gives the
 * page no wheel event (the wheel does); the overscroll shifts the drawing
 * with the canvas in the gap and leaves the scroll, is limited to the
 * view's height, and a new page starts without it; without a page the
 * calls say ENOENT.  Prints one line per failed check and a summary.
 */

#include <browser/browser.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The public header has no helper for unused parameters. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* The view's size. */
#define TEST_WIDTH	400U
#define TEST_HEIGHT	300U

/* The page's colors, as the CPU drawing writes them (0xAARRGGBB). */
#define TEST_CANVAS	0xff0ac81eU
#define TEST_RED	0xffdc1414U
#define TEST_BLUE	0xff1414dcU

/* The number of checks run. */
static int checks;

/* The number of checks that failed. */
static int failures;

/* How many wheel events the page wrote to the console. */
static int wheels;

/* How many times the view asked to be drawn again. */
static int redraws;

/* The drawing of the view. */
static uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];

static void check(int condition, const char *what);
static uint32_t pixel_at(struct browser_view *view, unsigned x, unsigned y);
static void on_console(void *context, struct browser_view *view, int level, const char *text, size_t length);
static void on_redraw(void *context, struct browser_view *view);

/*
 * Runs every check and reports the result.
 */
int
main(
	int argc,
	char **argv)
{
	struct browser_fonts paths;
	struct browser_callbacks callbacks;
	struct browser_view_options options;
	struct browser_view *view;
	char path[1024];
	double largest_x;
	double largest_y;
	int before;
	int error;

	/* The arguments. */
	if (argc < 5) {
		fprintf(stderr, "usage: host-browser-scroll PAGES SANS MONO FALLBACK\n");
		return 2;
	}

	/* The view at 400 by 300, every page read at once. */
	paths.sans = argv[2];
	paths.mono = argv[3];
	paths.fallback = argv[4];
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.console = on_console;
	callbacks.redraw = on_redraw;
	memset(&options, 0, sizeof(options));
	options.version = BROWSER_API_VERSION;
	options.fonts = &paths;
	options.callbacks = &callbacks;
	options.stack_base = __builtin_frame_address(0);
	options.width = TEST_WIDTH;
	options.height = TEST_HEIGHT;
	options.fetch = BROWSER_FETCH_AT_ONCE;
	error = browser_view_create(&options, &view);
	check(error == 0, "view: made");
	if (error != 0)
		return 1;

	/* Without a page nothing scrolls. */
	error = browser_view_scroll_to(view, 0.0, 100.0);
	check(error == ENOENT, "no page: scroll_to says ENOENT");
	error = browser_view_scroll_range(view, &largest_x, &largest_y);
	check(error == ENOENT && largest_y == 0.0, "no page: scroll_range says ENOENT and 0");

	/* The page. */
	snprintf(path, sizeof(path), "%s/scroll.html", argv[1]);
	error = browser_view_load(view, path);
	check(error == 0, "view: scroll.html loads");
	if (error != 0)
		return 1;
	error = browser_view_settle(view, 1000.0, BROWSER_SETTLE_LAYOUT);
	check(error == 0, "view: scroll.html settles");

	/* The range: 3200 pixels of document less the 300 of the view. */
	error = browser_view_scroll_range(view, &largest_x, &largest_y);
	check(error == 0 && largest_x == 0.0 && largest_y == 2900.0, "range: the document less the view");
	if (largest_y != 2900.0)
		printf("  range %.1f, %.1f\n", largest_x, largest_y);

	/* A place inside: kept, drawn again, no wheel event. */
	before = redraws;
	error = browser_view_scroll_to(view, 0.0, 1234.5);
	check(error == 0 && browser_view_scroll_y(view) > 1234.0 && browser_view_scroll_y(view) < 1235.0, "scroll_to: the place asked");
	check(redraws > before, "scroll_to: drawn again");
	check(wheels == 0, "scroll_to: the page gets no wheel event");

	/* The same place is no change. */
	before = redraws;
	(void)browser_view_scroll_to(view, 0.0, 1234.5);
	check(redraws == before, "scroll_to: the same place draws nothing again");

	/* Past the ends: kept inside. */
	(void)browser_view_scroll_to(view, 0.0, -50.0);
	check(browser_view_scroll_y(view) == 0.0, "scroll_to: above the top is the top");
	(void)browser_view_scroll_to(view, 0.0, 99999.0);
	check(browser_view_scroll_y(view) == 2900.0, "scroll_to: past the end is the end");
	check(pixel_at(view, 200, 250) == TEST_BLUE, "scroll_to: the end block is drawn at the end");

	/* The wheel, for comparison, gives the page its event. */
	(void)browser_view_wheel(view, 200.0f, 150.0f, 0.0f, -100.0f, 0);
	check(wheels == 1 && browser_view_scroll_y(view) == 2800.0, "wheel: the page gets its event and scrolls");

	/* The overscroll at the top: the canvas above, the red block moved down, the scroll left. */
	(void)browser_view_scroll_to(view, 0.0, 0.0);
	check(pixel_at(view, 200, 10) == TEST_RED, "overscroll: none, the red block at the top");
	before = redraws;
	error = browser_view_set_overscroll(view, 0.0, 50.0);
	check(error == 0 && redraws > before, "overscroll: set and drawn again");
	check(pixel_at(view, 200, 10) == TEST_CANVAS, "overscroll: the gap shows the canvas");
	check(pixel_at(view, 200, 60) == TEST_RED, "overscroll: the red block moved down");
	check(pixel_at(view, 200, 140) == TEST_RED && pixel_at(view, 200, 160) == TEST_CANVAS, "overscroll: by 50 pixels");
	check(browser_view_scroll_y(view) == 0.0, "overscroll: the scroll stays");

	/* The overscroll at the end: the blue block moved up, the canvas below. */
	(void)browser_view_set_overscroll(view, 0.0, 0.0);
	(void)browser_view_scroll_to(view, 0.0, 2900.0);
	(void)browser_view_set_overscroll(view, 0.0, -40.0);
	check(pixel_at(view, 200, 250) == TEST_BLUE && pixel_at(view, 200, 280) == TEST_CANVAS, "overscroll: past the end, the canvas below");
	check(browser_view_scroll_y(view) == 2900.0, "overscroll: the scroll stays at the end");

	/* The limit: the view's height. */
	(void)browser_view_scroll_to(view, 0.0, 0.0);
	(void)browser_view_set_overscroll(view, 0.0, 5000.0);
	check(pixel_at(view, 200, 299) == TEST_CANVAS, "overscroll: at most the view's height");

	/* A new page starts without it. */
	error = browser_view_load(view, path);
	check(error == 0, "view: scroll.html again");
	(void)browser_view_settle(view, 1000.0, BROWSER_SETTLE_LAYOUT);
	check(pixel_at(view, 200, 10) == TEST_RED && browser_view_scroll_y(view) == 0.0, "overscroll: a new page starts without it");

	/* The view goes. */
	browser_view_destroy(view);

	/* Reports a failure. */
	if (failures != 0) {
		printf("host-browser-scroll: %d checks, %d failed\n", checks, failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-browser-scroll: %d checks, 0 failed\n", checks);
	return 0;
}

/* Counts one check and prints it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* The count; a passed check prints nothing. */
	checks++;
	if (condition)
		return;

	/* The failure. */
	failures++;
	printf("FAIL: %s\n", what);
}

/* Draws the view with the CPU and reports one pixel of it. */
static uint32_t
pixel_at(
	struct browser_view *view,
	unsigned x,
	unsigned y)
{
	int error;

	/* The drawing. */
	error = browser_view_draw_pixels(view, pixels, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	if (error != 0)
		return 0;

	/* Reports the pixel. */
	return pixels[y * TEST_WIDTH + x];
}

/* Counts the page's wheel events from its console. */
static void
on_console(
	void *context,
	struct browser_view *view,
	int level,
	const char *text,
	size_t length)
{
	int differs;

	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	UNUSED_PARAMETER(level);

	/* A wheel event's line. */
	if (length < 6)
		return;
	differs = memcmp(text, "wheel ", 6);
	if (differs == 0)
		wheels++;
}

/* Counts the view's requests to be drawn again. */
static void
on_redraw(
	void *context,
	struct browser_view *view)
{
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);

	/* One more. */
	redraws++;
}
