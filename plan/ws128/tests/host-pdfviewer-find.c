/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws128-p004: PDF Viewer's Find and selection (find.c) on the host, with
 * the viewer's core (view, frame, document, canvas, text) and libpdf, on
 * WS175's edit-basic.pdf (three pages; page 1's paragraph begins "The
 * quick brown fox", page 3 is turned a quarter): the words typed find
 * their first place, F3 and Shift+F3 go on and back round the document,
 * words not there find nothing, a drag over page 1's first line selects
 * and Ctrl+C copies it, a press off the words drags the view, Esc lets
 * the marks go; the frames with the marks are drawn (PPM).
 *
 *   host-pdfviewer-find FONT edit-basic.pdf OUTDIR
 */

#include "../../../userland/desktop/pdfviewer/viewer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window's size in the test. */
#define TEST_WIDTH	1000
#define TEST_HEIGHT	760

static struct pv_app app;
static struct pv_text text;
static uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];
static const char *out_dir;
static int failures;
static int passes;

static void frame(const char *name);
static void key(uint32_t code, uint32_t modifiers);
static void button(int x, int y, int pressed);
static void motion(int x, int y);
static void screen_of(size_t page, double x, double y, int *screen_x, int *screen_y);
static void check(int condition, const char *what);

/* Runs the checks. */
int
main(
	int argc,
	char **argv)
{
	const double *quad;
	double scroll;
	size_t first;
	int from_x;
	int from_y;
	int to_x;
	int to_y;
	int error;

	/* The font, the document and the folder. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-pdfviewer-find FONT edit-basic.pdf OUTDIR\n");
		return 2;
	}
	out_dir = argv[3];
	error = pv_text_open(&text, argv[1]);
	check(error == 0, "the font opens");
	pv_app_init(&app, &text, TEST_WIDTH, TEST_HEIGHT);
	app.now = 1000;
	error = pv_app_open(&app, argv[2]);
	check(error == 0 && app.has_document && app.document.count == 3, "edit-basic.pdf opens, 3 pages");
	if (error != 0)
		return 1;
	frame("00-open");

	/* Words typed: their first place (page 1, "lazy" of the first line). */
	pv_find_text(&app, "LAZY");
	check(app.find_found && app.find_page == 0 && app.find_from == 35 && app.find_length == 4, "\"LAZY\" found on page 1 at 35 (no case)");
	first = app.find_from;
	frame("01-found");

	/* F3: the next place round the document comes back to the same one (it is the only one). */
	key(PV_KEY_F3, 0);
	check(app.find_found && app.find_page == 0 && app.find_from == first, "F3: round to the only place");

	/* "line": the third line's, then the fourth's, page 3's, and back with Shift+F3. */
	pv_find_text(&app, "line");
	check(app.find_found && app.find_page == 0, "\"line\" found on page 1");
	first = app.find_from;
	key(PV_KEY_F3, 0);
	check(app.find_found && app.find_page == 0 && app.find_from > first, "F3: the next \"line\" on page 1");
	key(PV_KEY_F3, 0);
	check(app.find_found && app.find_page == 2, "F3: on to page 3 (turned)");
	frame("02-page-3");
	key(PV_KEY_F3, PV_MOD_SHIFT);
	check(app.find_found && app.find_page == 0 && app.find_from > first, "Shift+F3: back to page 1");

	/* Words not in the document. */
	pv_find_text(&app, "zebra");
	check(!app.find_found, "\"zebra\" is not found");

	/* A drag over the first line's first nine characters selects them; Ctrl+C copies "The quick". */
	pv_find_clear(&app);
	app.scroll_y = 0.0;
	pv_app_clamp(&app);
	screen_of(0, 74.0, 87.0, &from_x, &from_y);
	button(from_x, from_y, 1);
	check(app.selecting && app.select_page == 0 && app.select_anchor == 0, "a press on the first letter starts a selection");
	quad = app.document.pages[0].text->characters[8].quad;
	screen_of(0, (quad[0] + quad[2]) / 2.0, (quad[1] + quad[7]) / 2.0, &to_x, &to_y);
	motion((from_x + to_x) / 2, from_y);
	motion(to_x, to_y);
	button(to_x, to_y, 0);
	check(app.has_selection && !app.selecting && app.select_caret == 8U, "the drag selects to the k of \"quick\"");
	frame("03-selected");
	key(PV_KEY_C, PV_MOD_CTRL);
	check(app.copy_text != NULL && strncmp(app.copy_text, "The quick", 9) == 0, "Ctrl+C copies \"The quick\"");
	free(app.copy_text);
	app.copy_text = NULL;

	/* A press off the words drags the view, and the selection goes. */
	scroll = app.scroll_y;
	screen_of(0, 400.0, 700.0, &from_x, &from_y);
	button(from_x, from_y, 1);
	motion(from_x, from_y - 100);
	button(from_x, from_y - 100, 0);
	check(!app.has_selection && app.scroll_y > scroll + 50.0, "a drag off the words scrolls, the selection goes");

	/* Esc lets the place found go. */
	pv_find_text(&app, "fox");
	key(PV_KEY_ESCAPE, 0);
	check(!app.find_found, "Esc lets the place found go");

	/* Done. */
	pv_app_release(&app);
	pv_text_close(&text);
	printf("host-pdfviewer-find: %d passed, %d failed\n", passes, failures);
	if (failures != 0)
		return 1;
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
	uint32_t pixel;
	size_t at;

	/* The frame. */
	canvas.pixels = pixels;
	canvas.stride = TEST_WIDTH;
	canvas.width = TEST_WIDTH;
	canvas.height = TEST_HEIGHT;
	pv_draw(&app, &canvas);

	/* The PPM. */
	(void)snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, name);
	file = fopen(path, "wb");
	if (file == NULL)
		return;
	fprintf(file, "P6\n%d %d\n255\n", TEST_WIDTH, TEST_HEIGHT);
	for (at = 0; at < (size_t)TEST_WIDTH * TEST_HEIGHT; at++) {
		pixel = pixels[at];
		fputc((int)((pixel >> 16) & 0xffU), file);
		fputc((int)((pixel >> 8) & 0xffU), file);
		fputc((int)(pixel & 0xffU), file);
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

	/* A press. */
	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_KEY;
	event.key = code;
	event.pressed = 1;
	event.modifiers = modifiers;
	event.time = app.now;
	pv_app_event(&app, &event);
}

