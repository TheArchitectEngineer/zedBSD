/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The touch screen of the terminal (ws081-p011, plan/ws081/design.md
 * section 5): one finger (or two) scrolls the scrollback smoothly, a pixel
 * at a time, and a flick glides on (libkeiland's scroller; past the live
 * screen or the oldest line kept it stretches and springs back); a touch
 * catches it.  A tap is a click of the left button, a double tap selects a
 * word, and a long press selects the word under the finger and a drag
 * after it grows the selection by words (the pointer's selection, fed with
 * the presses and motions made here).
 *
 * Nothing here speaks Wayland or Vulkan: the window queues the wl_touch
 * events, the main loop hands them here with the screen's state each
 * round, and takes the view and the pointer's events made here.
 */

#ifndef TERMINAL_TOUCH_H
#define TERMINAL_TOUCH_H

#include <stdint.h>

#include <keiland.h>

/* The kinds of touch input the window queues. */
#define TERMINAL_TOUCH_DOWN	0U
#define TERMINAL_TOUCH_MOTION	1U
#define TERMINAL_TOUCH_UP	2U
#define TERMINAL_TOUCH_CANCEL	3U

/* The kinds of pointer event the fingers make (the values of TERMINAL_POINTER_*). */
#define TERMINAL_TOUCH_PRESS		1U
#define TERMINAL_TOUCH_RELEASE		2U
#define TERMINAL_TOUCH_POINTER_MOTION	3U

/* How many pointer events the fingers make before the main loop takes them, at most. */
#define TERMINAL_TOUCH_POINTERS	16U

/*
 * One touch input: its kind (TERMINAL_TOUCH_*), the finger (wl_touch's
 * id), where in the window (surface pixels; not for UP and CANCEL), the
 * compositor's time (milliseconds of CLOCK_MONOTONIC, the low 32 bits; not
 * for CANCEL), a down's serial, and when the window read it (microseconds
 * of the same clock, terminal_touch_clock).
 */
struct terminal_touch_event {
	unsigned type;
	int32_t id;
	float x;
	float y;
	uint32_t time;
	uint32_t serial;
	uint64_t arrival;
};

/*
 * One pointer event the fingers make: its kind (TERMINAL_TOUCH_PRESS,
 * _RELEASE or _POINTER_MOTION), where (surface pixels), its time
 * (milliseconds) and the serial of the touch that made it.
 */
struct terminal_touch_pointer {
	unsigned kind;
	int32_t x;
	int32_t y;
	uint32_t time;
	uint32_t serial;
};

/*
 * The fingers and the scrollback they move.
 *
 * The layout (terminal_touch_layout) gives the screen the view is of (a
 * token: another tab is another screen), a line's height, how many lines
 * the scrollback keeps, the grid's height and the view as the screen has
 * it (lines back from the live screen, and a pixel offset within a line,
 * content moved down by it).  The scroller holds the view's position, in
 * pixels back from the live screen, as its negative (a finger moving down
 * shows older lines).  view and offset are the view the fingers last set
 * (changed says the main loop has not taken it yet); a view set elsewhere
 * (a key, the wheel, new output keeping the view on its text, another tab)
 * is taken over and stops a glide.
 *
 * pressed says the scroller holds the fingers' touch, moving that it owns
 * the view (from a touch until the view rests), dragging that the drag
 * scrolls, selecting that a long press made the pointer's selection and the
 * first finger's motions go to it, and caught that the touch caught a
 * gliding view (it taps nothing); first_id is the first finger, last_* its
 * last place (a lift has none), serial its down's serial, base_* the drag's
 * offset when the scroller was last pressed, and repress that the view was
 * taken over under a finger and the next tick presses the scroller again.
 */
struct terminal_touch {
	struct keiland_gesture *gesture;
	struct keiland_scroller *scroller;
	unsigned followed;
	int pressed;
	int moving;
	int dragging;
	int selecting;
	int caught;
	int32_t first_id;
	double last_x;
	double last_y;
	uint32_t serial;
	double base_x;
	double base_y;
	int repress;

	/* The layout. */
	const void *screen;
	unsigned cell_height;
	unsigned history;
	double grid_height;

	/* The view the fingers last set, and whether the main loop has taken it. */
	unsigned view;
	int offset;
	int changed;

	/* The scroller's bounds as last set. */
	double bounds_top;
	double bounds_height;

	/* The pointer events made and not yet taken, oldest first. */
	struct terminal_touch_pointer pointers[TERMINAL_TOUCH_POINTERS];
	unsigned pointer_count;
};

/* The touch screen (touch.c). */
int terminal_touch_open(struct terminal_touch *touch);
void terminal_touch_close(struct terminal_touch *touch);
void terminal_touch_layout(struct terminal_touch *touch, const void *screen, unsigned cell_height, unsigned history, unsigned grid_height, unsigned view, int offset);
void terminal_touch_event(struct terminal_touch *touch, const struct terminal_touch_event *event);
int terminal_touch_tick(struct terminal_touch *touch, uint64_t now);
int terminal_touch_view(struct terminal_touch *touch, unsigned *view, int *offset);
int terminal_touch_take_pointer(struct terminal_touch *touch, struct terminal_touch_pointer *pointer);
uint64_t terminal_touch_clock(void);

#endif
