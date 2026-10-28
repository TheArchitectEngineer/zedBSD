/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws210-p014: the host test of the trace of a window's frames
 * (userland/desktop/libkeiland/ui/trace.c, KL_TRACE_FRAMES=1): the inputs
 * gathered for a frame up to the answer that lets it be drawn, the wait,
 * the parts' area, the presenter's times, the answer's compositor time
 * and the latency, a frame not shown told at once, and the oldest frame
 * told when every slot awaits its answer.  The lines go to the standard
 * error as in the library.
 *
 *     host-trace      prints "host-trace: N checks, F failures"
 */

#include "userland/desktop/libkeiland/ui/trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The checks made and those that failed. */
static unsigned long checks;
static unsigned long failures;

/* A stand-in for a frame callback (the slot's, not a Wayland object). */
static int callback_token;

int main(void);
static void check(int condition, const char *what);
static int has(const struct keiui_trace_frame *frame, const char *field);

/*
 * Runs the cases and reports the checks.
 */
int
main(void)
{
	static struct keiui_trace trace;
	struct keiui_trace_frame *frame;
	struct keiui_trace_frame *second;
	struct keiui_present_stamps stamps;
	struct kl_rect parts[2];
	unsigned index;

	/* Three motions (one merged) at 1.000 s on, the compositor's time of the last 1.010 s; told to wait at 1.012 s, to draw at 1.040 s. */
	keiui_trace_input(&trace, 1000000U, 0);
	keiui_trace_input(&trace, 1005000U, 0);
	keiui_trace_input(&trace, 1011000U, 1);
	keiui_trace_stamp(&trace, 1010000U);
	keiui_trace_answer(&trace, 16, 1012000U, 10.5, 20.5);
	keiui_trace_answer(&trace, -1, 1020000U, 11.0, 21.0);
	keiui_trace_answer(&trace, 0, 1040000U, 12.0, 22.0);

	/* A motion after the frame is begun belongs to the next frame. */
	keiui_trace_input(&trace, 1041000U, 0);

	/* The frame shown from 1.050 s in two parts, the presenter's steps 1 ms apart, the answer at 1.120 s told as 1.119 s. */
	frame = keiui_trace_open(&trace, 1050000U);
	frame->callback = &callback_token;
	memset(&stamps, 0, sizeof(stamps));
	stamps.started_us = 1050100U;
	stamps.copied_us = 1051100U;
	stamps.acquired_us = 1052100U;
	stamps.submitted_us = 1053100U;
	stamps.queued_us = 1054100U;
	stamps.finished_us = 1055100U;
	parts[0].x = 0;
	parts[0].y = 0;
	parts[0].width = 10;
	parts[0].height = 20;
	parts[1].x = 0;
	parts[1].y = 40;
	parts[1].width = 30;
	parts[1].height = 2;
	keiui_trace_shown(&trace, frame, &stamps, parts, 2U, 6400U, 0, 1056000U);
	check(frame->used == 1, "awaits its answer");
	keiui_trace_done(frame, 1120000U, 1119U);
	check(frame->used == 0, "slot free after the answer");
	check(has(frame, "frame=1 "), "numbered 1");
	check(has(frame, " in=2 merged=1 "), "two queued, one merged");
	check(has(frame, " in_at=1000000 in_last_at=1011000 in_ev_at=1010000 "), "the inputs' times");
	check(has(frame, " x=12 y=22 "), "the pointer when told to draw");
	check(has(frame, " paced_us=28000 draw_us=10000 parts=2 area=260 "), "wait, drawing and parts");
	check(has(frame, " copy_us=1000 acquire_us=1000 submit_us=1000 queue_us=1000 fence_us=1000 present_us=6000 "), "the presenter's steps");
	check(has(frame, " done_at=1120000 done_ev_at=1119000 lat_us=108000 ev_lat_us=109000 error=0"), "the answer and the latency");

	/* The next frame, never told to draw (a window not paced): the input after the last one begun, its drawing's start not known, the whole frame, not shown. */
	second = keiui_trace_open(&trace, 1200000U);
	keiui_trace_shown(&trace, second, NULL, NULL, 0U, 6400U, 11, 1201000U);
	check(second->used == 0, "a frame not shown is told at once");
	check(has(second, "frame=2 "), "numbered 2");
	check(has(second, " in=1 merged=0 in_at=1041000 "), "the input after the first frame began");
	check(has(second, " ready_at=0 paced_us=0 draw_us=0 parts=0 area=6400 "), "whole, start not known");
	check(has(second, " done_at=0 done_ev_at=0 lat_us=0 ev_lat_us=0 error=11"), "no answer");

	/* Nine frames awaiting their answers: the ninth takes the oldest one's slot, its callback left to the caller. */
	for (index = 0U; index < KEIUI_TRACE_FRAMES; index++) {
		frame = keiui_trace_open(&trace, 2000000U + index);
		frame->callback = &callback_token;
		keiui_trace_shown(&trace, frame, NULL, NULL, 0U, 1U, 0, 2000000U + index);
	}
	frame = keiui_trace_open(&trace, 3000000U);
	check(frame->number == 3U + KEIUI_TRACE_FRAMES, "the new frame's number");
	check(frame->callback == &callback_token, "the oldest one's callback left");
	check(frame->used == 1, "the slot in use");

	/* The rest told as the window closes. */
	frame->callback = NULL;
	keiui_trace_shown(&trace, frame, NULL, NULL, 0U, 1U, 0, 3000001U);
	keiui_trace_flush(&trace);
	for (index = 0U; index < KEIUI_TRACE_FRAMES; index++)
		check(trace.slots[index].used == 0, "every slot free after the flush");

	/* The result. */
	printf("host-trace: %lu checks, %lu failures\n", checks, failures);
	if (failures != 0UL)
		return 1;
	return 0;
}

/* Counts a check, telling one that failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One check more. */
	checks++;
	if (condition)
		return;

	/* A failure, told. */
	failures++;
	printf("FAIL %s\n", what);
}

/* Tells whether a frame's line holds a text. */
static int
has(
	const struct keiui_trace_frame *frame,
	const char *field)
{
	char line[1024];
	const char *found;

	/* The line, and the text in it. */
	(void)keiui_trace_line(frame, line, sizeof(line));
	found = strstr(line, field);
	if (found == NULL) {
		printf("line: %s", line);
		return 0;
	}
	return 1;
}
