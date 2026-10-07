/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws052-p010: the host test of networkd's sleep record
 * (userland/base/networkd/sleep-state.c, compiled unchanged; managed-wlan.c
 * needs the zedBSD network headers and is not built on the host, so its
 * networkd_managed_wlan_resume is reviewed only).  The cases:
 *   1. a PREPARE records the policy and begins the sleep; a second one
 *      keeps the first record and only moves the end;
 *   2. the end gives what each recorded state takes up again (off: none;
 *      disconnected by the user: manual; searching, connecting, connected,
 *      reconnecting: search; retiring: the state the retirement goes to);
 *   3. an END while awake takes nothing up;
 *   4. the safety's end comes after NETWORKD_SLEEP_SAFETY_SECONDS, and the
 *      poll timeout counts down to it;
 *   5. while asleep the Wi-Fi changes and CONFIRMED_ARM are refused, the
 *      rest is served; awake, everything is served.
 * Prints "host-networkd-sleep: PASS" or the failed checks and FAIL.
 *
 *   sh plan/ws052/tests/run-host-networkd-sleep.sh
 */

#include "userland/base/networkd/sleep-state.h"
#include "userland/base/net/protocol.h"

#include <stdio.h>

/* How many checks ran and how many failed. */
static unsigned checks;
static unsigned failures;

static void check(int passed, const char *what);
static void case_begin(void);
static void case_resume(void);
static void case_safety(void);
static void case_admits(void);

