/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the handwriting's recognition pool
 * (userland/desktop/wayland/keyboard-hand.c): the pool's threads, the
 * answers by generation through the wake pipe, a newer ink replacing one
 * not started, the pool's answer the same as the event loop's own, and
 * the stop.  The templates are read once however many threads need them
 * at once (run.sh counts the read's line).
 */

#include "userland/desktop/wayland/keyboard.h"
#include "userland/desktop/wayland/threads.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The answers the test's main thread was given, in order. */
static uint64_t answer_generations[64];
static struct kwl_hand_result answer_results[64];
static unsigned answer_count;
static unsigned failures;

static void answer_take(void *context, const struct kwl_hand_answer *answer);
static void ink_circle(struct kwl_hand_ink *ink, int32_t cx, int32_t cy, int32_t radius);
static void ink_line(struct kwl_hand_ink *ink, int32_t x, int32_t top, int32_t bottom);
static void check(int condition, const char *what);
static unsigned take_until(int descriptor, unsigned wanted);

int
main(void)
{
	static struct kwl_hand_ink circle;
	static struct kwl_hand_ink line;
	struct kwl_hand_result result;
	struct kwl_hand_result own;
	unsigned index;
	unsigned taken;
	int pipes[2];
	int error;

	/* The wake pipe, as the compositor makes it: neither end blocks. */
	error = pipe(pipes);
	check(error == 0, "pipe");
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(pipes[1], F_SETFL, O_NONBLOCK);

	/* Two inks: an o and an l, on an area 0..400 down. */
	kwl_hand_clear(&circle);
	ink_circle(&circle, 200, 200, 120);
	kwl_hand_clear(&line);
	ink_line(&line, 200, 40, 360);

	/* The pool starts; its threads are hand-1.. (the count depends on the processors). */
	memset(&result, 0, sizeof(result));
	kwl_hand_preload(&result, pipes[1]);
	check(kwl_threads_count() >= 1U, "the pool's threads in the register");
	check(kwl_threads_count() <= 4U, "at most four threads");
	check(result.note[0] == '\0', "no note before the read");

	/* Given at once, before the templates are read: each waits for the one read. */
	error = kwl_hand_submit(&circle, 0, 400, 1U);
	check(error == 0, "submit 1");
	taken = take_until(pipes[0], 1U);
	check(taken >= 1U, "answer 1 taken");
	printf("hand-pool: jobs taken with answer 1: %u\n", taken);
	check(answer_count == 1U && answer_generations[0] == 1U, "answer 1 is generation 1");
	check(answer_results[0].count > 0U, "answer 1 has candidates");

	/* The pool's answer is the event loop's own. */
	kwl_hand_recognize_on(&circle, 0, 400, &own);
	check(memcmp(&own, &answer_results[0], sizeof(own)) == 0, "the pool answers as the event loop does");
	printf("hand-pool: circle first=%s line ", own.candidates[0]);

	/* Many inks in a row: the latest is always answered, older ones not started are replaced. */
	for (index = 2U; index < 30U; index++) {
		if (index % 2U == 0U)
			error = kwl_hand_submit(&line, 0, 400, index);
		else
			error = kwl_hand_submit(&circle, 0, 400, index);
		check(error == 0, "submit in a row");
	}
	answer_count = 0U;
	(void)take_until(pipes[0], 0U);
	for (index = 0U; index < 200U; index++) {
		(void)take_until(pipes[0], 0U);
		if (answer_count > 0U && answer_generations[answer_count - 1U] == 29U)
			break;
		usleep(10000);
	}
	check(answer_count > 0U, "answers in a row");
	check(answer_count < 28U, "older inks replaced");
	check(answer_count > 0U && answer_generations[answer_count - 1U] == 29U, "the latest is answered last");
	kwl_hand_recognize_on(&line, 0, 400, &own);
	printf("first=%s answers=%u of 28\n", own.candidates[0], answer_count);

	/* The stop with a recognition given and not taken: freed, not answered. */
	error = kwl_hand_submit(&line, 0, 400, 99U);
	check(error == 0, "submit before the stop");
	kwl_hand_stop();
	check(kwl_threads_count() == 0U, "the pool's threads ended");
	error = kwl_hand_submit(&line, 0, 400, 100U);
	check(error == ENODEV, "no pool after the stop");

	/* The outcome. */
	close(pipes[0]);
	close(pipes[1]);
	printf("hand-pool: %s (%u failures)\n", failures == 0U ? "PASS" : "FAIL", failures);
	return failures == 0U ? 0 : 1;
}

/* Keeps an answer the pool gave. */
static void
answer_take(
	void *context,
	const struct kwl_hand_answer *answer)
{
	(void)context;
	if (answer_count >= sizeof(answer_generations) / sizeof(answer_generations[0]))
		return;
	answer_generations[answer_count] = answer->generation;
	answer_results[answer_count] = answer->result;
	answer_count++;
}

/* Writes a circle as one stroke. */
static void
ink_circle(
	struct kwl_hand_ink *ink,
	int32_t cx,
	int32_t cy,
	int32_t radius)
{
	static const int32_t steps[][2] = {
		{ 0, -100 }, { 38, -92 }, { 71, -71 }, { 92, -38 }, { 100, 0 }, { 92, 38 }, { 71, 71 }, { 38, 92 },
		{ 0, 100 }, { -38, 92 }, { -71, 71 }, { -92, 38 }, { -100, 0 }, { -92, -38 }, { -71, -71 }, { -38, -92 }, { 0, -100 }
	};
	unsigned index;

	(void)kwl_hand_begin(ink, cx + steps[0][0] * radius / 100, cy + steps[0][1] * radius / 100);
	for (index = 1U; index < sizeof(steps) / sizeof(steps[0]); index++)
		(void)kwl_hand_add(ink, cx + steps[index][0] * radius / 100, cy + steps[index][1] * radius / 100);
}

/* Writes a line down as one stroke. */
static void
ink_line(
	struct kwl_hand_ink *ink,
	int32_t x,
	int32_t top,
	int32_t bottom)
{
	int32_t y;

	(void)kwl_hand_begin(ink, x, top);
	for (y = top + 10; y <= bottom; y += 10)
		(void)kwl_hand_add(ink, x, y);
}

/* Counts a failed check, with what it was. */
static void
check(
	int condition,
	const char *what)
{
	if (condition)
		return;
	failures++;
	printf("hand-pool: FAIL %s\n", what);
}

/*
 * Waits on the wake pipe (up to 5 s) and takes the pool's jobs until at
 * least wanted answers came (the templates' job is taken too but answers
 * nothing); 0 takes what is there.  Returns the jobs taken.
 */
static unsigned
take_until(
	int descriptor,
	unsigned wanted)
{
	struct pollfd poller;
	char bytes[64];
	unsigned taken;
	unsigned waited;

	taken = 0U;
	for (waited = 0U; waited < 500U; waited++) {
		taken += kwl_hand_take(answer_take, NULL);
		if (answer_count >= wanted)
			break;
		poller.fd = descriptor;
		poller.events = POLLIN;
		poller.revents = 0;
		(void)poll(&poller, 1U, 10);
		while (read(descriptor, bytes, sizeof(bytes)) > 0)
			;
	}
	return taken;
}
