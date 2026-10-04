/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws089-p022: the host test of networkd's wired configuration
 * (userland/base/networkd/lan-configure.c with userland/base/net/netconf.c,
 * both compiled unchanged).
 *   - Good configurations pass: DHCP, DHCP with name servers, a static
 *     address with and without a router.
 *   - Bad input is refused (Q1 2026-10-05: strict checks): names that are no
 *     wired interface's, addresses that are not dotted IPv4 or not a
 *     machine's, netmasks that are not contiguous or too long, the subnet's
 *     own and broadcast address, routers outside the subnet, a netmask or
 *     router with DHCP, bad name servers, three name servers.
 *   - The edit: a new interface is added, a static one gets its address and
 *     the default route, DHCP drops it unless another interface keeps a
 *     static address, the name servers' mode follows; the file written
 *     reads back the same and is valid; the other entries are kept.
 * Prints one line a check and "host-lan-configure: PASS" or FAIL.
 *
 *   sh plan/ws089/tests/run-host-lan-configure.sh
 */

#include "userland/base/networkd/lan-configure.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *what, int passed);
static int accepted(const char *interface, const char *address, const char *netmask, const char *gateway, const char *dns1, const char *dns2, size_t dns_count);
static void base(struct netconf *configuration);
static int find(const struct netconf *configuration, const char *name);
static int has_default(const struct netconf *configuration, const char *gateway);

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

/* Tells whether a configuration passes the checks. */
static int
accepted(
	const char *interface,
	const char *address,
	const char *netmask,
	const char *gateway,
	const char *dns1,
	const char *dns2,
	size_t dns_count)
{
	struct networkd_lan_configure request;
	char diagnostic[160];
	int result;

	/* The request. */
	memset(&request, 0, sizeof(request));
	request.interface = interface;
	request.address = address;
	request.netmask = netmask;
	request.gateway = gateway;
	request.dns[0] = dns1;
	request.dns[1] = dns2;
	request.dns_count = dns_count;

	/* Checked. */
	diagnostic[0] = '\0';
	result = networkd_lan_configure_check(&request, diagnostic, sizeof(diagnostic));
	if (result != 0 && errno != EINVAL)
		return -1;

	/* Passed, or why not. */
	if (result == 0)
		return 1;
	printf("  refused (%s)\n", diagnostic);
	return 0;
}

/* A configuration as the default net.conf, with a static em1 kept. */
static void
base(
	struct netconf *configuration)
{
	struct netconf_interface *item;

	/* The loopback. */
	memset(configuration, 0, sizeof(*configuration));
	configuration->version = 1;
	item = &configuration->interfaces[0];
	(void)snprintf(item->name, sizeof(item->name), "%s", "lo0");
	item->type = NETCONF_INTERFACE_LOOPBACK;
	item->enabled = 1;
	item->enabled_set = 1;
	item->dhcp_set = 1;
	(void)snprintf(item->addresses[0].address, sizeof(item->addresses[0].address), "%s", "127.0.0.1");
	item->addresses[0].prefix_length = 8;
	item->address_count = 1;
	configuration->interface_count = 1;
	configuration->dns_mode = NETCONF_DNS_DHCP;
}

/* Finds an interface's entry, or -1. */
static int
find(
	const struct netconf *configuration,
	const char *name)
{
	size_t index;
	int differs;

	/* Each entry. */
	for (index = 0; index < configuration->interface_count; index++) {
		differs = strcmp(configuration->interfaces[index].name, name);
		if (differs == 0)
			return (int)index;
	}

	/* None. */
	return -1;
}

/* Tells whether the default route is there through a gateway (NULL: any). */
static int
has_default(
	const struct netconf *configuration,
	const char *gateway)
{
	size_t index;
	int differs;

	/* Each route. */
	for (index = 0; index < configuration->route_count; index++) {
		differs = strcmp(configuration->routes[index].destination, "default");
		if (differs != 0)
			continue;
		if (gateway == NULL)
			return 1;
		differs = strcmp(configuration->routes[index].gateway, gateway);
		if (differs == 0)
			return 1;
	}

	/* None. */
	return 0;
}

/*
 * Runs the checks.
 */
