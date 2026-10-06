/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host tests of libkeiland's scroll, input and text view touch (ws090-p003),
 * built on Linux with libkeiland's scroller, gestures and touch motion:
 * times are given, so every position is checked against the formulas.
 *
 *   host-input
 */

#include <keiui.h>
#include <keiland.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The fake text view: a monospaced grid of cells, and the text's lines. */
#define VIEW_CELL_WIDTH		10
#define VIEW_LINE_HEIGHT	20
#define VIEW_COLUMNS		80
#define VIEW_LINES		100

/* A second, in microseconds. */
#define SECOND			1000000U

/* The tests run and failed. */
static int test_count;
static int test_failed;

/* The time of the tests' clock, in microseconds. */
static uint64_t test_now;

static void check(int condition, const char *what);
static int near(double value, double expected, double within);
static size_t view_position_at(void *data, double x, double y);
static void view_caret_rect(void *data, size_t position, struct kui_rect *rect);
static void view_word_at(void *data, size_t position, size_t *start, size_t *end);
static void test_scroll(void);
static void test_scroll_touch(void);
static void test_scroll_axis(void);
unsigned kl_appearance_get(const struct kl_appearance *appearance);
static void test_pointer(void);
static void test_touch(void);
static void test_text(void);
static void frame(struct kui_ui *ui, const struct kui_rect *widget, struct kui_scroll *scroll, const struct kui_rect *region, struct kui_text_touch *text, unsigned *state);
static void finger(struct kui_ui *ui, int32_t id, double x, double y, int down);
static void swipe(struct kui_ui *ui, int32_t id, double x, double y, double dx, double dy, int steps, struct kui_scroll *scroll, const struct kui_rect *region, struct kui_text_touch *text);

/* The fake view's answers. */
static const struct kui_text_view view_answers = {
	view_position_at,
	view_caret_rect,
	view_word_at
};

/*
 * Runs the tests.
 */
int
main(void)
{
	/* Each part. */
	test_now = 10U * SECOND;
	test_scroll();
	test_scroll_touch();
	test_scroll_axis();
	test_pointer();
	test_touch();
	test_text();

	/* The outcome. */
	printf("host-input: %d/%d passed\n", test_count - test_failed, test_count);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Records one check. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted, and a failure said. */
	test_count++;
	if (condition)
		return;
	test_failed++;
	printf("FAIL: %s\n", what);
}

/* Tells whether a value is within a distance of the one expected. */
static int
near(
	double value,
	double expected,
	double within)
{
	double distance;

	/* The distance. */
	distance = fabs(value - expected);
	if (distance <= within)
		return 1;
	printf("  (%.3f, expected %.3f)\n", value, expected);
	return 0;
}

/* The fake view: the position nearest a point (cell by cell, a line of VIEW_COLUMNS characters and its newline). */
static size_t
view_position_at(
	void *data,
	double x,
	double y)
{
	long line;
	long column;

	/* The line and the cell boundary nearest. */
	(void)data;
	line = (long)floor(y / VIEW_LINE_HEIGHT);
	column = (long)floor(x / VIEW_CELL_WIDTH + 0.5);
	if (line < 0)
		line = 0;
	if (line >= VIEW_LINES)
		line = VIEW_LINES - 1;
	if (column < 0)
		column = 0;
	if (column > VIEW_COLUMNS)
		column = VIEW_COLUMNS;

	/* The position. */
	return (size_t)(line * (VIEW_COLUMNS + 1) + column);
}

/* The fake view: the caret's rectangle at a position. */
static void
view_caret_rect(
	void *data,
	size_t position,
	struct kui_rect *rect)
{
	/* The line and column of the position. */
	(void)data;
	rect->x = (int)(position % (VIEW_COLUMNS + 1U)) * VIEW_CELL_WIDTH;
	rect->y = (int)(position / (VIEW_COLUMNS + 1U)) * VIEW_LINE_HEIGHT;
	rect->width = 2;
	rect->height = VIEW_LINE_HEIGHT;
}

/* The fake view: words are runs of 5 characters and a space (columns 0-4, 6-10, ...). */
static void
view_word_at(
	void *data,
	size_t position,
	size_t *start,
	size_t *end)
{
	size_t line;
	size_t column;

	/* The word the column falls in. */
	(void)data;
	line = position / (VIEW_COLUMNS + 1U);
	column = position % (VIEW_COLUMNS + 1U);
	*start = line * (VIEW_COLUMNS + 1U) + column / 6U * 6U;
	*end = *start + 5U;
}

