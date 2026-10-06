/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the system bar's network details (ws099-p032):
 * userland/desktop/wayland/network-info.c alone, on made-up states and
 * readings.  It checks the rows of a connected Wi-Fi (title, network,
 * interface, status, address, mask, two DNS servers of three, MAC, MTU,
 * signal, bytes), the rates of a second reading one second later, a
 * counter that went back (no rate), a wired connection (no signal, no
 * network row), nothing connected (the Wi-Fi's interface, "Not
 * connected"), the daemon not running, no interface at all, the capacity,
 * and the byte units.
 *
 *   sh plan/ws099/tests/host-network-info.sh
 */

#include "userland/desktop/wayland/network-info.h"

#include <stdio.h>
#include <string.h>

static void check(int condition, const char *what);
static const char *value_of(const struct kwl_network_info_row *rows, unsigned count, const char *label);

/* The checks that failed, and those that ran. */
static int failures;
static int checks;

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* The value of the row of a label, or "" when there is none. */
static const char *
value_of(
	const struct kwl_network_info_row *rows,
	unsigned count,
	const char *label)
{
	unsigned index;
	int same;

	/* The row of the label. */
	for (index = 0; index < count; index++) {
		same = strcmp(rows[index].label, label);
		if (same == 0)
			return rows[index].value;
	}

	/* None. */
	return "";
}

int
main(void)
{
	static const char dns[3][KL_BACKEND_NETWORK_ADDRESS_MAX] = { "10.0.2.3", "1.1.1.1", "8.8.8.8" };
	struct kl_backend_network_state state;
	struct kl_backend_network_ap scan[2];
	struct kl_backend_network_link links[3];
	struct kwl_network_info_input input;
	struct kwl_network_info_sample sample;
	struct kwl_network_info_row rows[KWL_NETWORK_INFO_ROWS];
	char text[32];
	unsigned count;

	/* A connected Wi-Fi on wlan0, with the loopback and a wired interface that is down. */
	memset(&state, 0, sizeof(state));
	state.reachable = 1;
	state.connected = 1;
	state.kind = KL_BACKEND_NETWORK_WIFI;
	strcpy(state.interface, "wlan0");
	strcpy(state.wifi_interface, "wlan0");
	state.wifi = KL_BACKEND_WIFI_CONNECTED;
	strcpy(state.ssid, "Kei Lab");
	memset(scan, 0, sizeof(scan));
	strcpy(scan[0].ssid, "Other");
	scan[0].rssi = -80;
	strcpy(scan[1].ssid, "Kei Lab");
	scan[1].rssi = -52;
	memset(links, 0, sizeof(links));
	strcpy(links[0].name, "lo0");
	links[0].loopback = 1;
	strcpy(links[1].name, "ue0");
	strcpy(links[2].name, "wlan0");
	links[2].up = 1;
	links[2].running = 1;
	strcpy(links[2].address, "192.168.1.20");
	strcpy(links[2].netmask, "255.255.255.0");
	memcpy(links[2].hardware, "\x02\x11\x22\x33\x44\x55", 6U);
	links[2].mtu = 1500;
	links[2].received_bytes = 3U * 1024U * 1024U;
	links[2].sent_bytes = 512U;
	memset(&input, 0, sizeof(input));
	input.state = &state;
	input.scan = scan;
	input.scan_count = 2;
	input.links = links;
	input.link_count = 3;
	input.dns = dns;
	input.dns_count = 3;
	input.now_ms = 10000;
	memset(&sample, 0, sizeof(sample));
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(count == 12U, "twelve rows for a connected Wi-Fi");
	check(rows[0].label[0] == '\0' && strcmp(rows[0].value, "Wi-Fi") == 0, "the title Wi-Fi");
	check(strcmp(value_of(rows, count, "Network"), "Kei Lab") == 0, "the network");
	check(strcmp(value_of(rows, count, "Interface"), "wlan0") == 0, "the interface carrying it");
	check(strcmp(value_of(rows, count, "Status"), "Connected") == 0, "connected");
	check(strcmp(value_of(rows, count, "IPv4 address"), "192.168.1.20") == 0, "the address");
	check(strcmp(value_of(rows, count, "Subnet mask"), "255.255.255.0") == 0, "the mask");
	check(strcmp(value_of(rows, count, "DNS"), "10.0.2.3, 1.1.1.1") == 0, "two DNS servers");
	check(strcmp(value_of(rows, count, "MAC address"), "02:11:22:33:44:55") == 0, "the MAC address");
	check(strcmp(value_of(rows, count, "MTU"), "1500") == 0, "the MTU");
	check(strcmp(value_of(rows, count, "Signal"), "-52 dBm") == 0, "the signal of its network");
	check(strcmp(value_of(rows, count, "Received"), "3.0 MB") == 0, "received, no rate at the first reading");
	check(strcmp(value_of(rows, count, "Sent"), "512 B") == 0, "sent");
	check(sample.valid == 1U && strcmp(sample.name, "wlan0") == 0, "the sample kept");

	/* A second reading a second later: the rates. */
	links[2].received_bytes += 2048U;
	links[2].sent_bytes += 100U;
	input.now_ms = 11000;
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(strcmp(value_of(rows, count, "Received"), "3.0 MB (2.0 KB/s)") == 0, "the received rate");
	check(strcmp(value_of(rows, count, "Sent"), "612 B (100 B/s)") == 0, "the sent rate");

	/* A counter that went back shows no rate. */
	links[2].sent_bytes = 10U;
	input.now_ms = 12000;
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(strcmp(value_of(rows, count, "Sent"), "10 B") == 0, "no rate from a counter that went back");

	/* Wired: no network row, no signal; the interface is the wired one. */
	state.kind = KL_BACKEND_NETWORK_WIRED;
	strcpy(state.interface, "ue0");
	strcpy(state.wired, "ue0");
	strcpy(links[1].address, "10.0.2.15");
	links[1].mtu = 1500;
	input.now_ms = 13000;
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(strcmp(rows[0].value, "Ethernet") == 0, "the title Ethernet");
	check(strcmp(value_of(rows, count, "Interface"), "ue0") == 0, "the wired interface");
	check(value_of(rows, count, "Network")[0] == '\0' && value_of(rows, count, "Signal")[0] == '\0', "no network, no signal");
	check(value_of(rows, count, "MAC address")[0] == '\0', "no MAC address row for a zero address");
	check(strcmp(value_of(rows, count, "Received"), "0 B") == 0, "another interface: no rate");

	/* Nothing connected: the Wi-Fi's interface, not connected. */
	state.connected = 0;
	state.kind = KL_BACKEND_NETWORK_NONE;
	state.interface[0] = '\0';
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(strcmp(rows[0].value, "Network") == 0, "the title Network");
	check(strcmp(value_of(rows, count, "Interface"), "wlan0") == 0, "the Wi-Fi's interface");
	check(strcmp(value_of(rows, count, "Status"), "Not connected") == 0, "not connected");

	/* The daemon not running. */
	state.reachable = 0;
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(strcmp(value_of(rows, count, "Status"), "Network service not running") == 0, "the service not running");

	/* No interface: title and status (and the DNS), and no sample. */
	input.link_count = 1;
	state.wifi_interface[0] = '\0';
	state.wired[0] = '\0';
	count = kwl_network_info_build(&input, &sample, rows, KWL_NETWORK_INFO_ROWS);
	check(count == 3U && sample.valid == 0U, "only the title, the status and the DNS without an interface");

	/* The capacity. */
	input.link_count = 3;
	strcpy(state.wifi_interface, "wlan0");
	count = kwl_network_info_build(&input, &sample, rows, 4U);
	check(count == 4U, "no more rows than the capacity");

	/* The units. */
	kwl_network_info_bytes(1023U, text, sizeof(text));
	check(strcmp(text, "1023 B") == 0, "bytes");
	kwl_network_info_bytes(1536U, text, sizeof(text));
	check(strcmp(text, "1.5 KB") == 0, "kilobytes");
	kwl_network_info_bytes((uint64_t)5U * 1024U * 1024U * 1024U, text, sizeof(text));
	check(strcmp(text, "5.0 GB") == 0, "gigabytes");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-network-info: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-network-info: %d checks passed\n", checks);
	return 0;
}