/* Presses or lets go the left button at a point of the window. */
static void
button(
	int x,
	int y,
	int pressed)
{
	struct pv_event event;

	/* The button. */
	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_BUTTON;
	event.button = PV_BUTTON_LEFT;
	event.pressed = pressed;
	event.x = x;
	event.y = y;
	app.now += 20U;
	event.time = app.now;
	pv_app_event(&app, &event);
}

/* Moves the pointer to a point of the window. */
static void
motion(
	int x,
	int y)
{
	struct pv_event event;

	/* The motion. */
	memset(&event, 0, sizeof(event));
	event.type = PV_EVENT_MOTION;
	event.x = x;
	event.y = y;
	app.now += 20U;
	event.time = app.now;
	pv_app_event(&app, &event);
}

/* Gives the window's point of a point of a page (points from its top left). */
static void
screen_of(
	size_t page,
	double x,
	double y,
	int *screen_x,
	int *screen_y)
{
	double scale;

	/* Through the page's place and scale in the view (no sidebar). */
	scale = pv_app_scale(&app, page);
	*screen_x = (int)(pv_app_page_left(&app, page) + x * scale);
	*screen_y = (int)(pv_app_page_top(&app, page) + y * scale - app.scroll_y);
}

/* Counts one check and prints it. */
static void
check(
	int condition,
	const char *what)
{
	/* Passed or failed. */
	if (condition) {
		passes++;
		printf("ok %s\n", what);
		return;
	}

	/* Failed. */
	failures++;
	printf("FAILED %s\n", what);
}
