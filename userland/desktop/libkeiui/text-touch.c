/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch of a view of editable text (ws090-p003, plan/ws090/design.md
 * section 6.1; the user's decision of 2026-09-29: in a text view one
 * finger selects and two fingers scroll).
 *
 * This file keeps the selection the fingers make: a tap puts the caret, a
 * double tap selects a word, one finger's drag selects from where it
 * touched, and a drag that starts on a handle moves that end.  Which
 * finger's gesture is which is kui_ui's (ui.c); the positions are the
 * view's (its three answers).  All points here are in the view's content
 * coordinates except the context menu's place, which is the window's.
 */

#include <keiui.h>

#include <math.h>
#include <string.h>

static int touch_near_handle(const struct kui_text_touch *touch, size_t position, double x, double y);
static double touch_edge_speed(double place, double size);

/*
 * Makes a text view's touch, with the caret at position 0.
 */
void
kui_text_touch_init(
	struct kui_text_touch *touch,
	const struct kui_text_view *view,
	void *data)
{
	/* Nothing selected, no finger. */
	memset(touch, 0, sizeof(*touch));
	touch->view = view;
	touch->data = data;
}

/*
 * Takes the selection the view has now (a key or the pointer changed it):
 * the handles go, since the selection is no longer the fingers'.
 */
void
kui_text_touch_set_selection(
	struct kui_text_touch *touch,
	size_t anchor,
	size_t caret)
{
	/* The view's selection, without handles. */
	touch->anchor = anchor;
	touch->caret = caret;
	touch->handles = 0;
	touch->handle = KUI_TEXT_HANDLE_NONE;
}

/*
 * A tap at a point: the caret there, or (twice) the word there selected
 * with its handles.
 */
void
kui_text_touch_tap(
	struct kui_text_touch *touch,
	double x,
	double y,
	int twice)
{
	size_t position;
	size_t start;
	size_t end;

	/* The position under the finger. */
	position = touch->view->position_at(touch->data, x, y);
	touch->selecting = 0;
	touch->handle = KUI_TEXT_HANDLE_NONE;

	/* Once: the caret there, nothing selected. */
	if (!twice) {
		touch->anchor = position;
		touch->caret = position;
		touch->handles = 0;
		touch->changes |= KUI_TEXT_TOUCH_SELECTION;
		return;
	}

	/* Twice: the word there, its ends shown with handles. */
	start = position;
	end = position;
	touch->view->word_at(touch->data, position, &start, &end);
	touch->anchor = start;
	touch->caret = end;
	touch->handles = 0;
	if (end != start)
		touch->handles = 1;
	touch->changes |= KUI_TEXT_TOUCH_SELECTION;
}

/*
 * A long press: the view is asked for its context menu at the finger (a
 * place of the window).
 */
void
kui_text_touch_long_press(
	struct kui_text_touch *touch,
	double window_x,
	double window_y)
{
	/* The request and its place. */
	touch->menu_x = window_x;
	touch->menu_y = window_y;
	touch->changes |= KUI_TEXT_TOUCH_MENU;
}

/*
 * One finger's drag begins at a point: on a handle it moves that end,
 * anywhere else it selects from the point.
 */
void
kui_text_touch_drag_begin(
	struct kui_text_touch *touch,
	double x,
	double y)
{
	size_t other;
	int near;

	/* The finger. */
	touch->finger_x = x;
	touch->finger_y = y;
	touch->selecting = 1;
	touch->handle = KUI_TEXT_HANDLE_NONE;

	/* A handle under the finger: the caret is always the end that moves, so the anchor is the other one. */
	if (touch->handles) {
		near = touch_near_handle(touch, touch->caret, x, y);
		if (near) {
			touch->handle = KUI_TEXT_HANDLE_CARET;
			return;
		}

		/* The anchor's handle: the ends change places. */
		near = touch_near_handle(touch, touch->anchor, x, y);
		if (near) {
			other = touch->caret;
			touch->caret = touch->anchor;
			touch->anchor = other;
			touch->handle = KUI_TEXT_HANDLE_ANCHOR;
			return;
		}
	}

	/* Anywhere else: a new selection from the point. */
	touch->anchor = touch->view->position_at(touch->data, x, y);
	touch->caret = touch->anchor;
	touch->handles = 0;
	touch->changes |= KUI_TEXT_TOUCH_SELECTION;
}

/*
 * The selecting finger is at a point: the caret follows it.
 */
void
kui_text_touch_drag(
	struct kui_text_touch *touch,
	double x,
	double y)
{
	size_t position;

	/* Only while a finger selects. */
	if (!touch->selecting)
		return;

	/* The caret where the finger is. */
	touch->finger_x = x;
	touch->finger_y = y;
	position = touch->view->position_at(touch->data, x, y);
	if (position == touch->caret)
		return;
	touch->caret = position;
	touch->changes |= KUI_TEXT_TOUCH_SELECTION;
}

/*
 * The selecting finger lifts: a selection it made keeps its handles.
 */
