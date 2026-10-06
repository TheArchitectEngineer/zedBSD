/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws157-p003: draws Photos' view (userland/desktop/photos/view.c) on the
 * host into pictures and drives it as the window would, the thumbnails
 * and the whole pictures made by the real thread (thumbs.c, decode.c):
 * the timeline by month, a damaged file, the lists at the left, a photo
 * shown whole by a double click, the arrows, a turn, a favourite, back to
 * the grid, the slideshow, the cards on glass, and an empty library.  The
 * "PHOTOS" lines the view logs are checked.
 *
 *     host-photos FONT FALLBACK PREFIX FOLDER
 *
 * FOLDER is the Pictures folder make-photos.py --view writes.  Writes
 * PREFIX-NAME.ppm for each picture (PREFIX-NAME.pam and .panels on glass)
 * and prints "PASS name" or "FAIL name ..." for each check; exits with 1
 * when one failed.
 */

#include "userland/desktop/photos/app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The window's size. */
#define TEST_WIDTH		1040
#define TEST_HEIGHT		700

/* Where the view puts things at that size: the lists' rows, the grid's cells (four columns of 183, 6 apart). */
#define TEST_LIST_X		120
#define TEST_TIMELINE_Y		84
#define TEST_FAVORITES_Y	128
#define TEST_ALBUM_Y		208
#define TEST_ALBUM_ROW		52
#define TEST_CELL_X		360
#define TEST_CELL_STEP		189
#define TEST_SEPTEMBER_Y	207
#define TEST_AUGUST_Y		436
#define TEST_JULY_Y		665

/* The keys: F, R, Escape, Space, Right. */
#define TEST_KEY_F		33U
#define TEST_KEY_R		19U

/* The log the view wrote, for the checks. */
static char test_log[65536];
static size_t test_log_length;

/* The checks that failed. */
static int test_failures;

/* The frame's time. */
static uint64_t test_now = 1000000000U;

int main(int argc, char **argv);
static void test_frame(struct ph_view *view, struct kl_ui *ui, const struct kl_style *style);
static void test_pictures(struct ph_view *view, struct kl_ui *ui, const struct kl_style *style);
static void test_click(struct ph_view *view, struct kl_ui *ui, const struct kl_style *style, int x, int y, int twice);
static void test_key(struct ph_view *view, struct kl_ui *ui, const struct kl_style *style, uint32_t key, unsigned modifiers);
static void test_check(const char *name, const char *expected);
static void test_true(const char *name, int passed);
static int test_save(const struct ph_view *view, const struct kl_canvas *canvas, const char *prefix, const char *name);
unsigned kl_appearance_get(const struct kl_appearance *appearance);

/*
 * Draws and drives the view and checks what it did.
 */
