/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch screen of Text Editor (touch.h; PDF Viewer's touch.c is the
 * model): the first finger presses the scroller, a drag moves it with the
 * fingers resampled for each frame, and a lift lets it glide.  While the
 * scroller owns the view it places it at each tick; a move made elsewhere
 * in the meantime (a key, the wheel) is taken over rather than undone.
 */

#include "touch.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* How often the view is placed while fingers are down or the text glides, in milliseconds. */
#define TOUCH_TICK_MS		16

/* The oldest a wl_touch time may be and still be taken (older is another clock), in milliseconds. */
#define TOUCH_TIME_BEHIND	2000U

static uint64_t touch_time(const struct te_touch_event *event);
static void touch_press(struct te_touch *touch, struct te_app *app, uint64_t now);
static void touch_gestures(struct te_touch *touch, struct te_app *app, uint64_t now);
static void touch_bounds(struct te_touch *touch, struct te_app *app);
static int touch_covered(const struct te_app *app);

/*
 * Makes the gestures and the scroller.
 *
 * Returns 0, or ENOMEM.
 */
int
te_touch_open(
	struct te_touch *touch)
{
	/* Nothing held yet. */
	memset(touch, 0, sizeof(*touch));

	/* The gestures of the window. */
	touch->gesture = keiland_gesture_create();
	if (touch->gesture == NULL)
		return ENOMEM;

	/* The scroller of the view. */
	touch->scroller = keiland_scroller_create();
	if (touch->scroller == NULL) {
		keiland_gesture_destroy(touch->gesture);
		touch->gesture = NULL;
		return ENOMEM;
	}

	/* Succeeded: fingers can be taken. */
	return 0;
}

/*
 * Frees the gestures and the scroller.
 */
void
te_touch_close(
	struct te_touch *touch)
{
	/* Both, when they were made. */
	if (touch->scroller != NULL)
		keiland_scroller_destroy(touch->scroller);
	if (touch->gesture != NULL)
		keiland_gesture_destroy(touch->gesture);
	memset(touch, 0, sizeof(*touch));
}

/*
 * Takes one touch input of the window: the fingers go to the gestures,
 * and the first of them presses the scroller (unless a dialog or the
 * chooser covers the text).
 */
void
te_touch_event(
	struct te_touch *touch,
	struct te_app *app,
	const struct te_touch_event *event)
{
	uint64_t time;
	int covered;
	int error;

	/* Nothing without the gestures. */
	if (touch->gesture == NULL)
		return;

	/* The event's time. */
	time = touch_time(event);
	covered = touch_covered(app);

	/* Hands the finger to the gestures. */
	switch (event->type) {
	case TE_TOUCH_DOWN:
		/* The first finger presses the scroller, over the text. */
		if (touch->fingers == 0U && !covered)
			touch_press(touch, app, event->arrival);
		error = keiland_gesture_down(touch->gesture, event->id, time, event->arrival, event->x, event->y);
		if (error == 0)
			touch->fingers++;
		break;
	case TE_TOUCH_MOTION:
		(void)keiland_gesture_motion(touch->gesture, event->id, time, event->arrival, event->x, event->y);
		break;
	case TE_TOUCH_UP:
		error = keiland_gesture_up(touch->gesture, event->id, time);
		if (error == 0 && touch->fingers > 0U)
			touch->fingers--;
		break;
	case TE_TOUCH_CANCEL:
		keiland_gesture_cancel(touch->gesture);
		touch->fingers = 0;
		break;
	}

	/* What the fingers mean so far. */
	touch_gestures(touch, app, event->arrival);
}

/*
 * Moves time on for the fingers: finds a long press, places the view from
 * the fingers' drag or the gliding text, and notices a move made
 * elsewhere.  Reports in how many milliseconds the view should be placed
 * again (-1 when nothing moves and no finger is down).
 */
