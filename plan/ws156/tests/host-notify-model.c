/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws156-p002: the compositor's model of the notifications
 * (userland/desktop/wayland/notify.c) alone on the host: numbers, the
 * replacement, the words' bounds, a client's limit, the queue's size, the
 * show and the log (newest first, its size, expired), the dismissal not
 * logged, the withdrawal, the clearing.
 */

#include "userland/desktop/wayland/notify.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int passed;
static int failed;

static void check(int ok, const char *what);

/* Runs the checks. */
int
main(void)
{
	static struct kwl_notify_model model;
	static const struct kwl_notification *log[KWL_NOTIFY_LOG + 1U];
	struct kwl_notify_closed closed[KWL_NOTIFY_LOG];
	char long_body[KWL_NOTIFY_BODY_MAX + 2U];
	size_t closed_count;
	size_t count;
	uint32_t first;
	uint32_t second;
	uint32_t id;
	unsigned index;
	int error;

	/* A post and its replacement. */
	kwl_notify_model_init(&model);
	error = kwl_notify_post(&model, 5U, 9U, 0U, "App", "One", "Body", 0U, &first, closed, &closed_count);
	check(error == 0 && first == 1U && kwl_notify_waiting(&model) == 1U, "a post waits, numbered 1");
	error = kwl_notify_post(&model, 5U, 9U, first, "App", "One again", "Body", 0U, &id, closed, &closed_count);
	check(error == 0 && id == first && strcmp(kwl_notify_find(&model, first)->title, "One again") == 0 && kwl_notify_waiting(&model) == 1U,
	      "a replacement keeps the number and the place");
	error = kwl_notify_post(&model, 6U, 9U, first, "App", "Not mine", "", 0U, &id, closed, &closed_count);
	check(error == ENOENT, "another client's number is not replaced");

	/* The words' bounds. */
	memset(long_body, 'x', sizeof(long_body) - 1U);
	long_body[sizeof(long_body) - 1U] = '\0';
	error = kwl_notify_post(&model, 5U, 9U, 0U, "App", "Long", long_body, 0U, &id, closed, &closed_count);
	check(error == EINVAL, "a body past 512 bytes is refused");

	/* Shown, then logged; the next shown. */
	error = kwl_notify_post(&model, 5U, 9U, 0U, "App", "Two", "", KWL_NOTIFY_ACTION, &second, closed, &closed_count);
	check(error == 0 && second == 2U, "a second post");
	error = kwl_notify_show_next(&model, &id);
	check(error == 0 && id == first && kwl_notify_shown(&model)->id == first, "the oldest is shown");
	error = kwl_notify_show_next(&model, &id);
	check(error == EBUSY, "one at a time");
	error = kwl_notify_hide(&model, closed, &closed_count);
	count = kwl_notify_log(&model, log, KWL_NOTIFY_LOG);
	check(error == 0 && closed_count == 0U && count == 1U && log[0]->id == first && kwl_notify_shown(&model) == NULL, "hidden into the log");
	error = kwl_notify_show_next(&model, &id);
	check(error == 0 && id == second, "the next is shown");

	/* The x: gone, not logged. */
	error = kwl_notify_dismiss(&model, second, &closed[0]);
	count = kwl_notify_log(&model, log, KWL_NOTIFY_LOG);
	check(error == 0 && closed[0].reason == KWL_NOTIFY_DISMISSED && closed[0].client == 5U && closed[0].object == 9U && count == 1U && kwl_notify_shown(&model) == NULL,
	      "dismissed: closed, not logged");

	/* A withdrawal, the client's own only. */
	error = kwl_notify_withdraw(&model, 6U, first, &closed[0]);
	check(error == ENOENT, "another client cannot withdraw");
	error = kwl_notify_withdraw(&model, 5U, first, &closed[0]);
	check(error == 0 && closed[0].reason == KWL_NOTIFY_WITHDRAWN && kwl_notify_find(&model, first) == NULL, "withdrawn from the log");

	/* A client's limit (the compositor has none). */
	for (index = 0; index < KWL_NOTIFY_PER_CLIENT; index++)
		(void)kwl_notify_post(&model, 7U, 1U, 0U, "", "Many", "", 0U, &id, closed, &closed_count);
	error = kwl_notify_post(&model, 7U, 1U, 0U, "", "One too many", "", 0U, &id, closed, &closed_count);
	check(error == EBUSY, "a client's 33rd is refused");
	error = kwl_notify_post(&model, 0U, 0U, 0U, "System", "The system's", "", 0U, &id, closed, &closed_count);
	check(error == 0, "the compositor has no limit");

	/* The queue keeps 8: the rest went to the log by age. */
	count = kwl_notify_log(&model, log, KWL_NOTIFY_LOG);
	check(kwl_notify_waiting(&model) == KWL_NOTIFY_QUEUE && count == 33U - KWL_NOTIFY_QUEUE + 0U + 1U - 1U, "the queue keeps 8, the older went to the log");
	check(count >= 2U && log[0]->order > log[1]->order, "the log is newest first");

	/* The log keeps 100: the oldest expires. */
	for (index = 0; index < 120U; index++) {
		error = kwl_notify_post(&model, 0U, 0U, 0U, "System", "Flood", "", 0U, &id, closed, &closed_count);
		if (error != 0)
			break;
	}
	count = kwl_notify_log(&model, log, KWL_NOTIFY_LOG);
	check(error == 0 && count == KWL_NOTIFY_LOG, "the log keeps 100");
	check(closed_count == 1U && closed[0].reason == KWL_NOTIFY_EXPIRED, "the oldest expires");

	/* Clearing empties the log, each closed as cleared. */
	count = kwl_notify_clear(&model, closed, KWL_NOTIFY_LOG);
	check(count == KWL_NOTIFY_LOG && closed[0].reason == KWL_NOTIFY_CLEARED && kwl_notify_log(&model, log, KWL_NOTIFY_LOG) == 0U && kwl_notify_waiting(&model) == KWL_NOTIFY_QUEUE,
	      "cleared: the log empty, the queue kept");

	/* Done. */
	kwl_notify_model_free(&model);
	printf("host-notify-model: %d passed, %d failed\n", passed, failed);
	if (failed != 0)
		return 1;
	return 0;
}

/* Counts one check and prints it. */
static void
check(
	int ok,
	const char *what)
{
	/* Passed or failed. */
	if (ok) {
		passed++;
		printf("ok %s\n", what);
		return;
	}

	/* Failed. */
	failed++;
	printf("FAIL %s\n", what);
}