/* The scroll: the wheel's glide, the ends, reveal, keys, sizes and the bars. */
static void
test_scroll(void)
{
	static uint32_t pixels[200 * 200];
	struct kui_scroll scroll;
	struct kui_canvas canvas;
	struct kui_rect rect;
	int moving;
	int error;
	int drawn;
	int index;

	/* A tall content in a smaller viewport. */
	error = kui_scroll_init(&scroll, KUI_SCROLL_Y);
	check(error == 0, "scroll init");
	kui_scroll_set_size(&scroll, 400.0, 5000.0, 400.0, 300.0);
	check(near(kui_scroll_limit_y(&scroll), 4700.0, 0.0), "limit is the content less the viewport");
	check(kui_scroll_limit_x(&scroll) == 0.0, "no horizontal scroll");

	/* The wheel glides: after one time constant 1 - 1/e of the way (Text Editor's 70 ms). */
	kui_scroll_wheel(&scroll, 0.0, 100.0, test_now);
	moving = kui_scroll_step(&scroll, test_now + KUI_SCROLL_GLIDE_US);
	check(moving == 1 && near(scroll.y, 100.0 * (1.0 - exp(-1.0)), 0.01), "glide after one time constant");
	moving = kui_scroll_step(&scroll, test_now + SECOND);
	check(moving == 0 && scroll.y == 100.0 && !scroll.gliding, "glide arrives and stops");

	/* Two turns add up: the second from the first's target. */
	test_now += 2U * SECOND;
	kui_scroll_wheel(&scroll, 0.0, 100.0, test_now);
	kui_scroll_wheel(&scroll, 0.0, 100.0, test_now + 10000U);
	(void)kui_scroll_step(&scroll, test_now + SECOND);
	check(scroll.y == 300.0, "two turns glide 200");

	/* Past the end: the end. */
	kui_scroll_wheel(&scroll, 0.0, 1.0e6, test_now + SECOND);
	(void)kui_scroll_step(&scroll, test_now + 3U * SECOND);
	check(scroll.y == 4700.0, "the wheel stops at the end");
	kui_scroll_move_to(&scroll, 0.0, -50.0, 0, test_now);
	check(scroll.y == 0.0, "a move before the start is the start");

	/* Reveal: a row below the viewport glides just into view; one above glides to its top. */
	rect.x = 0;
	rect.y = 1000;
	rect.width = 10;
	rect.height = 20;
	kui_scroll_reveal(&scroll, &rect, test_now);
	(void)kui_scroll_step(&scroll, test_now + SECOND);
	check(scroll.y == 720.0, "reveal below");
	rect.y = 100;
	kui_scroll_reveal(&scroll, &rect, test_now + SECOND);
	(void)kui_scroll_step(&scroll, test_now + 3U * SECOND);
	check(scroll.y == 100.0, "reveal above");

	/* Keys: a line, a page less a line, the end and the start. */
	test_now += 4U * SECOND;
	(void)kui_scroll_key(&scroll, KUI_KEY_DOWN, 0U, 20.0, test_now);
	(void)kui_scroll_step(&scroll, test_now + SECOND);
	check(scroll.y == 120.0, "down a line");
	(void)kui_scroll_key(&scroll, KUI_KEY_PAGEDOWN, 0U, 20.0, test_now + SECOND);
	(void)kui_scroll_step(&scroll, test_now + 2U * SECOND);
	check(scroll.y == 400.0, "down a page less a line");
	(void)kui_scroll_key(&scroll, KUI_KEY_END, KUI_MOD_CTRL, 20.0, test_now + 2U * SECOND);
	(void)kui_scroll_step(&scroll, test_now + 3U * SECOND);
	check(scroll.y == 4700.0, "the end");
	index = kui_scroll_key(&scroll, 30U, 0U, 20.0, test_now);
	check(index == 0, "a letter is not a scrolling key");
	(void)kui_scroll_key(&scroll, KUI_KEY_HOME, KUI_MOD_CTRL, 20.0, test_now + 3U * SECOND);
	(void)kui_scroll_step(&scroll, test_now + 4U * SECOND);
	check(scroll.y == 0.0, "the start");

	/* A smaller content keeps the position within the new end. */
	kui_scroll_move_to(&scroll, 0.0, 4000.0, 0, test_now);
	kui_scroll_set_size(&scroll, 400.0, 500.0, 400.0, 300.0);
	check(scroll.y == 200.0, "a shorter content moves the position back");

	/* The bars show after a move and are gone after the fade. */
	kui_canvas_init(&canvas, pixels, 200, 200, 200);
	kui_scroll_set_size(&scroll, 400.0, 5000.0, 200.0, 200.0);
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, test_now + 5U * SECOND);
	rect.x = 0;
	rect.y = 0;
	rect.width = 200;
	rect.height = 200;
	moving = kui_scroll_draw_bars(&scroll, &canvas, &rect, kui_theme_default(), test_now + 5U * SECOND + 100000U);
	drawn = 0;
	for (index = 0; index < 200 * 200; index++) {
		if (pixels[index] != 0U)
			drawn++;
	}

	/* Something of the bar was drawn. */
	check(moving == 1 && drawn > 50, "the bar shows after a move");
	moving = kui_scroll_draw_bars(&scroll, &canvas, &rect, kui_theme_default(), test_now + 7U * SECOND);
	check(moving == 0, "the bar is gone after the fade");
	kui_canvas_release(&canvas);

	/* An axis that does not scroll ignores the wheel on it. */
	kui_scroll_release(&scroll);
	error = kui_scroll_init(&scroll, KUI_SCROLL_X);
	kui_scroll_set_size(&scroll, 2000.0, 1000.0, 400.0, 300.0);
	kui_scroll_wheel(&scroll, 50.0, 100.0, test_now);
	(void)kui_scroll_step(&scroll, test_now + SECOND);
	check(error == 0 && scroll.x == 50.0 && scroll.y == 0.0, "only the scroll's own axis moves");
	kui_scroll_release(&scroll);
	error = kui_scroll_init(&scroll, 0U);
	check(error != 0, "a scroll with no axis is refused");
	test_now += 10U * SECOND;
}