void
kui_text_touch_drag_end(
	struct kui_text_touch *touch)
{
	/* Only a finger that selected. */
	if (!touch->selecting)
		return;

	/* The drag is over; a selection (not a caret) shows its handles. */
	touch->selecting = 0;
	touch->handle = KUI_TEXT_HANDLE_NONE;
	touch->handles = 0;
	if (touch->anchor != touch->caret)
		touch->handles = 1;
}

/*
 * Reports how fast the content should scroll by itself while a selecting
 * finger is near the viewport's edge (pixels a second on each axis, 0 away
 * from the edges).  Returns 1 when it should scroll.
 */
int
kui_text_touch_edge(
	const struct kui_text_touch *touch,
	const struct kui_scroll *scroll,
	double *vx,
	double *vy)
{
	/* Nothing while no finger selects. */
	*vx = 0.0;
	*vy = 0.0;
	if (!touch->selecting)
		return 0;

	/* Each axis the scroll moves along, by how near the finger is to its edges in the viewport. */
	if ((scroll->axes & KUI_SCROLL_X) != 0U)
		*vx = touch_edge_speed(touch->finger_x - scroll->x, scroll->viewport_width);
	if ((scroll->axes & KUI_SCROLL_Y) != 0U)
		*vy = touch_edge_speed(touch->finger_y - scroll->y, scroll->viewport_height);

	/* Away from the edges. */
	if (*vx == 0.0 && *vy == 0.0)
		return 0;

	/* Succeeded: the content should scroll. */
	return 1;
}

/*
 * Reports what the fingers changed since the last call
 * (KUI_TEXT_TOUCH_* bits), and forgets it.
 */
unsigned
kui_text_touch_take(
	struct kui_text_touch *touch)
{
	unsigned changes;

	/* The changes, then none. */
	changes = touch->changes;
	touch->changes = 0;

	/* Reports them. */
	return changes;
}

/*
 * Draws the handles of a selection the fingers made: a small round knob
 * under each end, on a stem as tall as the caret.  origin is where the
 * content's 0, 0 is in the window (the viewport's corner less the scroll).
 */
void
kui_text_touch_draw_handles(
	const struct kui_text_touch *touch,
	struct kui_canvas *canvas,
	double origin_x,
	double origin_y,
	const struct kui_theme *theme)
{
	struct kui_rect rect;
	size_t ends[2];
	float x;
	float top;
	float bottom;
	int index;

	/* No handles, nothing to draw. */
	if (!touch->handles || touch->anchor == touch->caret)
		return;

	/* Each end's stem and knob. */
	ends[0] = touch->anchor;
	ends[1] = touch->caret;
	for (index = 0; index < 2; index++) {
		touch->view->caret_rect(touch->data, ends[index], &rect);
		x = (float)(origin_x + (double)rect.x);
		top = (float)(origin_y + (double)rect.y);
		bottom = top + (float)rect.height;
		kui_canvas_line(canvas, x, top, x, bottom, 2.0f, theme->accent);
		kui_canvas_circle(canvas, x, bottom + (float)KUI_TEXT_HANDLE / 2.0f, (float)KUI_TEXT_HANDLE / 2.0f, theme->accent);
	}
}

/* Tells whether a point is within a finger's reach of an end's handle. */
static int
touch_near_handle(
	const struct kui_text_touch *touch,
	size_t position,
	double x,
	double y)
{
	struct kui_rect rect;
	double centre_x;
	double centre_y;
	double distance;

	/* The knob's centre under the end's caret. */
	touch->view->caret_rect(touch->data, position, &rect);
	centre_x = (double)rect.x;
	centre_y = (double)(rect.y + rect.height) + (double)KUI_TEXT_HANDLE / 2.0;

	/* Within the reach. */
	distance = hypot(x - centre_x, y - centre_y);
	if (distance > (double)KUI_TEXT_HANDLE_REACH / 2.0)
		return 0;

	/* Succeeded: the finger is on the handle. */
	return 1;
}

/*
 * Reports the speed of the self-scroll for a finger at a place of a
 * viewport of a size (negative toward the start), in proportion to how deep
 * it is into the edge band, and at full speed past the edge.
 */
static double
touch_edge_speed(
	double place,
	double size)
{
	double depth;

	/* Near the start: toward the start. */
	if (place < (double)KUI_TEXT_EDGE) {
		depth = ((double)KUI_TEXT_EDGE - place) / (double)KUI_TEXT_EDGE;
		if (depth > 1.0)
			depth = 1.0;
		return -KUI_TEXT_EDGE_SPEED * depth;
	}

	/* Near the end: toward the end. */
	if (place > size - (double)KUI_TEXT_EDGE) {
		depth = (place - (size - (double)KUI_TEXT_EDGE)) / (double)KUI_TEXT_EDGE;
		if (depth > 1.0)
			depth = 1.0;
		return KUI_TEXT_EDGE_SPEED * depth;
	}

	/* Away from both. */
	return 0.0;
}
