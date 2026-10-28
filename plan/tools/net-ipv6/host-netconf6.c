/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws130-p005: the host test of net.conf's IPv6 (userland/base/net/netconf.c
 * with the host's headers): the ipv6 section's keys and static addresses,
 * an IPv6 route through a link-local gateway with its interface, an IPv6
 * DNS server, the defaults of an interface without the section, a round
 * trip through netconf_write, and the refused files.
 */

#include "userland/base/net/netconf.h"
#include "userland/base/net/reconcile.h"

#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);
static int parse_text(const char *text, struct netconf *configuration, char *error, size_t size);
static int record(const char *operation, const char *operands, void *context);

/* The operations the reconcile emitted, one a line. */
static char test_program[2048];

/* The file with every IPv6 key. */
static const char test_full[] =
	"version: 1\n"
	"\n"
	"interfaces:\n"
	"  ue0:\n"
	"    type: ethernet\n"
	"    enabled: true\n"
	"    ipv4:\n"
	"      dhcp: true\n"
	"    ipv6:\n"
	"      enabled: true\n"
	"      autoconf: false\n"
	"      dhcp: stateful\n"
	"      stable-address: false\n"
	"      temporary: false\n"
	"      addresses:\n"
	"        - address: 2001:db8::10\n"
	"          prefix-length: 64\n"
	"  lo0:\n"
	"    type: loopback\n"
	"    enabled: true\n"
	"\n"
	"routes:\n"
	"  - destination: ::/0\n"
	"    gateway: fe80::1\n"
	"    interface: ue0\n"
	"  - destination: default\n"
	"    gateway: 10.0.2.2\n"
	"\n"
	"dns:\n"
	"  mode: static\n"
	"  servers:\n"
	"    - 2001:db8::53\n"
	"    - 10.0.2.3\n";