/* The scroll under a finger follows libkeiland's scroller exactly. */
static void
test_scroll_touch(void)
{
	struct keiland_scroller *reference;
	struct kui_scroll scroll;
	double x;
	double y;
	int moving;
	int same;
	int step;

	/* The scroll and a bare scroller with the same bounds, both at 1000. */
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 400.0, 5000.0, 400.0, 300.0);
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, test_now);
	reference = keiland_scroller_create();
	(void)keiland_scroller_set_bounds(reference, 0.0, 0.0, 0.0, 4700.0, 400.0, 300.0);
	keiland_scroller_set_position(reference, 0.0, 1000.0);

	/* A finger drags up by 200 and flings up at 2000 px/s. */
	(void)kui_scroll_press(&scroll, test_now);
	(void)keiland_scroller_press(reference, test_now);
	kui_scroll_drag(&scroll, 0.0, -200.0);
	keiland_scroller_drag(reference, 0.0, -200.0);
	(void)kui_scroll_step(&scroll, test_now + 50000U);
	check(scroll.y == 1200.0, "the content follows the finger");
	kui_scroll_fling(&scroll, 0.0, -2000.0, test_now + 60000U);
	keiland_scroller_release(reference, test_now + 60000U, 0.0, -2000.0);

	/* Frame by frame both are at the same place until they rest. */
	same = 1;
	moving = 1;
	for (step = 1; step < 400 && moving; step++) {
		moving = kui_scroll_step(&scroll, test_now + 60000U + (uint64_t)step * 16667U);
		(void)keiland_scroller_step(reference, test_now + 60000U + (uint64_t)step * 16667U, &x, &y);
		if (scroll.y != y)
			same = 0;
	}

	/* The same all the way, and at rest. */
	check(same, "the flight is the scroller's, frame by frame");
	check(scroll.y > 1300.0 && !moving && !scroll.touched, "the content flew on and rests");
	keiland_scroller_destroy(reference);
	kui_scroll_release(&scroll);
	test_now += 10U * SECOND;
}

/*
 * A touch pad's two fingers (BUG-211, BUG-218): their moves move the
 * content at once, their velocity is the track's, it flies on when they
 * lift, fingers that rested throw nothing, and a wheel still glides.
 */
