/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p025: the host test of what a session may ask of the system's
 * services through sessiond (userland/desktop/sessiond/service-rules.c
 * compiled unchanged): root and members of wheel may turn sshd on and off
 * and ask its state; anyone else is refused (EPERM); another service, a
 * word that is not one of the three, more words, missing words or stray
 * spaces are refused (EINVAL).
 * Prints one line a check and "host-service-rules: PASS" or FAIL.
 *
 *   sh plan/ws089/tests/run-host-service-rules.sh
 */

#include "userland/desktop/sessiond/service-rules.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *what, int passed);
static int decide(const char *arguments, uid_t uid, int in_wheel, unsigned *action);

/* Prints one check's verdict. */
static void
check(
	const char *what,
	int passed)
{
	/* A pass. */
	if (passed) {
		printf("%s: ok\n", what);
		return;
	}

	/* A failure counted. */
	printf("%s: FAIL\n", what);
	failures++;
}

/* Decides a request; gives what it asks. */
static int
decide(
	const char *arguments,
	uid_t uid,
	int in_wheel,
	unsigned *action)
{
	struct sessiond_service_request request;
	int differs;
	int error;

	/* Decided. */
	error = sessiond_service_decide(arguments, uid, in_wheel, &request);
	*action = request.action;
	differs = strcmp(request.name, "sshd");
	if (error == 0 && differs != 0)
		return -1;
	return error;
}

/*
 * Runs the checks.
 */
int
main(void)
{
	unsigned action;

	/* Who may. */
	check("root turns sshd on", decide("sshd on", 0, 0, &action) == 0 && action == SESSIOND_SERVICE_ON);
	check("wheel turns sshd off", decide("sshd off", 1000, 1, &action) == 0 && action == SESSIOND_SERVICE_OFF);
	check("wheel asks the state", decide("sshd status", 1000, 1, &action) == 0 && action == SESSIOND_SERVICE_STATUS);

	/* Who may not. */
	check("not wheel: on refused", decide("sshd on", 1001, 0, &action) == EPERM);
	check("not wheel: status refused", decide("sshd status", 1001, 0, &action) == EPERM);

	/* Other services and words. */
	check("another service", decide("cron on", 0, 1, &action) == EINVAL);
	check("a longer name", decide("sshdx on", 0, 1, &action) == EINVAL);
	check("a shorter name", decide("ssh on", 0, 1, &action) == EINVAL);
	check("another word", decide("sshd restart", 0, 1, &action) == EINVAL);
	check("capitals", decide("sshd ON", 0, 1, &action) == EINVAL);
	check("more words", decide("sshd on now", 0, 1, &action) == EINVAL);
	check("no word", decide("sshd", 0, 1, &action) == EINVAL);
	check("an empty word", decide("sshd ", 0, 1, &action) == EINVAL);
	check("two spaces", decide("sshd  on", 0, 1, &action) == EINVAL);
	check("a leading space", decide(" sshd on", 0, 1, &action) == EINVAL);
	check("nothing", decide("", 0, 1, &action) == EINVAL);
	check("a path", decide("../sshd on", 0, 1, &action) == EINVAL);
	check("a bad request from a non-member is still refused", decide("cron on", 1001, 0, &action) != 0);

	/* The verdict. */
	if (failures != 0) {
		printf("host-service-rules: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-service-rules: PASS\n");
	return 0;
}
