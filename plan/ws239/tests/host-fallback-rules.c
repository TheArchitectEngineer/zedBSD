/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of sessiond's fallback rules (ws239-p001,
 * userland/desktop/sessiond/fallback-rules.c): which greeter ends are
 * failures to start (before READY whatever the status, an error or a signal
 * soon after its start; a clean end or a late one is not), and when the
 * console's login takes over (six failures, or two or more that kept the
 * screen a minute and a half).
 */

#include "userland/desktop/sessiond/fallback-rules.h"

#include <stdio.h>

/* The number of failed checks. */
static int test_failures;

static void check(int condition, const char *what);
static int exit_status(int code);
static int signal_status(int number);

/*
 * Runs the checks.
 */
int
main(void)
{
	/* Before READY: every end is a failure to start, quick or slow, clean or not. */
	check(sessiond_greeter_failed(0, exit_status(0), 0L) == 1, "a clean exit before READY failed");
	check(sessiond_greeter_failed(0, exit_status(1), 5L) == 1, "an error before READY failed");
	check(sessiond_greeter_failed(0, signal_status(11), 3L) == 1, "a crash before READY failed");
	check(sessiond_greeter_failed(0, signal_status(15), SESSIOND_GREETER_START_SECONDS) == 1, "a hang ended by sessiond failed");
	check(sessiond_greeter_failed(0, exit_status(0), 600L) == 1, "a late end before READY failed");

	/* After READY: an error or a signal soon after the start (BUG-122), nothing else. */
	check(sessiond_greeter_failed(1, exit_status(1), 2L) == 1, "an error soon after READY failed");
	check(sessiond_greeter_failed(1, signal_status(9), 19L) == 1, "a kill soon after READY failed");
	check(sessiond_greeter_failed(1, exit_status(0), 2L) == 0, "a clean quick end after READY is no failure");
	check(sessiond_greeter_failed(1, exit_status(1), SESSIOND_GREETER_QUICK_SECONDS) == 0, "an error after the quick time is no failure");
	check(sessiond_greeter_failed(1, signal_status(11), 3600L) == 0, "a crash an hour later is no failure");

	/* The count: nothing, then six in a row (BUG-122's tries). */
	check(sessiond_console_due(0U, 0LL) == 0, "no failure keeps the greeter");
	check(sessiond_console_due(0U, 999999LL) == 0, "no failure keeps the greeter, however long");
	check(sessiond_console_due(1U, 1000LL) == 0, "one quick failure tries again");
	check(sessiond_console_due(5U, 31000LL) == 0, "five quick failures try again");
	check(sessiond_console_due(SESSIOND_GREETER_FAILURES, 31000LL) == 1, "six quick failures give the console");
	check(sessiond_console_due(SESSIOND_GREETER_FAILURES + 1U, 0LL) == 1, "more than six give the console");

	/* The time: slow failures (hangs) after a second try at least. */
	check(sessiond_console_due(1U, SESSIOND_GREETER_START_SECONDS * 1000LL) == 0, "one hang tries again");
	check(sessiond_console_due(1U, SESSIOND_FAILURE_STREAK_MS * 2LL) == 0, "one failure is tried again however long it took");
	check(sessiond_console_due(2U, SESSIOND_FAILURE_STREAK_MS - 1LL) == 0, "two failures within the bound try again");
	check(sessiond_console_due(2U, SESSIOND_FAILURE_STREAK_MS) == 1, "two failures over the bound give the console");
	check(sessiond_console_due(2U, 2LL * SESSIOND_GREETER_START_SECONDS * 1000LL - 2000LL) == 1, "two hangs give the console");
	check(sessiond_console_due(5U, 30000LL + 5LL * 1000LL) == 0, "five quick crashes in half a minute try again");

	/* The outcome. */
	if (test_failures != 0) {
		printf("host-fallback-rules: %d checks FAILED\n", test_failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("host-fallback-rules: PASS\n");
	return 0;
}

/* Counts and names a check that did not hold. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that held says nothing. */
	if (condition)
		return;

	/* Names the one that did not. */
	printf("FAIL: %s\n", what);
	test_failures++;
}

/* Gives the wait status of an exit with a code (the traditional encoding the host and zedBSD share). */
static int
exit_status(
	int code)
{
	/* The code in the second byte, no signal. */
	return (code & 0xff) << 8;
}

/* Gives the wait status of an end by a signal. */
static int
signal_status(
	int number)
{
	/* The signal in the low bits, no core. */
	return number & 0x7f;
}