static void
test_scroll_axis(void)
{
	struct kl_axis_track track;
	struct kl_window_event event;
	struct kui_scroll scroll;
	struct kui_rect widget;
	struct kui_rect region;
	struct kui_ui *ui;
	unsigned state[2];
	int taken;
	double vx;
	double vy;
	int moving;
	int step;
	int index;

	/* The track: ten moves of 30 px every 10 ms are 3000 px/s; a single move, or a rest, none. */
	kl_axis_track_reset(&track);
	kl_axis_track_add(&track, 0.0, 30.0, test_now);
	kl_axis_track_velocity(&track, test_now + 5000U, &vx, &vy);
	check(vx == 0.0 && vy == 0.0, "track: one move has no velocity");
	for (index = 1; index < 10; index++)
		kl_axis_track_add(&track, 0.0, 30.0, test_now + (uint64_t)index * 10000U);
	kl_axis_track_velocity(&track, test_now + 95000U, &vx, &vy);
	check(near(vy, 3000.0, 1.0) && vx == 0.0, "track: the velocity of the last moves");
	kl_axis_track_velocity(&track, test_now + 90000U + KL_AXIS_TRACK_REST_US, &vx, &vy);
	check(vy == 0.0, "track: fingers that rested throw nothing");

	/* The fingers scroll the content at once, as far as they moved. */
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 400.0, 5000.0, 400.0, 300.0);
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, test_now);
	for (index = 0; index < 10; index++)
		kl_scroll_axis(&scroll, 0.0, 30.0, KL_AXIS_SOURCE_FINGER, test_now + (uint64_t)index * 10000U);
	(void)kui_scroll_step(&scroll, test_now + 90000U);
	check(scroll.y == 1300.0 && scroll.axis_holding, "axis: the fingers move the content at once");

	/* They lift moving: the content flies on further down, and rests. */
	kl_scroll_axis_stop(&scroll, test_now + 95000U);
	moving = 1;
	for (step = 1; step < 400 && moving; step++)
		moving = kui_scroll_step(&scroll, test_now + 95000U + (uint64_t)step * 16667U);
	check(scroll.y > 1600.0 && !moving && !scroll.touched && !scroll.axis_holding, "axis: the content flies on when the fingers lift");
	test_now += 10U * SECOND;

	/* Fingers that rest before lifting leave the content where it is. */
	kui_scroll_move_to(&scroll, 0.0, 1000.0, 0, test_now);
	for (index = 0; index < 10; index++)
		kl_scroll_axis(&scroll, 0.0, 30.0, KL_AXIS_SOURCE_FINGER, test_now + (uint64_t)index * 10000U);
	kl_scroll_axis_stop(&scroll, test_now + 200000U);
	for (step = 1; step < 400; step++)
		(void)kui_scroll_step(&scroll, test_now + 200000U + (uint64_t)step * 16667U);
	check(scroll.y == 1300.0 && !scroll.touched, "axis: rested fingers throw nothing");
	test_now += 10U * SECOND;

	/* A wheel's axis glides as kl_scroll_wheel does. */
	kl_scroll_axis(&scroll, 0.0, 100.0, KL_AXIS_SOURCE_WHEEL, test_now);
	check(scroll.gliding && !scroll.axis_holding && scroll.to_y == 1400.0, "axis: a wheel glides");
	kui_scroll_release(&scroll);
	test_now += 10U * SECOND;

	/* Through the window's events: the fingers' moves reach the scroll under the pointer, their end lets it fly. */
	ui = kui_ui_create();
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 300.0, 3000.0, 300.0, 300.0);
	widget.x = 10;
	widget.y = 10;
	widget.width = 100;
	widget.height = 30;
	region.x = 200;
	region.y = 0;
	region.width = 300;
	region.height = 300;
	frame(ui, &widget, &scroll, &region, NULL, state);
	(void)kui_ui_pointer_motion(ui, 300.0, 100.0);
	memset(&event, 0, sizeof(event));
	event.kind = KL_WINDOW_AXIS;
	event.axis_source = KL_AXIS_SOURCE_FINGER;
	event.dy = 40.0;
	for (index = 0; index < 5; index++) {
		event.time_us = test_now + (uint64_t)index * 10000U;
		event.arrival_us = event.time_us;
		taken = kl_ui_axis(ui, &event);
	}
	(void)kui_scroll_step(&scroll, test_now + 40000U);
	check(taken == 1 && scroll.y == 200.0, "ui axis: the fingers scroll the region under the pointer");
	event.kind = KL_WINDOW_AXIS_STOP;
	event.time_us = test_now + 45000U;
	taken = kl_ui_axis(ui, &event);
	moving = kui_scroll_step(&scroll, test_now + 60000U);
	check(taken == 1 && moving && scroll.y > 200.0, "ui axis: the end lets the content fly");
	kui_scroll_release(&scroll);
	kui_ui_destroy(ui);
	test_now += 10U * SECOND;
}

/* The appearance the theme asks for: the light one (the tests draw nothing that depends on it). */
unsigned
kl_appearance_get(
	const struct kl_appearance *appearance)
{
	(void)appearance;

	/* The light appearance. */
	return KL_APPEARANCE_LIGHT;
}

