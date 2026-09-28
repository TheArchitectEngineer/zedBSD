/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of PDF Viewer's core (ws079-p006): the view, the frame,
 * the document cache and the chooser, without Wayland and Vulkan.
 *
 *   host-pdfviewer FONT DOCUMENT.pdf OUTDIR
 *
 * It opens a three-page document in a 1000x760 window and checks, writing
 * a frame of each step to a PPM in OUTDIR: the scroll mode (fit width, a
 * wheel scroll, a drag), the page mode (a sideways drag that turns to the
 * next page, one that springs back, the keys), the zoom (Ctrl+plus, minus
 * and 0), Home and End, the file chooser, a document that cannot be
 * opened, and the Annotate action.
 */

#include "../../../userland/desktop/pdfviewer/viewer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window's size in the test. */
#define TEST_WIDTH 1000
#define TEST_HEIGHT 760

static struct pv_app app;
static struct pv_text text;
static uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];
static const char *out_dir;
static int failures;
static uint64_t now;

static void frame(const char *name);
static void key(uint32_t code, uint32_t modifiers);
static void wheel(int amount, uint32_t modifiers);
static void drag(int from_x, int from_y, int to_x, int to_y, int steps, int milliseconds);
static void settle(void);
static void check(int condition, const char *what);

int
main(
	int argc,
	char **argv)
{
	double before;
	int error;

	if (argc != 4) {
		fprintf(stderr, "usage: host-pdfviewer FONT DOCUMENT.pdf OUTDIR\n");
		return 2;
	}
	out_dir = argv[3];
	error = pv_text_open(&text, argv[1]);
	check(error == 0, "font opens");
	pv_app_init(&app, &text, TEST_WIDTH, TEST_HEIGHT);
	now = 1000;
	app.now = now;
	frame("00-empty");

	/* A missing file leaves a message. */
	error = pv_app_open(&app, "/nonexistent/missing.pdf");
	check(error != 0 && app.message[0] != '\0' && !app.has_document, "a missing file leaves a message");
	frame("01-missing");

	/* The document in the scroll mode, fitting the width. */
	error = pv_app_open(&app, argv[2]);
	check(error == 0 && app.has_document && app.document.count == 3, "the document opens with 3 pages");
	frame("02-scroll-fit-width");
	check(pv_app_scale(&app, 0) > 1.5 && pv_app_scale(&app, 0) < 1.7, "fit width scales A4 to the window");

	/* The wheel and a drag scroll. */
	wheel(600, 0);
	check(app.scroll_y == 600.0, "the wheel scrolls");
	drag(500, 600, 500, 200, 8, 160);
	check(app.scroll_y == 1000.0, "a drag scrolls with the pointer");
	frame("03-scroll-moved");

	/* End and Home. */
	key(PV_KEY_END, 0);
	check(app.scroll_y > 2000.0, "End goes to the last page");
	frame("04-scroll-end");
	key(PV_KEY_HOME, 0);
	check(app.scroll_y == 0.0, "Home goes to the first page");

	/* The zoom: in twice, out, and back to the fit. */
	before = pv_app_scale(&app, 0);
	key(PV_KEY_EQUAL, PV_MOD_CTRL);
	key(PV_KEY_EQUAL, PV_MOD_CTRL);
	check(app.fit == PV_FIT_CUSTOM && pv_app_scale(&app, 0) > before * 1.5, "Ctrl+plus zooms in");
	frame("05-zoom-in");
	key(PV_KEY_MINUS, PV_MOD_CTRL);
	check(pv_app_scale(&app, 0) < before * 1.3, "Ctrl+minus zooms out");
	key(PV_KEY_0, PV_MOD_CTRL);
	check(app.fit == PV_FIT_WIDTH, "Ctrl+0 returns to the fit");
	pv_app_action(&app, PV_ACTION_FIT_PAGE);
	frame("06-scroll-fit-page");
	check(app.fit == PV_FIT_PAGE, "Fit Page");

	/* The page mode: a swipe to the left turns to the next page. */
	pv_app_action(&app, PV_ACTION_MODE_PAGE);
	check(app.mode == PV_MODE_PAGE && app.page == 0, "the page mode starts on the page in view");
	frame("07-page-1");
	drag(800, 380, 420, 390, 6, 300);
	check(app.turning || app.page == 1, "a long swipe turns");
	settle();
	check(app.page == 1 && app.swipe == 0.0, "the swipe shows page 2");
	frame("09-page-2");

	/* A short, slow swipe springs back. */
	drag(500, 380, 560, 380, 6, 600);
	settle();
	check(app.page == 1, "a short swipe springs back");

	/* A frame in the middle of a swipe shows both pages. */
	app.pressed = 1;
	app.dragging = 1;
	app.swipe = -380.0;
	frame("08-swipe-middle");
	app.pressed = 0;
	app.dragging = 0;
	app.swipe = 0.0;

	/* The keys turn pages. */
	key(PV_KEY_PAGE_DOWN, 0);
	settle();
	check(app.page == 2, "Page Down turns to page 3");
	frame("10-page-3");
	key(PV_KEY_RIGHT, 0);
	settle();
	check(app.page == 2, "Right at the last page stays");
	key(PV_KEY_LEFT, 0);
	settle();
	check(app.page == 1, "Left turns back");
	key(PV_KEY_HOME, 0);
	check(app.page == 0, "Home shows page 1");

	/* Zoom in the page mode, and the scroll mode again keeps the page. */
	key(PV_KEY_EQUAL, PV_MOD_CTRL);
	frame("11-page-zoom");
	key(PV_KEY_0, PV_MOD_CTRL);
	check(app.fit == PV_FIT_PAGE, "Ctrl+0 fits the page in the page mode");
	key(PV_KEY_PAGE_DOWN, 0);
	settle();
	pv_app_action(&app, PV_ACTION_MODE_SCROLL);
	check(app.mode == PV_MODE_SCROLL && app.scroll_y > 1000.0, "the scroll mode keeps page 2 in view");

	/* Annotate asks the window to start Notes. */
	key(PV_KEY_E, PV_MOD_CTRL);
	check(app.want_annotate == 1, "Ctrl+E asks to annotate");

	/* The chooser. */
	key(PV_KEY_O, PV_MOD_CTRL);
	check(app.choosing == 1, "Ctrl+O opens the chooser");
	frame("12-chooser");
	key(PV_KEY_ESCAPE, 0);
	check(app.choosing == 0, "Escape closes the chooser");

	/* Ctrl+W closes the document, and again the window. */
	key(PV_KEY_W, PV_MOD_CTRL);
	check(!app.has_document && app.want_close == 0, "Ctrl+W closes the document");
	key(PV_KEY_W, PV_MOD_CTRL);
	check(app.want_close == 1, "Ctrl+W on an empty window closes it");

	pv_app_release(&app);
	pv_text_close(&text);
	if (failures != 0) {
		printf("host-pdfviewer: %d FAILED\n", failures);
		return 1;
	}
	printf("host-pdfviewer: ok\n");
	return 0;
}

