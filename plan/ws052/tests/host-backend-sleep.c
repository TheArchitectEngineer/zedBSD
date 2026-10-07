/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws052-p011: the host test of the backend's reading of sessiond's sleep
 * answers (userland/desktop/libkeiland-backend-zedbsd/power-outcome.c,
 * compiled unchanged; the errno values are the host's).  Each answer line
 * sessiond/sleep-rules.c makes is read into its kind, its error, the wake,
 * the device, networkd's reason and the resume's error; a line not
 * understood is an error.  Prints "host-backend-sleep: PASS" or the failed
 * checks and FAIL.
 *
 *   sh plan/ws052/tests/run-host-backend-sleep.sh
 */

#include "userland/desktop/libkeiland-backend-zedbsd/power-outcome.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* How many checks ran and how many failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *what);

/* Reads each answer and checks what it came to. */
int
main(void)
{
	struct kl_backend_power_outcome outcome;
	int error;

	/* Slept, woken by the lid. */
	error = kl_backend_power_parse_outcome("SLEPT woke=lid", &outcome);
	check(error == 0 && outcome.kind == KL_BACKEND_SLEEP_SLEPT && strcmp(outcome.wake, "lid") == 0 && outcome.resume_error == 0, "slept, the lid");

	/* Slept, a device not back. */
	error = kl_backend_power_parse_outcome("SLEPT woke=power-button resume-error=EIO", &outcome);
	check(error == 0 && strcmp(outcome.wake, "power-button") == 0 && outcome.resume_error == EIO, "slept, a device not back");

	/* The machine cannot sleep. */
	error = kl_backend_power_parse_outcome("NOSLEEP unsupported", &outcome);
	check(error == EOPNOTSUPP && outcome.kind == KL_BACKEND_SLEEP_UNSUPPORTED, "unsupported");

	/* A device refused, with spaces in its name. */
	error = kl_backend_power_parse_outcome("NOSLEEP device error=EBUSY device=pci 0000:00:14.3 intel-ax211", &outcome);
	check(error == EBUSY && outcome.kind == KL_BACKEND_SLEEP_DEVICE && strcmp(outcome.device, "pci 0000:00:14.3 intel-ax211") == 0, "a device refused");

	/* A driver without suspend, no name. */
	error = kl_backend_power_parse_outcome("NOSLEEP device error=EOPNOTSUPP device=-", &outcome);
	check(error == EOPNOTSUPP && outcome.device[0] == '\0', "a driver without suspend");

	/* networkd's reasons. */
	error = kl_backend_power_parse_outcome("NOSLEEP network reason=radio radio=- error=EBUSY", &outcome);
	check(error == EBUSY && outcome.kind == KL_BACKEND_SLEEP_NETWORK && outcome.network == KL_BACKEND_SLEEP_NETWORK_RADIO, "networkd: a radio");
	(void)kl_backend_power_parse_outcome("NOSLEEP network reason=timeout radio=- error=ETIMEDOUT", &outcome);
	check(outcome.network == KL_BACKEND_SLEEP_NETWORK_TIMEOUT, "networkd: timeout");
	(void)kl_backend_power_parse_outcome("NOSLEEP network reason=confirmed radio=- error=EBUSY", &outcome);
	check(outcome.network == KL_BACKEND_SLEEP_NETWORK_CONFIRMED, "networkd: confirmed");
	(void)kl_backend_power_parse_outcome("NOSLEEP network reason=busy radio=- error=EBUSY", &outcome);
	check(outcome.network == KL_BACKEND_SLEEP_NETWORK_BUSY, "networkd: busy");

	/* Cancelled, busy, an error and a line not understood. */
	error = kl_backend_power_parse_outcome("NOSLEEP cancelled", &outcome);
	check(error == ECANCELED && outcome.kind == KL_BACKEND_SLEEP_CANCELLED, "cancelled");
	error = kl_backend_power_parse_outcome("ERROR busy", &outcome);
	check(error == EBUSY && outcome.kind == KL_BACKEND_SLEEP_BUSY, "busy");
	error = kl_backend_power_parse_outcome("ERROR", &outcome);
	check(error == EIO && outcome.kind == KL_BACKEND_SLEEP_ERROR, "an error");
	error = kl_backend_power_parse_outcome("SLEPTX woke=lid", &outcome);
	check(error == EIO && outcome.kind == KL_BACKEND_SLEEP_ERROR, "a line not understood");

	/* The verdict. */
	printf("host-backend-sleep: checks=%u failures=%u\n", checks, failures);
	if (failures != 0U) {
		printf("host-backend-sleep: FAIL\n");
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-backend-sleep: PASS\n");
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