/* Draws one frame of the pointer and touch tests: a widget, a scroll's region (or a text view's), and a widget over the region. */
static void
frame(
	struct kui_ui *ui,
	const struct kui_rect *widget,
	struct kui_scroll *scroll,
	const struct kui_rect *region,
	struct kui_text_touch *text,
	unsigned *state)
{
	struct kui_rect row;

	/* The frame's time. */
	kui_ui_begin(ui, test_now);

	/* The region, then a widget: the first alone, and a row in the region. */
	if (text != NULL)
		kui_ui_text_region(ui, 20U, region, scroll, text);
	else if (scroll != NULL)
		kui_ui_scroll_region(ui, 10U, region, scroll);
	state[0] = kui_ui_hit(ui, 1U, 0U, widget);
	state[1] = 0;
	if (scroll != NULL && text == NULL) {
		row.x = region->x;
		row.y = region->y + 40 - (int)scroll->y;
		row.width = region->width;
		row.height = 28;
		state[1] = kui_ui_hit(ui, 2U, 7U, &row);
	}

	/* The frame is drawn. */
	(void)kui_ui_end(ui, test_now);
}

/* The pointer: hover, click, double click, the wheel and what no part takes. */
static void
test_pointer(void)
{
	struct kui_scroll scroll;
	struct kui_event event;
	struct kui_rect widget;
	struct kui_rect region;
	struct kui_ui *ui;
	unsigned state[2];
	int redraw;
	int taken;

	/* A widget and a scroll's region beside it. */
	ui = kui_ui_create();
	check(ui != NULL, "ui create");
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 300.0, 3000.0, 300.0, 300.0);
	widget.x = 10;
	widget.y = 10;
	widget.width = 100;
	widget.height = 30;
	region.x = 200;
	region.y = 0;
	region.width = 300;
	region.height = 300;
	frame(ui, &widget, &scroll, &region, NULL, state);

	/* Hover: onto the widget draws, moving within it does not, leaving it does. */
	redraw = kui_ui_pointer_motion(ui, 20.0, 20.0);
	check(redraw == 1, "the pointer onto a widget redraws");
	redraw = kui_ui_pointer_motion(ui, 30.0, 25.0);
	check(redraw == 0, "moving within the widget does not redraw");
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_HOT) != 0U, "the widget is lit");
	redraw = kui_ui_pointer_motion(ui, 150.0, 100.0);
	check(redraw == 1, "leaving the widget redraws");

	/* A press and a release on it click it, for one frame. */
	(void)kui_ui_pointer_motion(ui, 20.0, 20.0);
	(void)kui_ui_pointer_button(ui, 1, test_now);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_ACTIVE) != 0U && (state[0] & KUI_HIT_CLICKED) == 0U, "held, not yet clicked");
	(void)kui_ui_pointer_button(ui, 0, test_now);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_CLICKED) != 0U && (state[0] & KUI_HIT_DOUBLE) == 0U, "clicked");
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_CLICKED) == 0U, "a click is seen by one frame");

	/* A second click soon after is a double click. */
	(void)kui_ui_pointer_button(ui, 1, test_now + 200000U);
	(void)kui_ui_pointer_button(ui, 0, test_now + 250000U);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_DOUBLE) != 0U, "double click");

	/* A press on the widget released elsewhere clicks nothing. */
	test_now += SECOND;
	(void)kui_ui_pointer_button(ui, 1, test_now);
	(void)kui_ui_pointer_motion(ui, 150.0, 20.0);
	(void)kui_ui_pointer_button(ui, 0, test_now);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_CLICKED) == 0U, "released elsewhere: no click");

	/* The wheel over the region glides its scroll. */
	(void)kui_ui_pointer_motion(ui, 300.0, 200.0);
	(void)kui_ui_wheel(ui, 0.0, 120.0, test_now);
	test_now += SECOND;
	frame(ui, &widget, &scroll, &region, NULL, state);
	check(scroll.y == 120.0, "the wheel over a scroll glides it");

	/* A press and the wheel over nothing are the application's. */
	(void)kui_ui_pointer_motion(ui, 150.0, 350.0);
	(void)kui_ui_wheel(ui, 0.0, 30.0, test_now);
	(void)kui_ui_pointer_button(ui, 1, test_now);
	taken = kui_ui_take(ui, &event);
	check(taken == 1 && event.kind == KUI_EVENT_WHEEL && event.dy == 30.0, "an unclaimed wheel");
	taken = kui_ui_take(ui, &event);
	check(taken == 1 && event.kind == KUI_EVENT_PRESS && event.x == 150.0 && event.region == 0U, "an unclaimed press");
	taken = kui_ui_take(ui, &event);
	check(taken == 0, "no more");

	/* A press in the scroll region on no widget names the region. */
	(void)kui_ui_pointer_button(ui, 0, test_now);
	(void)kui_ui_take(ui, &event);
	(void)kui_ui_pointer_motion(ui, 300.0, 250.0);
	(void)kui_ui_pointer_button(ui, 1, test_now);
	taken = kui_ui_take(ui, &event);
	check(taken == 1 && event.kind == KUI_EVENT_PRESS && event.region == 10U, "a press over a region names it");
	(void)kui_ui_pointer_button(ui, 0, test_now);
	(void)kui_ui_take(ui, &event);

	/* The scroll and the input go. */
	/* The scroll and the input go. */
	/* The scroll and the input go. */
	kui_scroll_release(&scroll);
	kui_ui_destroy(ui);
	test_now += 10U * SECOND;
}

