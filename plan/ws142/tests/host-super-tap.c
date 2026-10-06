/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Windows key pressed alone (ws142-p002):
 * userland/desktop/wayland/super-tap.c alone.  It checks a tap of either
 * Super, a hold too long, another key meanwhile (Tab, L, the other Super),
 * a Super pressed with Shift, Control or Alt held, a cancel (a button, a
 * wheel, a touch), a repeat, a release without a press, and a key that was
 * already down before Super.
 *
 *   sh plan/ws142/tests/run-host-super-tap.sh
 */

#include "userland/desktop/wayland/super-tap.h"

#include <stdio.h>
#include <string.h>

/* The evdev codes of Tab and L. */
#define KEY_TAB		15U
#define KEY_L		38U

static void check(int condition, const char *what);

/* The checks that failed, and those that ran. */
static int failures;
static int checks;

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

int
main(void)
{
	struct kwl_super_tap tap;
	int result;

	/* A tap of the left Super: the press arms, the release is the tap. */
	memset(&tap, 0, sizeof(tap));
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 1000U);
	check(result == 0 && tap.armed, "the press arms");
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 1150U);
	check(result == 1 && !tap.armed, "the release is a tap");

	/* The right Super too. */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_RIGHT, 1U, 0U, 2000U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_RIGHT, 0U, 0U, 2100U);
	check(result == 1, "the right Super taps");

	/* Held a second exactly is still a tap; longer is not. */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 3000U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 4000U);
	check(result == 1, "held a second is a tap");
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 5000U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 6001U);
	check(result == 0, "held longer is no tap");

	/* Super+Tab and Super+L are no taps (their releases neither). */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 7000U);
	(void)kwl_super_tap_key(&tap, KEY_TAB, 1U, 0U, 7050U);
	(void)kwl_super_tap_key(&tap, KEY_TAB, 0U, 0U, 7080U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 7100U);
	check(result == 0, "Super+Tab is no tap");
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 8000U);
	(void)kwl_super_tap_key(&tap, KEY_L, 1U, 0U, 8050U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 8100U);
	check(result == 0, "Super+L is no tap");

	/* Both Supers: no tap from either. */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 9000U);
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_RIGHT, 1U, 0U, 9010U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_RIGHT, 0U, 0U, 9050U);
	check(result == 0, "the second Super's release is no tap");
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 9060U);
	check(result == 0, "nor the first's");

	/* With Shift, Control or Alt held, Super does not arm. */
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 1U, 10000U);
	check(result == 0 && !tap.armed, "with a modifier held Super does not arm");
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 10100U);
	check(result == 0, "and its release is no tap");

	/* A button, a wheel or a touch while it is down cancels. */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 11000U);
	kwl_super_tap_cancel(&tap);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 11100U);
	check(result == 0, "a cancel takes the tap away");

	/* A repeat changes nothing. */
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 12000U);
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 2U, 0U, 12500U);
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 12600U);
	check(result == 1, "a repeat leaves the tap");

	/* A release without a press, and a release of another key, are nothing. */
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 13000U);
	check(result == 0, "a release alone is no tap");
	(void)kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 1U, 0U, 14000U);
	result = kwl_super_tap_key(&tap, KEY_TAB, 0U, 0U, 14050U);
	check(result == 0 && tap.armed, "another key's release (pressed before) leaves it armed");
	result = kwl_super_tap_key(&tap, KWL_SUPER_TAP_LEFT, 0U, 0U, 14100U);
	check(result == 1, "and the Super's release is a tap");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-super-tap: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-super-tap: %d checks passed\n", checks);
	return 0;
}
