/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws052-p011: the host test of sessiond's sleep answers
 * (userland/desktop/sessiond/sleep-rules.c, compiled unchanged; the
 * errno values are the host's, the names the same).  The cases:
 *   1. networkd's answer to SLEEP_PREPARE: not running, timed out, done,
 *      a confirmed change waiting, the user's Wi-Fi work under way, a radio
 *      that would not go off;
 *   2. the answer lines: slept (and a device not back), unsupported, a
 *      device refused (with and without a name), networkd's four failures,
 *      cancelled, busy, another ioctl error; a line too long for its buffer.
 * Prints "host-sessiond-sleep: PASS" or the failed checks and FAIL.
 *
 *   sh plan/ws052/tests/run-host-sessiond-sleep.sh
 */

#include "userland/desktop/sessiond/sleep-rules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* How many checks ran and how many failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *what);
static void check_line(const struct sessiond_sleep_seen *seen, const char *expected, const char *what);
static void case_network(void);
static void case_answers(void);

/* Runs every case and prints the verdict. */
int
main(void)
{
	/* Each case. */
	case_network();
	case_answers();

	/* The verdict. */
	printf("host-sessiond-sleep: checks=%u failures=%u\n", checks, failures);
	if (failures != 0U) {
		printf("host-sessiond-sleep: FAIL\n");
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-sessiond-sleep: PASS\n");
	return 0;
}

/* Counts a check, and names it when it failed. */
static void
check(
	int passed,
	const char *what)
{
	/* Counted; a failure is named. */
	checks++;
	if (!passed) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Checks the answer line made from what was seen. */
static void
check_line(
	const struct sessiond_sleep_seen *seen,
	const char *expected,
	const char *what)
{
	char line[SESSIOND_SLEEP_LINE_MAX];
	int error;
	int same;

	/* The line, whole and as expected. */
	error = sessiond_sleep_answer(seen, line, sizeof(line));
	same = strcmp(line, expected);
	if (error != 0 || same != 0)
		printf("  got \"%s\" (error %d), expected \"%s\"\n", line, error, expected);
	check(error == 0 && same == 0, what);
}

/* Case 1: networkd's answer to SLEEP_PREPARE. */
static void
case_network(void)
{
	/* Each kind of answer. */
	check(sessiond_sleep_network_of(0, 1, 0, ENOENT, NULL) == SESSIOND_SLEEP_NETWORK_ABSENT, "1: no networkd");
	check(sessiond_sleep_network_of(0, 0, 1, EAGAIN, NULL) == SESSIOND_SLEEP_NETWORK_TIMEOUT, "1: no answer in time");
	check(sessiond_sleep_network_of(1, 0, 0, 0, NULL) == SESSIOND_SLEEP_NETWORK_OFF, "1: the radios are off");
	check(sessiond_sleep_network_of(0, 0, 0, EBUSY, "confirmed transaction") == SESSIOND_SLEEP_NETWORK_CONFIRMED, "1: a confirmed change waits");
	check(sessiond_sleep_network_of(0, 0, 0, EBUSY, "Wi-Fi operation in progress; retry") == SESSIOND_SLEEP_NETWORK_BUSY, "1: the user's Wi-Fi work");
	check(sessiond_sleep_network_of(0, 0, 0, EBUSY, "sleep: Wi-Fi radio") == SESSIOND_SLEEP_NETWORK_RADIO, "1: a radio stays on");
	check(sessiond_sleep_network_of(0, 0, 0, EIO, NULL) == SESSIOND_SLEEP_NETWORK_RADIO, "1: another failure is the radio's");
}

/* Case 2: the answer lines. */
static void
case_answers(void)
{
	struct sessiond_sleep_seen seen;
	char line[24];
	int error;

	/* Slept and woke by the lid. */
	memset(&seen, 0, sizeof(seen));
	seen.network = SESSIOND_SLEEP_NETWORK_OFF;
	seen.wake = "lid";
	check_line(&seen, "SLEPT woke=lid", "2: slept, woken by the lid");

	/* Slept without networkd, a device not back. */
	seen.network = SESSIOND_SLEEP_NETWORK_ABSENT;
	seen.wake = "power-button";
	seen.resume_result = EIO;
	check_line(&seen, "SLEPT woke=power-button resume-error=EIO", "2: slept, a device not back");

	/* The machine cannot sleep. */
	memset(&seen, 0, sizeof(seen));
	seen.ioctl_error = EOPNOTSUPP;
	seen.wake = "none";
	check_line(&seen, "NOSLEEP unsupported", "2: unsupported");

	/* Another sleep, another refusal. */
	seen.ioctl_error = EBUSY;
	check_line(&seen, "ERROR busy", "2: busy");
	seen.ioctl_error = EPERM;
	check_line(&seen, "ERROR", "2: another ioctl error");

	/* A device refused, named and not. */
	seen.ioctl_error = 0;
	seen.result = EBUSY;
	snprintf(seen.device, sizeof(seen.device), "pci 0000:00:14.3 intel-ax211");
	check_line(&seen, "NOSLEEP device error=EBUSY device=pci 0000:00:14.3 intel-ax211", "2: a device refused");
	seen.result = EOPNOTSUPP;
	seen.device[0] = '\0';
	check_line(&seen, "NOSLEEP device error=EOPNOTSUPP device=-", "2: a driver without suspend, no name");

	/* networkd's failures: the kernel was not asked. */
	memset(&seen, 0, sizeof(seen));
	seen.wake = "none";
	seen.network = SESSIOND_SLEEP_NETWORK_RADIO;
	seen.network_error = EBUSY;
	check_line(&seen, "NOSLEEP network reason=radio radio=- error=EBUSY", "2: a radio stays on");
	seen.network = SESSIOND_SLEEP_NETWORK_TIMEOUT;
	seen.network_error = ETIMEDOUT;
	check_line(&seen, "NOSLEEP network reason=timeout radio=- error=ETIMEDOUT", "2: networkd's time ran out");
	seen.network = SESSIOND_SLEEP_NETWORK_CONFIRMED;
	seen.network_error = EBUSY;
	check_line(&seen, "NOSLEEP network reason=confirmed radio=- error=EBUSY", "2: a confirmed change waits");
	seen.network = SESSIOND_SLEEP_NETWORK_BUSY;
	check_line(&seen, "NOSLEEP network reason=busy radio=- error=EBUSY", "2: the user's Wi-Fi work");

	/* A cancel before the kernel was asked wins over the rest. */
	seen.network = SESSIOND_SLEEP_NETWORK_OFF;
	seen.cancelled = 1U;
	check_line(&seen, "NOSLEEP cancelled", "2: cancelled");

	/* A line too long for its buffer. */
	memset(&seen, 0, sizeof(seen));
	seen.result = EBUSY;
	snprintf(seen.device, sizeof(seen.device), "pci 0000:00:14.3 intel-ax211");
	error = sessiond_sleep_answer(&seen, line, sizeof(line));
	check(error == ENOSPC && strlen(line) == sizeof(line) - 1U, "2: a line too long is cut and said so");

	/* An errno the answers do not name. */
	check(strcmp(sessiond_sleep_errno_name(9999), "EUNKNOWN") == 0, "2: an unnamed errno");
}