/* One finger down or up at a place, at the test's time. */
static void
finger(
	struct kui_ui *ui,
	int32_t id,
	double x,
	double y,
	int down)
{
	/* Down, or up. */
	if (down)
		(void)kui_ui_touch_down(ui, id, test_now, test_now, x, y);
	else
		(void)kui_ui_touch_up(ui, id, test_now, test_now);
}

/* A finger that is down moves by dx, dy in steps of a frame (16.7 ms), a frame drawn after each. */
static void
swipe(
	struct kui_ui *ui,
	int32_t id,
	double x,
	double y,
	double dx,
	double dy,
	int steps,
	struct kui_scroll *scroll,
	const struct kui_rect *region,
	struct kui_text_touch *text)
{
	static const struct kui_rect nowhere = { -100, -100, 1, 1 };
	unsigned state[2];
	int step;

	/* Each step: the report, then a frame. */
	for (step = 1; step <= steps; step++) {
		test_now += 16667U;
		(void)kui_ui_touch_motion(ui, id, test_now, test_now, x + dx * step / steps, y + dy * step / steps);
		frame(ui, &nowhere, scroll, region, text, state);
	}
}

/* A finger: a tap on a widget, a drag and a fling of a scroll, a tap that catches it, and a drag no part takes. */
static void
test_touch(void)
{
	static const struct kui_rect nowhere = { -100, -100, 1, 1 };
	struct kui_scroll scroll;
	struct kui_event event;
	struct kui_rect widget;
	struct kui_rect region;
	struct kui_ui *ui;
	unsigned state[2];
	double before;
	double distance;
	double dx;
	double dy;
	int taken;
	int error;
	int step;

	/* A widget and a scroll's region with a row in it. */
	ui = kui_ui_create();
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 300.0, 3000.0, 300.0, 300.0);
	widget.x = 10;
	widget.y = 10;
	widget.width = 100;
	widget.height = 30;
	region.x = 200;
	region.y = 0;
	region.width = 300;
	region.height = 300;
	frame(ui, &widget, &scroll, &region, NULL, state);

	/* A tap on the widget clicks it. */
	finger(ui, 1, 30.0, 20.0, 1);
	test_now += 60000U;
	finger(ui, 1, 30.0, 20.0, 0);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[0] & KUI_HIT_CLICKED) != 0U, "a tap clicks the widget");

	/* A tap on the row in the region clicks the row. */
	test_now += SECOND;
	finger(ui, 1, 300.0, 50.0, 1);
	test_now += 60000U;
	finger(ui, 1, 300.0, 50.0, 0);
	frame(ui, &widget, &scroll, &region, NULL, state);
	check((state[1] & KUI_HIT_CLICKED) != 0U, "a tap clicks a row in a scroll");
	check(!scroll.touched && scroll.y == 0.0, "the tap leaves the scroll where it was");

	/* A drag over the row scrolls the region (the row does not take a drag), and a fast lift flies on. */
	test_now += SECOND;
	finger(ui, 1, 300.0, 250.0, 1);
	swipe(ui, 1, 300.0, 250.0, 0.0, -200.0, 8, &scroll, &region, NULL);
	before = scroll.y;
	check(before > 100.0, "a drag moves the scroll with the finger");
	finger(ui, 1, 300.0, 50.0, 0);
	for (step = 0; step < 10; step++) {
		test_now += 16667U;
		frame(ui, &nowhere, &scroll, &region, NULL, state);
	}

	/* Farther than the finger took it, and still flying. */
	check(scroll.y > before + 50.0 && scroll.touched, "the content flies on after the lift");

	/* A tap while it flies catches it and clicks nothing. */
	finger(ui, 1, 300.0, 60.0, 1);
	test_now += 60000U;
	finger(ui, 1, 300.0, 60.0, 0);
	frame(ui, &widget, &scroll, &region, NULL, state);
	before = scroll.y;
	for (step = 0; step < 10; step++) {
		test_now += 16667U;
		frame(ui, &nowhere, &scroll, &region, NULL, state);
	}

	/* Stopped where it was caught, and no click. */
	distance = fabs(scroll.y - before);
	check((state[1] & KUI_HIT_CLICKED) == 0U && distance < 1.0, "a tap stops the flight without a click");

	/* A drag over nothing is the application's, with its offset. */
	test_now += SECOND;
	for (;;) {
		taken = kui_ui_take(ui, &event);
		if (taken == 0)
			break;
	}

	/* A finger far from every part drags. */
	finger(ui, 1, 150.0, 400.0, 1);
	swipe(ui, 1, 150.0, 400.0, 60.0, 0.0, 6, &scroll, &region, NULL);
	taken = kui_ui_take(ui, &event);
	check(taken == 1 && event.kind == KUI_EVENT_DRAG_BEGIN, "an unclaimed drag begins");
	error = kui_ui_drag_offset(ui, test_now, &dx, &dy);
	check(error == 0 && dx > 40.0, "its offset");
	finger(ui, 1, 210.0, 400.0, 0);
	taken = kui_ui_take(ui, &event);
	check(taken == 1 && event.kind == KUI_EVENT_DRAG_END, "and ends");

	/* The scroll and the input go. */
	kui_scroll_release(&scroll);
	kui_ui_destroy(ui);
	test_now += 10U * SECOND;
}

