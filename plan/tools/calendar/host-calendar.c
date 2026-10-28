/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws155-p000: draws Calendar's view (userland/desktop/calendar/view.c) on
 * the host into pictures at chosen times, and drives it as the window
 * would: the start, a date clicked and the small desk calendar's page
 * turning, a kind of event dragged onto a date and the cell sinking, the
 * memo typed in and dragged onto a date, the event kept double clicked
 * and double tapped on its pill to edit it (ws155-p006), a click where a day
 * has no item opening a new event in the months, the week and the day and in
 * a window too narrow for the panel (the editor over the months), the next
 * month, a calendar hidden, the motion reduced, and the cards on glass.  The "CALENDAR" lines the
 * view logs are checked.
 *
 *     host-calendar FONT FALLBACK PREFIX
 *
 * Today is fixed at 2026-10-05.  Writes PREFIX-NAME.ppm for each picture
 * (PREFIX-NAME.pam and .panels on glass) and prints "PASS name" or "FAIL
 * name ..." for each check; exits with 1 when one failed.
 */

#include "userland/desktop/calendar/calendar.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window's size, and where its months' cells are (the months' card at 236, its band at 102, cells 99 by 105). */
#define TEST_WIDTH		1280
#define TEST_HEIGHT		800
#define TEST_SHORT_HEIGHT	680
#define TEST_GRID_X		248
#define TEST_GRID_Y		102
#define TEST_COLUMN		99
#define TEST_ROW		105

/* The editor's Save in the panel (x 980, the editor inset 14; its buttons under the title, all day, the times and the calendars). */
#define TEST_SAVE_X		1226
#define TEST_SAVE_Y		274
#define TEST_CANCEL_X		(TEST_SAVE_X - 78)

/*
 * The narrower window, with no room for the panel, and its editor's Save
 * over the months (the months' card 676 wide from 224 less the gap, the
 * card 300 wide under the bar, the editor inset 14).
 */
#define TEST_NARROW_WIDTH	900
#define TEST_NARROW_SAVE_X	834
#define TEST_NARROW_SAVE_Y	334

/* The week's and the day's band (the months' card at 224, 744 wide, under the bar and the days' row). */
#define TEST_LIST_X		224
#define TEST_LIST_Y		90
#define TEST_WEEK_COLUMN	(744 / 7)

/* The log the view wrote, for the checks. */
static char test_log[32768];
static size_t test_log_length;

/* The checks that failed. */
static int test_failures;

/* The frame's time. */
static uint64_t test_now = 1000000000U;

/* The window's width the frames are drawn at. */
static int test_width = TEST_WIDTH;

int main(int argc, char **argv);
int test_calendar_data_load(const char *folder, const struct cal_date *today);
static void test_frame(struct cal_view *view, struct kl_ui *ui, const struct kl_style *style);
static void test_click(struct cal_view *view, struct kl_ui *ui, const struct kl_style *style, int x, int y);
static void test_double(struct cal_view *view, struct kl_ui *ui, const struct kl_style *style, int x, int y, int finger);
static void test_check(const char *name, const char *expected);
static void test_has(const char *name, const char *expected);
static void test_absent(const char *name, const char *unexpected);
static void test_narrow(struct cal_view *view, struct kl_ui *ui, const struct kl_style *style, const char *prefix);
static int test_save(struct cal_view *view, const struct kl_canvas *canvas, const char *prefix, const char *name, int glass);

/*
 * Draws and drives the view and checks what it logged.
 */
