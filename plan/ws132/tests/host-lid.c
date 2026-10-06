/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of what the lid does (ws132-p008, the user decision D2):
 * userland/desktop/wayland/lid.c alone, and the compositor's handling of
 * the backend's lid callback (backend-host.c) with fakes of the lock, the
 * clock and the backlight.
 *
 * Checks: closing puts the screen out (the backlight to 0, its brightness
 * kept) and locks a session; opening within 15 minutes lights it again
 * (the brightness back) and unlocks without the password; later, or a lock
 * that was not the lid's, or one unlocked meanwhile with the password,
 * keeps the lock screen; the login screen is not locked; without a
 * backlight the screen is only drawn black; a lid that says the same twice
 * does nothing more; a session that cannot be locked is not unlocked by
 * the lid either.
 *
 *   sh plan/ws132/tests/run-host-lid.sh
 */

#include "userland/desktop/wayland/kwl.h"
#include "userland/desktop/wayland/lid.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fake backlight's record: whether it exists, its brightness, and how often it was set. */
struct kl_backend_backlight {
	unsigned percent;
};

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* The fakes' state: the clock, whether a backlight exists, its record, whether a lock is possible, and the calls seen. */
static uint64_t fake_now;
static int fake_backlight_exists;
static struct kl_backend_backlight fake_backlight;
static int fake_lock_possible;
static unsigned locks;
static unsigned releases;
static unsigned backlight_sets;

/* The compositor (most of it unused here). */
static struct kwl_server server;

static void check(int condition, const char *what);
static void start_case(int backlight, int lockable, int greeter);
static void test_logic(void);

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

/* Starts a case on a fresh compositor: whether the machine has a backlight, whether a lock is possible, and whether this is the login screen. */
static void
start_case(
	int backlight,
	int lockable,
	int greeter)
{
	/* Everything forgotten. */
	memset(&server, 0, sizeof(server));
	server.greeter = (unsigned)greeter;
	fake_now = 1000000U;
	fake_backlight_exists = backlight;
	fake_backlight.percent = 70U;
	fake_lock_possible = lockable;
	locks = 0;
	releases = 0;
	backlight_sets = 0;
}

/* The state machine alone. */
static void
test_logic(void)
{
	struct kwl_lid lid;
	unsigned actions;

	/* Closing a session not locked: out and locked; opening at once: lit and unlocked. */
	memset(&lid, 0, sizeof(lid));
	actions = kwl_lid_close(&lid, 100, 1, 0);
	check(actions == (KWL_LID_SCREEN_OFF | KWL_LID_LOCK), "logic: closing locks");
	actions = kwl_lid_close(&lid, 200, 1, 1);
	check(actions == 0U, "logic: closed twice does nothing");
	actions = kwl_lid_open(&lid, 300, 1);
	check(actions == (KWL_LID_SCREEN_ON | KWL_LID_UNLOCK), "logic: opening at once unlocks");
	actions = kwl_lid_open(&lid, 400, 0);
	check(actions == 0U, "logic: open twice does nothing");

	/* Exactly 15 minutes unlocks; one millisecond more does not. */
	(void)kwl_lid_close(&lid, 1000, 1, 0);
	actions = kwl_lid_open(&lid, 1000 + KWL_LID_GRACE_MS, 1);
	check(actions == (KWL_LID_SCREEN_ON | KWL_LID_UNLOCK), "logic: 15 minutes unlocks");
	(void)kwl_lid_close(&lid, 1000, 1, 0);
	actions = kwl_lid_open(&lid, 1001 + KWL_LID_GRACE_MS, 1);
	check(actions == KWL_LID_SCREEN_ON, "logic: past 15 minutes keeps the lock");

	/* A lock that was there before the closing is not the lid's. */
	(void)kwl_lid_close(&lid, 1000, 1, 1);
	actions = kwl_lid_open(&lid, 1001, 1);
	check(actions == KWL_LID_SCREEN_ON, "logic: an earlier lock stays");

	/* Unlocked with the password meanwhile, then locked otherwise: that lock stays. */
	(void)kwl_lid_close(&lid, 1000, 1, 0);
	kwl_lid_unlocked(&lid);
	actions = kwl_lid_open(&lid, 1001, 1);
	check(actions == KWL_LID_SCREEN_ON, "logic: a lock after an unlock is not the lid's");

	/* The login screen: only out. */
	actions = kwl_lid_close(&lid, 1000, 0, 0);
	check(actions == KWL_LID_SCREEN_OFF, "logic: the login screen is not locked");
	actions = kwl_lid_open(&lid, 1001, 0);
	check(actions == KWL_LID_SCREEN_ON, "logic: the login screen lights");

	/* A clock that went back keeps the lock. */
	(void)kwl_lid_close(&lid, 5000, 1, 0);
	actions = kwl_lid_open(&lid, 4000, 1);
	check(actions == KWL_LID_SCREEN_ON, "logic: a clock that went back keeps the lock");

	/* A failed lock leaves nothing to unlock. */
	(void)kwl_lid_close(&lid, 1000, 1, 0);
	kwl_lid_lock_failed(&lid);
	actions = kwl_lid_open(&lid, 1001, 1);
	check(actions == KWL_LID_SCREEN_ON, "logic: a failed lock unlocks nothing");
}

