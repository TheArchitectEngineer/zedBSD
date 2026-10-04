/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of what a USB storage device's detach does with its disk
 * (BUG-192, include/drivers/usb/usb-storage-removal.h compiled unchanged),
 * stepped through the USB core's retries of a pulled-out mounted stick:
 * the first detach revokes the medium and posts its going, the retries
 * wait while it is mounted, the one after the unmount retires and frees it;
 * an idle stick goes at once; a detach that is no removal never revokes.
 *
 *   sh plan/ws132/tests/run-host-storage-removal.sh
 */

#include "drivers/usb/usb-storage-removal.h"

#include <stdio.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* A simulated stick: mounted or not, its medium revoked or not, whether its going was posted, freed. */
static int mounted;
static int revoked;
static int posted;
static int freed;

static void check(int condition, const char *what);
static int detach(int force);

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

/*
 * One detach as usb-storage.c does it: a revoked medium retired, a live
 * disk removed, each only when idle; then the decision.  Returns the
 * detach's error.
 */
static int
detach(
	int force)
{
	enum storage_removal_step step;
	int error;

	/* Removal or retirement refuses a mounted disk. */
	error = 0;
	if (mounted)
		error = EBUSY;

	/* The decision, carried out. */
	step = storage_removal_decide(revoked, error, force);
	if (step == STORAGE_REMOVAL_REVOKE) {
		revoked = 1;
		posted++;
	}

	/* Anything but the destruction waits. */
	if (step != STORAGE_REMOVAL_DESTROY)
		return EBUSY;

	/* Succeeded: freed. */
	freed = 1;
	return 0;
}

/* Runs every case. */
int
main(void)
{
	int error;

	/* 1. A mounted stick pulled out: revoked and posted at once, kept while mounted, freed after the unmount. */
	mounted = 1;
	revoked = 0;
	posted = 0;
	freed = 0;
	error = detach(1);
	check(error == EBUSY && revoked && posted == 1 && !freed, "pulled while mounted: revoked, posted, kept");
	error = detach(1);
	check(error == EBUSY && posted == 1 && !freed, "a retry while mounted: no second post");
	mounted = 0;
	error = detach(1);
	check(error == 0 && freed && posted == 1, "after the unmount: retired and freed");

	/* 2. An idle stick pulled out: freed at once, nothing revoked (its own REMOVE comes from the disk layer). */
	mounted = 0;
	revoked = 0;
	posted = 0;
	freed = 0;
	error = detach(1);
	check(error == 0 && freed && !revoked && posted == 0, "idle: freed at once");

	/* 3. A detach that is no removal (no FORCE) of a mounted disk: waits, never revokes. */
	mounted = 1;
	revoked = 0;
	posted = 0;
	freed = 0;
	error = detach(0);
	check(error == EBUSY && !revoked && posted == 0, "no FORCE: no revocation");

	/* 4. Other refusals wait without revoking. */
	check(storage_removal_decide(0, ENXIO, 1) == STORAGE_REMOVAL_WAIT, "ENXIO waits");
	check(storage_removal_decide(1, EBUSY, 1) == STORAGE_REMOVAL_WAIT, "revoked and busy waits");

	/* The result. */
	if (failures != 0) {
		printf("host-storage-removal: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-storage-removal: ok (%d checks)\n", checks);
	return 0;
}