int
main(
	int argc,
	char **argv)
{
	static const char *const flips[] = { "flip-0", "flip-1", "flip-2", "flip-3", "flip-4", "flip-5" };
	static const char *const sinks[] = { "sink-0", "sink-1", "sink-2" };
	static const uint32_t memo_keys[] = { KL_KEY_SPACE, 24U, 37U };
	struct cal_date today;
	struct kl_canvas canvas;
	struct kl_canvas short_canvas;
	struct kl_style style;
	struct kl_style short_style;
	struct kl_text text;
	struct cal_view view;
	struct kl_ui *ui;
	uint32_t *pixels;
	uint32_t *copy;
	size_t count;
	size_t kept;
	int desk_only;
	int error;
	int same;
	int i;

	/* The fonts and the prefix of the pictures. */
	if (argc != 5) {
		fprintf(stderr, "usage: host-calendar FONT FALLBACK PREFIX FOLDER\n");
		return 2;
	}

	/* The fonts. */
	kl_text_companions("userland/desktop/fonts/Mahora-Bold.ttf", "userland/desktop/fonts/JetBrainsMono-Regular.ttf");
	error = kl_text_open(&text, argv[1], argv[2]);
	if (error != 0) {
		fprintf(stderr, "host-calendar: fonts error=%d\n", error);
		return 2;
	}

	/* The frame. */
	pixels = calloc((size_t)TEST_WIDTH * TEST_HEIGHT, sizeof(pixels[0]));
	if (pixels == NULL)
		return 2;

	/* A copy of a frame, to compare. */
	copy = calloc((size_t)TEST_WIDTH * TEST_HEIGHT, sizeof(copy[0]));
	if (copy == NULL)
		return 2;
	error = kl_canvas_init(&canvas, pixels, TEST_WIDTH, TEST_WIDTH, TEST_HEIGHT);
	if (error != 0)
		return 2;

	/* The shorter window's canvas, on the same pixels. */
	error = kl_canvas_init(&short_canvas, pixels, TEST_WIDTH, TEST_WIDTH, TEST_SHORT_HEIGHT);
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
	short_style = style;
	short_style.canvas = &short_canvas;

	/* The view at the start: October 2026 in view, the 5th chosen. */
	today.year = 2026;
	today.month = 10;
	today.day = 5;
	error = test_calendar_data_load(argv[4], &today);
	if (error != 0) {
		fprintf(stderr, "host-calendar: store error=%d\n", error);
		return 2;
	}
	error = cal_view_init(&view, &today, test_now);
	if (error != 0) {
		fprintf(stderr, "host-calendar: view error=%d\n", error);
		return 2;
	}

	/* Its first frames. */
	test_frame(&view, ui, &style);
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "start", 0);
	test_check("start", "READY today=2026-10-05");

	/* A frame of the desk calendar alone (as the window draws while only it moves) is the whole frame's. */
	test_now += 400000U;
	desk_only = cal_view_desk_only(&view);
	cal_view_draw_desk(&view, &style, test_now);
	memcpy(copy, pixels, (size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(pixels[0]));
	test_frame(&view, ui, &style);
	same = memcmp(copy, pixels, (size_t)TEST_WIDTH * TEST_HEIGHT * sizeof(pixels[0]));
	if (!desk_only || same != 0) {
		printf("FAIL desk-only desk_only=%d same=%d\n", desk_only, same);
		test_failures++;
	} else {
		printf("PASS desk-only\n");
	}

	/* A date clicked (the 14th, Wednesday of the third week): its page turns, six frames over the turn. */
	test_click(&view, ui, &style, TEST_GRID_X + 3 * TEST_COLUMN + 49, TEST_GRID_Y + 52 + 2 * TEST_ROW + 52);
	test_has("blank-month", "BLANK date=2026-10-14 mode=Month");
	test_has("blank-new", "EDIT new list=Work");
	test_check("select", "FLIP from=2026-10-05 to=2026-10-14");
	for (i = 0; i < 6; i++) {
		/* One frame of the turn. */
		(void)test_save(&view, &canvas, argv[3], flips[i], 0);
		test_now += 110000U;
		test_frame(&view, ui, &style);
	}

	/* ws155-p006: the click was where the 14th has no item, so a new event of the 14th opened; Cancel closes it. */
	test_click(&view, ui, &style, TEST_CANCEL_X, TEST_SAVE_Y);
	test_check("blank-cancel", "EDIT cancel");

	/* The Work card dragged onto the 22nd: the cell sinks, three frames. */
	test_now += 2000000U;
	(void)kl_ui_pointer_motion(ui, 1048.0, 160.0);
	(void)kl_ui_pointer_button(ui, 1, test_now);
	test_frame(&view, ui, &style);
	(void)kl_ui_pointer_motion(ui, 900.0, 300.0);
	test_frame(&view, ui, &style);
	(void)kl_ui_pointer_motion(ui, TEST_GRID_X + 4 * TEST_COLUMN + 49, TEST_GRID_Y + 52 + 3 * TEST_ROW + 52);
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "drag", 0);
	(void)kl_ui_pointer_button(ui, 0, test_now + 20000U);
	test_frame(&view, ui, &style);
	test_frame(&view, ui, &style);
	test_check("drop", "FLIP from=2026-10-14 to=2026-10-22 reduced=0\nEDIT new list=Work");
	for (i = 0; i < 3; i++) {
		/* One frame of the sink. */
		(void)test_save(&view, &canvas, argv[3], sinks[i], 0);
		test_now += 100000U;
		test_frame(&view, ui, &style);
	}

	/* The editor: Save keeps the new event at 09:00 on the 22nd. */
	(void)test_save(&view, &canvas, argv[3], "editor", 0);
	test_now += 1000000U;
	test_click(&view, ui, &style, TEST_SAVE_X, TEST_SAVE_Y);
	test_check("save", "list=Work date=2026-10-22 all_day=0 start=540 end=600 error=0");

	/* ws155-p006: the event kept, double clicked on its pill on the 22nd, opens in the editor; Cancel changes nothing. */
	(void)cal_items(&count);
	test_double(&view, ui, &style, TEST_GRID_X + 4 * TEST_COLUMN + 49, TEST_GRID_Y + 52 + 3 * TEST_ROW + 39, 0);
	test_check("pill-open", "EDIT open index=");
	test_click(&view, ui, &style, TEST_SAVE_X - 78, TEST_SAVE_Y);
	test_check("pill-cancel", "EDIT cancel");

	/* Double tapped by a finger, it opens again, and Save keeps it in its place (no new item). */
	test_double(&view, ui, &style, TEST_GRID_X + 4 * TEST_COLUMN + 49, TEST_GRID_Y + 52 + 3 * TEST_ROW + 39, 1);
	test_check("pill-tap", "EDIT open index=");
	(void)test_save(&view, &canvas, argv[3], "pill-editor", 0);
	test_click(&view, ui, &style, TEST_SAVE_X, TEST_SAVE_Y);
	(void)cal_items(&kept);
	test_check("pill-save", "list=Work date=2026-10-22 all_day=0 start=540 end=600 error=0");
	if (kept != count) {
		printf("FAIL pill-count before=%zu after=%zu\n", count, kept);
		test_failures++;
	} else {
		printf("PASS pill-count\n");
	}

	/* A double click on the cell beside its pills opens no item, but the new event its first click opened; Cancel closes it. */
	test_double(&view, ui, &style, TEST_GRID_X + 4 * TEST_COLUMN + 49, TEST_GRID_Y + 52 + 3 * TEST_ROW + 12, 0);
	test_has("blank-double", "BLANK date=2026-10-22 mode=Month");
	test_absent("pill-none", "EDIT open");
	test_click(&view, ui, &style, TEST_CANCEL_X, TEST_SAVE_Y);
	test_check("blank-double-cancel", "EDIT cancel");

	/* A window too narrow for the panel: a click where a day has no item opens the editor over the months. */
	test_narrow(&view, ui, &style, argv[3]);

	/* The memo: a click after its last line's end gives it the keyboard with the caret there, " ok" is typed, then it is dragged by its header onto the 26th. */
	test_now += 1000000U;
	test_click(&view, ui, &style, 1240, 500);
	for (i = 0; i < 3; i++) {
		/* One key: Space, O, K. */
		(void)kl_ui_key(ui, memo_keys[i], 1, 0U);
		test_frame(&view, ui, &style);
	}

	/* The memo's header pressed, moved over the 26th and let go. */
	(void)kl_ui_pointer_motion(ui, 1050.0, 417.0);
	(void)kl_ui_pointer_button(ui, 1, test_now);
	test_frame(&view, ui, &style);
	(void)kl_ui_pointer_motion(ui, 700.0, 500.0);
	test_frame(&view, ui, &style);
	(void)kl_ui_pointer_motion(ui, TEST_GRID_X + TEST_COLUMN + 49, TEST_GRID_Y + 52 + 4 * TEST_ROW + 52);
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "memo-drag", 0);
	(void)kl_ui_pointer_button(ui, 0, test_now + 20000U);
	test_frame(&view, ui, &style);
	test_frame(&view, ui, &style);
	test_check("memo", "MEMO date=2026-10-26 length=85");
	test_now += 700000U;
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "memo", 0);

	/* The next month: November chosen, the months glide to it. */
	test_now += 1000000U;
	test_click(&view, ui, &style, 306, 42);
	test_check("next", "SELECT date=2026-11-26");
	for (i = 0; i < 40; i++) {
		/* The glide and the turn. */
		test_now += 16000U;
		test_frame(&view, ui, &style);
	}

	/* November in view. */
	(void)test_save(&view, &canvas, argv[3], "november", 0);

	/* Work hidden from the sidebar. */
	test_click(&view, ui, &style, 100, 280);
	test_check("hide", "LIST Work shown=0");
	(void)test_save(&view, &canvas, argv[3], "hidden", 0);

	/* The motion reduced: a click turns the page at once. */
	cal_view_action(&view, CAL_ACTION_MOTION, test_now);
	test_check("motion", "MOTION reduced=1");
	cal_view_action(&view, CAL_ACTION_TODAY, test_now);
	for (i = 0; i < 40; i++) {
		/* The months move at once too. */
		test_now += 16000U;
		test_frame(&view, ui, &style);
	}

	/* The page turned at once. */
	test_check("reduced", "reduced=1");
	(void)test_save(&view, &canvas, argv[3], "reduced", 0);

	/* The week and the day of the day chosen, then the months again. */
	test_now += 1000000U;
	test_click(&view, ui, &style, 596, 30);
	test_check("week", "MODE Week");
	(void)test_save(&view, &canvas, argv[3], "week", 0);

	/* ws155-p006: in the week, a click on an item's row opens it, not a new event; Cancel closes it. */
	test_click(&view, ui, &style, TEST_LIST_X + TEST_WEEK_COLUMN + 50, TEST_LIST_Y + 55);
	test_has("week-row", "EDIT open index=");
	test_absent("week-row-only", "BLANK");

	/* The frame the window draws again for the editor opened, then Cancel. */
	test_frame(&view, ui, &style);
	test_click(&view, ui, &style, TEST_CANCEL_X, TEST_SAVE_Y);
	test_check("week-row-cancel", "EDIT cancel");

	/* Under Wednesday the 7th, which has no item: a new event of the 7th, of the first calendar shown (Work is hidden); Cancel closes it. */
	test_click(&view, ui, &style, TEST_LIST_X + 3 * TEST_WEEK_COLUMN + 50, TEST_LIST_Y + 300);
	test_has("blank-week", "BLANK date=2026-10-07 mode=Week");
	test_check("blank-week-new", "EDIT new list=Personal");

	/* The frame the window draws again for the editor opened, then Cancel. */
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "week-new", 0);
	test_click(&view, ui, &style, TEST_CANCEL_X, TEST_SAVE_Y);
	test_check("blank-week-cancel", "EDIT cancel");

	/* The day. */
	test_now += 1000000U;
	test_click(&view, ui, &style, 657, 30);
	test_check("day", "MODE Day");
	(void)test_save(&view, &canvas, argv[3], "day", 0);

	/* ws155-p006: anywhere under the day's name (the 7th chosen, with no item): a new event of the 7th; Cancel closes it. */
	test_click(&view, ui, &style, 600, TEST_LIST_Y + 400);
	test_has("blank-day", "BLANK date=2026-10-07 mode=Day");
	test_check("blank-day-new", "EDIT new list=Personal");

	/* The frame the window draws again for the editor opened, then Cancel. */
	test_frame(&view, ui, &style);
	test_click(&view, ui, &style, TEST_CANCEL_X, TEST_SAVE_Y);
	test_check("blank-day-cancel", "EDIT cancel");
	test_now += 1000000U;
	test_click(&view, ui, &style, 533, 30);
	test_check("month", "MODE Month");

	/* A shorter window (the desktop's 800 less the title bar): the memo shortens so that the day chosen still shows. */
	kl_ui_begin(ui, test_now);
	cal_view_draw(&view, ui, &short_style, TEST_WIDTH, TEST_SHORT_HEIGHT, test_now);
	(void)kl_ui_end(ui, test_now);
	(void)test_save(&view, &short_canvas, argv[3], "short", 0);

	/* On glass: the cards with the desktop between. */
	cal_view_action(&view, CAL_ACTION_MOTION, test_now);
	view.glass = 1;
	style.glass = 1;
	test_frame(&view, ui, &style);
	(void)test_save(&view, &canvas, argv[3], "glass", 1);

	/* Everything goes. */
	cal_view_release(&view);
	kl_ui_destroy(ui);
	kl_canvas_release(&canvas);
	kl_canvas_release(&short_canvas);
	free(pixels);
	free(copy);
	kl_text_close(&text);

	/* Reports whether every check passed. */
	if (test_failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Keeps a log line of the view for the checks.
 */
void
cal_log(
	const char *format,
	...)
{
	va_list arguments;
	int length;

	/* The line, after the others. */
	va_start(arguments, format);
	length = vsnprintf(test_log + test_log_length, sizeof(test_log) - test_log_length - 1U, format, arguments);
	va_end(arguments);
	if (length < 0 || (size_t)length >= sizeof(test_log) - test_log_length - 1U)
		return;

	/* The line's end. */
	test_log_length += (size_t)length;
	test_log[test_log_length] = '\n';
	test_log_length++;
	test_log[test_log_length] = '\0';
}

/*
 * Draws one frame of the view as the window does, and gives the keys no
 * widget took to the view.
 */
static void
test_frame(
	struct cal_view *view,
	struct kl_ui *ui,
	const struct kl_style *style)
{
	struct kl_event event;
	int taken;

	/* The frame. */
	kl_ui_begin(ui, test_now);
	cal_view_draw(view, ui, style, test_width, TEST_HEIGHT, test_now);
	(void)kl_ui_end(ui, test_now);

	/* The keys no widget took. */
	for (;;) {
		taken = kl_ui_take(ui, &event);
		if (!taken)
			break;

		/* A key is the view's. */
		if (event.kind == KL_EVENT_KEY)
			cal_view_key(view, event.code, event.modifiers, test_now);
	}
}

/*
 * Clicks at a point: the pointer goes there, presses and releases, and a
 * frame takes the click.
 */
static void
test_click(
	struct cal_view *view,
	struct kl_ui *ui,
	const struct kl_style *style,
	int x,
	int y)
{
	/* A second after the last, so that it is a single click. */
	test_now += 1000000U;
	(void)kl_ui_pointer_motion(ui, (double)x, (double)y);
	(void)kl_ui_pointer_button(ui, 1, test_now);
	(void)kl_ui_pointer_button(ui, 0, test_now + 50000U);
	test_frame(view, ui, style);
}

/*
 * Clicks twice at a point within a double click's time (with a finger:
 * taps twice), a frame after each, and the frames a tap waits for.
 */
static void
test_double(
	struct cal_view *view,
	struct kl_ui *ui,
	const struct kl_style *style,
	int x,
	int y,
	int finger)
{
	int i;

	/* Two presses and releases 150 ms apart. */
	test_now += 1000000U;
	for (i = 0; i < 2; i++) {
		if (finger) {
			(void)kl_ui_touch_down(ui, 7, test_now, test_now, (double)x, (double)y);
			(void)kl_ui_touch_up(ui, 7, test_now + 50000U, test_now + 50000U);
		} else {
			(void)kl_ui_pointer_motion(ui, (double)x, (double)y);
			(void)kl_ui_pointer_button(ui, 1, test_now);
			(void)kl_ui_pointer_button(ui, 0, test_now + 50000U);
		}

		/* The frame after it. */
		test_now += 150000U;
		test_frame(view, ui, style);
	}

	/* The frames after, as the window would draw while a tap is pending. */
	for (i = 0; i < 3; i++) {
		test_now += 200000U;
		test_frame(view, ui, style);
	}
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

/*
 * Checks that the view logged a line, and keeps the log for the next check.
 */
static void
test_has(
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
}

/*
 * Checks that the view logged no line with some words, and empties the log.
 */
static void
test_absent(
	const char *name,
	const char *unexpected)
{
	const char *found;

	/* No such line among those logged. */
	found = strstr(test_log, unexpected);
	if (found != NULL) {
		printf("FAIL %s unexpected \"%s\" in:\n%s", name, unexpected, test_log);
		test_failures++;
	} else {
		printf("PASS %s\n", name);
	}

	/* The next check reads only what comes after. */
	test_log_length = 0;
	test_log[0] = '\0';
}

/*
 * ws155-p006: in a window too narrow for the panel, clicks where a day has
 * no item: the editor opens over the months, the months under it take no
 * click, and its Save keeps the new event (on the 27th, from 9 to 10); the
 * window is wide again after.
 */
static void
test_narrow(
	struct cal_view *view,
	struct kl_ui *ui,
	const struct kl_style *style,
	const char *prefix)
{
	struct kl_canvas narrow;
	struct kl_style narrow_style;
	const char *found;
	int error;
	int x;
	int y;
	int width;
	int height;
	int scanned;

	/* The narrower canvas on the same pixels, and its style. */
	error = kl_canvas_init(&narrow, style->canvas->pixels, TEST_WIDTH, TEST_NARROW_WIDTH, TEST_HEIGHT);
	if (error != 0) {
		printf("FAIL narrow canvas error=%d\n", error);
		test_failures++;
		return;
	}

	/* Its first frame: today's cell (Monday the 5th) says where it went. */
	narrow_style = *style;
	narrow_style.canvas = &narrow;
	test_width = TEST_NARROW_WIDTH;
	test_now += 1000000U;
	test_frame(view, ui, &narrow_style);
	found = strstr(test_log, "CELL today ");
	scanned = 0;
	if (found != NULL)
		scanned = sscanf(found, "CELL today x=%d y=%d width=%d height=%d", &x, &y, &width, &height);
	if (scanned != 4) {
		printf("FAIL narrow-cell\n%s", test_log);
		test_failures++;
		test_width = TEST_WIDTH;
		kl_canvas_release(&narrow);
		return;
	}

	/* Tuesday the 27th, three weeks under the 5th, low in its cell where it has no item: the editor over the months. */
	test_log_length = 0;
	test_log[0] = '\0';
	test_click(view, ui, &narrow_style, x + width + width / 2, y + 3 * height + height - 12);
	test_has("narrow-blank", "BLANK date=2026-10-27 mode=Month");
	test_check("narrow-new", "EDIT new list=Work");
	(void)test_save(view, &narrow, prefix, "narrow-editor", 0);

	/* The 26th, left of the editor's card: the months under it take no click. */
	test_click(view, ui, &narrow_style, x + width / 2, y + 3 * height + height - 12);
	test_absent("narrow-veil", "BLANK");

	/* Save over the months keeps the new event of the 27th. */
	test_click(view, ui, &narrow_style, TEST_NARROW_SAVE_X, TEST_NARROW_SAVE_Y);
	test_check("narrow-save", "list=Work date=2026-10-27 all_day=0 start=540 end=600 error=0");

	/* The window wide again. */
	test_width = TEST_WIDTH;
	test_frame(view, ui, style);
	kl_canvas_release(&narrow);
	test_log_length = 0;
	test_log[0] = '\0';
}

/*
 * Writes a canvas as PREFIX-NAME.ppm, or on glass as PREFIX-NAME.pam with
 * its alpha and PREFIX-NAME.panels; nonzero when it cannot.
 */
static int
test_save(
	struct cal_view *view,
	const struct kl_canvas *canvas,
	const char *prefix,
	const char *name,
	int glass)
{
	struct kl_glass_panel panels[4];
	const char *extension;
	char path[512];
	FILE *file;
	uint32_t pixel;
	size_t count;
	size_t i;
	int x;
	int y;

	/* The picture's file: PAM with its alpha on glass, else PPM. */
	extension = "ppm";
	if (glass)
		extension = "pam";
	(void)snprintf(path, sizeof(path), "%s-%s.%s", prefix, name, extension);
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;

	/* The header. */
	if (glass)
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
			if (glass)
				fputc((int)((pixel >> 24) & 0xffU), file);
		}
	}

	/* The picture is written. */
	fclose(file);
	if (!glass)
		return 0;

	/* On glass, the panels' file. */
	(void)snprintf(path, sizeof(path), "%s-%s.panels", prefix, name);
	file = fopen(path, "w");
	if (file == NULL)
		return -1;

	/* A line each. */
	count = cal_view_panels(view, canvas->width, canvas->height, panels, 4U);
	for (i = 0; i < count; i++)
		fprintf(file, "%d %d %d %d %d\n", (int)panels[i].x, (int)panels[i].y, (int)panels[i].width, (int)panels[i].height, (int)panels[i].radius);

	/* Succeeded: both are written. */
	fclose(file);
	return 0;
}

/* The appearance libkeiland's theme asks for: the light one (the host test has no compositor to ask, q796). */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	(void)appearance;

	/* The light appearance. */
	return KL_APPEARANCE_LIGHT;
}