int
te_touch_tick(
	struct te_touch *touch,
	struct te_app *app,
	uint64_t now)
{
	double x;
	double y;
	double dx;
	double dy;
	int animating;
	int error;

	/* Nothing without the gestures. */
	if (touch->gesture == NULL)
		return -1;

	/* The gestures found by now (a long press among them). */
	touch_gestures(touch, app, now);

	/* Nothing to do while nothing is touched or moving. */
	if (!touch->pressed && !touch->moving && touch->fingers == 0U) {
		app->touching = 0;
		return -1;
	}

	/* The bounds as laid out now; a move made elsewhere is taken over. */
	touch_bounds(touch, app);
	if (touch->moving && (app->scroll_x != touch->written_x || app->scroll_y != touch->written_y)) {
		keiland_scroller_set_position(touch->scroller, app->scroll_x, app->scroll_y);
		touch->written_x = app->scroll_x;
		touch->written_y = app->scroll_y;
		if (touch->pressed)
			touch_press(touch, app, now);
	}

	/* A drag moves the scroller with the fingers, resampled for the frame. */
	if (touch->dragging && touch->pressed) {
		error = keiland_gesture_drag_offset(touch->gesture, now, &dx, &dy);
		if (error == 0)
			keiland_scroller_drag(touch->scroller, dx - touch->base_x, dy - touch->base_y);
	}

	/* The view where the scroller is at the frame's time. */
	animating = keiland_scroller_step(touch->scroller, now, &x, &y);
	if (touch->moving && (x != app->scroll_x || y != app->scroll_y)) {
		app->scroll_x = x;
		app->scroll_y = y;
		app->target_y = y;
		app->gliding = 0;
		app->dirty = 1;
	}
	touch->written_x = app->scroll_x;
	touch->written_y = app->scroll_y;

	/* The text rests once it stops with no finger on it. */
	if (!animating && !touch->pressed && touch->fingers == 0U && touch->moving) {
		touch->moving = 0;
		te_log("TOUCH rest y=%.1f", app->scroll_y);
		te_app_clamp(app);
	}

	/* Fingers down or gliding text want the next tick soon. */
	app->touching = touch->moving;
	if (animating || touch->fingers > 0U)
		return TOUCH_TICK_MS;

	/* Nothing moves: no tick is due. */
	return -1;
}

/*
 * Reports the monotonic clock in microseconds, the clock of the touch
 * events' arrival and of te_touch_tick.
 */
