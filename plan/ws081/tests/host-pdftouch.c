/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of PDF Viewer's touch screen (ws081-p012,
 * userland/desktop/pdfviewer/touch.c) on the viewer's core and libkeiland,
 * without Wayland and Vulkan.
 *
 *   host-pdftouch FONT DOCUMENT.pdf OUTDIR
 *
 * DOCUMENT.pdf has eight Letter pages.  The window is 1000x760, the clock
 * the test's own, and the fingers report at 60 Hz while the frames come
 * every 16 ms.  It checks: a flick that glides on and stops as far as the
 * scroller's fling goes; the rubber band past the top and its spring back;
 * a fling into the end; a touch that catches gliding pages (and taps
 * nothing); a cancel that does not glide; a key that takes the view over
 * from a glide; two fingers that zoom about the place between them (the
 * frame stretches the rasters meanwhile, and draws them again at the end);
 * the double tap in and out; the page mode's swipe that turns and one that
 * slides back; and a tap on the sidebar that shows a page (the finger plays
 * the pointer there).  Frames of the rubber band and the zoom are written
 * to OUTDIR as PPM.
 */

#include "../../../userland/desktop/pdfviewer/touch.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window's size in the test. */
#define TEST_WIDTH	1000
#define TEST_HEIGHT	760

/* The finger's report interval and the frame interval, in microseconds. */
#define TEST_REPORT_US	16667U
#define TEST_FRAME_US	16000U

/* The viewer, its font, its touch screen and the frame it draws. */
static struct pv_app app;

/* The font of the viewer's words. */
static struct pv_text text;

/* The touch screen under test. */
static struct pv_touch touch;

/* The frame's pixels. */
static uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];

/* Where the frames are written. */
static const char *out_dir;

/* The test's clock, microseconds. */
static uint64_t clock_us;

/* The number of checks run. */
static unsigned checks;

/* The number of checks that failed. */
static unsigned failures;

static void check(int passed, const char *format, ...);
static void frame_tick(void);
static void run(double milliseconds);
static void finger(unsigned kind, int32_t id, double x, double y);
static void stroke(int32_t id, double x, double y, double dx, double dy, double milliseconds, int down, int up);
static void frame(const char *name);
static void draw(void);
static double largest_y(void);
static void reset(double y);
static void test_flick(void);
static void test_edges(void);
static void test_catch(void);
static void test_pinch(void);
static void test_swipe(void);
static void test_sidebar(void);

/*
 * Runs every test and reports the result.
 */
