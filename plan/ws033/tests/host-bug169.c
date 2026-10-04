/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of BUG-169: the zedBSD backend's reading of networkd's
 * state (userland/desktop/libkeiland-backend-zedbsd/network-zedbsd.c,
 * included unchanged) never takes the WLAN interface for a wired one, also
 * while no Wi-Fi connection names it (networkd's "radio=" field).
 *
 *   sh plan/ws033/tests/host-bug169.sh
 */

#include "userland/desktop/libkeiland-backend-zedbsd/network-zedbsd.c"

#include <stdio.h>

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

static void check(int condition, const char *what);
static void parse(const char *text, struct kl_backend_network_state *state);

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

/* Reads a daemon's state text. */
static void
parse(
	const char *text,
	struct kl_backend_network_state *state)
{
	/* The whole text. */
	network_parse_state(state, text, strlen(text));
}

/* Runs every case. */
int
main(void)
{
	struct kl_backend_network_state state;

	/* 1. The cable pulled, the Wi-Fi up with an address but no connection named (BUG-169): wlan0 is no wired interface. */
	parse("lo0 static online\nwlan0 static online\nwifi state=idle interface=- ssid=- radios=1 scan=0 radio=wlan0\nroute default=wlan0\n", &state);
	check(state.wired[0] == '\0', "the radio is not the wired interface");
	check(state.kind != KL_BACKEND_NETWORK_WIRED, "the connection is not wired");

	/* 2. The cable in again: ue0 is the wired interface, wlan0 still not. */
	parse("lo0 static online\nwlan0 static online\nue0 static online\nwifi state=idle interface=- ssid=- radios=1 scan=0 radio=wlan0\nroute default=ue0\n", &state);
	check(strcmp(state.wired, "ue0") == 0, "the cable's interface is the wired one");
	check(state.kind == KL_BACKEND_NETWORK_WIRED && strcmp(state.interface, "ue0") == 0, "the connection is ue0's");

	/* 3. A connected Wi-Fi, as before. */
	parse("lo0 static online\nwlan0 static online\nwifi state=connected interface=wlan0 ssid=6b6569 radios=1 scan=0 radio=wlan0\nroute default=wlan0\n", &state);
	check(state.kind == KL_BACKEND_NETWORK_WIFI && strcmp(state.interface, "wlan0") == 0, "a connected Wi-Fi carries it");
	check(state.wired[0] == '\0', "no wired interface with the Wi-Fi");

	/* 4. An older daemon without radio=: as before (the connection's interface excludes the radio). */
	parse("lo0 static online\nem0 static online\nwifi state=connected interface=wlan0 ssid=6b6569 radios=1 scan=0\nroute default=em0\n", &state);
	check(strcmp(state.wired, "em0") == 0 && state.kind == KL_BACKEND_NETWORK_WIRED, "without radio= the old reading");

	/* 5. No radio at all. */
	parse("lo0 static online\nue0 static online\nwifi state=idle interface=- ssid=- radios=0 scan=0 radio=-\nroute default=ue0\n", &state);
	check(state.wifi == KL_BACKEND_WIFI_ABSENT && strcmp(state.wired, "ue0") == 0, "no radio: ue0 wired");

	/* The result. */
	if (failures != 0) {
		printf("host-bug169: %d of %d checks FAILED\n", failures, checks);
		return 1;
	}

	/* Succeeded. */
	printf("host-bug169: ok (%d checks)\n", checks);
	return 0;
}
