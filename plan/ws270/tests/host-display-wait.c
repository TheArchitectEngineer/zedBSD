/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* ws270-p006a: host test of sessiond_display_next (userland/desktop/sessiond/fallback-rules.c). */

#include "userland/desktop/sessiond/fallback-rules.h"

#include <stdio.h>

static int failures;
static int passes;

static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}
	failures++;
	printf("FAIL %s\n", what);
}

int
main(
	void)
{
	/* No device attaching: the answer comes at once (amd64 after i915 settles, rpi4 always). */
	check(sessiond_display_next(1, 0U, 0LL, 120000LL) == SESSIOND_DISPLAY_READY, "display, none attaching: ready");
	check(sessiond_display_next(0, 0U, 0LL, 120000LL) == SESSIOND_DISPLAY_NONE, "no display, none attaching: none");
	check(sessiond_display_next(1, 0U, 200000LL, 120000LL) == SESSIOND_DISPLAY_READY, "none attaching wins over the bound");

	/* A device attaching: gpu0 alone is not enough (PowerVR's gpu1 comes later). */
	check(sessiond_display_next(1, 1U, 0LL, 120000LL) == SESSIOND_DISPLAY_WAIT, "display, one attaching: wait");
	check(sessiond_display_next(0, 1U, 0LL, 120000LL) == SESSIOND_DISPLAY_WAIT, "no display, one attaching: wait");
	check(sessiond_display_next(1, 2U, 119999LL, 120000LL) == SESSIOND_DISPLAY_WAIT, "just under the bound: wait");

	/* The bound. */
	check(sessiond_display_next(1, 1U, 120000LL, 120000LL) == SESSIOND_DISPLAY_TIMEOUT, "display at the bound: timeout");
	check(sessiond_display_next(0, 1U, 130000LL, 120000LL) == SESSIOND_DISPLAY_TIMEOUT, "no display past the bound: timeout");

	printf("%s host-display-wait PASS %d FAIL %d\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}
