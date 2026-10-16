/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Exercises the DHCP retry of networkd's wired interface policy on the host
 * (ws254).
 *
 * A wired interface whose DHCP obtained nothing is asked again every
 * minute, and each retry resets the link first (2026-10-16 user).  The
 * policy holds the clock it is given, so the minute is put to it as a
 * number rather than waited for.
 *
 *   sh plan/ws254/tests/dhcp-retry-host-test.sh
 */

#include "userland/base/networkd/managed-lan.h"

#include <stdio.h>
#include <string.h>
#include <uapi/route.h>

/* One second on the monotonic clock the policy is given, in microseconds. */
#define SECOND 1000000ULL

/* When the test starts, on the clock the policy is given. */
#define START (1000ULL * SECOND)

/*
 * The number of checks that did not hold.  The test fails when it is not
 * zero at the end.
 */
static int failures;

static void check(const char *name, int held);
static struct rtm_ifinfo event(uint32_t ifindex, uint32_t transition);
static struct networkd_lan_policy policy(const char *name, enum networkd_lan_mode mode);
static void managed(struct networkd_lan *lan);
static int next_is(const struct networkd_lan *lan, const char *name, int reset);
static int not_configuring(const struct networkd_lan *lan, const char *name);
static enum networkd_lan_state state_of(struct networkd_lan *lan, const char *name);
static void test_retry_cycle(void);
static void test_static_not_retried(void);
static void test_cable_and_hold(void);
static void test_up_and_reset_window(void);

/*
 * Runs every case and reports the outcome.
 */