int
main(
	int argc,
	char **argv)
{
	struct ph_photo *photos;
	struct kl_canvas canvas;
	struct kl_style style;
	struct kl_text text;
	struct ph_view view;
	struct kl_ui *ui;
	uint32_t *pixels;
	size_t count;
	int error;

	/* The arguments. */
	if (argc != 5) {
		fprintf(stderr, "usage: host-photos FONT FALLBACK PREFIX FOLDER\n");
		return 2;
	}

	/* The fonts. */
	kl_text_companions("userland/desktop/fonts/Mahora-Bold.ttf", "userland/desktop/fonts/JetBrainsMono-Regular.ttf");
	error = kl_text_open(&text, argv[1], argv[2]);
	if (error != 0) {
		fprintf(stderr, "host-photos: fonts error=%d\n", error);
		return 2;
	}

	/* The frame and its canvas. */
	pixels = calloc((size_t)TEST_WIDTH * TEST_HEIGHT, sizeof(pixels[0]));
	if (pixels == NULL)
		return 2;
	error = kl_canvas_init(&canvas, pixels, TEST_WIDTH, TEST_WIDTH, TEST_HEIGHT);
	if (error != 0)
		return 2;

	/* The input and the style. */
	ui = kl_ui_create();
	if (ui == NULL)
		return 2;
	style.canvas = &canvas;
	style.text = &text;
	style.theme = kl_theme_default();
	style.glass = 0;

	/* The library and the thread. */
	error = ph_library_scan(argv[4]);
	photos = ph_photos(&count);
	if (error != 0 || count != 9U) {
		printf("FAIL library error=%d photos=%lu (9 expected)\n", error, (unsigned long)count);
		return 1;
	}

	/*  ph_view_init(&view);=The view. */
	error = ph_view_init(&view);
	if (error != 0)
		return 2;
	error = ph_worker_start();
	if (error != 0)
		return 2;

	/* The timeline: the thumbnails made, the damaged file's not. */
	test_pictures(&view, ui, &style);
	test_check("thumb-broken", "THUMB photo=8 error=22");
	test_true("thumbs-made", view.kept == 8U);
	test_true("columns", view.columns == 4);
	(void)test_save(&view, &canvas, argv[3], "timeline");

	/* The lists: Favorites (none yet), the albums Family and Trips. */
	test_click(&view, ui, &style, TEST_LIST_X, TEST_FAVORITES_Y, 0);
	test_check("favorites", "LIST list=1 album=0");
	(void)test_save(&view, &canvas, argv[3], "favorites-empty");
	test_click(&view, ui, &style, TEST_LIST_X, TEST_ALBUM_Y + TEST_ALBUM_ROW, 0);
	test_check("album", "LIST list=2 album=1");
	test_pictures(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "album");
	test_click(&view, ui, &style, TEST_LIST_X, TEST_TIMELINE_Y, 0);
	test_check("timeline", "LIST list=0 album=0");

	/* One click chooses; a double click shows the photo (hill, turned by its EXIF) whole. */
	test_click(&view, ui, &style, TEST_CELL_X + TEST_CELL_STEP, TEST_SEPTEMBER_Y, 0);
	test_true("chosen", view.chosen == 1 && view.open < 0);
	test_click(&view, ui, &style, TEST_CELL_X + TEST_CELL_STEP, TEST_SEPTEMBER_Y, 1);
	test_check("open", "OPEN photo=1 name=hill.jpg turns=0 favorite=0");
	test_pictures(&view, ui, &style);
	test_check("picture", "PICTURE photo=1 error=0 width=640 height=480");
	(void)test_save(&view, &canvas, argv[3], "whole");

	/* Right: the next photo; R turns it; F marks it. */
	test_key(&view, ui, &style, KL_KEY_RIGHT, 0U);
	test_check("next", "OPEN photo=2 name=sea.jpg");
	test_pictures(&view, ui, &style);
	test_key(&view, ui, &style, TEST_KEY_R, 0U);
	test_check("turn", "TURN photo=2 turns=1");
	test_true("turned", view.picture.width == 450 && view.picture.height == 800 && view.thumbs[2].turns == 1);
	test_key(&view, ui, &style, TEST_KEY_F, 0U);
	test_check("favorite", "FAVORITE photo=2 on=1");
	test_true("save", view.save == 1 && photos[2].favorite && photos[2].turns == 1);
	view.save = 0;
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "turned");

	/* Shift+R turns it back; Escape goes back to the grid with the photo chosen. */
	test_key(&view, ui, &style, TEST_KEY_R, KL_MOD_SHIFT);
	test_check("turn-back", "TURN photo=2 turns=0");
	test_key(&view, ui, &style, KL_KEY_ESC, 0U);
	test_check("back", "BACK photo=2");
	test_true("back-chosen", view.open < 0 && view.chosen == 2);
	test_pictures(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "grid-favorite");

	/* The arrows in the grid, Enter shows. */
	test_key(&view, ui, &style, KL_KEY_DOWN, 0U);
	test_true("down", view.chosen == 6);
	test_key(&view, ui, &style, KL_KEY_ENTER, 0U);
	test_check("enter", "OPEN photo=6 name=wave.gif");
	test_pictures(&view, ui, &style);
	test_check("gif", "PICTURE photo=6 error=0 width=320 height=240");
	test_key(&view, ui, &style, KL_KEY_ESC, 0U);

	/* Favorites lists the one marked. */
	test_click(&view, ui, &style, TEST_LIST_X, TEST_FAVORITES_Y, 0);
	test_pictures(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "favorites");
	test_click(&view, ui, &style, TEST_LIST_X, TEST_TIMELINE_Y, 0);

	/* The slideshow from the photo chosen (another list chooses none: wave chosen again), round from the end. */
	test_click(&view, ui, &style, TEST_CELL_X + TEST_CELL_STEP, TEST_JULY_Y, 0);
	test_key(&view, ui, &style, KL_KEY_SPACE, 0U);
	test_check("slideshow", "SLIDESHOW on=1 photo=6");
	test_now += PH_SLIDE_US;
	(void)ph_view_tick(&view, test_now);
	test_check("slide", "SLIDE photo=7");
	test_now += PH_SLIDE_US;
	(void)ph_view_tick(&view, test_now);
	test_now += PH_SLIDE_US;
	(void)ph_view_tick(&view, test_now);
	test_check("slide-round", "SLIDE photo=0");
	test_key(&view, ui, &style, KL_KEY_SPACE, 0U);
	test_check("slideshow-stop", "SLIDESHOW on=0");
	test_key(&view, ui, &style, KL_KEY_ESC, 0U);

	/* The damaged file shown whole says so. */
	ph_view_open(&view, 8, test_now);
	test_pictures(&view, ui, &style);
	test_check("broken-whole", "PICTURE photo=8 error=22");
	(void)test_save(&view, &canvas, argv[3], "broken");
	test_key(&view, ui, &style, KL_KEY_ESC, 0U);

	/* On glass: the two cards. */
	view.glass = 1;
	style.glass = 1;
	test_pictures(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "glass");
	view.glass = 0;
	style.glass = 0;

	/* An empty library. */
	ph_worker_stop();
	ph_library_release();
	error = ph_view_reset(&view);
	test_true("reset", error == 0 && view.thumb_count == 0U && view.open < 0);
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "empty");

	/* The result. */
	ph_view_release(&view);
	kl_ui_destroy(ui);
	free(pixels);
	if (test_failures != 0)
		return 1;
	return 0;
}