/* Draws a frame and writes it as OUTDIR/NAME.ppm. */
static void
frame(
	const char *name)
{
	struct pv_canvas canvas;
	char path[1024];
	FILE *file;
	size_t index;
	unsigned char rgb[3];

	canvas.pixels = pixels;
	canvas.stride = TEST_WIDTH;
	canvas.width = TEST_WIDTH;
	canvas.height = TEST_HEIGHT;
	pv_draw(&app, &canvas);
	snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
	file = fopen(path, "wb");
	if (file == NULL) {
		check(0, "the frame is written");
		return;
	}
	fprintf(file, "P6\n%d %d\n255\n", TEST_WIDTH, TEST_HEIGHT);
	for (index = 0; index < (size_t)TEST_WIDTH * TEST_HEIGHT; index++) {
		rgb[0] = (unsigned char)(pixels[index] >> 16);
		rgb[1] = (unsigned char)(pixels[index] >> 8);
		rgb[2] = (unsigned char)pixels[index];
		fwrite(rgb, 1, 3, file);
	}
	fclose(file);
}

/* Presses a key. */
static void
key(
	uint32_t code,
	uint32_t modifiers)
{
	struct pv_event event;

	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_KEY;
	event.key = code;
	event.pressed = 1;
	event.modifiers = modifiers;
	event.time = now;
	pv_app_event(&app, &event);
}

/* Turns the wheel. */
static void
wheel(
	int amount,
	uint32_t modifiers)
{
	struct pv_event event;

	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_AXIS;
	event.scroll = amount;
	event.modifiers = modifiers;
	event.time = now;
	pv_app_event(&app, &event);
}

/* Drags the pointer with the left button from one place to another in steps over a time. */
static void
drag(
	int from_x,
	int from_y,
	int to_x,
	int to_y,
	int steps,
	int milliseconds)
{
	struct pv_event event;
	int step;

	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_BUTTON;
	event.button = PV_BUTTON_LEFT;
	event.pressed = 1;
	event.x = from_x;
	event.y = from_y;
	event.time = now;
	pv_app_event(&app, &event);
	for (step = 1; step <= steps; step++) {
		now += (uint64_t)(milliseconds / steps);
		app.now = now;
		memset(&event, 0, sizeof(event));
		event.type = PV_EVENT_MOTION;
		event.x = from_x + (to_x - from_x) * step / steps;
		event.y = from_y + (to_y - from_y) * step / steps;
		event.time = now;
		pv_app_event(&app, &event);
	}
	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_BUTTON;
	event.button = PV_BUTTON_LEFT;
	event.pressed = 0;
	event.x = to_x;
	event.y = to_y;
	event.time = now;
	pv_app_event(&app, &event);
}

/* Lets time pass until a turn ends. */
static void
settle(void)
{
	int rounds;

	for (rounds = 0; rounds < 100; rounds++) {
		now += 16;
		pv_app_tick(&app, now);
		if (!app.turning)
			break;
	}
}

/* Counts a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		printf("ok: %s\n", what);
		return;
	}
	printf("FAILED: %s\n", what);
	failures++;
}
