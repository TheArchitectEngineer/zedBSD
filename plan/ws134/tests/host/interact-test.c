/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws134-p004: host tests of the System Monitor's input
 * (userland/desktop/monitor/interact.c with libkeiland's gestures,
 * gesture.c and motion.c), without a window: a tap brings a plate forward
 * and one outside sends it back, a long press pins, a swipe changes the
 * range, a pinch and a two-finger tap change the view without changing the
 * range, the keys move the keyboard's plate and open and close the card,
 * and a drag on the core turns it and lets it come back.
 *
 * The clock is the test's (kl_clock_us below), and the range is recorded
 * by the sm_set_range below; the monitor's log goes to standard output and
 * the verdict to standard error.
 *
 *	plan/ws134/tests/host/run.sh
 */

#include "app.h"

#include <stdio.h>
#include <string.h>

/* The evdev codes of the keys the tests press. */
#define TEST_KEY_ESC		1U
#define TEST_KEY_TAB		15U
#define TEST_KEY_ENTER		28U
#define TEST_KEY_LEFT		105U
#define TEST_KEY_RIGHT		106U

/* The number of failed checks. */
static int failures;

/* The test's clock in microseconds: what kl_clock_us gives. */
static uint64_t test_clock_us;

/* How many times the range changed, and the last one. */
static unsigned range_changes;

/* The application under test, made again for each test. */
static struct sm_app app;

static void check(int condition, const char *what);
static void setup(void);
static void advance(uint64_t ms);
static void finger(unsigned kind, int32_t id, double x, double y);
static void key(uint32_t code, unsigned modifiers);
static void button(uint32_t code, int pressed, double x, double y);
static void pointer(double x, double y);
static void settle(void);
static void test_tap(void);
static void test_long_press(void);
static void test_swipe(void);
static void test_pinch(void);
static void test_two_finger_tap(void);
static void test_keys(void);
static void test_core(void);