/* Runs every case and prints the verdict. */
int
main(void)
{
	/* Each case. */
	case_begin();
	case_resume();
	case_safety();
	case_admits();

	/* The verdict. */
	printf("host-networkd-sleep: checks=%u failures=%u\n", checks, failures);
	if (failures != 0U) {
		printf("host-networkd-sleep: FAIL\n");
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-networkd-sleep: PASS\n");
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

/* Case 1: the record and the idempotent PREPARE. */
static void
case_begin(void)
{
	struct networkd_sleep record;
	int began;

	/* Awake at the start. */
	networkd_sleep_init(&record);
	check(!record.asleep, "1: awake at the start");

	/* The first PREPARE records the connected policy. */
	began = networkd_sleep_begin(&record, NETWORKD_WLAN_CONNECTED, NETWORKD_WLAN_AUTO_SEARCHING, 1000U);
	check(began == 1 && record.asleep && record.recorded == NETWORKD_WLAN_CONNECTED, "1: the first PREPARE records CONNECTED");
	check(record.deadline_us == 1000U + (uint64_t)NETWORKD_SLEEP_SAFETY_SECONDS * 1000000ULL, "1: the end is the safety's time after it");

	/* The second keeps the record (the policy is MANUAL_DISCONNECTED by then) and moves the end. */
	began = networkd_sleep_begin(&record, NETWORKD_WLAN_MANUAL_DISCONNECTED, NETWORKD_WLAN_MANUAL_DISCONNECTED, 5000U);
	check(began == 0 && record.recorded == NETWORKD_WLAN_CONNECTED, "1: a second PREPARE keeps the first record");
	check(record.deadline_us == 5000U + (uint64_t)NETWORKD_SLEEP_SAFETY_SECONDS * 1000000ULL, "1: a second PREPARE moves the end");
}

/* Case 2 and 3: what each recorded state takes up again, and an END while awake. */
static void
case_resume(void)
{
	static const struct {
		enum networkd_managed_wlan_state state;
		enum networkd_managed_wlan_state target;
		enum networkd_sleep_resume resume;
		const char *what;
	} rows[] = {
		{ NETWORKD_WLAN_DISABLED, NETWORKD_WLAN_DISABLED, NETWORKD_SLEEP_RESUME_NONE, "2: off takes nothing up" },
		{ NETWORKD_WLAN_MANUAL_DISCONNECTED, NETWORKD_WLAN_MANUAL_DISCONNECTED, NETWORKD_SLEEP_RESUME_MANUAL, "2: disconnected by the user: the radios up, no connection" },
		{ NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_SLEEP_RESUME_SEARCH, "2: searching: the search" },
		{ NETWORKD_WLAN_CONNECTING, NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_SLEEP_RESUME_SEARCH, "2: connecting: the search" },
		{ NETWORKD_WLAN_CONNECTED, NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_SLEEP_RESUME_SEARCH, "2: connected: the search" },
		{ NETWORKD_WLAN_RECONNECTING, NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_SLEEP_RESUME_SEARCH, "2: reconnecting: the search" },
		{ NETWORKD_WLAN_RETIRING, NETWORKD_WLAN_MANUAL_DISCONNECTED, NETWORKD_SLEEP_RESUME_MANUAL, "2: retiring to disconnected: manual" },
		{ NETWORKD_WLAN_RETIRING, NETWORKD_WLAN_AUTO_SEARCHING, NETWORKD_SLEEP_RESUME_SEARCH, "2: retiring to the search: the search" }
	};
	struct networkd_sleep record;
	enum networkd_sleep_resume resume;
	unsigned index;

	/* Each row: begin, end, and awake again. */
	for (index = 0U; index < sizeof(rows) / sizeof(rows[0]); index++) {
		networkd_sleep_init(&record);
		(void)networkd_sleep_begin(&record, rows[index].state, rows[index].target, 0U);
		resume = networkd_sleep_end(&record);
		check(resume == rows[index].resume && !record.asleep, rows[index].what);
	}

	/* An END while awake takes nothing up. */
	networkd_sleep_init(&record);
	resume = networkd_sleep_end(&record);
	check(resume == NETWORKD_SLEEP_RESUME_NONE && !record.asleep, "3: an END while awake takes nothing up");
}

/* Case 4: the safety's end and the poll timeout. */
static void
case_safety(void)
{
	struct networkd_sleep record;
	uint64_t end;
	int timeout;
	int due;

	/* Awake: no timeout, never due. */
	networkd_sleep_init(&record);
	timeout = networkd_sleep_poll_timeout(&record, 0U);
	due = networkd_sleep_due(&record, ~(uint64_t)0);
	check(timeout == -1 && !due, "4: awake has no end");

	/* Asleep: the time left, rounded up, then due. */
	(void)networkd_sleep_begin(&record, NETWORKD_WLAN_CONNECTED, NETWORKD_WLAN_AUTO_SEARCHING, 0U);
	end = (uint64_t)NETWORKD_SLEEP_SAFETY_SECONDS * 1000000ULL;
	timeout = networkd_sleep_poll_timeout(&record, end - 1500U);
	due = networkd_sleep_due(&record, end - 1U);
	check(timeout == 2 && !due, "4: 1.5 ms before the end waits 2 ms and is not due");
	timeout = networkd_sleep_poll_timeout(&record, end);
	due = networkd_sleep_due(&record, end);
	check(timeout == 0 && due, "4: at the end the sleep is due");
}

/* Case 5: the requests served while asleep. */
static void
case_admits(void)
{
	static const uint32_t refused[] = {
		NETWORKD_OP_WIFI_ENABLE, NETWORKD_OP_WIFI_DISABLE, NETWORKD_OP_WIFI_CONNECT,
		NETWORKD_OP_WIFI_DISCONNECT, NETWORKD_OP_CONFIRMED_ARM
	};
	static const uint32_t served[] = {
		NETWORKD_OP_SHOW, NETWORKD_OP_UP, NETWORKD_OP_DHCP, NETWORKD_OP_WIFI_LIST,
		NETWORKD_OP_WIFI_PROFILES_CHANGED, NETWORKD_OP_WIFI_SESSION_OPEN, NETWORKD_OP_WIFI_SESSION_CLOSE,
		NETWORKD_OP_WIFI_SCAN_START, NETWORKD_OP_WIFI_SCAN_STOP, NETWORKD_OP_LAN_CONFIGURE,
		NETWORKD_OP_CONFIRMED_CHECK, NETWORKD_OP_SLEEP_PREPARE, NETWORKD_OP_SLEEP_END
	};
	struct networkd_sleep record;
	unsigned index;
	int admitted;
	int all;

	/* Awake: everything, the Wi-Fi changes too. */
	networkd_sleep_init(&record);
	all = 1;
	for (index = 0U; index < sizeof(refused) / sizeof(refused[0]); index++) {
		admitted = networkd_sleep_admits(&record, refused[index]);
		if (!admitted)
			all = 0;
	}
	check(all, "5: awake serves the Wi-Fi changes");

	/* Asleep: the Wi-Fi changes and CONFIRMED_ARM are refused. */
	(void)networkd_sleep_begin(&record, NETWORKD_WLAN_CONNECTED, NETWORKD_WLAN_AUTO_SEARCHING, 0U);
	all = 1;
	for (index = 0U; index < sizeof(refused) / sizeof(refused[0]); index++) {
		admitted = networkd_sleep_admits(&record, refused[index]);
		if (admitted)
			all = 0;
	}
	check(all, "5: asleep refuses the Wi-Fi changes and CONFIRMED_ARM");

	/* Asleep: the rest is served. */
	all = 1;
	for (index = 0U; index < sizeof(served) / sizeof(served[0]); index++) {
		admitted = networkd_sleep_admits(&record, served[index]);
		if (!admitted)
			all = 0;
	}
	check(all, "5: asleep serves the wired requests, the lists, the leases, the sessions and the sleep's own");
}