/*
 * Writes a line of the view's log: kept for the checks.
 */
void
ph_log(
	const char *format,
	...)
{
	va_list arguments;
	int length;

	/* At the end of the log, with its newline. */
	va_start(arguments, format);
	length = vsnprintf(test_log + test_log_length, sizeof(test_log) - test_log_length, format, arguments);
	va_end(arguments);
	if (length < 0 || test_log_length + (size_t)length + 2U >= sizeof(test_log))
		return;
	test_log_length += (size_t)length;
	test_log[test_log_length] = '\n';
	test_log_length++;
	test_log[test_log_length] = '\0';
}

/* Draws a frame as the window would (without showing it), then gives the view the keys no widget took. */
static void
test_frame(
	struct ph_view *view,
	struct kl_ui *ui,
	const struct kl_style *style)
{
	struct kl_event event;
	int taken;

	/* The frame. */
	kl_ui_begin(ui, test_now);
	ph_view_draw(view, ui, style, TEST_WIDTH, TEST_HEIGHT, test_now);
	(void)kl_ui_end(ui, test_now);

	/* The keys no widget took. */
	for (;;) {
		taken = kl_ui_take(ui, &event);
		if (!taken)
			break;
		if (event.kind == KL_EVENT_KEY)
			ph_view_key(view, event.code, event.modifiers, test_now);
	}
}

/*
 * Draws frames as the window would until the view wants no picture the
 * thread can make: the wants queued, the thread waited for, the results
 * given.
 */
static void
test_pictures(
	struct ph_view *view,
	struct kl_ui *ui,
	const struct kl_style *style)
{
	struct ph_photo *photos;
	struct ph_result result;
	struct timespec pause;
	size_t count;
	size_t index;
	size_t photo;
	int rounds;
	int busy;
	int taken;

	/* Rounds of a frame and its pictures. */
	photos = ph_photos(&count);
	for (rounds = 0; rounds < 20; rounds++) {
		test_frame(view, ui, style);
		if (view->want_count == 0U)
			return;

		/* Queued as ph_jobs does. */
		ph_worker_drop_thumbs();
		for (index = 0; index < view->want_count; index++) {
			photo = view->wants[index];
			(void)ph_worker_queue(photo, (long)photo == view->open, photos[photo].turns, photos[photo].path, view->generation);
		}

		/* Made. */
		for (;;) {
			busy = ph_worker_busy();
			taken = ph_worker_take(&result);
			if (taken) {
				ph_view_result(view, &result);
				continue;
			}

			/* Done when the thread is idle. */
			if (!busy)
				break;
			pause.tv_sec = 0;
			pause.tv_nsec = 2000000;
			(void)nanosleep(&pause, NULL);
		}
	}
}