int
main(
	void)
{
	/* Runs the cases one after another. */
	test_retry_cycle();
	test_static_not_retried();
	test_cable_and_hold();
	test_up_and_reset_window();

	/* Reports a failure when any check did not hold. */
	if (failures != 0) {
		printf("WS254-T001 dhcp-retry: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("WS254-T001 dhcp-retry: PASS\n");
	return 0;
}

/* Prints one check and counts it when it did not hold. */
static void
check(
	const char *name,
	int held)
{
	/* A check that held is printed as ok. */
	if (held) {
		printf("ok %s\n", name);
		return;
	}

	/* One that did not is printed and counted. */
	printf("FAIL %s\n", name);
	failures++;
}

/* Builds one carrier event of the route socket. */
static struct rtm_ifinfo
event(
	uint32_t ifindex,
	uint32_t transition)
{
	struct rtm_ifinfo item;

	/* A carrier transition of one interface, in the form the kernel sends. */
	memset(&item, 0, sizeof(item));
	item.rtm_version = RTM_VERSION;
	item.rtm_type = RTM_IFINFO;
	item.rtm_length = sizeof(item);
	item.rtm_ifindex = ifindex;
	item.rtm_transition = transition;

	/* Reports the event. */
	return item;
}

/* Fills what the configuration says of one interface. */
static struct networkd_lan_policy
policy(
	const char *name,
	enum networkd_lan_mode mode)
{
	struct networkd_lan_policy item;

	/* The interface's name, its mode and the DHCP timeout net gives. */
	memset(&item, 0, sizeof(item));
	strncpy(item.interface, name, sizeof(item.interface) - 1U);
	item.mode = mode;
	item.dhcp_timeout = 10U;

	/* A static interface has its address. */
	if (mode == NETWORKD_LAN_MODE_STATIC) {
		strcpy(item.address, "192.0.2.10");
		strcpy(item.netmask, "255.255.255.0");
	}

	/* Reports the record. */
	return item;
}

/*
 * Sets up a managed policy with en0 (DHCP, index 1) and en1 (static,
 * index 2), both with their cables in.
 */
static void
managed(
	struct networkd_lan *lan)
{
	struct networkd_lan_policy items[2];

	/* The configuration, and management on. */
	networkd_lan_init(lan);
	items[0] = policy("en0", NETWORKD_LAN_MODE_DHCP);
	items[1] = policy("en1", NETWORKD_LAN_MODE_STATIC);
	(void)networkd_lan_set_policy(lan, items, 2U);
	(void)networkd_lan_enable(lan);

	/* The snapshot that enabling asks for, with both cables in. */
	networkd_lan_snapshot_begin(lan);
	(void)networkd_lan_observe(lan, "en0", 1U, 1U, 1);
	(void)networkd_lan_observe(lan, "en1", 2U, 1U, 1);
	networkd_lan_snapshot_end(lan);
}

/* Tells whether the next work configures NAME, as a retry or not. */
static int
next_is(
	const struct networkd_lan *lan,
	const char *name,
	int reset)
{
	struct networkd_lan_work work;
	int result;
	int differs;

	/* Asks the policy what it would do. */
	result = networkd_lan_next(lan, &work);
	if (result != 0)
		return 0;

	/* Only a configure counts. */
	if (work.action != NETWORKD_LAN_ACTION_CONFIGURE)
		return 0;

	/* Only one of that interface counts. */
	differs = strcmp(work.interface, name);
	if (differs != 0)
		return 0;

	/* The reset of a retry, or its absence, as expected. */
	if (work.reset != reset)
		return 0;

	/* Succeeded: it is that configure. */
	return 1;
}

/* Tells whether the next work is anything but a configure of NAME. */
static int
not_configuring(
	const struct networkd_lan *lan,
	const char *name)
{
	struct networkd_lan_work work;
	int result;
	int differs;

	/* Asks the policy what it would do. */
	result = networkd_lan_next(lan, &work);
	if (result != 0)
		return 0;

	/* Anything but a configure is not one. */
	if (work.action != NETWORKD_LAN_ACTION_CONFIGURE)
		return 1;

	/* A configure of another interface is not one of this. */
	differs = strcmp(work.interface, name);
	if (differs != 0)
		return 1;

	/* It configures NAME. */
	return 0;
}

/* Reports the state the policy holds for one interface. */
static enum networkd_lan_state
state_of(
	struct networkd_lan *lan,
	const char *name)
{
	size_t index;
	int differs;

	/* Looks the interface up by name. */
	for (index = 0U; index < lan->interface_count; index++) {
		differs = strcmp(lan->interfaces[index].name, name);
		if (differs == 0)
			return lan->interfaces[index].state;
	}

	/* An interface not held is idle. */
	return NETWORKD_LAN_IDLE;
}

/*
 * A DHCP interface that obtained nothing is asked again a minute later,
 * as a retry with a reset, and again after each failure until it obtains
 * an address.
 */
static void
test_retry_cycle(
	void)
{
	struct networkd_lan lan;
	int planned;
	int count;
	int wait;

	/* The first configure is a plain one. */
	managed(&lan);
	check("the first configure of en0 is not a retry", next_is(&lan, "en0", 0));

	/* It obtains nothing; a retry is planned a minute later. */
	(void)networkd_lan_configured(&lan, "en0", 0);
	planned = networkd_lan_retry_schedule(&lan, "en0", START);
	check("a failed dhcp interface gets a retry", planned == 1);
	wait = networkd_lan_retry_timeout(&lan, START);
	check("the retry is due in 60 seconds", wait == 60000);

	/* Before the minute is out nothing is asked. */
	count = networkd_lan_retry_due(&lan, START + 59U * SECOND);
	check("the retry is not due before the minute", count == 0);
	check("en0 is not configured before the minute", not_configuring(&lan, "en0"));

	/* At the minute it is asked again, with a reset. */
	count = networkd_lan_retry_due(&lan, START + 60U * SECOND);
	check("the retry is due at the minute", count == 1);
	check("the retry configures en0 with a reset", next_is(&lan, "en0", 1));
	wait = networkd_lan_retry_timeout(&lan, START + 60U * SECOND);
	check("a retry being made leaves nothing to wait for", wait == -1);

	/* It fails again; the next retry is a minute after that. */
	(void)networkd_lan_configured(&lan, "en0", 0);
	planned = networkd_lan_retry_schedule(&lan, "en0", START + 75U * SECOND);
	check("a failed retry plans the next", planned == 1);
	count = networkd_lan_retry_due(&lan, START + 135U * SECOND);
	check("the next retry is due a minute later", count == 1);
	check("the next retry resets en0 too", next_is(&lan, "en0", 1));

	/* The server answers: the retries end. */
	(void)networkd_lan_configured(&lan, "en0", 1);
	check("a lease ends the retries", state_of(&lan, "en0") == NETWORKD_LAN_CONFIGURED);
	wait = networkd_lan_retry_timeout(&lan, START + 200U * SECOND);
	check("nothing is waited for after a lease", wait == -1);
	count = networkd_lan_retry_due(&lan, START + 1000U * SECOND);
	check("a configured interface is not asked again", count == 0);
	check("nothing is configured after a lease", not_configuring(&lan, "en0"));
}

/*
 * A static interface whose configure failed is left alone, and so is
 * every interface while the daemon does not manage them.
 */
static void
test_static_not_retried(
	void)
{
	struct networkd_lan lan;
	int planned;
	int wait;

	/* en0 obtains its lease; en1 (static) is configured and fails. */
	managed(&lan);
	(void)networkd_lan_configured(&lan, "en0", 1);
	check("the static interface is configured next", next_is(&lan, "en1", 0));
	(void)networkd_lan_configured(&lan, "en1", 0);
	planned = networkd_lan_retry_schedule(&lan, "en1", START);
	check("a static interface gets no retry", planned == 0);
	wait = networkd_lan_retry_timeout(&lan, START);
	check("nothing is waited for a static interface", wait == -1);

	/* Management off: a failed DHCP interface gets no retry. */
	managed(&lan);
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_disable(&lan);
	planned = networkd_lan_retry_schedule(&lan, "en0", START);
	check("no retry while not managing", planned == 0);

	/* An interface not held is refused. */
	planned = networkd_lan_retry_schedule(&lan, "en9", START);
	check("an unknown interface is refused", planned == -1);
}

/*
 * A cable that goes, and an interface the administrator holds down, end
 * a planned retry.
 */
static void
test_cable_and_hold(
	void)
{
	struct networkd_lan lan;
	struct rtm_ifinfo item;
	enum networkd_lan_action action;
	int count;
	int wait;

	/* A planned retry, then the cable goes. */
	managed(&lan);
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_retry_schedule(&lan, "en0", START);
	item = event(1U, RTM_IFINFO_CARRIER_DOWN);
	action = networkd_lan_event(&lan, &item);
	check("a cable that goes takes en0 down", action == NETWORKD_LAN_ACTION_DOWN);
	wait = networkd_lan_retry_timeout(&lan, START);
	check("nothing is waited for without a cable", wait == -1);
	count = networkd_lan_retry_due(&lan, START + 60U * SECOND);
	check("no retry without a cable", count == 0);

	/* The cable comes back: a plain configure, as before. */
	item = event(1U, RTM_IFINFO_CARRIER_UP);
	(void)networkd_lan_event(&lan, &item);
	check("a cable that comes back configures en0 plainly", next_is(&lan, "en0", 0));

	/* A planned retry, then the administrator takes en0 down. */
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_retry_schedule(&lan, "en0", START);
	(void)networkd_lan_hold_down(&lan, "en0");
	count = networkd_lan_retry_due(&lan, START + 60U * SECOND);
	check("no retry while held down", count == 0);
	check("an interface held down is not configured", not_configuring(&lan, "en0"));
}

/*
 * `net up' configures plainly, as before; the down and up of a retry
 * change nothing in the policy until the reset is over.
 */
static void
test_up_and_reset_window(
	void)
{
	struct networkd_lan lan;
	struct rtm_ifinfo item;
	enum networkd_lan_action action;
	int planned;
	int count;

	/* A failed interface with a retry planned is brought up by the administrator. */
	managed(&lan);
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_retry_schedule(&lan, "en0", START);
	(void)networkd_lan_release(&lan, "en0");
	check("net up configures en0 without a reset", next_is(&lan, "en0", 0));
	count = networkd_lan_retry_due(&lan, START + 60U * SECOND);
	check("net up replaces the planned retry", count == 0);

	/* A retry under way: its down and up report the carrier going and coming. */
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_retry_schedule(&lan, "en0", START);
	(void)networkd_lan_retry_due(&lan, START + 60U * SECOND);
	(void)networkd_lan_reset_begin(&lan, "en0");
	item = event(1U, RTM_IFINFO_CARRIER_DOWN);
	action = networkd_lan_event(&lan, &item);
	check("the reset's carrier down asks nothing", action == NETWORKD_LAN_ACTION_NONE);
	check("the reset's carrier down keeps en0 pending", state_of(&lan, "en0") == NETWORKD_LAN_PENDING);

	/* A snapshot in the middle of the reset changes nothing either. */
	networkd_lan_snapshot_begin(&lan);
	(void)networkd_lan_observe(&lan, "en0", 1U, 1U, 0);
	(void)networkd_lan_observe(&lan, "en1", 2U, 1U, 1);
	networkd_lan_snapshot_end(&lan);
	check("a snapshot during the reset keeps en0 pending", state_of(&lan, "en0") == NETWORKD_LAN_PENDING);
	item = event(1U, RTM_IFINFO_CARRIER_UP);
	action = networkd_lan_event(&lan, &item);
	check("the reset's carrier up asks nothing", action == NETWORKD_LAN_ACTION_NONE);
	check("the retry is still the one to configure", next_is(&lan, "en0", 1));

	/* The retry ends; a snapshot that sees no cable then says the cable went. */
	(void)networkd_lan_configured(&lan, "en0", 0);
	(void)networkd_lan_reset_end(&lan, "en0");
	networkd_lan_snapshot_begin(&lan);
	(void)networkd_lan_observe(&lan, "en0", 1U, 1U, 0);
	(void)networkd_lan_observe(&lan, "en1", 2U, 1U, 1);
	networkd_lan_snapshot_end(&lan);
	check("a cable gone after the reset leaves en0 idle", state_of(&lan, "en0") == NETWORKD_LAN_IDLE);
	planned = networkd_lan_retry_schedule(&lan, "en0", START);
	check("an idle interface gets no retry", planned == 0);
	check("an idle interface is not configured", not_configuring(&lan, "en0"));
}
