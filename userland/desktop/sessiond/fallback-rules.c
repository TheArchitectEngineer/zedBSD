/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the graphical login's failure (ws239-p001;
 * fallback-rules.h says what they are).
 *
 * A greeter (the compositor --greeter) has failed to start when it ended
 * before it said READY, whatever its status (it never took the display: it
 * exited at once, crashed during its start, or hung and was ended), or when
 * it ended with an error or a signal soon after its start (BUG-122).  After
 * SESSIOND_GREETER_FAILURES such greeters in a row, or once the failures in
 * a row have kept the screen for SESSIOND_FAILURE_STREAK_MS, sessiond gives
 * the screen back to the text console and init starts the console's getty.
 */

#include "fallback-rules.h"

#include <sys/wait.h>

/*
 * Decides whether a greeter that ended without a login failed to start:
 * ready says it said READY, status is its wait status, seconds how long it
 * ran.  Returns 1 when it failed, 0 when it ended by itself.
 */
int
sessiond_greeter_failed(
	int ready,
	int status,
	long seconds)
{
	int exited;
	int code;

	/* It never took the display: an exit, a crash or a hang during its start. */
	if (!ready)
		return 1;

	/* A later end after a while is the greeter's own (it is started again with no count). */
	if (seconds >= SESSIOND_GREETER_QUICK_SECONDS)
		return 0;

	/* Killed by a signal soon after its start. */
	exited = WIFEXITED(status);
	if (!exited)
		return 1;

	/* An error soon after its start. */
	code = WEXITSTATUS(status);
	if (code != 0)
		return 1;

	/* Succeeded: a clean end, not a failure. */
	return 0;
}

/*
 * Decides whether the console's login takes over after greeters failed in
 * a row: failures of them, the first started streak_ms ago.  Returns 1 when
 * it does, 0 when another greeter is tried.
 */
int
sessiond_console_due(
	unsigned failures,
	long long streak_ms)
{
	/* Nothing failed. */
	if (failures == 0U)
		return 0;

	/* As many tries as the GPU is given to recover (BUG-122). */
	if (failures >= SESSIOND_GREETER_FAILURES)
		return 1;

	/* Slow failures (greeters that hang) keep the screen no longer than the bound, after a second try at least. */
	if (failures >= 2U && streak_ms >= SESSIOND_FAILURE_STREAK_MS)
		return 1;

	/* Succeeded: another greeter is tried. */
	return 0;
}