/* Runs the checks; the exit status is 1 when one failed. */
int
main(void)
{
	struct netconf configuration;
	struct netconf again;
	const struct netconf_interface *ue0;
	const struct netconf_interface *lo0;
	char error[256];
	char written[4096];
	FILE *stream;
	size_t length;
	int status;

	/* Every key read. */
	status = parse_text(test_full, &configuration, error, sizeof(error));
	check(status == 0, "the full file is read");
	if (status != 0)
		printf("  error: %s\n", error);
	ue0 = &configuration.interfaces[0];
	lo0 = &configuration.interfaces[1];
	check(netconf_ipv6_enabled(ue0) == 1 && netconf_ipv6_autoconf(ue0) == 0, "ue0: IPv6 on, SLAAC off");
	check(netconf_ipv6_dhcp(ue0) == NETCONF_IPV6_DHCP_STATEFUL, "ue0: DHCPv6 stateful");
	check(netconf_ipv6_stable_address(ue0) == 0 && netconf_ipv6_temporary(ue0) == 0, "ue0: no stable or temporary address");
	check(ue0->ipv6.address_count == 1U && strcmp(ue0->ipv6.addresses[0].address, "2001:db8::10") == 0, "ue0: its static IPv6 address");
	check(ue0->ipv6.addresses[0].prefix_length == 64U, "ue0: its prefix length 64");
	check(netconf_ipv6_enabled(lo0) == 1 && netconf_ipv6_autoconf(lo0) == 1 && netconf_ipv6_dhcp(lo0) == NETCONF_IPV6_DHCP_AUTO, "lo0: the defaults");
	check(configuration.route_count == 2U && strcmp(configuration.routes[0].interface, "ue0") == 0, "the IPv6 route's interface");
	check(strcmp(configuration.dns_servers[0], "2001:db8::53") == 0, "an IPv6 DNS server");

	/* Written and read again: the same. */
	stream = fmemopen(written, sizeof(written), "w");
	status = -1;
	if (stream != NULL) {
		status = netconf_write(stream, &configuration);
		fclose(stream);
	}
	check(status == 0, "written");
	length = strnlen(written, sizeof(written));
	status = length < sizeof(written) ? parse_text(written, &again, error, sizeof(error)) : -1;
	check(status == 0, "read again");
	check(memcmp(&again.interfaces[0].ipv6, &ue0->ipv6, sizeof(ue0->ipv6)) == 0, "the IPv6 section kept");
	check(strstr(written, "  lo0:\n    type: loopback\n    enabled: true\n\n") != NULL, "lo0 written without an IPv6 section");
	check(strcmp(again.routes[0].interface, "ue0") == 0, "the route's interface kept");

	/* The reconcile's program: IPv6 on, the static address, the IPv6 route after the IPv4 one, both DNS servers. */
	test_program[0] = '\0';
	status = netconf_reconcile(&configuration, &configuration, record, NULL, error, sizeof(error));
	check(status == 0, "reconciled");
	if (status != 0)
		printf("  error: %s\n", error);
	check(strstr(test_program, "DEFAULTROUTE_CLEAR\nDNS_CLEAR\nROUTE6_CLEAR\n") == test_program, "the old routes and DNS cleared first, IPv6's too");
	check(strstr(test_program, "UP ue0\nIPV6 ue0 on\nSTATIC6 ue0 2001:db8::10/64\nDHCP ue0 10\n") != NULL, "ue0: up, IPv6 on, its address, DHCP");
	check(strstr(test_program, "DEFAULTROUTE 10.0.2.2\nROUTE6 ::/0 fe80::1 ue0\nDNS 2001:db8::53 10.0.2.3\n") != NULL, "the routes, then the DNS");

	/* Refused: an IPv6 route through an IPv4 gateway. */
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\nroutes:\n  - destination: 2001:db8::/32\n    gateway: 10.0.2.2\ndns:\n  mode: dhcp\n", &again, error, sizeof(error));
	check(status != 0, "refused: an IPv6 route through an IPv4 gateway");

	/* Refused: a link-local gateway without its interface. */
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\nroutes:\n  - destination: ::/0\n    gateway: fe80::1\ndns:\n  mode: dhcp\n", &again, error, sizeof(error));
	check(status != 0, "refused: a link-local gateway without its interface");

	/* Refused: a prefix length past 128, and an unknown key. */
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    ipv6:\n      addresses:\n        - address: 2001:db8::1\n          prefix-length: 129\ndns:\n  mode: dhcp\n", &again, error, sizeof(error));
	check(status != 0, "refused: prefix-length 129");
	status = parse_text("version: 1\ninterfaces:\n  ue0:\n    type: ethernet\n    enabled: true\n    ipv6:\n      privacy: true\ndns:\n  mode: dhcp\n", &again, error, sizeof(error));
	check(status != 0, "refused: an unknown IPv6 key");

	/* The result. */
	if (test_failures != 0) {
		printf("host-netconf6: %d failed\n", test_failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-netconf6: PASS\n");
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

/* Reads a net.conf text and validates it; 0, or -1 with the error. */
static int
parse_text(
	const char *text,
	struct netconf *configuration,
	char *error,
	size_t size)
{
	FILE *stream;
	int status;

	/* The text as a stream. */
	stream = fmemopen((void *)text, strlen(text), "r");
	if (stream == NULL)
		return -1;
	status = netconf_parse(stream, configuration, error, size);
	fclose(stream);
	if (status != 0)
		return status;

	/* Succeeded when it is valid. */
	status = netconf_validate(configuration, error, size);
	return status;
}

/* Records one operation of the reconcile's program. */
static int
record(
	const char *operation,
	const char *operands,
	void *context)
{
	size_t used;

	/* After the ones before it. */
	(void)context;
	used = strlen(test_program);
	(void)snprintf(test_program + used, sizeof(test_program) - used, "%s%s%s\n", operation, operands != NULL ? " " : "",
	    operands != NULL ? operands : "");
	return 0;
}