/* A text view: one finger selects, two scroll, a double tap selects a word, a long press asks for the menu, the handles and the edge. */
static void
test_text(void)
{
	static const struct kui_rect nowhere = { -100, -100, 1, 1 };
	struct kui_text_touch text;
	struct kui_scroll scroll;
	struct kui_rect region;
	struct kui_ui *ui;
	unsigned changes;
	unsigned state[2];
	size_t anchor;
	double before;
	int step;

	/* A text view of 100 lines in a 300-pixel viewport at (0, 0). */
	ui = kui_ui_create();
	(void)kui_scroll_init(&scroll, KUI_SCROLL_Y);
	kui_scroll_set_size(&scroll, 800.0, (double)(VIEW_LINES * VIEW_LINE_HEIGHT), 800.0, 300.0);
	kui_text_touch_init(&text, &view_answers, NULL);
	region.x = 0;
	region.y = 0;
	region.width = 800;
	region.height = 300;
	frame(ui, &nowhere, &scroll, &region, &text, state);

	/* A tap puts the caret: line 2, column 3. */
	finger(ui, 1, 31.0, 45.0, 1);
	test_now += 60000U;
	finger(ui, 1, 31.0, 45.0, 0);
	changes = kui_text_touch_take(&text);
	check((changes & KUI_TEXT_TOUCH_SELECTION) != 0U && text.caret == 2U * 81U + 3U && text.anchor == text.caret, "a tap puts the caret");

	/* A double tap selects the word there (columns 6 to 11) with handles. */
	test_now += SECOND;
	finger(ui, 1, 72.0, 45.0, 1);
	test_now += 50000U;
	finger(ui, 1, 72.0, 45.0, 0);
	test_now += 100000U;
	finger(ui, 1, 72.0, 45.0, 1);
	test_now += 50000U;
	finger(ui, 1, 72.0, 45.0, 0);
	check(text.anchor == 2U * 81U + 6U && text.caret == 2U * 81U + 11U && text.handles, "a double tap selects a word");

	/* One finger's drag selects from where it touched, and the scroll stays. */
	test_now += SECOND;
	finger(ui, 1, 20.0, 5.0, 1);
	swipe(ui, 1, 20.0, 5.0, 80.0, 40.0, 8, &scroll, &region, &text);
	finger(ui, 1, 100.0, 45.0, 0);
	frame(ui, &nowhere, &scroll, &region, &text, state);
	check(text.anchor == 2U && text.caret == 2U * 81U + 10U, "one finger selects");
	check(scroll.y == 0.0, "one finger does not scroll");
	check(text.handles && !text.selecting, "the selection keeps its handles");

	/* Dragging the caret's handle (under the caret at column 10, line 2) 40 pixels down moves only the caret, two lines. */
	test_now += SECOND;
	anchor = text.anchor;
	finger(ui, 1, 100.0, 66.0, 1);
	swipe(ui, 1, 100.0, 66.0, 0.0, 40.0, 6, &scroll, &region, &text);
	finger(ui, 1, 100.0, 106.0, 0);
	frame(ui, &nowhere, &scroll, &region, &text, state);
	check(text.anchor == anchor && text.caret == 4U * 81U + 10U, "the caret's handle moves the caret, by the caret's line (not the knob's)");

	/* Dragging the anchor's handle moves the other end. */
	test_now += SECOND;
	finger(ui, 1, 20.0, 26.0, 1);
	swipe(ui, 1, 20.0, 26.0, 30.0, 20.0, 6, &scroll, &region, &text);
	finger(ui, 1, 50.0, 46.0, 0);
	frame(ui, &nowhere, &scroll, &region, &text, state);
	check(text.anchor == 4U * 81U + 10U && text.caret == 1U * 81U + 5U, "the anchor's handle moves that end");

	/* Two fingers scroll and leave the selection. */
	test_now += SECOND;
	anchor = text.anchor;
	(void)kui_ui_touch_down(ui, 1, test_now, test_now, 300.0, 250.0);
	(void)kui_ui_touch_down(ui, 2, test_now, test_now, 400.0, 250.0);
	for (step = 1; step <= 8; step++) {
		test_now += 16667U;
		(void)kui_ui_touch_motion(ui, 1, test_now, test_now, 300.0, 250.0 - 15.0 * step);
		(void)kui_ui_touch_motion(ui, 2, test_now, test_now, 400.0, 250.0 - 15.0 * step);
		frame(ui, &nowhere, &scroll, &region, &text, state);
	}

	/* The content moved with them; they lift. */
	before = scroll.y;
	(void)kui_ui_touch_up(ui, 1, test_now, test_now);
	(void)kui_ui_touch_up(ui, 2, test_now, test_now);
	check(before > 60.0 && text.anchor == anchor, "two fingers scroll and keep the selection");
	for (step = 0; step < 200 && scroll.touched; step++) {
		test_now += 16667U;
		frame(ui, &nowhere, &scroll, &region, &text, state);
	}

	/* At rest in the end. */
	check(!scroll.touched, "the two fingers' flight rests");

	/* A long press asks for the menu at the finger. */
	test_now += SECOND;
	(void)kui_text_touch_take(&text);
	finger(ui, 1, 200.0, 150.0, 1);
	test_now += 600000U;
	frame(ui, &nowhere, &scroll, &region, &text, state);
	changes = kui_text_touch_take(&text);
	check((changes & KUI_TEXT_TOUCH_MENU) != 0U && text.menu_x == 200.0 && text.menu_y == 150.0, "a long press asks for the menu");
	finger(ui, 1, 200.0, 150.0, 0);
	frame(ui, &nowhere, &scroll, &region, &text, state);
	check(!scroll.touched, "the long press lets the scroll go");

	/* A selecting finger held near the bottom edge scrolls the content by itself and the selection follows. */
	test_now += SECOND;
	kui_scroll_move_to(&scroll, 0.0, 0.0, 0, test_now);
	frame(ui, &nowhere, &scroll, &region, &text, state);
	finger(ui, 1, 50.0, 100.0, 1);
	swipe(ui, 1, 50.0, 100.0, 0.0, 190.0, 6, &scroll, &region, &text);
	before = scroll.y;
	for (step = 0; step < 30; step++) {
		test_now += 16667U;
		(void)kui_ui_touch_motion(ui, 1, test_now, test_now, 50.0, 290.0);
		frame(ui, &nowhere, &scroll, &region, &text, state);
	}

	/* The content moved under the still finger. */
	check(scroll.y > before + 100.0, "the edge scrolls the content");
	check(text.caret >= (size_t)((scroll.y + 280.0) / VIEW_LINE_HEIGHT) * 81U, "the selection follows the content under the finger");
	finger(ui, 1, 50.0, 290.0, 0);
	frame(ui, &nowhere, &scroll, &region, &text, state);

	/* A key's selection takes the handles away. */
	kui_text_touch_set_selection(&text, 3U, 9U);
	check(!text.handles && text.anchor == 3U && text.caret == 9U, "the view's own selection has no handles");

	/* The keys' characters. */
	check(kui_key_character(30U, 0U) == 'a' && kui_key_character(30U, KUI_MOD_SHIFT) == 'A', "keys type characters");
	check(kui_key_character(30U, KUI_MOD_CTRL) == 0U && kui_key_character(200U, 0U) == 0U, "commands and other keys type none");

	/* The scroll and the input go. */
	kui_scroll_release(&scroll);
	kui_ui_destroy(ui);
}