int
main(void)
{
	static struct netconf configuration;
	static struct netconf again;
	struct networkd_lan_configure request;
	char diagnostic[160];
	char text[8192];
	FILE *stream;
	size_t length;
	int index;
	int result;

	/* Good configurations. */
	check("dhcp", accepted("em0", "", "", "", NULL, NULL, 0) == 1);
	check("dhcp with name servers", accepted("em0", "", "", "", "1.1.1.1", "8.8.8.8", 2) == 1);
	check("static without router", accepted("re0", "192.168.7.20", "255.255.255.0", "", NULL, NULL, 0) == 1);
	check("static with router and dns", accepted("ue0", "10.1.2.3", "255.255.0.0", "10.1.0.1", "10.1.0.53", NULL, 1) == 1);
	check("static /30", accepted("em0", "192.168.0.1", "255.255.255.252", "192.168.0.2", NULL, NULL, 0) == 1);

	/* Names that are no wired interface's. */
	check("refuses the loopback", accepted("lo0", "", "", "", NULL, NULL, 0) == 0);
	check("refuses a radio", accepted("wlan0", "", "", "", NULL, NULL, 0) == 0);
	check("refuses capitals", accepted("EM0", "", "", "", NULL, NULL, 0) == 0);
	check("refuses a shell word", accepted("em0;rm", "", "", "", NULL, NULL, 0) == 0);
	check("refuses an empty name", accepted("", "", "", "", NULL, NULL, 0) == 0);
	check("refuses a long name", accepted("abcdefghijklmnop", "", "", "", NULL, NULL, 0) == 0);
	check("refuses a digit first", accepted("0em", "", "", "", NULL, NULL, 0) == 0);

	/* Addresses that are not dotted IPv4 or not a machine's. */
	check("refuses three numbers", accepted("em0", "192.168.7", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses 256", accepted("em0", "192.168.7.256", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses a leading zero", accepted("em0", "192.168.07.2", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses a space", accepted("em0", "192.168.7.2 ", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses a sign", accepted("em0", "+192.168.7.2", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses 0.x", accepted("em0", "0.1.2.3", "255.0.0.0", "", NULL, NULL, 0) == 0);
	check("refuses the loopback address", accepted("em0", "127.0.0.2", "255.0.0.0", "", NULL, NULL, 0) == 0);
	check("refuses multicast", accepted("em0", "224.0.0.5", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses the broadcast", accepted("em0", "255.255.255.255", "255.255.255.0", "", NULL, NULL, 0) == 0);

	/* Netmasks. */
	check("refuses a gap in the mask", accepted("em0", "192.168.7.20", "255.0.255.0", "", NULL, NULL, 0) == 0);
	check("refuses /31", accepted("em0", "192.168.7.20", "255.255.255.254", "", NULL, NULL, 0) == 0);
	check("refuses /32", accepted("em0", "192.168.7.20", "255.255.255.255", "", NULL, NULL, 0) == 0);
	check("refuses /0", accepted("em0", "192.168.7.20", "0.0.0.0", "", NULL, NULL, 0) == 0);
	check("refuses no mask", accepted("em0", "192.168.7.20", "", "", NULL, NULL, 0) == 0);

	/* The subnet's own and broadcast address. */
	check("refuses the subnet's own", accepted("em0", "192.168.7.0", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses the subnet's broadcast", accepted("em0", "192.168.7.255", "255.255.255.0", "", NULL, NULL, 0) == 0);

	/* Routers. */
	check("refuses a router outside", accepted("em0", "192.168.7.20", "255.255.255.0", "192.168.8.1", NULL, NULL, 0) == 0);
	check("refuses the address as router", accepted("em0", "192.168.7.20", "255.255.255.0", "192.168.7.20", NULL, NULL, 0) == 0);
	check("refuses the broadcast as router", accepted("em0", "192.168.7.20", "255.255.255.0", "192.168.7.255", NULL, NULL, 0) == 0);
	check("refuses a bad router", accepted("em0", "192.168.7.20", "255.255.255.0", "router", NULL, NULL, 0) == 0);

	/* DHCP carries no mask nor router. */
	check("refuses a mask with DHCP", accepted("em0", "", "255.255.255.0", "", NULL, NULL, 0) == 0);
	check("refuses a router with DHCP", accepted("em0", "", "", "192.168.7.1", NULL, NULL, 0) == 0);

	/* Name servers. */
	check("refuses a bad name server", accepted("em0", "", "", "", "1.1.1", NULL, 1) == 0);
	check("refuses a loopback name server", accepted("em0", "", "", "", "127.0.0.1", NULL, 1) == 0);
	check("refuses an empty name server", accepted("em0", "", "", "", "", NULL, 1) == 0);
	check("refuses three name servers", accepted("em0", "", "", "", "1.1.1.1", "8.8.8.8", 3) == 0);

	/* The edit: a static em0 with a router and a name server, added to the loopback's file. */
	base(&configuration);
	memset(&request, 0, sizeof(request));
	request.interface = "em0";
	request.address = "192.168.7.20";
	request.netmask = "255.255.255.0";
	request.gateway = "192.168.7.1";
	request.dns[0] = "192.168.7.53";
	request.dns_count = 1;
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	index = find(&configuration, "em0");
	check("static edit", result == 0 && index == 1);
	check("static entry", index == 1 && configuration.interfaces[1].type == NETCONF_INTERFACE_ETHERNET && configuration.interfaces[1].enabled &&
	    !configuration.interfaces[1].dhcp && configuration.interfaces[1].address_count == 1 &&
	    strcmp(configuration.interfaces[1].addresses[0].address, "192.168.7.20") == 0 && configuration.interfaces[1].addresses[0].prefix_length == 24);
	check("static default route", has_default(&configuration, "192.168.7.1") && configuration.route_count == 1);
	check("static name server", configuration.dns_mode == NETCONF_DNS_STATIC && configuration.dns_count == 1 && strcmp(configuration.dns_servers[0], "192.168.7.53") == 0);
	check("loopback kept", find(&configuration, "lo0") == 0 && configuration.interfaces[0].address_count == 1);
	result = netconf_validate(&configuration, diagnostic, sizeof(diagnostic));
	check("static valid", result == 0);

	/* Written and read back the same. */
	stream = fmemopen(text, sizeof(text), "w");
	result = netconf_write(stream, &configuration);
	length = (size_t)ftell(stream);
	(void)fclose(stream);
	text[length] = '\0';
	stream = fmemopen(text, length, "r");
	result |= netconf_parse(stream, &again, diagnostic, sizeof(diagnostic));
	(void)fclose(stream);
	check("written and read back", result == 0 && again.interface_count == 2 && has_default(&again, "192.168.7.1") &&
	    again.dns_count == 1 && find(&again, "em0") == 1 && again.interfaces[1].addresses[0].prefix_length == 24);

	/* The same interface again, static without a router: the default route goes. */
	request.gateway = "";
	request.dns_count = 0;
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	check("static without router drops the route", result == 0 && !has_default(&configuration, NULL) && configuration.interface_count == 2);
	check("no name servers is DHCP's", configuration.dns_mode == NETCONF_DNS_DHCP && configuration.dns_count == 0);

	/* Back to static with a router, then a second interface on DHCP: the route stays (em0 keeps a static address). */
	request.gateway = "192.168.7.1";
	(void)networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	memset(&request, 0, sizeof(request));
	request.interface = "ue0";
	request.address = "";
	request.netmask = "";
	request.gateway = "";
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	index = find(&configuration, "ue0");
	check("dhcp entry added", result == 0 && index == 2 && configuration.interfaces[2].dhcp && configuration.interfaces[2].address_count == 0);
	check("dhcp keeps another's route", has_default(&configuration, "192.168.7.1"));

	/* ue0 static in its own subnet without a router: em0's route stays; with a router of its own it replaces it. */
	request.address = "10.9.0.5";
	request.netmask = "255.255.255.0";
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	check("another static without router keeps the route", result == 0 && has_default(&configuration, "192.168.7.1") && configuration.route_count == 1);
	request.gateway = "10.9.0.1";
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	check("a router of its own replaces it", result == 0 && has_default(&configuration, "10.9.0.1") && configuration.route_count == 1);
	request.address = "";
	request.netmask = "";
	request.gateway = "";
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	check("ue0 to DHCP drops its own route", result == 0 && !has_default(&configuration, NULL));
	request.interface = "em0";
	request.address = "192.168.7.20";
	request.netmask = "255.255.255.0";
	request.gateway = "192.168.7.1";
	(void)networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	request.interface = "ue0";
	request.address = "";
	request.netmask = "";
	request.gateway = "";

	/* em0 itself to DHCP: no static address is left, so the route goes. */
	request.interface = "em0";
	result = networkd_lan_configure_edit(&configuration, &request, diagnostic, sizeof(diagnostic));
	check("dhcp drops the route", result == 0 && !has_default(&configuration, NULL) && configuration.interfaces[1].dhcp && configuration.interfaces[1].address_count == 0);
	result = netconf_validate(&configuration, diagnostic, sizeof(diagnostic));
	check("dhcp valid", result == 0);

	/* The verdict. */
	if (failures != 0) {
		printf("host-lan-configure: FAIL (%d)\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-lan-configure: PASS\n");
	return 0;
}
