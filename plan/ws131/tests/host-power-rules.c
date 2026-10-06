/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of who may end the machine through sessiond (ws131-p027,
 * userland/desktop/sessiond/power-rules.c compiled unchanged).
 *
 * Checks: "poweroff" and "reboot" name their programs, any other word none
 * (EINVAL); only root or a member of wheel may (EPERM otherwise; the
 * 2026-10-06 user decision: wheel only).
 *
 *   plan/ws131/tests/host-power-rules.sh
 */

#include "power-rules.h"

#include <errno.h>
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
	const char *program;
	int decided;

	/* 1. The words and their programs. */
	check(strcmp(sessiond_power_program("poweroff"), SESSIOND_POWEROFF) == 0, "poweroff: /sbin/poweroff");
	check(strcmp(sessiond_power_program("reboot"), SESSIOND_REBOOT) == 0, "reboot: /sbin/reboot");
	check(sessiond_power_program("halt") == NULL, "halt: no program");
	check(sessiond_power_program("poweroff now") == NULL, "a word with more after it: no program");
	check(sessiond_power_program("") == NULL, "no word: no program");

	/* 2. Another word is refused whoever asks. */
	decided = sessiond_power_decide("suspend", 0, 1, &program);
	check(decided == EINVAL && program == NULL, "suspend: EINVAL even for root");

	/* 3. A member of wheel and root may. */
	decided = sessiond_power_decide("poweroff", 1001, 1, &program);
	check(decided == 0 && strcmp(program, SESSIOND_POWEROFF) == 0, "wheel: may power off");
	decided = sessiond_power_decide("reboot", 1001, 1, &program);
	check(decided == 0 && strcmp(program, SESSIOND_REBOOT) == 0, "wheel: may restart");
	decided = sessiond_power_decide("poweroff", 0, 0, &program);
	check(decided == 0, "root: may power off");

	/* 4. Another user may not, alone on the machine too. */
	decided = sessiond_power_decide("poweroff", 1001, 0, &program);
	check(decided == EPERM, "not in wheel: refused");
	decided = sessiond_power_decide("reboot", 1002, 0, &program);
	check(decided == EPERM, "not in wheel: restart refused");

	/* The result. */
	if (failures != 0) {
		printf("host-power-rules: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-power-rules: ok (%d checks)\n", checks);
	return 0;
}