/*
 * Clicks at a point (twice, soon after each other, for a double click): the
 * pointer goes there, presses and releases, and a frame takes the click.
 */
static void
test_click(
	struct ph_view *view,
	struct kl_ui *ui,
	const struct kl_style *style,
	int x,
	int y,
	int twice)
{
	/* A second after the last, so that the first is a single click. */
	test_now += 1000000U;
	(void)kl_ui_pointer_motion(ui, (double)x, (double)y);
	(void)kl_ui_pointer_button(ui, 1, test_now);
	(void)kl_ui_pointer_button(ui, 0, test_now + 50000U);
	test_frame(view, ui, style);
	if (!twice)
		return;

	/* The second, soon after. */
	test_now += 150000U;
	(void)kl_ui_pointer_button(ui, 1, test_now);
	(void)kl_ui_pointer_button(ui, 0, test_now + 50000U);
	test_frame(view, ui, style);
}

/* Presses a key as the window would give it (no widget takes it). */
static void
test_key(
	struct ph_view *view,
	struct kl_ui *ui,
	const struct kl_style *style,
	uint32_t key,
	unsigned modifiers)
{
	/* The key, then a frame. */
	test_now += 100000U;
	ph_view_key(view, key, modifiers, test_now);
	test_frame(view, ui, style);
}

/*
 * Checks that the view logged a line, and empties the log.
 */
static void
test_check(
	const char *name,
	const char *expected)
{
	const char *found;

	/* The line among those logged. */
	found = strstr(test_log, expected);
	if (found == NULL) {
		printf("FAIL %s expected \"%s\" in:\n%s", name, expected, test_log);
		test_failures++;
	} else {
		printf("PASS %s\n", name);
	}

	/* The next check reads only what comes after. */
	test_log_length = 0;
	test_log[0] = '\0';
}

/* Checks a condition. */
static void
test_true(
	const char *name,
	int passed)
{
	/* Its line. */
	if (passed) {
		printf("PASS %s\n", name);
		return;
	}

	/* Failed. */
	printf("FAIL %s\n", name);
	test_failures++;
}

/*
 * Writes a canvas as PREFIX-NAME.ppm, or on glass as PREFIX-NAME.pam with
 * its alpha and PREFIX-NAME.panels; nonzero when it cannot.
 */
static int
test_save(
	const struct ph_view *view,
	const struct kl_canvas *canvas,
	const char *prefix,
	const char *name)
{
	struct kl_glass_panel panels[4];
	const char *extension;
	char path[512];
	uint32_t pixel;
	size_t count;
	size_t index;
	int x;
	int y;
	FILE *file;

	/* The picture's file: PAM with its alpha on glass, else PPM. */
	extension = "ppm";
	if (view->glass)
		extension = "pam";
	(void)snprintf(path, sizeof(path), "%s-%s.%s", prefix, name, extension);
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;

	/* The header. */
	if (view->glass)
		fprintf(file, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", canvas->width, canvas->height);
	else
		fprintf(file, "P6\n%d %d\n255\n", canvas->width, canvas->height);

	/* Each pixel's red, green and blue, and on glass its alpha. */
	for (y = 0; y < canvas->height; y++) {
		for (x = 0; x < canvas->width; x++) {
			pixel = canvas->pixels[(size_t)y * canvas->stride + (size_t)x];
			fputc((int)((pixel >> 16) & 0xffU), file);
			fputc((int)((pixel >> 8) & 0xffU), file);
			fputc((int)(pixel & 0xffU), file);
			if (view->glass)
				fputc((int)(pixel >> 24), file);
		}
	}

	/* Written. */
	(void)fclose(file);

	/* On glass, the panels beside it. */
	if (!view->glass)
		return 0;
	(void)snprintf(path, sizeof(path), "%s-%s.panels", prefix, name);
	file = fopen(path, "w");
	if (file == NULL)
		return -1;
	count = ph_view_panels(view, canvas->width, canvas->height, panels, 4);
	for (index = 0; index < count; index++)
		fprintf(file, "%d %d %d %d %d\n", panels[index].x, panels[index].y, panels[index].width, panels[index].height, panels[index].radius);
	(void)fclose(file);
	return 0;
}

/* The appearance libkeiland's theme asks for: the light one (the host test has no compositor to ask). */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	/* The light appearance. */
	(void)appearance;
	return KL_APPEARANCE_LIGHT;
}
