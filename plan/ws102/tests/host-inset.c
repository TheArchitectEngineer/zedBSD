/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of libkeiui's default for the on-screen keyboard's inset
 * (ws102-p015): the next kui_ui_end moves the frame's text view so that
 * the caret's line is in the middle of the part the keyboard leaves.
 * Built on Linux with ui.c and its neighbours (host-inset.sh).
 *
 *   host-inset
 */

#include "internal.h"

#include <stdio.h>
#include <string.h>

/* The fake text view: 20 pixel lines, 200 of them. */
#define VIEW_LINE_HEIGHT	20
#define VIEW_LINES		200

/* The tests run and failed. */
static int test_count;
static int test_failed;

static void check(int condition, const char *what);
static size_t view_position_at(void *data, double x, double y);
static void view_caret_rect(void *data, size_t position, struct kui_rect *rect);
static void view_word_at(void *data, size_t position, size_t *start, size_t *end);
static int frame(struct kui_ui *ui, const struct kui_rect *region, struct kui_scroll *scroll, struct kui_text_touch *touch, uint64_t now);

/* The fake view's answers. */
static const struct kui_text_view view_answers = {
	view_position_at,
	view_caret_rect,
	view_word_at
};

/* Runs the tests; exits 1 when one failed. */
int
main(void)
{
	struct kui_text_touch touch;
	struct kui_scroll scroll;
	struct kui_rect region;
	struct kui_ui *ui;
	int32_t caret[4];
	int moving;

	/* A window 800x700 with a text view from y 50 to 650, 200 lines of 20 pixels, scrolled to 1000. */
	ui = kui_ui_create();
	check(ui != NULL, "ui made");
	if (ui == NULL)
		return 1;
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_text_touch_init(&touch, &view_answers, NULL);
	region.x = 0;
	region.y = 50;
	region.width = 800;
	region.height = 600;
	kui_scroll_set_size(&scroll, 800.0, (double)(VIEW_LINES * VIEW_LINE_HEIGHT), 800.0, 600.0);
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, 1000U);
	(void)frame(ui, &region, &scroll, &touch, 2000U);
	check(scroll.y == 1000.0, "no inset: the scroll stays");

	/* The QWERTY row covers the bottom 300: the caret (window y 500, 20 high) goes to the middle of 50..400. */
	caret[0] = 10;
	caret[1] = 500;
	caret[2] = 2;
	caret[3] = 20;
	keiui_ui_inset_note(800U, 700U, 0, 300, KUI_KEYBOARD_INSET_BOTTOM, caret);
	moving = frame(ui, &region, &scroll, &touch, 3000U);
	printf("bottom 300: scroll %.0f (want 1285)\n", scroll.y);
	check(scroll.y == 1285.0, "bottom inset: the caret's line in the middle of the part left");
	check(moving == 1, "bottom inset: another frame is asked for");

	/* The same inset is acted on once. */
	kui_scroll_move_to(&scroll, 0.0, 900.0, 0, 4000U);
	(void)frame(ui, &region, &scroll, &touch, 5000U);
	check(scroll.y == 900.0, "an inset is acted on once");

	/* The keyboard closes (reason none): the scroll stays. */
	keiui_ui_inset_note(800U, 700U, 0, 0, KUI_KEYBOARD_INSET_NONE, caret);
	(void)frame(ui, &region, &scroll, &touch, 6000U);
	check(scroll.y == 900.0, "a closed keyboard moves nothing");

	/* Near the text's end the scroll stops at its end (3400). */
	caret[1] = 640;
	kui_scroll_move_to(&scroll, 0.0, 3350.0, 0, 7000U);
	keiui_ui_inset_note(800U, 700U, 0, 300, KUI_KEYBOARD_INSET_BOTTOM, caret);
	(void)frame(ui, &region, &scroll, &touch, 8000U);
	printf("at the end: scroll %.0f (want 3400)\n", scroll.y);
	check(scroll.y == 3400.0, "not past the text's end");

	/* Without the application's caret, the view's caret (the fingers', line 100: content y 2000..2020). */
	caret[3] = 0;
	kui_text_touch_set_selection(&touch, 100U, 100U);
	keiui_ui_inset_note(800U, 700U, 0, 300, KUI_KEYBOARD_INSET_BOTTOM, caret);
	(void)frame(ui, &region, &scroll, &touch, 9000U);
	printf("the view's caret: scroll %.0f (want 1835)\n", scroll.y);
	check(scroll.y == 1835.0, "the view's caret without the application's");

	/* The flick column (right) leaves the whole height: the caret in the middle of 50..650. */
	caret[1] = 300;
	caret[3] = 20;
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, 10000U);
	keiui_ui_inset_note(800U, 700U, 200, 0, KUI_KEYBOARD_INSET_RIGHT, caret);
	(void)frame(ui, &region, &scroll, &touch, 11000U);
	printf("right 200: scroll %.0f (want 960)\n", scroll.y);
	check(scroll.y == 960.0, "right inset: the middle of the whole view");

	/* A view that does not scroll stays. */
	kui_scroll_set_size(&scroll, 800.0, 400.0, 800.0, 600.0);
	kui_scroll_move_to(&scroll, 0.0, 0.0, 0, 12000U);
	keiui_ui_inset_note(800U, 700U, 0, 300, KUI_KEYBOARD_INSET_BOTTOM, caret);
	(void)frame(ui, &region, &scroll, &touch, 13000U);
	check(scroll.y == 0.0, "a view that does not scroll stays");

	/* The result. */
	kui_scroll_release(&scroll);
	kui_ui_destroy(ui);
	printf("host-inset: %d tests, %d failed\n", test_count, test_failed);
	if (test_failed != 0) {
		printf("host-inset: FAIL\n");
		return 1;
	}
	printf("host-inset: PASS\n");
	return 0;
}

/* Records one test's outcome. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted, and a failure named. */
	test_count++;
	if (condition)
		return;
	test_failed++;
	printf("FAILED: %s\n", what);
}

/* Draws one frame of the input: the text view, and its end. */
static int
frame(
	struct kui_ui *ui,
	const struct kui_rect *region,
	struct kui_scroll *scroll,
	struct kui_text_touch *touch,
	uint64_t now)
{
	int moving;

	/* The frame with the view. */
	kui_ui_begin(ui, now);
	kui_ui_text_region(ui, 1U, region, scroll, touch);
	moving = kui_ui_end(ui, now);
	return moving;
}

/* The fake view's position at a point: its line. */
static size_t
view_position_at(
	void *data,
	double x,
	double y)
{
	/* A line a position. */
	(void)data;
	(void)x;
	return (size_t)(y / VIEW_LINE_HEIGHT);
}

/* The fake view's caret: its line's rectangle. */
static void
view_caret_rect(
	void *data,
	size_t position,
	struct kui_rect *rect)
{
	/* At the line's left. */
	(void)data;
	rect->x = 0;
	rect->y = (int)position * VIEW_LINE_HEIGHT;
	rect->width = 2;
	rect->height = VIEW_LINE_HEIGHT;
}

/* The fake view's word: its line. */
static void
view_word_at(
	void *data,
	size_t position,
	size_t *start,
	size_t *end)
{
	/* A line a word. */
	(void)data;
	*start = position;
	*end = position + 1U;
}
