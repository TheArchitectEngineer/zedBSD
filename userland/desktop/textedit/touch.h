/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch screen of Text Editor (plan/ws092/design.md section 9): the
 * window's wl_touch events become libkeiland's gestures and scroller.
 * One finger scrolls the text and, let go fast, it glides on and slows
 * down (past either end it stretches and springs back); a touch catches
 * it.  A tap places the cursor, a double tap selects a word, and a long
 * press opens the context menu.  Over a dialog or the file chooser a tap
 * is the pointer's click.
 */

#ifndef TEXTEDIT_TOUCH_H
#define TEXTEDIT_TOUCH_H

#include "textedit.h"

#include <keiland.h>

/*
 * The kinds of touch input the window queues.
 */
enum te_touch_type {
	TE_TOUCH_DOWN = 0,
	TE_TOUCH_MOTION,
	TE_TOUCH_UP,
	TE_TOUCH_CANCEL
};

/*
 * One touch input: its kind, the finger (wl_touch's id), where in the
 * window (surface pixels; not for UP and CANCEL), the compositor's time
 * (milliseconds of CLOCK_MONOTONIC, the low 32 bits; not for CANCEL), and
 * when the window read it (microseconds of the same clock).
 */
struct te_touch_event {
	enum te_touch_type type;
	int32_t id;
	double x;
	double y;
	uint32_t time;
	uint64_t arrival;
};

/*
 * The fingers and what they are doing: the gestures, the scroller that
 * moves the view, and the state between them.
 *
 * fingers counts the fingers down.  pressed says the scroller holds a
 * touch not yet let go; moving that the scroller owns the view (from a
 * touch until the text rests), and written_x and written_y the view's
 * place it last set, so that a move made elsewhere (a key, the wheel) is
 * taken over.  dragging says the fingers drag the view, from base_x and
 * base_y (the drag's offset when the scroller was pressed).  caught says
 * the touch caught gliding text (it taps nothing).
 */
struct te_touch {
	struct keiland_gesture *gesture;
	struct keiland_scroller *scroller;
	unsigned fingers;
	int pressed;
	int moving;
	double written_x;
	double written_y;
	int dragging;
	double base_x;
	double base_y;
	int caught;
};

/* The touch screen (touch.c). */
int te_touch_open(struct te_touch *touch);
void te_touch_close(struct te_touch *touch);
void te_touch_event(struct te_touch *touch, struct te_app *app, const struct te_touch_event *event);
int te_touch_tick(struct te_touch *touch, struct te_app *app, uint64_t now);
uint64_t te_touch_clock(void);

#endif
