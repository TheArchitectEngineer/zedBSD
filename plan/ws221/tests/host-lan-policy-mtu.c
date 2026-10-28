/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the MTU in the wired policy: the LAN_ENABLE record
 * (userland/base/net/protocol.c) written and read back in every form,
 * the malformed ones refused; the Settings edit of net.conf
 * (userland/base/networkd/lan-configure.c) keeping an interface's MTU;
 * and the policy (userland/base/networkd/managed-lan.c) recording the
 * MTU an interface was given.
 */

#include "userland/base/net/netconf.h"
#include "userland/base/net/protocol.h"
#include "userland/base/networkd/lan-configure.h"
#include "userland/base/networkd/managed-lan.h"

#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);
static int round_trip(const struct networkd_lan_record *record, const char *expected);
static int parses(const char *text);

/* Runs the checks; the exit status is 1 when one failed. */
int
main(void)
{
	struct networkd_lan_record record;
	struct netconf configuration;
	struct netconf_interface *item;
	struct networkd_lan_configure configure;
	struct networkd_lan_policy policy[2];
	struct networkd_lan lan;
	char text[NETWORKD_LAN_RECORD_MAX];
	char diagnostic[160];
	size_t length;
	int status;

	/* Each form, with and without an MTU. */
	memset(&record, 0, sizeof(record));
	strcpy(record.interface, "ue0");
	record.mode = NETWORKD_LAN_RECORD_DHCP;
	record.dhcp_timeout = 10U;
	check(round_trip(&record, "ue0 dhcp 10") == 0, "dhcp without an MTU");
	record.mtu = 9000U;
	check(round_trip(&record, "ue0 dhcp 10 mtu 9000") == 0, "dhcp with mtu 9000");
	record.mode = NETWORKD_LAN_RECORD_STATIC;
	record.dhcp_timeout = 0U;
	strcpy(record.address, "192.168.100.100");
	strcpy(record.netmask, "255.255.255.0");
	record.mtu = 0U;
	check(round_trip(&record, "ue0 static 192.168.100.100 255.255.255.0") == 0, "static without an MTU");
	record.mtu = 1280U;
	check(round_trip(&record, "ue0 static 192.168.100.100 255.255.255.0 mtu 1280") == 0, "static with mtu 1280");
	memset(&record, 0, sizeof(record));
	strcpy(record.interface, "ue1");
	record.mode = NETWORKD_LAN_RECORD_DISABLED;
	check(round_trip(&record, "ue1 disabled") == 0, "disabled");
	record.mtu = 9000U;
	status = networkd_lan_record_format(&record, text, sizeof(text), &length);
	check(status == 0 && length == strlen("ue1 disabled") && memcmp(text, "ue1 disabled", length) == 0,
	    "disabled: no MTU written even when one is named");

	/* Written: an MTU out of the bounds refused, and one too long for the buffer. */
	memset(&record, 0, sizeof(record));
	strcpy(record.interface, "ue0");
	record.mode = NETWORKD_LAN_RECORD_DHCP;
	record.dhcp_timeout = 10U;
	record.mtu = 1279U;
	check(networkd_lan_record_format(&record, text, sizeof(text), &length) != 0, "format refuses mtu 1279");
	record.mtu = 65536U;
	check(networkd_lan_record_format(&record, text, sizeof(text), &length) != 0, "format refuses mtu 65536");
	record.mtu = 9000U;
	check(networkd_lan_record_format(&record, text, 16U, &length) != 0, "format refuses a buffer too short");

	/* Read: the forms the old daemon read, still. */
	check(parses("ue0 dhcp 10") == 0 && parses("ue0 static 10.0.0.2 255.255.0.0") == 0 && parses("ue0 disabled") == 0,
	    "the forms without an MTU read");

	/* Read: the malformed ones refused. */
	check(parses("ue0 dhcp 10 mtu") != 0, "refused: dhcp with mtu and no value");
	check(parses("ue0 dhcp 10 size 9000") != 0, "refused: dhcp with another key");
	check(parses("ue0 dhcp 10 mtu 1279") != 0, "refused: mtu 1279");
	check(parses("ue0 dhcp 10 mtu 65536") != 0, "refused: mtu 65536");
	check(parses("ue0 dhcp 10 mtu 9k") != 0, "refused: mtu 9k");
	check(parses("ue0 dhcp 10 mtu 9000 x") != 0, "refused: a word after the MTU");
	check(parses("ue0 static 10.0.0.2 255.255.0.0 mtu") != 0, "refused: static with mtu and no value");
	check(parses("ue0 disabled mtu 9000") != 0, "refused: disabled with an MTU");
	check(parses("ue0 dhcp 0") != 0 && parses("ue0 dhcp 3601") != 0, "refused: dhcp timeouts 0 and 3601");
	check(parses("ue0") != 0 && parses("ue0 unknown") != 0, "refused: no mode, an unknown mode");
	check(parses("ue0123456789abcdef dhcp 10") != 0, "refused: a name too long for an interface");
	check(networkd_lan_record_parse("ue0 dhcp\0 10", 11U, &record) != 0, "refused: a NUL inside");

	/* Settings: changing ue0 from DHCP to static keeps its MTU. */
	memset(&configuration, 0, sizeof(configuration));
	configuration.version = 1;
	configuration.dns_mode = NETCONF_DNS_DHCP;
	configuration.interface_count = 1U;
	item = &configuration.interfaces[0];
	strcpy(item->name, "ue0");
	item->type = NETCONF_INTERFACE_ETHERNET;
	item->enabled = 1;
	item->enabled_set = 1;
	item->dhcp = 1;
	item->dhcp_set = 1;
	item->mtu = 9000U;
	item->mtu_set = 1;
	memset(&configure, 0, sizeof(configure));
	configure.interface = "ue0";
	configure.address = "192.168.100.100";
	configure.netmask = "255.255.255.0";
	configure.gateway = "";
	status = networkd_lan_configure_edit(&configuration, &configure, diagnostic, sizeof(diagnostic));
	check(status == 0 && item->dhcp == 0 && item->address_count == 1U, "Settings: ue0 made static");
	check(item->mtu_set && item->mtu == 9000U, "Settings: ue0's mtu 9000 kept");
	configure.address = "";
	configure.netmask = "";
	status = networkd_lan_configure_edit(&configuration, &configure, diagnostic, sizeof(diagnostic));
	check(status == 0 && item->dhcp == 1 && item->mtu_set && item->mtu == 9000U, "Settings: back to DHCP, the mtu kept");

	/* The policy records the MTU an interface was given, and only for one it names. */
	memset(&lan, 0, sizeof(lan));
	memset(policy, 0, sizeof(policy));
	strcpy(policy[0].interface, "ue0");
	policy[0].mode = NETWORKD_LAN_MODE_DHCP;
	policy[0].dhcp_timeout = 10U;
	policy[0].mtu = 9000U;
	strcpy(policy[1].interface, "ue1");
	policy[1].mode = NETWORKD_LAN_MODE_DHCP;
	policy[1].dhcp_timeout = 10U;
	check(networkd_lan_set_policy(&lan, policy, 2U) == 0 && lan.policy[0].mtu == 9000U && lan.policy[1].mtu == 0U,
	    "policy: the MTUs as set");
	check(networkd_lan_set_mtu(&lan, "ue1", 1400U) == 0 && lan.policy[1].mtu == 1400U, "policy: ue1 given 1400");
	check(networkd_lan_set_mtu(&lan, "ue0", 0U) == 0 && lan.policy[0].mtu == 0U, "policy: ue0 back to the default");
	check(networkd_lan_set_mtu(&lan, "ue7", 1400U) == 0 && lan.policy_count == 2U, "policy: an interface it does not name is left alone");
	check(networkd_lan_set_mtu(&lan, "", 1400U) != 0 && networkd_lan_set_mtu(&lan, "ue0123456789abcdef", 1400U) != 0,
	    "policy: an empty name and one too long refused");

	/* The result. */
	if (test_failures != 0) {
		printf("host-lan-policy-mtu: %d failed\n", test_failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-lan-policy-mtu: PASS\n");
	return 0;
}

/* Counts a failed check, and prints each check's outcome. */
static void
check(
	int condition,
	const char *what)
{
	/* A failure. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		test_failures++;
		return;
	}

	/* Succeeded. */
	printf("ok: %s\n", what);
}

/* Writes a record, compares the text, reads it back and compares the fields; 0 when all agree. */
static int
round_trip(
	const struct networkd_lan_record *record,
	const char *expected)
{
	struct networkd_lan_record again;
	char text[NETWORKD_LAN_RECORD_MAX];
	size_t length;

	/* The text. */
	if (networkd_lan_record_format(record, text, sizeof(text), &length) != 0)
		return -1;
	if (length != strlen(expected) || memcmp(text, expected, length) != 0) {
		printf("  wrote: %.*s\n", (int)length, text);
		return -1;
	}

	/* The fields read back. */
	if (networkd_lan_record_parse(text, length, &again) != 0)
		return -1;
	if (strcmp(again.interface, record->interface) != 0 || again.mode != record->mode ||
	    strcmp(again.address, record->address) != 0 || strcmp(again.netmask, record->netmask) != 0 ||
	    again.dhcp_timeout != record->dhcp_timeout || again.mtu != record->mtu)
		return -1;

	/* Agreed. */
	return 0;
}

/* Reads one record text; 0 when it is read. */
static int
parses(
	const char *text)
{
	struct networkd_lan_record record;

	/* The record as the daemon reads it. */
	return networkd_lan_record_parse(text, strlen(text), &record);
}