uint64_t
te_touch_clock(void)
{
	struct timespec now;

	/* The monotonic clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);

	/* Reports it in microseconds. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/*
 * Turns a wl_touch time (the compositor's milliseconds, the low 32 bits)
 * into microseconds of the full clock, by the time the event was read; a
 * time far behind or ahead of that is taken as the reading's time.
 */
static uint64_t
touch_time(
	const struct te_touch_event *event)
{
	uint64_t arrival_ms;
	uint32_t behind;

	/* How far the event's time is behind its reading, modulo 2^32 milliseconds. */
	arrival_ms = event->arrival / 1000U;
	behind = (uint32_t)arrival_ms - event->time;

	/* Another clock, or a time ahead: the reading's time. */
	if (behind > TOUCH_TIME_BEHIND)
		return event->arrival;

	/* Reports the event's time. */
	return (arrival_ms - behind) * 1000U;
}

/* Presses the scroller where the view is: it takes the view over, and a press on gliding text catches it. */
static void
touch_press(
	struct te_touch *touch,
	struct te_app *app,
	uint64_t now)
{
	double dx;
	double dy;
	int error;
	int caught;

	/* The bounds, and the view's place unless the scroller already owns it. */
	touch_bounds(touch, app);
	if (!touch->moving)
		keiland_scroller_set_position(touch->scroller, app->scroll_x, app->scroll_y);

	/* The press; a first press on gliding text catches it. */
	caught = keiland_scroller_press(touch->scroller, now);
	if (!touch->pressed)
		touch->caught = caught;

	/* The scroller owns the view from here. */
	touch->pressed = 1;
	touch->moving = 1;
	touch->written_x = app->scroll_x;
	touch->written_y = app->scroll_y;

	/* A drag already going on is measured from here. */
	touch->base_x = 0.0;
	touch->base_y = 0.0;
	error = keiland_gesture_drag_offset(touch->gesture, now, &dx, &dy);
	if (error == 0) {
		touch->base_x = dx;
		touch->base_y = dy;
	}
}

/* Takes the gestures found so far and does what each means. */
static void
touch_gestures(
	struct te_touch *touch,
	struct te_app *app,
	uint64_t now)
{
	struct keiland_gesture_event gesture;
	int found;
	int covered;

	/* Each gesture in turn. */
	for (;;) {
		found = keiland_gesture_next(touch->gesture, now, &gesture);
		if (!found)
			break;
		covered = touch_covered(app);

		/* Does what the gesture means. */
		switch (gesture.kind) {
		case KEILAND_GESTURE_TAP:
			/* A tap that caught gliding text only stops it. */
			te_log("TOUCH tap x=%.0f y=%.0f caught=%d", gesture.x, gesture.y, touch->caught);
			if (!touch->caught)
				te_app_tap(app, (int)floor(gesture.x), (int)floor(gesture.y), 1);
			break;
		case KEILAND_GESTURE_DOUBLE_TAP:
			te_app_tap(app, (int)floor(gesture.x), (int)floor(gesture.y), 2);
			break;
		case KEILAND_GESTURE_LONG_PRESS:
			/* The context menu, over the text. */
			te_log("TOUCH long-press x=%.0f y=%.0f", gesture.x, gesture.y);
			if (!covered && app->host.context_menu != NULL)
				app->host.context_menu(app->host.data, (int)floor(gesture.x), (int)floor(gesture.y));
			break;
		case KEILAND_GESTURE_DRAG_BEGIN:
			/* The fingers drag the view (over the text). */
			if (!covered && touch->pressed)
				touch->dragging = 1;
			break;
		case KEILAND_GESTURE_DRAG_END:
			/* Let go: the text glides on at the fingers' speed. */
			if (touch->dragging && touch->pressed) {
				keiland_scroller_release(touch->scroller, now, gesture.vx, gesture.vy);
				touch->pressed = 0;
			}
			touch->dragging = 0;
			break;
		case KEILAND_GESTURE_CANCEL:
			/* Taken away: no glide, text past an end springs back. */
			if (touch->pressed)
				keiland_scroller_cancel(touch->scroller, now);
			touch->pressed = 0;
			touch->dragging = 0;
			break;
		default:
			break;
		}
	}

	/* The last finger lifted without a drag: the scroller is let go still. */
	if (touch->fingers == 0U && touch->pressed) {
		keiland_scroller_release(touch->scroller, now, 0.0, 0.0);
		touch->pressed = 0;
		touch->dragging = 0;
	}
}

/* Gives the scroller the view's bounds as laid out now. */
static void
touch_bounds(
	struct te_touch *touch,
	struct te_app *app)
{
	struct te_rect text;
	double largest_x;
	double largest_y;
	int error;

	/* The offsets the view may take, and the view's size. */
	te_app_text_rect(app, &text);
	largest_x = te_app_max_scroll_x(app);
	largest_y = te_app_max_scroll_y(app);
	error = keiland_scroller_set_bounds(touch->scroller, 0.0, largest_x, 0.0, largest_y, (double)text.width, (double)text.height);
	if (error != 0)
		te_log("TOUCH bounds error=%d", error);
}

/* Tells whether a dialog or the chooser covers the text (fingers then only tap). */
static int
touch_covered(
	const struct te_app *app)
{
	/* A dialog. */
	if (app->dialog != TE_DIALOG_NONE)
		return 1;

	/* The chooser. */
	if (app->choosing)
		return 1;

	/* The text is open to the fingers. */
	return 0;
}
