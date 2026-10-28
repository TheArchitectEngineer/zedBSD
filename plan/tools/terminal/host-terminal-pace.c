/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the terminal's frame pacing (userland/desktop/terminal/pace.h):
 * the main loop's rounds are played with the pace alone, as window.c and
 * main.c use it, and the frames drawn are counted.  Motions between two
 * answers of the compositor draw one frame, a key draws at once but only
 * one frame goes ahead of the compositor, the watchdog draws for a
 * compositor that does not answer, and a frame not shown forgets its
 * callback.
 */

#include "userland/desktop/terminal/pace.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * The model of the window and the main loop: the pace, whether a frame
 * callback proxy is held (window.c's pace_callback), whether the screen
 * has a change to draw, the frames drawn, and the clock in milliseconds.
 */
struct model {
	struct terminal_pace pace;
	int callback;
	int changed;
	unsigned frames;
	uint64_t now;
};

/* The checks run and those that failed. */
static unsigned test_checks;
static unsigned test_failures;

static void test_check(int ok, const char *name);
static void model_input(struct model *model, unsigned kind);
static void model_round(struct model *model, int shown_error);
static void model_answer(struct model *model);
static void test_motions(void);
static void test_key(void);
static void test_watchdog(void);
static void test_not_shown(void);
static void test_kinds(void);

/* Runs every test; exits nonzero when one failed. */
int
main(void)
{
	/* The cases. */
	test_motions();
	test_key();
	test_watchdog();
	test_not_shown();
	test_kinds();

	/* The summary. */
	printf("terminal-pace: %u checks, %u failed\n", test_checks, test_failures);
	if (test_failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts a check, and says which failed. */
static void
test_check(
	int ok,
	const char *name)
{
	/* One more check. */
	test_checks++;
	if (ok)
		return;

	/* A failure, by name. */
	test_failures++;
	printf("FAIL %s\n", name);
}

/* Takes a window input that changes the screen, as terminal_window_dispatch and the main loop do. */
static void
model_input(
	struct model *model,
	unsigned kind)
{
	/* The pace hears of it, and the screen has something to draw. */
	terminal_pace_input(&model->pace, kind);
	model->changed = 1;
}

/* Plays the end of one main loop round: draws when a change waits and the pace allows it (main.c, window.c). */
static void
model_round(
	struct model *model,
	int shown_error)
{
	int wait;
	int asked;
	int forget;
	int stale;

	/* Nothing to draw, or the compositor has not taken the last frame. */
	if (!model->changed)
		return;
	wait = terminal_pace_wait(&model->pace, model->now);
	if (wait != 0)
		return;

	/* The callback of the frame (terminal_window_frame_ask): a stale one is given up first. */
	stale = terminal_pace_stale(&model->pace, model->now);
	if (stale) {
		model->callback = 0;
		terminal_pace_clear(&model->pace);
	}
	asked = 0;
	if (!model->callback) {
		model->callback = 1;
		terminal_pace_ask(&model->pace, model->now);
		asked = 1;
	}

	/* The frame, shown or not (terminal_window_frame_shown). */
	forget = terminal_pace_shown(&model->pace, asked, shown_error);
	if (forget) {
		model->callback = 0;
		terminal_pace_clear(&model->pace);
	}
	if (shown_error != 0)
		return;

	/* Drawn: the change is on the window. */
	model->frames++;
	model->changed = 0;
}

/* Plays the compositor's answer to the frame callback (window_paced). */
static void
model_answer(
	struct model *model)
{
	/* The callback is used up. */
	model->callback = 0;
	terminal_pace_clear(&model->pace);
}

/* Fifty motions between two answers of the compositor draw one frame, of the latest state. */
static void
test_motions(void)
{
	struct model model;
	unsigned index;
	int wait;

	/* The first frame is drawn at once. */
	memset(&model, 0, sizeof(model));
	model.now = 100U;
	model_input(&model, KL_WINDOW_MOTION);
	model_round(&model, 0);
	test_check(model.frames == 1U, "motions: first frame at once");
	test_check(model.callback, "motions: first frame asks for its callback");

	/* Fifty motions, a round each, while the compositor has not answered: none drawn. */
	for (index = 0U; index < 50U; index++) {
		model.now++;
		model_input(&model, KL_WINDOW_MOTION);
		model_round(&model, 0);
	}
	test_check(model.frames == 1U, "motions: none drawn before the answer");
	wait = terminal_pace_wait(&model.pace, model.now);
	test_check(wait > 0 && wait <= (int)TERMINAL_PACE_WATCHDOG_MS, "motions: the wait is bounded by the watchdog");

	/* The answer: one frame for them all. */
	model_answer(&model);
	model_round(&model, 0);
	test_check(model.frames == 2U, "motions: one frame after the answer");
	test_check(!model.changed, "motions: nothing left to draw");

	/* A round with nothing new draws nothing. */
	model_answer(&model);
	model_round(&model, 0);
	test_check(model.frames == 2U, "motions: no frame without a change");
}

/* A key draws at once, but only one frame goes ahead of the compositor. */
static void
test_key(void)
{
	struct model model;
	int wait;

	/* A frame shown and awaited. */
	memset(&model, 0, sizeof(model));
	model.now = 100U;
	model_input(&model, KL_WINDOW_MOTION);
	model_round(&model, 0);

	/* A key while it is awaited: drawn at once, without a callback of its own. */
	model.now += 5U;
	model_input(&model, KL_WINDOW_KEY);
	model_round(&model, 0);
	test_check(model.frames == 2U, "key: drawn at once");
	test_check(model.pace.overdue, "key: the frame went ahead of the answer");

	/* Another key: waits for the answer (one frame ahead at most). */
	model.now += 5U;
	model_input(&model, KL_WINDOW_KEY);
	model_round(&model, 0);
	test_check(model.frames == 2U, "key: a second waits for the answer");
	wait = terminal_pace_wait(&model.pace, model.now);
	test_check(wait > 0, "key: the second's wait is bounded");

	/* The answer draws it. */
	model_answer(&model);
	model_round(&model, 0);
	test_check(model.frames == 3U, "key: drawn after the answer");
	test_check(!model.pace.urgent, "key: nothing urgent left");
}

/* A compositor that does not answer is drawn for after the watchdog, with a new callback. */
static void
test_watchdog(void)
{
	struct model model;
	int stale;

	/* A frame shown and awaited. */
	memset(&model, 0, sizeof(model));
	model.now = 100U;
	model_input(&model, KL_WINDOW_MOTION);
	model_round(&model, 0);

	/* Just before the watchdog: not drawn, not stale. */
	model.now = 100U + TERMINAL_PACE_WATCHDOG_MS - 1U;
	model_input(&model, KL_WINDOW_MOTION);
	model_round(&model, 0);
	test_check(model.frames == 1U, "watchdog: waits until it");
	stale = terminal_pace_stale(&model.pace, model.now);
	test_check(!stale, "watchdog: not stale before it");

	/* At the watchdog: drawn, and its own callback asked for. */
	model.now = 100U + TERMINAL_PACE_WATCHDOG_MS;
	model_round(&model, 0);
	test_check(model.frames == 2U, "watchdog: drawn at it");
	test_check(model.pace.asked && model.pace.asked_ms == model.now, "watchdog: a new callback awaited");
	test_check(!model.pace.overdue, "watchdog: the new frame is not ahead");
}

/* A frame not shown forgets the callback asked for with it; the next is drawn at once. */
static void
test_not_shown(void)
{
	struct model model;
	int wait;

	/* A frame that fails to show. */
	memset(&model, 0, sizeof(model));
	model.now = 100U;
	model_input(&model, KL_WINDOW_MOTION);
	model_round(&model, EIO);
	test_check(model.frames == 0U, "not shown: no frame");
	test_check(!model.callback && !model.pace.asked, "not shown: its callback forgotten");

	/* The next round draws at once. */
	wait = terminal_pace_wait(&model.pace, model.now);
	test_check(wait == 0, "not shown: the next may be drawn at once");
	model_round(&model, 0);
	test_check(model.frames == 1U, "not shown: drawn the next round");
}

/* The moves and the wheel wait; a key, a button, a resize, a menu's choice do not. */
static void
test_kinds(void)
{
	/* Those that wait. */
	test_check(terminal_pace_waits(KL_WINDOW_MOTION), "kinds: motion waits");
	test_check(terminal_pace_waits(KL_WINDOW_AXIS), "kinds: wheel waits");
	test_check(terminal_pace_waits(KL_WINDOW_TOUCH_MOTION), "kinds: finger's move waits");

	/* Those that do not. */
	test_check(!terminal_pace_waits(KL_WINDOW_KEY), "kinds: key does not wait");
	test_check(!terminal_pace_waits(KL_WINDOW_BUTTON), "kinds: button does not wait");
	test_check(!terminal_pace_waits(KL_WINDOW_RESIZE), "kinds: resize does not wait");
	test_check(!terminal_pace_waits(KL_WINDOW_ACTION), "kinds: menu's choice does not wait");
}
