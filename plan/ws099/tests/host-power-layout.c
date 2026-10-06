/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the power dialog's layout and keys (ws099-p037,
 * BUG-235, userland/desktop/wayland/power-layout.c compiled unchanged).
 *
 * Checks: the card is in the middle of a 1280x800 output with its four
 * buttons one under the other inside it; a point on each button is that
 * choice, between them the card, beside the card outside; the keys' choice
 * steps down and up round the ends over the choices that may not be taken
 * (a session without Power Off and Restart goes between Log Out and
 * Cancel), from out of range to Cancel's neighbours; the names.
 *
 *   plan/ws099/tests/host-power-layout.sh
 */

#include "power-layout.h"

#include <stdio.h>
#include <string.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Runs every case. */
int
main(void)
{
	struct kwl_power_layout layout;
	unsigned all;
	unsigned session;
	int choice;
	int hit;
	int ordered;

	/* 1. The card in the middle, its buttons inside it, from the top. */
	kwl_power_layout(1280, 800, &layout);
	check(layout.card[0] + layout.card[2] / 2 == 640, "the card in the middle across");
	check(layout.card[1] + layout.card[3] / 2 == 400 || layout.card[1] + layout.card[3] / 2 == 399, "the card in the middle down");
	ordered = 1;
	for (choice = 0; choice < KWL_POWER_CHOICES; choice++) {
		if (layout.buttons[choice][0] < layout.card[0] || layout.buttons[choice][1] < layout.card[1])
			ordered = 0;
		if (layout.buttons[choice][0] + layout.buttons[choice][2] > layout.card[0] + layout.card[2])
			ordered = 0;
		if (layout.buttons[choice][1] + layout.buttons[choice][3] > layout.card[1] + layout.card[3])
			ordered = 0;
		if (choice > 0 && layout.buttons[choice][1] <= layout.buttons[choice - 1][1] + layout.buttons[choice - 1][3])
			ordered = 0;
	}

	/* None of them out of the card or out of order. */
	check(ordered, "the buttons inside the card, one under the other");

	/* 2. What is at a point: each button, between two, beside the card. */
	for (choice = 0; choice < KWL_POWER_CHOICES; choice++) {
		hit = kwl_power_hit(&layout, layout.buttons[choice][0] + 10, layout.buttons[choice][1] + 10);
		check(hit == choice, "a point on a button is its choice");
	}

	/* Between two buttons, and beside the card. */
	hit = kwl_power_hit(&layout, layout.card[0] + layout.card[2] / 2, layout.buttons[1][1] - 2);
	check(hit == KWL_POWER_IN_CARD, "between two buttons: the card");
	hit = kwl_power_hit(&layout, layout.card[0] - 1, 400);
	check(hit == KWL_POWER_OUTSIDE, "left of the card: outside");
	hit = kwl_power_hit(&layout, 640, layout.card[1] + layout.card[3]);
	check(hit == KWL_POWER_OUTSIDE, "just under the card: outside");

	/* 3. The keys' choice with every choice: down and up, round the ends. */
	all = KWL_POWER_BIT(KWL_POWER_POWEROFF) | KWL_POWER_BIT(KWL_POWER_RESTART) | KWL_POWER_BIT(KWL_POWER_LOGOUT) | KWL_POWER_BIT(KWL_POWER_CANCEL);
	check(kwl_power_focus_step(KWL_POWER_CANCEL, 1, all) == KWL_POWER_POWEROFF, "down from Cancel: round to Power Off");
	check(kwl_power_focus_step(KWL_POWER_POWEROFF, -1, all) == KWL_POWER_CANCEL, "up from Power Off: round to Cancel");
	check(kwl_power_focus_step(KWL_POWER_RESTART, 1, all) == KWL_POWER_LOGOUT, "down from Restart: Log Out");

	/* 4. A session without Power Off and Restart: between Log Out and Cancel only. */
	session = KWL_POWER_BIT(KWL_POWER_LOGOUT) | KWL_POWER_BIT(KWL_POWER_CANCEL);
	check(kwl_power_focus_step(KWL_POWER_CANCEL, 1, session) == KWL_POWER_LOGOUT, "session: down from Cancel past the faint ones: Log Out");
	check(kwl_power_focus_step(KWL_POWER_LOGOUT, -1, session) == KWL_POWER_CANCEL, "session: up from Log Out past the faint ones: Cancel");
	check(kwl_power_focus_step(KWL_POWER_LOGOUT, 1, session) == KWL_POWER_CANCEL, "session: down from Log Out: Cancel");
	check(kwl_power_focus_step(KWL_POWER_CANCEL, 1, 0U) == KWL_POWER_CANCEL, "nothing but Cancel: stays");
	check(kwl_power_focus_step(7, 1, session) == KWL_POWER_LOGOUT, "out of range: from Cancel");

	/* 5. The names the log uses. */
	check(strcmp(kwl_power_choice_name(KWL_POWER_POWEROFF), "poweroff") == 0, "poweroff's name");
	check(strcmp(kwl_power_choice_name(KWL_POWER_CANCEL), "cancel") == 0, "cancel's name");
	check(strcmp(kwl_power_choice_name(9), "none") == 0, "no choice's name");

	/* The result. */
	if (failures != 0) {
		printf("host-power-layout: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-power-layout: ok (%d checks)\n", checks);
	return 0;
}
