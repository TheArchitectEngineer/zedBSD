/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the answers the compositor owes the seat
 * (userland/desktop/wayland/seat-answer.c): no answer before its detach is
 * acknowledged, the answers in the callbacks' order, the pending test,
 * the late acknowledgement and a full record.
 */

#include "userland/desktop/wayland/seat-answer.h"

#include <errno.h>
#include <stdio.h>

static unsigned checks_run;
static unsigned checks_passed;

static void check(int passed, const char *what);

/*
 * Runs the cases and prints the summary.
 */
int
main(
	void)
{
	struct kwl_seat_answers answers;
	uint64_t waited;
	unsigned index;
	int descriptor;
	int due;
	int error;
	int late;

	/* An answer waits for its acknowledgement. */
	kwl_seat_answers_init(&answers);
	kwl_seat_answers_asked(&answers, 1U, 0U);
	error = kwl_seat_answers_add(&answers, 1U, 5);
	check(error == 0, "an answer is owed");
	check(kwl_seat_answers_pending(&answers, 1U) == 1, "detach 1 pending before its acknowledgement");
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 0, "no let-go before the acknowledgement");
	kwl_seat_answers_heard(&answers, 1U, 10U);
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 1 && descriptor == 5, "the let-go of 5 after acknowledgement 1");
	check(kwl_seat_answers_pending(&answers, 1U) == 0, "detach 1 no longer pending");
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 0, "nothing more owed");

	/* The callbacks' order; an answer of a later detach waits for it. */
	kwl_seat_answers_asked(&answers, 2U, 20U);
	kwl_seat_answers_asked(&answers, 3U, 21U);
	(void)kwl_seat_answers_add(&answers, 2U, 7);
	(void)kwl_seat_answers_add(&answers, 2U, 8);
	(void)kwl_seat_answers_add(&answers, 3U, 9);
	kwl_seat_answers_heard(&answers, 2U, 30U);
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 1 && descriptor == 7, "7 first");
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 1 && descriptor == 8, "8 second");
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 0, "9 waits for acknowledgement 3");
	kwl_seat_answers_heard(&answers, 3U, 40U);
	due = kwl_seat_answers_next(&answers, &descriptor);
	check(due == 1 && descriptor == 9, "9 after acknowledgement 3");

	/* An older acknowledgement changes nothing. */
	kwl_seat_answers_heard(&answers, 2U, 50U);
	check(answers.acked == 3U, "the acknowledgement never goes back");

	/* Late: the wait starts at the first detach not acknowledged and again at each acknowledgement. */
	kwl_seat_answers_asked(&answers, 4U, 100U);
	late = kwl_seat_answers_late(&answers, 150U, 100U, &waited);
	check(late == 0 && waited == 50U, "50 ms is not late");
	kwl_seat_answers_asked(&answers, 5U, 180U);
	late = kwl_seat_answers_late(&answers, 201U, 100U, &waited);
	check(late == 1 && waited == 101U, "101 ms since the first detach not acknowledged is late");
	kwl_seat_answers_heard(&answers, 4U, 210U);
	late = kwl_seat_answers_late(&answers, 300U, 100U, &waited);
	check(late == 0 && waited == 90U, "the acknowledgement of 4 starts the wait for 5 again");
	kwl_seat_answers_heard(&answers, 5U, 320U);
	late = kwl_seat_answers_late(&answers, 1000U, 100U, &waited);
	check(late == 0 && waited == 0U, "nothing awaited is never late");

	/* A full record refuses (the compositor then waits itself). */
	kwl_seat_answers_init(&answers);
	error = 0;
	for (index = 0; index < KWL_SEAT_ANSWERS_MAX && error == 0; index++)
		error = kwl_seat_answers_add(&answers, 1U, (int)index);
	check(error == 0 && answers.count == KWL_SEAT_ANSWERS_MAX, "the record holds its maximum");
	error = kwl_seat_answers_add(&answers, 1U, 999);
	check(error == ENOSPC, "one more is refused with ENOSPC");

	/* The summary. */
	printf("host-seat-answer: %u/%u passed\n", checks_passed, checks_run);
	if (checks_passed != checks_run)
		return 1;
	return 0;
}

/* Counts one check and prints it. */
static void
check(
	int passed,
	const char *what)
{
	checks_run++;
	if (passed)
		checks_passed++;
	printf("%s: %s\n", passed ? "ok" : "FAIL", what);
}