/* Runs every test. */
int
main(void)
{
	/* Each part. */
	test_tap();
	test_long_press();
	test_swipe();
	test_pinch();
	test_two_finger_tap();
	test_keys();
	test_core();

	/* The verdict. */
	if (failures != 0) {
		fprintf(stderr, "monitor-interact: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check held. */
	fprintf(stderr, "monitor-interact: PASS\n");

	/* Succeeded: the run is over. */
	return 0;
}

/*
 * The test's clock, standing in for libkeiland's.
 */
uint64_t
kl_clock_us(void)
{
	/* The time the test set. */
	return test_clock_us;
}

/*
 * Records a new range, standing in for main.c's.
 */
void
sm_set_range(
	struct sm_app *application,
	unsigned range)
{
	/* The range and the count of changes. */
	application->range = range;
	range_changes++;
	printf("ZMON RANGE %u\n", range);
}

/* Counts and prints a failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that held says so. */
	if (condition) {
		fprintf(stderr, "ok: %s\n", what);
		return;
	}

	/* A failure is counted. */
	fprintf(stderr, "FAILED: %s\n", what);
	failures++;
}

/* Makes the application again: a 1280x800 window with the plates the input looks at, the 5 minutes' range. */
static void
setup(void)
{
	/* The plates of the logical layout (scene.c's sm_layout_compute at 1280x800). */
	static const struct sm_box plates[SM_PLATES] = {
		{ 24.0f, 48.0f, 233.6f, 112.0f },
		{ 273.6f, 48.0f, 233.6f, 112.0f },
		{ 523.2f, 48.0f, 233.6f, 112.0f },
		{ 772.8f, 48.0f, 233.6f, 112.0f },
		{ 1022.4f, 48.0f, 233.6f, 112.0f },
		{ 24.0f, 176.0f, 400.0f, 344.0f },
		{ 440.0f, 176.0f, 400.0f, 344.0f },
		{ 856.0f, 176.0f, 400.0f, 344.0f },
		{ 24.0f, 536.0f, 496.0f, 204.0f },
		{ 536.0f, 536.0f, 256.0f, 204.0f },
		{ 808.0f, 536.0f, 448.0f, 204.0f },
		{ 1100.0f, 572.0f, 140.0f, 152.0f },
		{ 24.0f, 752.0f, 1232.0f, 36.0f }
	};
	int error;

	/* The last test's gestures go first. */
	sm_interact_close(&app);
	memset(&app, 0, sizeof(app));
	app.range = 1;
	app.layout.width = 1280.0f;
	app.layout.height = 800.0f;
	app.layout.scale = 1.0f;
	memcpy(app.layout.plates, plates, sizeof(plates));
	app.view = app.layout;
	range_changes = 0;
	test_clock_us += 10000000U;

	/* The input. */
	error = sm_interact_open(&app);
	check(error == 0, "the gestures are made");
}

/* Moves the clock on and keeps the input's time at each 8 ms of it. */
static void
advance(
	uint64_t ms)
{
	uint64_t step;

	/* A tick every 8 ms, as frames would come. */
	for (step = 0; step < ms; step += 8U) {
		test_clock_us += 8000U;
		(void)sm_interact_tick(&app, test_clock_us / 1000U);
	}
}

/* A finger's event at the test's clock. */
static void
finger(
	unsigned kind,
	int32_t id,
	double x,
	double y)
{
	struct kl_window_event event;

	/* The event as libkeiland gives it. */
	memset(&event, 0, sizeof(event));
	event.kind = kind;
	event.id = id;
	event.x = x;
	event.y = y;
	event.time_us = test_clock_us;
	event.arrival_us = test_clock_us;
	sm_interact_event(&app, &event);
}

/* A key's press. */
static void
key(
	uint32_t code,
	unsigned modifiers)
{
	struct kl_window_event event;

	/* The press as libkeiland gives it. */
	memset(&event, 0, sizeof(event));
	event.kind = KL_WINDOW_KEY;
	event.code = code;
	event.pressed = 1;
	event.modifiers = modifiers;
	sm_interact_event(&app, &event);
}

/* A button of the pointer, pressed or released at a point. */
static void
button(
	uint32_t code,
	int pressed,
	double x,
	double y)
{
	struct kl_window_event event;

	/* The button as libkeiland gives it. */
	memset(&event, 0, sizeof(event));
	event.kind = KL_WINDOW_BUTTON;
	event.code = code;
	event.pressed = pressed;
	event.x = x;
	event.y = y;
	sm_interact_event(&app, &event);
}

/* The pointer moving to a point. */
static void
pointer(
	double x,
	double y)
{
	struct kl_window_event event;

	/* The motion as libkeiland gives it. */
	memset(&event, 0, sizeof(event));
	event.kind = KL_WINDOW_MOTION;
	event.x = x;
	event.y = y;
	sm_interact_event(&app, &event);
}

/* Lets the card and the core come to rest. */
static void
settle(void)
{
	/* Half a second of ticks is more than the card's 360 ms. */
	advance(600U);
}

/* A tap brings the CPU's plate forward; a tap on the card keeps it; one outside sends it back. */
static void
test_tap(void)
{
	/* A tap on the CPU's plate. */
	setup();
	finger(KL_WINDOW_TOUCH_DOWN, 1, 100.0, 100.0);
	advance(80U);
	finger(KL_WINDOW_TOUCH_UP, 1, 100.0, 100.0);
	settle();
	check(app.focus.plate == SM_PLATE_CPU, "tap: the CPU's card");
	check(app.focus.opening && app.focus.progress >= 1.0f, "tap: the card all the way out");

	/* A tap on the card itself keeps it. */
	finger(KL_WINDOW_TOUCH_DOWN, 2, 640.0, 400.0);
	advance(80U);
	finger(KL_WINDOW_TOUCH_UP, 2, 640.0, 400.0);
	settle();
	check(app.focus.plate == SM_PLATE_CPU && app.focus.opening, "tap on the card: it stays");

	/* A tap outside it sends it back. */
	finger(KL_WINDOW_TOUCH_DOWN, 3, 20.0, 790.0);
	advance(80U);
	finger(KL_WINDOW_TOUCH_UP, 3, 20.0, 790.0);
	settle();
	check(app.focus.plate == -1, "tap outside: the card went back");
	check(range_changes == 0U, "tap: the range is the same");
}

/* A long press brings a plate forward pinned; a tap outside does not send it back; Esc does. */
static void
test_long_press(void)
{
	/* A finger held still on the network's plate. */
	setup();
	finger(KL_WINDOW_TOUCH_DOWN, 1, 200.0, 600.0);
	advance(700U);
	finger(KL_WINDOW_TOUCH_UP, 1, 200.0, 600.0);
	settle();
	check(app.focus.plate == SM_PLATE_FLOW && app.focus.pinned, "long press: the network's card, pinned");

	/* A tap outside keeps a pinned card. */
	finger(KL_WINDOW_TOUCH_DOWN, 2, 20.0, 790.0);
	advance(80U);
	finger(KL_WINDOW_TOUCH_UP, 2, 20.0, 790.0);
	settle();
	check(app.focus.plate == SM_PLATE_FLOW && app.focus.opening, "tap outside a pinned card: it stays");

	/* Esc: the overview, pinned or not. */
	key(TEST_KEY_ESC, 0U);
	settle();
	check(app.focus.plate == -1 && !app.focus.pinned, "Esc: the pinned card went back");

	/* The right button pins too. */
	button(KL_BUTTON_RIGHT, 1, 100.0, 100.0);
	button(KL_BUTTON_RIGHT, 0, 100.0, 100.0);
	settle();
	check(app.focus.plate == SM_PLATE_CPU && app.focus.pinned, "right click: the CPU's card, pinned");
}

/* A swipe to the left across a plate: a longer range; to the right: a shorter; on the core: none. */
static void
test_swipe(void)
{
	int step;

	/* One finger 300 px to the left over 100 ms across the network's plate. */
	setup();
	finger(KL_WINDOW_TOUCH_DOWN, 1, 400.0, 620.0);
	for (step = 1; step <= 12; step++) {
		advance(8U);
		finger(KL_WINDOW_TOUCH_MOTION, 1, 400.0 - 25.0 * step, 620.0);
	}

	/* The lift ends the swipe. */
	finger(KL_WINDOW_TOUCH_UP, 1, 100.0, 620.0);
	settle();
	check(app.range == 2U && range_changes == 1U, "swipe left: the next longer range");
	check(app.focus.plate == -1, "swipe: no card");

	/* The same to the right. */
	finger(KL_WINDOW_TOUCH_DOWN, 2, 100.0, 620.0);
	for (step = 1; step <= 12; step++) {
		advance(8U);
		finger(KL_WINDOW_TOUCH_MOTION, 2, 100.0 + 25.0 * step, 620.0);
	}

	/* The lift ends the swipe. */
	finger(KL_WINDOW_TOUCH_UP, 2, 400.0, 620.0);
	settle();
	check(app.range == 1U && range_changes == 2U, "swipe right: the shorter range again");

	/* Across the core: it turns, the range stays. */
	finger(KL_WINDOW_TOUCH_DOWN, 3, 560.0, 330.0);
	for (step = 1; step <= 12; step++) {
		advance(8U);
		finger(KL_WINDOW_TOUCH_MOTION, 3, 560.0 + 15.0 * step, 330.0);
	}

	/* Turned while held, back after the lift. */
	check(app.touch.core_turn > 0.3f, "drag on the core: it turned");
	finger(KL_WINDOW_TOUCH_UP, 3, 740.0, 330.0);
	settle();
	advance(1000U);
	check(range_changes == 2U, "drag on the core: the range is the same");
	check(app.touch.core_turn == 0.0f, "drag on the core: it came back");
}

/* Two fingers apart over the state's plate: its detail; together: the overview; the range never changes. */
static void
test_pinch(void)
{
	int step;

	/* Apart from 80 px to 240 px. */
	setup();
	finger(KL_WINDOW_TOUCH_DOWN, 1, 600.0, 350.0);
	advance(16U);
	finger(KL_WINDOW_TOUCH_DOWN, 2, 680.0, 350.0);
	for (step = 1; step <= 10; step++) {
		advance(16U);
		finger(KL_WINDOW_TOUCH_MOTION, 1, 600.0 - 8.0 * step, 350.0);
		finger(KL_WINDOW_TOUCH_MOTION, 2, 680.0 + 8.0 * step, 350.0);
	}

	/* Both lift. */
	advance(16U);
	finger(KL_WINDOW_TOUCH_UP, 1, 520.0, 350.0);
	finger(KL_WINDOW_TOUCH_UP, 2, 760.0, 350.0);
	settle();
	check(app.focus.plate == SM_PLATE_STATE && app.focus.opening, "pinch apart: the state's detail");

	/* Together from 240 px to 80 px. */
	finger(KL_WINDOW_TOUCH_DOWN, 3, 520.0, 350.0);
	advance(16U);
	finger(KL_WINDOW_TOUCH_DOWN, 4, 760.0, 350.0);
	for (step = 1; step <= 10; step++) {
		advance(16U);
		finger(KL_WINDOW_TOUCH_MOTION, 3, 520.0 + 8.0 * step, 350.0);
		finger(KL_WINDOW_TOUCH_MOTION, 4, 760.0 - 8.0 * step, 350.0);
	}

	/* Both lift. */
	advance(16U);
	finger(KL_WINDOW_TOUCH_UP, 3, 600.0, 350.0);
	finger(KL_WINDOW_TOUCH_UP, 4, 680.0, 350.0);
	settle();
	check(app.focus.plate == -1, "pinch together: the overview");
	check(range_changes == 0U, "pinch: the range is the same");
}

/* Two fingers tapping together: the detail of the plate under them, then the overview. */
static void
test_two_finger_tap(void)
{
	/* Down 20 ms apart over the GPU's card, up 100 ms later. */
	setup();
	finger(KL_WINDOW_TOUCH_DOWN, 1, 1000.0, 300.0);
	advance(16U);
	finger(KL_WINDOW_TOUCH_DOWN, 2, 1100.0, 300.0);
	advance(96U);
	finger(KL_WINDOW_TOUCH_UP, 1, 1000.0, 300.0);
	finger(KL_WINDOW_TOUCH_UP, 2, 1100.0, 300.0);
	settle();
	check(app.focus.plate == SM_PLATE_GRAPHICS && app.focus.opening, "two-finger tap: the graphics' detail");

	/* Again: the overview. */
	finger(KL_WINDOW_TOUCH_DOWN, 3, 1000.0, 300.0);
	advance(16U);
	finger(KL_WINDOW_TOUCH_DOWN, 4, 1100.0, 300.0);
	advance(96U);
	finger(KL_WINDOW_TOUCH_UP, 3, 1000.0, 300.0);
	finger(KL_WINDOW_TOUCH_UP, 4, 1100.0, 300.0);
	settle();
	check(app.focus.plate == -1, "two-finger tap again: the overview");

	/* Held too long: no tap. */
	finger(KL_WINDOW_TOUCH_DOWN, 5, 1000.0, 300.0);
	advance(16U);
	finger(KL_WINDOW_TOUCH_DOWN, 6, 1100.0, 300.0);
	advance(400U);
	finger(KL_WINDOW_TOUCH_UP, 5, 1000.0, 300.0);
	finger(KL_WINDOW_TOUCH_UP, 6, 1100.0, 300.0);
	settle();
	check(app.focus.plate == -1, "two fingers held 400 ms: nothing");
	check(range_changes == 0U, "two-finger taps: the range is the same");
}

/* Tab and Shift+Tab move the keyboard's plate, Enter opens its card and closes it, Left and Right change the range. */
static void
test_keys(void)
{
	/* Tab from none: the CPU's; Shift+Tab from none: the events'. */
	setup();
	key(TEST_KEY_TAB, 0U);
	check(app.focus.keyboard == SM_PLATE_CPU, "Tab: the CPU's plate");
	app.focus.keyboard = -1;
	key(TEST_KEY_TAB, KL_MOD_SHIFT);
	check(app.focus.keyboard == SM_PLATE_EVENTS, "Shift+Tab from none: the events' plate");
	key(TEST_KEY_TAB, KL_MOD_SHIFT);
	check(app.focus.keyboard == SM_PLATE_LANES, "Shift+Tab: the disks' plate");
	key(TEST_KEY_TAB, 0U);
	key(TEST_KEY_TAB, 0U);
	check(app.focus.keyboard == SM_PLATE_CPU, "Tab past the last: the CPU's plate");

	/* Enter: the card; Enter again: back. */
	key(TEST_KEY_ENTER, 0U);
	settle();
	check(app.focus.plate == SM_PLATE_CPU && app.focus.opening, "Enter: the CPU's card");
	key(TEST_KEY_ENTER, 0U);
	settle();
	check(app.focus.plate == -1, "Enter again: back");

	/* Left and Right: the range, within its bounds. */
	key(TEST_KEY_LEFT, 0U);
	key(TEST_KEY_LEFT, 0U);
	check(app.range == 0U && range_changes == 1U, "Left twice: the shortest range, once");
	key(TEST_KEY_RIGHT, 0U);
	key(TEST_KEY_RIGHT, 0U);
	key(TEST_KEY_RIGHT, 0U);
	key(TEST_KEY_RIGHT, 0U);
	check(app.range == SM_RANGES - 1U && range_changes == 4U, "Right four times: the longest range, three changes");
}

/* The pointer: a click brings a plate forward, a drag on the core turns it and it comes back, a long press pins. */
static void
test_core(void)
{
	/* A drag on the core with the left button. */
	setup();
	button(KL_BUTTON_LEFT, 1, 600.0, 330.0);
	pointer(700.0, 330.0);
	advance(16U);
	check(app.touch.core_turn > 0.39f && app.touch.core_turn < 0.41f, "pointer drag on the core: turned 0.4");
	button(KL_BUTTON_LEFT, 0, 700.0, 330.0);
	advance(1000U);
	check(app.touch.core_turn == 0.0f, "pointer released: the core came back");
	check(app.focus.plate == -1, "a drag is no click");

	/* A click on the memory's plate. */
	button(KL_BUTTON_LEFT, 1, 600.0, 100.0);
	advance(80U);
	button(KL_BUTTON_LEFT, 0, 600.0, 100.0);
	settle();
	check(app.focus.plate == SM_PLATE_MEMORY && !app.focus.pinned, "click: the memory's card");

	/* A long press with the pointer pins it. */
	button(KL_BUTTON_LEFT, 1, 640.0, 400.0);
	advance(600U);
	button(KL_BUTTON_LEFT, 0, 640.0, 400.0);
	settle();
	check(app.focus.plate == SM_PLATE_MEMORY && app.focus.pinned, "pointer long press: pinned");
	sm_interact_close(&app);
}