int
main(
	int argc,
	char **argv)
{
	int error;

	/* The arguments. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-pdftouch FONT DOCUMENT.pdf OUTDIR\n");
		return 2;
	}

	/* Where the frames go. */
	out_dir = argv[3];

	/* The viewer with the document, and the touch screen. */
	error = pv_text_open(&text, argv[1]);
	check(error == 0, "the font opens");
	pv_app_init(&app, &text, TEST_WIDTH, TEST_HEIGHT);
	clock_us = 5000000U;
	app.now = clock_us / 1000U;
	error = pv_app_open(&app, argv[2]);
	check(error == 0 && app.document.count == 8, "the document opens with 8 pages (%lu)", (unsigned long)app.document.count);
	error = pv_touch_open(&touch);
	check(error == 0, "the touch screen opens");
	if (failures != 0) {
		printf("host-pdftouch: FAILED to start\n");
		return 1;
	}

	/* The tests. */
	test_flick();
	test_edges();
	test_catch();
	test_pinch();
	test_swipe();
	test_sidebar();

	/* Everything goes. */
	pv_touch_close(&touch);
	pv_app_release(&app);
	pv_text_close(&text);
	if (failures != 0) {
		printf("host-pdftouch: FAILED %u of %u checks\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-pdftouch: ok (%u checks)\n", checks);
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

/* Moves the clock on by a frame: the viewer's time and the fingers' tick, as the main loop does. */
static void
frame_tick(void)
{
	clock_us += TEST_FRAME_US;
	app.now = clock_us / 1000U;
	(void)pv_app_tick(&app, app.now);
	(void)pv_touch_tick(&touch, &app, clock_us);
}

/* Runs frames for a while. */
static void
run(
	double milliseconds)
{
	uint64_t until;

	/* Frames until the time. */
	until = clock_us + (uint64_t)(milliseconds * 1000.0);
	while (clock_us < until)
		frame_tick();
}

/* Sends one touch input at the clock's time. */
static void
finger(
	unsigned kind,
	int32_t id,
	double x,
	double y)
{
	struct kl_window_event event;

	/* The input (libkeiland's window's), read as it is made; the compositor's time is whole milliseconds. */
	memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time_us = clock_us / 1000U * 1000U;
	event.arrival_us = clock_us;
	pv_touch_event(&touch, &app, &event);
}

/*
 * Moves a finger from a place by dx, dy over a time at 60 Hz, with a frame
 * between reports; it touches first when down says so, and lifts at the
 * end when up says so.
 */
static void
stroke(
	int32_t id,
	double x,
	double y,
	double dx,
	double dy,
	double milliseconds,
	int down,
	int up)
{
	uint64_t start;
	uint64_t next;
	double share;

	/* The touch. */
	if (down)
		finger(KL_WINDOW_TOUCH_DOWN, id, x, y);
	start = clock_us;

	/* The reports along the way, frames between them. */
	next = start + TEST_REPORT_US;
	while (next <= start + (uint64_t)(milliseconds * 1000.0)) {
		while (clock_us + TEST_FRAME_US <= next)
			frame_tick();
		clock_us = next;
		share = (double)(next - start) / (milliseconds * 1000.0);
		finger(KL_WINDOW_TOUCH_MOTION, id, x + dx * share, y + dy * share);
		next += TEST_REPORT_US;
	}

	/* The lift, a few milliseconds after the last report. */
	if (up) {
		clock_us += 4000U;
		finger(KL_WINDOW_TOUCH_UP, id, 0.0, 0.0);
	}
}

/* Writes the frame as a PPM. */
static void
frame(
	const char *name)
{
	struct pv_canvas canvas;
	char path[512];
	FILE *file;
	uint32_t pixel;
	int index;

	/* Draws the frame. */
	canvas.pixels = pixels;
	canvas.stride = TEST_WIDTH;
	canvas.width = TEST_WIDTH;
	canvas.height = TEST_HEIGHT;
	pv_draw(&app, &canvas);

	/* Writes it. */
	snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
	file = fopen(path, "wb");
	if (file == NULL)
		return;
	fprintf(file, "P6\n%d %d\n255\n", TEST_WIDTH, TEST_HEIGHT);
	for (index = 0; index < TEST_WIDTH * TEST_HEIGHT; index++) {
		pixel = pixels[index];
		fputc((int)((pixel >> 16) & 0xffU), file);
		fputc((int)((pixel >> 8) & 0xffU), file);
		fputc((int)(pixel & 0xffU), file);
	}

	/* The file is complete. */
	fclose(file);
}

/* Draws the frame without writing it (the rasters the frame makes). */
static void
draw(void)
{
	struct pv_canvas canvas;

	/* Draws it. */
	canvas.pixels = pixels;
	canvas.stride = TEST_WIDTH;
	canvas.width = TEST_WIDTH;
	canvas.height = TEST_HEIGHT;
	pv_draw(&app, &canvas);
}

/* The view's largest scroll_y. */
static double
largest_y(void)
{
	double largest;

	/* The document's height less the view's. */
	largest = pv_app_content_height(&app) - (double)app.height;
	if (largest < 0.0)
		largest = 0.0;
	return largest;
}

/* Puts the view in the scroll mode at the fit, at a place, and lets everything rest. */
static void
reset(
	double y)
{
	/* The scroll mode at the width's fit. */
	pv_app_action(&app, PV_ACTION_MODE_SCROLL);
	pv_app_action(&app, PV_ACTION_FIT_WIDTH);
	run(3000.0);
	app.scroll_x = 0.0;
	app.scroll_y = y;
	run(100.0);
}

/* A flick glides on and stops where the scroller's fling says. */
static void
test_flick(void)
{
	double released;
	double later;
	double expected;
	double velocity;

	/* A finger flicked up by 300 px in 100 ms. */
	reset(1000.0);
	stroke(1, 500.0, 600.0, 0.0, -300.0, 100.0, 1, 1);
	released = app.scroll_y;
	check(released > 1200.0 && released < 1300.0, "the pages followed the finger (as drawn, resampled): %.1f", released);
	check(touch.moving, "the pages glide after the lift");

	/* It glides on. */
	run(200.0);
	later = app.scroll_y;
	check(later > released + 200.0, "the glide goes on: %.1f after %.1f", later, released);

	/* And stops about as far as a 3000 px/s fling goes, the view at rest. */
	run(3000.0);
	velocity = 3000.0;
	expected = 0.45 * velocity - 300.0 * 0.45 * 0.45 * log(1.0 + velocity / 135.0);
	check(fabs(app.scroll_y - released - expected) < expected * 0.15, "the glide went %.0f px (a 3000 px/s fling: %.0f)",
	      app.scroll_y - released, expected);
	check(!touch.moving && !app.touching, "the view rests");
}

/* The rubber band past the top, and its spring back; a fling into the end. */
static void
test_edges(void)
{
	double peak;
	double past;
	int passed;

	/* At the top, a finger pulls down by 240 px and holds. */
	reset(0.0);
	stroke(1, 500.0, 200.0, 0.0, 240.0, 300.0, 1, 0);
	run(100.0);
	check(app.scroll_y < -80.0 && app.scroll_y > -240.0 * 0.55, "the top stretches, less than the finger: %.1f", app.scroll_y);
	frame("rubber-band");

	/* Let go still: back to the top within a second, never below it. */
	clock_us += 4000U;
	finger(KL_WINDOW_TOUCH_UP, 1, 0.0, 0.0);
	passed = 1;
	peak = app.scroll_y;
	run(1000.0);
	if (app.scroll_y != 0.0)
		passed = 0;
	check(passed && peak < 0.0, "the top springs back: %.3f", app.scroll_y);

	/* A flick toward the end, near it: past the end a little, then back to it exactly. */
	reset(largest_y() - 150.0);
	stroke(1, 500.0, 600.0, 0.0, -240.0, 80.0, 1, 1);
	peak = 0.0;
	while (touch.moving) {
		frame_tick();
		past = app.scroll_y - largest_y();
		if (past > peak)
			peak = past;
	}

	/* The result. */
	check(peak > 5.0 && peak < 120.0, "a fling into the end passes it by %.1f px", peak);
	check(app.scroll_y == largest_y(), "and rests at the end: %.2f of %.2f", app.scroll_y, largest_y());
}

/* A touch catches the glide and taps nothing; a cancel does not glide; a key takes the view over. */
static void
test_catch(void)
{
	struct pv_event key;
	double caught;
	double placed;

	/* A flick, and a touch 150 ms later. */
	reset(1000.0);
	stroke(1, 500.0, 600.0, 0.0, -300.0, 100.0, 1, 1);
	run(150.0);
	finger(KL_WINDOW_TOUCH_DOWN, 2, 500.0, 400.0);
	caught = app.scroll_y;
	run(300.0);
	check(touch.caught && app.scroll_y == caught, "the touch caught the glide: %.1f then %.1f", caught, app.scroll_y);
	clock_us += 4000U;
	finger(KL_WINDOW_TOUCH_UP, 2, 0.0, 0.0);
	run(500.0);
	check(app.scroll_y == caught && app.fit == PV_FIT_WIDTH, "the caught touch tapped nothing");

	/* A drag the compositor cancels: no glide. */
	reset(1000.0);
	stroke(1, 500.0, 600.0, 0.0, -300.0, 100.0, 1, 0);
	caught = app.scroll_y;
	finger(KL_WINDOW_TOUCH_CANCEL, -1, 0.0, 0.0);
	run(1000.0);
	check(fabs(app.scroll_y - caught) < 20.0 && !touch.moving, "a cancelled drag does not glide: %.1f then %.1f", caught,
	      app.scroll_y);

	/* A glide, then End: the view stays where End put it (the last page's top). */
	reset(1000.0);
	stroke(1, 500.0, 600.0, 0.0, -300.0, 100.0, 1, 1);
	run(100.0);
	memset(&key, 0, sizeof(key));
	key.type = PV_EVENT_KEY;
	key.key = PV_KEY_END;
	key.pressed = 1;
	pv_app_event(&app, &key);
	placed = app.scroll_y;
	run(2000.0);
	check(placed > 8000.0 && app.scroll_y == placed && !touch.moving, "End took the view over from the glide: %.1f then %.1f",
	      placed, app.scroll_y);
}

/* Two fingers zoom about the place between them; the frame stretches meanwhile. */
static void
test_pinch(void)
{
	struct pv_place before;
	struct pv_place after;
	double scale;
	double raster_scale;
	size_t page;
	int k;

	/* The place between the fingers, at the fit. */
	reset(1200.0);
	draw();
	page = pv_app_current_page(&app);
	scale = pv_app_scale(&app, page);
	pv_app_place_at(&app, 500.0, 380.0, &before);

	/* Two fingers 200 px apart spread to 400 px in 20 reports. */
	finger(KL_WINDOW_TOUCH_DOWN, 1, 400.0, 380.0);
	finger(KL_WINDOW_TOUCH_DOWN, 2, 600.0, 380.0);
	for (k = 1; k <= 20; k++) {
		clock_us += TEST_REPORT_US;
		finger(KL_WINDOW_TOUCH_MOTION, 1, 400.0 - 5.0 * k, 380.0);
		finger(KL_WINDOW_TOUCH_MOTION, 2, 600.0 + 5.0 * k, 380.0);
		frame_tick();
	}

	/* They hold still for 100 ms, reported at 60 Hz as a touch screen does while it is touched. */
	for (k = 1; k <= 6; k++) {
		clock_us += TEST_REPORT_US;
		finger(KL_WINDOW_TOUCH_MOTION, 1, 300.0, 380.0);
		finger(KL_WINDOW_TOUCH_MOTION, 2, 700.0, 380.0);
		frame_tick();
	}

	/* The zoom so far. */
	check(app.zooming && touch.pinching, "two fingers zoom");
	check(touch.ratio > 1.05 && touch.ratio < 1.15, "the zoom started once the fingers parted by 5%%: %.3f", touch.ratio);
	check(fabs(app.zoom / scale - 2.0 / touch.ratio) < 0.02, "the zoom followed the fingers since: %.3f from %.3f (%.3f)",
	      app.zoom, scale, 2.0 / touch.ratio);

	/* The frame stretches the raster rather than drawing it again. */
	raster_scale = app.document.pages[page].raster_scale;
	frame("pinch-stretched");
	check(app.document.pages[page].raster_scale == raster_scale, "the frame stretched the raster while zooming");

	/* The same place is between the fingers. */
	pv_app_place_at(&app, 500.0, 380.0, &after);
	check(after.page == before.page && fabs(after.x - before.x) < 1.0 && fabs(after.y - before.y) < 1.0,
	      "the place between the fingers stayed: page %lu %.1f,%.1f then page %lu %.1f,%.1f", (unsigned long)before.page,
	      before.x, before.y, (unsigned long)after.page, after.x, after.y);

	/* The fingers lift: the zoom ends, the pages are drawn at the new scale. */
	clock_us += 4000U;
	finger(KL_WINDOW_TOUCH_UP, 1, 0.0, 0.0);
	finger(KL_WINDOW_TOUCH_UP, 2, 0.0, 0.0);
	run(1500.0);
	check(!app.zooming && !touch.pinching && app.fit == PV_FIT_CUSTOM, "the zoom ended at the user's zoom");
	frame("pinch-done");
	check(fabs(app.document.pages[page].raster_scale - app.zoom) < 1e-6, "the page was drawn again at %.3f (%.3f)",
	      app.zoom, app.document.pages[page].raster_scale);

	/* A double tap goes back to the fit; another zooms in twice about the tap. */
	finger(KL_WINDOW_TOUCH_DOWN, 3, 500.0, 380.0);
	clock_us += 60000U;
	finger(KL_WINDOW_TOUCH_UP, 3, 0.0, 0.0);
	run(120.0);
	finger(KL_WINDOW_TOUCH_DOWN, 4, 502.0, 381.0);
	clock_us += 60000U;
	finger(KL_WINDOW_TOUCH_UP, 4, 0.0, 0.0);
	run(200.0);
	check(app.fit == PV_FIT_WIDTH, "a double tap went back to the fit");
	run(400.0);
	scale = pv_app_scale(&app, pv_app_current_page(&app));
	pv_app_place_at(&app, 300.0, 300.0, &before);
	finger(KL_WINDOW_TOUCH_DOWN, 5, 300.0, 300.0);
	clock_us += 60000U;
	finger(KL_WINDOW_TOUCH_UP, 5, 0.0, 0.0);
	run(120.0);
	finger(KL_WINDOW_TOUCH_DOWN, 6, 300.0, 300.0);
	clock_us += 60000U;
	finger(KL_WINDOW_TOUCH_UP, 6, 0.0, 0.0);
	run(200.0);
	pv_app_place_at(&app, 300.0, 300.0, &after);
	check(app.fit == PV_FIT_CUSTOM && fabs(app.zoom / scale - 2.0) < 1e-6, "a double tap zoomed in twice: %.3f from %.3f",
	      app.zoom, scale);
	check(fabs(after.x - before.x) < 1.0 && fabs(after.y - before.y) < 1.0, "about the tapped place");
}

/* The page mode's swipe: a flick turns, a short slow drag slides back. */
static void
test_swipe(void)
{
	/* The page mode at the page's fit, on the first page. */
	pv_app_action(&app, PV_ACTION_MODE_PAGE);
	pv_app_action(&app, PV_ACTION_FIT_PAGE);
	pv_app_action(&app, PV_ACTION_FIRST);
	run(500.0);
	check(app.mode == PV_MODE_PAGE && app.page == 0, "the page mode on page 1");

	/* A flick to the left turns to the next page. */
	stroke(1, 700.0, 380.0, -200.0, 0.0, 80.0, 1, 1);
	run(600.0);
	check(app.page == 1 && !app.turning && app.swipe == 0.0, "a flick turned to page %lu", (unsigned long)app.page + 1);

	/* A short slow drag slides back. */
	stroke(1, 500.0, 380.0, 60.0, 0.0, 400.0, 1, 0);
	run(100.0);
	check(app.swipe > 30.0, "the page follows the finger: %.1f", app.swipe);
	clock_us += 150000U;
	finger(KL_WINDOW_TOUCH_UP, 1, 0.0, 0.0);
	run(600.0);
	check(app.page == 1 && app.swipe == 0.0, "a short slow drag slid back to page %lu", (unsigned long)app.page + 1);
}

/* A tap on the sidebar's thumbnail shows its page: the finger plays the pointer. */
static void
test_sidebar(void)
{
	int x;
	int y;
	int width;
	int height;
	size_t shown;

	/* The scroll mode with the sidebar. */
	reset(0.0);
	pv_app_action(&app, PV_ACTION_THUMBNAILS);
	run(100.0);
	check(pv_app_sidebar_width(&app) > 0, "the sidebar is shown");

	/* A tap on page 3's thumbnail. */
	pv_thumbnail_place(&app, 2, &x, &y, &width, &height);
	finger(KL_WINDOW_TOUCH_DOWN, 1, (double)(x + width / 2), (double)(y + height / 2));
	clock_us += 80000U;
	finger(KL_WINDOW_TOUCH_UP, 1, 0.0, 0.0);
	run(300.0);
	shown = pv_app_current_page(&app);
	check(shown == 2, "a tap on the thumbnail showed page %lu", (unsigned long)shown + 1);
	check(!touch.moving && touch.fingers == 0U, "the tap did not scroll");
	pv_app_action(&app, PV_ACTION_THUMBNAILS);
}