/* Runs every case. */
int
main(void)
{
	/* 1. The state machine. */
	test_logic();

	/* 2. A session with a backlight: closed, opened 5 minutes later. */
	start_case(1, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	check(server.locked == 1U && locks == 1U, "closed: the session locks");
	check(server.screen_off == 1U, "closed: the screen is black");
	check(fake_backlight.percent == 0U && server.backlight_saved == 70U, "closed: the backlight off, 70% kept");
	fake_now += 5U * 60U * 1000U;
	kwl_backend_lid_changed(&server, 1U);
	check(server.locked == 0U && releases == 1U, "opened in 5 minutes: unlocked without the password");
	check(server.screen_off == 0U && fake_backlight.percent == 70U, "opened: lit at 70%");

	/* 3. Opened 16 minutes later: lit, still locked. */
	start_case(1, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	fake_now += 16U * 60U * 1000U;
	kwl_backend_lid_changed(&server, 1U);
	check(server.locked == 1U && releases == 0U, "opened in 16 minutes: the lock screen stays");
	check(server.screen_off == 0U && fake_backlight.percent == 70U, "opened late: lit");

	/* 4. Already locked before the closing: the lock stays. */
	start_case(1, 1, 0);
	server.locked = 1U;
	kwl_backend_lid_changed(&server, 0U);
	kwl_backend_lid_changed(&server, 1U);
	check(server.locked == 1U && releases == 0U && locks == 0U, "an earlier lock stays");

	/* 5. Unlocked meanwhile with the password, locked again by the idle time: that lock stays. */
	start_case(1, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	server.locked = 0U;
	kwl_lid_unlocked(&server.lid);
	server.locked = 1U;
	kwl_backend_lid_changed(&server, 1U);
	check(server.locked == 1U && releases == 0U, "a lock after a password unlock stays");

	/* 6. The login screen: black and lit, never locked. */
	start_case(1, 1, 1);
	kwl_backend_lid_changed(&server, 0U);
	check(server.locked == 0U && locks == 0U && server.screen_off == 1U, "login screen: black, not locked");
	kwl_backend_lid_changed(&server, 1U);
	check(server.screen_off == 0U && fake_backlight.percent == 70U, "login screen: lit");

	/* 7. No backlight: only black. */
	start_case(0, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	check(server.screen_off == 1U && server.backlight == NULL && backlight_sets == 0U, "no backlight: black only");
	kwl_backend_lid_changed(&server, 1U);
	check(server.screen_off == 0U && server.locked == 0U, "no backlight: lit and unlocked");

	/* 8. The same state twice: nothing more. */
	start_case(1, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	kwl_backend_lid_changed(&server, 0U);
	check(locks == 1U && backlight_sets == 1U, "closed twice: one lock, one dimming");

	/* 9. A session that cannot be locked: black, and the opening unlocks nothing. */
	start_case(1, 0, 0);
	kwl_backend_lid_changed(&server, 0U);
	check(server.locked == 0U && server.screen_off == 1U, "unlockable session: black only");
	server.locked = 1U;
	kwl_backend_lid_changed(&server, 1U);
	check(releases == 0U, "unlockable session: the opening unlocks nothing");

	/* 10. The compositor ending with the lid closed lights the panel for the next session. */
	start_case(1, 1, 0);
	kwl_backend_lid_changed(&server, 0U);
	kwl_lid_screen_restore(&server);
	check(fake_backlight.percent == 70U && server.screen_off == 0U, "the end lights the panel");

	/* The result. */
	if (failures != 0) {
		printf("host-lid: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-lid: ok (%d checks)\n", checks);
	return 0;
}

/* The fakes of what backend-host.c's lid handling calls. */

/* The fake clock. */
uint64_t
kwl_milliseconds(void)
{
	/* Its time. */
	return fake_now;
}

/* Locks, when the session can be. */
int
kwl_lock(
	struct kwl_server *compositor,
	const char *reason)
{
	/* Not possible: not locked. */
	(void)reason;
	if (!fake_lock_possible)
		return 0;

	/* Locked. */
	locks++;
	compositor->locked = 1U;
	return 1;
}

/* Unlocks without the password. */
void
kwl_lock_release(
	struct kwl_server *compositor,
	const char *reason)
{
	/* Unlocked. */
	(void)reason;
	releases++;
	compositor->locked = 0U;
}

/* Opens the backlight, when the machine has one. */
int
kl_backend_backlight_open(
	struct kl_backend_backlight **backlight)
{
	/* None. */
	if (!fake_backlight_exists)
		return ENOENT;

	/* The record. */
	*backlight = &fake_backlight;
	return 0;
}

/* Reads the brightness. */
int
kl_backend_backlight_get(
	struct kl_backend_backlight *backlight,
	unsigned *percent)
{
	/* Its value. */
	*percent = backlight->percent;
	return 0;
}

/* Sets the brightness. */
int
kl_backend_backlight_set(
	struct kl_backend_backlight *backlight,
	unsigned percent)
{
	/* Set, and counted. */
	backlight->percent = percent;
	backlight_sets++;
	return 0;
}
