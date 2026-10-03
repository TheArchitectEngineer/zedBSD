/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-102 probe: the privilege drop that the set-user-ID ping relies on.
 *
 * Installed set-user-ID root and run by an ordinary user, the program
 * opens an ICMP raw socket, gives the privilege up with setuid(getuid())
 * exactly as ping does, and then checks that the privilege is gone for
 * good: both user identities are the caller's, a second raw socket is
 * refused, and neither setuid(0) nor seteuid(0) takes the superuser back.
 * Prints SETUID-DROP:PASS and exits 0.
 */

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * The number of checks that failed.
 *
 * It only grows while the checks run; main turns it into the result line.
 */
static int failures;

static void expect(int condition, const char *what);

/*
 * Runs the privilege-drop checks.
 */
int
main(
	void)
{
	int descriptor, second, error;
	uid_t real_user, effective_user;

	/* Reports the identities the set-user-ID bit gave the process. */
	real_user = getuid();
	effective_user = geteuid();
	printf("before: uid=%u euid=%u\n", (unsigned)real_user, (unsigned)effective_user);
	expect(effective_user == 0, "the set-user-ID bit lends the superuser");

	/* Opens the ICMP socket while the privilege is lent, as ping does. */
	descriptor = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (descriptor < 0)
		printf("socket: %s\n", strerror(errno));
	expect(descriptor >= 0, "the raw socket opens with the lent privilege");

	/* Gives the privilege up the way ping does. */
	error = setuid(real_user);
	expect(error == 0, "setuid(getuid()) succeeds");

	/* Reports the identities after the drop. */
	effective_user = geteuid();
	printf("after: uid=%u euid=%u\n", (unsigned)getuid(), (unsigned)effective_user);
	expect(effective_user == real_user, "the effective identity is the caller's");

	/* A second raw socket must now be refused. */
	second = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	expect(second < 0, "a raw socket is refused after the drop");

	/* The superuser cannot be taken back through the saved identity. */
	error = setuid(0);
	expect(error != 0, "setuid(0) is refused after the drop");
	error = seteuid(0);
	expect(error != 0, "seteuid(0) is refused after the drop");

	/* Reports every failed check as one result line. */
	if (failures != 0) {
		printf("SETUID-DROP:FAIL %d\n", failures);
		return 1;
	}

	/* Succeeded: the privilege is gone and cannot come back. */
	printf("SETUID-DROP:PASS\n");
	return 0;
}

/* Counts and names one failed check. */
static void
expect(
	int condition,
	const char *what)
{
	/* Names the check that did not hold. */
	if (!condition) {
		printf("SETUID-DROP:FAIL %s\n", what);
		failures++;
	}
}
