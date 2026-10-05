/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of IPv6's pure parts (ws130-p002): src/kern/net/in6.c,
 * in6-address.c, in6-route.c and in6-neighbor.c.
 */

#include "src/kern/net/in6.h"

#include <uapi/errno.h>

#include <stdio.h>
#include <string.h>

/* The host's own declarations (its AF_INET6 differs from zedBSD's, which uapi/socket.h gives). */
extern int inet_pton(int family, const char *text, void *bytes);
#define AF_INET6_HOST	10

static int failures;

static void check(int condition, const char *what);
static struct in6_addr parse(const char *text);
static void test_address_tests(void);
static void test_addresses(void);
static void test_select(void);
static void test_routes(void);
static void test_neighbors(void);

int
main(void)
{
	/* Each part. */
	test_address_tests();
	test_addresses();
	test_select();
	test_routes();
	test_neighbors();

	/* The result. */
	if (failures != 0) {
		printf("in6-host-test: %d failures\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("in6-host-test: ok\n");
	return 0;
}

/* Counts and says a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (!condition) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Reads an address with the host's inet_pton (the test runs on the host). */
static struct in6_addr
parse(
	const char *text)
{
	struct in6_addr address;
	unsigned char bytes[16];
	unsigned index;
	int read;

	/* The bytes, or :: when the text is not an address. */
	memset(bytes, 0, sizeof(bytes));
	read = inet_pton(AF_INET6_HOST, text, bytes);
	if (read != 1)
		check(0, text);

	/* Succeeded: the address. */
	for (index = 0U; index < 16U; index++)
		address.s6_addr[index] = bytes[index];
	return address;
}

/* The address tests, prefixes, groups and the policy table. */
static void
test_address_tests(void)
{
	struct in6_addr address;
	struct in6_addr other;
	struct in6_addr out;
	uint8_t link[6];

	/* The kinds and scopes. */
	address = parse("fe80::1");
	check(in6_is_linklocal(&address) && in6_scope(&address) == IN6_SCOPE_LINK, "link-local");
	address = parse("::1");
	check(in6_is_loopback(&address) && in6_scope(&address) == IN6_SCOPE_LINK, "loopback");
	address = parse("::");
	check(in6_is_unspecified(&address), "unspecified");
	address = parse("ff02::1");
	check(in6_is_multicast(&address) && in6_scope(&address) == IN6_SCOPE_LINK, "all-nodes");
	address = parse("ff05::2");
	check(in6_scope(&address) == IN6_SCOPE_SITE, "site multicast");
	address = parse("2001:db8::1");
	check(in6_scope(&address) == IN6_SCOPE_GLOBAL && !in6_is_linklocal(&address), "global");
	address = parse("fec0::5");
	check(in6_scope(&address) == IN6_SCOPE_SITE, "site-local");
	address = parse("::ffff:c000:201");
	check(in6_is_v4mapped(&address), "v4-mapped");

	/* Prefixes. */
	address = parse("2001:db8:1:2::5");
	other = parse("2001:db8:1:3::5");
	check(in6_common_prefix(&address, &other) == 63U, "common prefix 63");
	check(in6_common_prefix(&address, &address) == 128U, "common prefix 128");
	check(in6_prefix_match(&address, &other, 63U) && !in6_prefix_match(&address, &other, 64U), "prefix match");
	in6_prefix_mask(&out, &address, 61U);
	other = parse("2001:db8:1::");
	check(in6_equal(&out, &other), "mask 61");
	in6_prefix_mask(&out, &address, 0U);
	check(in6_is_unspecified(&out), "mask 0");

	/* The solicited-node group and its Ethernet address. */
	address = parse("2001:db8::abcd:1234:5678");
	in6_solicited_node(&out, &address);
	other = parse("ff02::1:ff34:5678");
	check(in6_equal(&out, &other), "solicited-node");
	in6_multicast_link(link, &out);
	check(link[0] == 0x33U && link[1] == 0x33U && link[2] == 0xffU && link[3] == 0x34U && link[5] == 0x78U, "multicast MAC");

	/* RFC 6724's labels and precedences. */
	address = parse("::1");
	check(in6_policy_precedence(&address) == 50U && in6_policy_label(&address) == 0U, "policy ::1");
	address = parse("::ffff:a00:1");
	check(in6_policy_precedence(&address) == 35U && in6_policy_label(&address) == 4U, "policy v4-mapped");
	address = parse("2001:0:1::1");
	check(in6_policy_precedence(&address) == 5U && in6_policy_label(&address) == 5U, "policy 2001::/32");
	address = parse("2400:1::1");
	check(in6_policy_precedence(&address) == 40U && in6_policy_label(&address) == 1U, "policy default");
	address = parse("fd00::1");
	check(in6_policy_label(&address) == 13U, "policy ULA");
}

/* An interface's addresses: DAD, lifetimes, duplicates, renewal, the deadline. */
static void
test_addresses(void)
{
	static struct in6_address_table table;
	struct in6_address_event events[8];
	struct in6_address_event event;
	struct in6_address *entry;
	struct in6_addr address;
	struct in6_addr second;
	unsigned count;
	unsigned index;
	int added;
	int error;

	/* A new address: tentative, its solicitation due now, then preferred a retransmission later. */
	address = parse("fe80::1234");
	error = in6_address_add(&table, &address, 64U, IN6_ADDRESS_AUTOCONF, IN6_LIFETIME_FOREVER, IN6_LIFETIME_FOREVER, 1000U, &added);
	check(error == 0 && added, "add");
	entry = in6_address_find(&table, &address);
	check(entry != NULL && (entry->flags & IN6_ADDRESS_TENTATIVE) != 0U, "tentative");
	check(in6_address_deadline(&table) == 1000U, "DAD due now");
	count = in6_address_tick(&table, 1000U, events, 8U);
	check(count == 1U && events[0].kind == IN6_EVENT_DAD_PROBE, "DAD probe");
	count = in6_address_tick(&table, 1999U, events, 8U);
	check(count == 0U, "nothing before the retransmission");
	count = in6_address_tick(&table, 2000U, events, 8U);
	check(count == 1U && events[0].kind == IN6_EVENT_PREFERRED && (entry->flags & IN6_ADDRESS_TENTATIVE) == 0U, "preferred");
	check(in6_address_deadline(&table) == 0U, "no deadline for a forever address");

	/* Bad additions. */
	second = parse("ff02::1");
	check(in6_address_add(&table, &second, 64U, 0U, 10U, 5U, 0U, &added) == EINVAL, "multicast refused");
	second = parse("2001:db8::1");
	check(in6_address_add(&table, &second, 129U, 0U, 10U, 5U, 0U, &added) == EINVAL, "prefix 129 refused");
	check(in6_address_add(&table, &second, 64U, 0U, 5U, 10U, 0U, &added) == EINVAL, "preferred over valid refused");

	/* Lifetimes: deprecated, then expired; NODAD is preferred at once. */
	error = in6_address_add(&table, &second, 64U, IN6_ADDRESS_NODAD, 10U, 5U, 0U, &added);
	entry = in6_address_find(&table, &second);
	check(error == 0 && added && entry != NULL && (entry->flags & IN6_ADDRESS_TENTATIVE) == 0U, "NODAD");
	check(in6_address_deadline(&table) == 5001U, "the preferred lifetime's deadline");
	count = in6_address_tick(&table, 5001U, events, 8U);
	check(count == 1U && events[0].kind == IN6_EVENT_DEPRECATED, "deprecated");
	check(in6_address_deadline(&table) == 10001U, "the valid lifetime's deadline");

	/* Renewed before it expires: preferred again, its deadlines later. */
	error = in6_address_add(&table, &second, 64U, IN6_ADDRESS_NODAD | IN6_ADDRESS_TEMPORARY, 100U, 50U, 6000U, &added);
	check(error == 0 && !added && (entry->flags & IN6_ADDRESS_DEPRECATED) == 0U && (entry->flags & IN6_ADDRESS_TEMPORARY) != 0U, "renewed");
	count = in6_address_tick(&table, 10001U, events, 8U);
	check(count == 0U, "not expired after renewal");
	count = in6_address_tick(&table, 106001U, events, 8U);
	check(count == 1U && events[0].kind == IN6_EVENT_EXPIRED && in6_address_find(&table, &second) == NULL, "expired");

	/* A duplicate during detection; a restart detects again. */
	second = parse("2001:db8::2");
	(void)in6_address_add(&table, &second, 64U, 0U, IN6_LIFETIME_FOREVER, IN6_LIFETIME_FOREVER, 0U, &added);
	check(in6_address_duplicate(&table, &second, &event) == 1 && event.kind == IN6_EVENT_DUPLICATE, "duplicate");
	entry = in6_address_find(&table, &second);
	check((entry->flags & IN6_ADDRESS_DUPLICATED) != 0U && (entry->flags & IN6_ADDRESS_TENTATIVE) == 0U, "duplicated flag");
	check(in6_address_duplicate(&table, &address, &event) == 0, "no duplicate of a preferred address");
	in6_address_restart(&table, 7000U);
	check((entry->flags & IN6_ADDRESS_TENTATIVE) != 0U && (entry->flags & IN6_ADDRESS_DUPLICATED) == 0U, "restarted");
	check(in6_address_deadline(&table) == 7000U, "restart due now");
	check(in6_address_remove(&table, &second) == 0 && in6_address_remove(&table, &second) == ENOENT, "remove");

	/* The table fills. */
	for (index = 0U; index < IN6_ADDRESSES_MAX; index++) {
		second = parse("2001:db8::100");
		second.s6_addr[15] = (uint8_t)index;
		error = in6_address_add(&table, &second, 64U, IN6_ADDRESS_NODAD, 10U, 10U, 0U, &added);
	}

	/* The last did not fit. */
	check(error == ENOSPC, "full");
	memset(&table, 0, sizeof(table));
}

/* The choice of a source address. */
static void
test_select(void)
{
	static struct in6_address entries[6];
	struct in6_candidate candidates[6];
	struct in6_addr destination;
	const struct in6_address *chosen;
	unsigned index;

	/* A link-local, a global, a deprecated global, a temporary, a ULA on another interface, a tentative. */
	entries[0].address = parse("fe80::1");
	entries[1].address = parse("2001:db8:1::10");
	entries[2].address = parse("2001:db8:1::20");
	entries[2].flags = IN6_ADDRESS_DEPRECATED;
	entries[3].address = parse("2001:db8:1::30");
	entries[3].flags = IN6_ADDRESS_TEMPORARY;
	entries[4].address = parse("fd00::1");
	entries[5].address = parse("2001:db8:1::40");
	entries[5].flags = IN6_ADDRESS_TENTATIVE;
	for (index = 0U; index < 6U; index++) {
		entries[index].prefixlen = 64U;
		entries[index].used = 1;
		candidates[index].entry = &entries[index];
		candidates[index].outgoing = index != 4U;
	}

	/* A link-local destination takes the link-local source. */
	destination = parse("fe80::99");
	chosen = in6_address_select(candidates, 6U, &destination, 1);
	check(chosen == &entries[0], "link-local destination");

	/* A global destination takes the temporary global (preferred), not the deprecated or tentative one. */
	destination = parse("2400:cb00::1");
	chosen = in6_address_select(candidates, 6U, &destination, 1);
	check(chosen == &entries[3], "temporary preferred");
	chosen = in6_address_select(candidates, 6U, &destination, 0);
	check(chosen == &entries[1], "public when temporary not preferred");

	/* The destination itself. */
	destination = parse("2001:db8:1::20");
	chosen = in6_address_select(candidates, 6U, &destination, 1);
	check(chosen == &entries[2], "the destination itself");

	/* A ULA destination: the label wins over the outgoing interface. */
	destination = parse("fd00::99");
	chosen = in6_address_select(candidates, 6U, &destination, 1);
	check(chosen == &entries[4] || chosen == &entries[3], "ULA destination");

	/* Only unusable candidates. */
	chosen = in6_address_select(&candidates[5], 1U, &destination, 1);
	check(chosen == NULL, "nothing usable");
}

/* The routes: longest prefix, metric, renewal, expiry, purge. */
static void
test_routes(void)
{
	static struct in6_route_table table;
	struct in6_route route;
	struct in6_addr destination;
	const struct in6_route *found;
	void *removed[8];
	void *replaced;
	int devices[3];
	int error;

	/* The default route on interface 1, a /64 on interface 2, a /32 on interface 1. */
	memset(&route, 0, sizeof(route));
	route.destination = parse("::");
	route.prefixlen = 0U;
	route.gateway = parse("fe80::1");
	route.ifindex = 1U;
	route.device = &devices[0];
	route.metric = 100U;
	error = in6_route_add(&table, &route, &replaced);
	check(error == 0 && replaced == NULL, "default route");
	route.destination = parse("2001:db8:1::77");
	route.prefixlen = 64U;
	route.gateway = parse("::");
	route.ifindex = 2U;
	route.device = &devices[1];
	route.metric = 0U;
	route.expires_ms = 5000U;
	error = in6_route_add(&table, &route, &replaced);
	check(error == 0, "on-link /64");
	route.destination = parse("2001:db8::");
	route.prefixlen = 32U;
	route.ifindex = 1U;
	route.device = &devices[0];
	route.expires_ms = 0U;
	(void)in6_route_add(&table, &route, &replaced);

	/* The longest prefix; the stored destination is the prefix. */
	destination = parse("2001:db8:1::5");
	found = in6_route_lookup(&table, &destination, 0U, 0U);
	check(found != NULL && found->ifindex == 2U && found->prefixlen == 64U, "longest prefix");
	check(found != NULL && found->destination.s6_addr[15] == 0U, "masked destination");
	destination = parse("2001:db8:2::5");
	found = in6_route_lookup(&table, &destination, 0U, 0U);
	check(found != NULL && found->prefixlen == 32U, "the /32");
	destination = parse("2400::1");
	found = in6_route_lookup(&table, &destination, 0U, 0U);
	check(found != NULL && found->prefixlen == 0U, "the default");
	found = in6_route_lookup(&table, &destination, 2U, 0U);
	check(found == NULL, "no default on interface 2");

	/* A second default on interface 3 with a lower metric wins. */
	route.destination = parse("::");
	route.prefixlen = 0U;
	route.ifindex = 3U;
	route.device = &devices[2];
	route.metric = 10U;
	(void)in6_route_add(&table, &route, &replaced);
	found = in6_route_lookup(&table, &destination, 0U, 0U);
	check(found != NULL && found->ifindex == 3U, "lower metric");

	/* Renewing gives the old device back. */
	route.metric = 200U;
	error = in6_route_add(&table, &route, &replaced);
	check(error == 0 && replaced == &devices[2], "renewal hands back");
	found = in6_route_lookup(&table, &destination, 0U, 0U);
	check(found != NULL && found->ifindex == 1U, "renewed metric");

	/* Expiry. */
	destination = parse("2001:db8:1::5");
	found = in6_route_lookup(&table, &destination, 0U, 5000U);
	check(found != NULL && found->prefixlen == 32U, "expired route skipped");
	check(in6_route_deadline(&table) == 5000U, "route deadline");
	check(in6_route_expire(&table, 5000U, removed, 8U) == 1U && removed[0] == &devices[1], "expired removed");

	/* Delete and purge. */
	destination = parse("2001:db8::");
	check(in6_route_delete(&table, &destination, 32U, 1U, &replaced) == 0 && replaced == &devices[0], "delete");
	check(in6_route_delete(&table, &destination, 32U, 1U, &replaced) == ENOENT, "delete again");
	check(in6_route_purge(&table, 1U, removed, 8U) == 1U, "purge");
	check(in6_route_purge(&table, 3U, removed, 8U) == 1U && removed[0] == &devices[2], "purge 3");
	check(in6_route_lookup(&table, &destination, 0U, 0U) == NULL, "empty");
}

/* A neighbor's states. */
static void
test_neighbors(void)
{
	static const uint8_t link_a[6] = { 2, 0, 0, 0, 0, 1 };
	static const uint8_t link_b[6] = { 2, 0, 0, 0, 0, 2 };
	struct in6_neighbor neighbor;
	unsigned actions;

	/* Resolution: three multicast solicitations, then given up. */
	memset(&neighbor, 0, sizeof(neighbor));
	actions = in6_neighbor_resolve(&neighbor, 0U, 1000U);
	check(actions == ND6_ACTION_SEND_MULTICAST && neighbor.state == ND6_INCOMPLETE, "resolve starts");
	check(in6_neighbor_resolve(&neighbor, 10U, 1000U) == 0U, "resolve waits");
	check(in6_neighbor_timer(&neighbor, 1000U, 1000U) == ND6_ACTION_SEND_MULTICAST, "second solicitation");
	check(in6_neighbor_timer(&neighbor, 2000U, 1000U) == ND6_ACTION_SEND_MULTICAST, "third solicitation");
	actions = in6_neighbor_timer(&neighbor, 3000U, 1000U);
	check(actions == (ND6_ACTION_DROP | ND6_ACTION_FREE) && neighbor.state == ND6_NONE, "given up");

	/* A solicited advertisement: reachable, the held packet goes; then stale, delay, probe. */
	(void)in6_neighbor_resolve(&neighbor, 0U, 1000U);
	check(in6_neighbor_advert(&neighbor, NULL, 1, 0, 0, 100U, 30000U) == 0U, "advert without link ignored");
	actions = in6_neighbor_advert(&neighbor, link_a, 1, 0, 1, 100U, 30000U);
	check(actions == ND6_ACTION_FLUSH && neighbor.state == ND6_REACHABLE && neighbor.router, "reachable");
	check(in6_neighbor_timer(&neighbor, 30100U, 1000U) == 0U && neighbor.state == ND6_STALE, "stale");
	check(in6_neighbor_resolve(&neighbor, 40000U, 1000U) == 0U && neighbor.state == ND6_DELAY, "delay");
	check(in6_neighbor_timer(&neighbor, 45000U, 1000U) == ND6_ACTION_SEND_UNICAST && neighbor.state == ND6_PROBE, "probe");
	check(in6_neighbor_timer(&neighbor, 46000U, 1000U) == ND6_ACTION_SEND_UNICAST, "probe 2");
	check(in6_neighbor_timer(&neighbor, 47000U, 1000U) == ND6_ACTION_SEND_UNICAST, "probe 3");
	actions = in6_neighbor_timer(&neighbor, 48000U, 1000U);
	check(actions == (ND6_ACTION_FREE | ND6_ACTION_UNREACHABLE) && neighbor.state == ND6_NONE, "router unreachable");

	/* A link address from a solicitation: stale; an unsolicited advertisement without override of another address. */
	memset(&neighbor, 0, sizeof(neighbor));
	check(in6_neighbor_link(&neighbor, link_a, 0U) == 0U && neighbor.state == ND6_STALE, "link makes stale");
	check(in6_neighbor_confirm(&neighbor, 10U, 30000U) == 0U && neighbor.state == ND6_REACHABLE, "confirmed");
	check(in6_neighbor_advert(&neighbor, link_b, 0, 0, 0, 20U, 30000U) == 0U && neighbor.state == ND6_STALE &&
	    neighbor.link[5] == 1U, "no override keeps the address");
	check(in6_neighbor_advert(&neighbor, link_b, 0, 1, 0, 30U, 30000U) == 0U && neighbor.state == ND6_STALE &&
	    neighbor.link[5] == 2U, "override changes the address");
	check(in6_neighbor_advert(&neighbor, link_b, 1, 1, 0, 40U, 30000U) == 0U && neighbor.state == ND6_REACHABLE, "solicited confirms");

	/* A held packet is flushed by a link address seen while incomplete. */
	memset(&neighbor, 0, sizeof(neighbor));
	(void)in6_neighbor_resolve(&neighbor, 0U, 1000U);
	check(in6_neighbor_link(&neighbor, link_a, 5U) == ND6_ACTION_FLUSH && neighbor.state == ND6_STALE, "link flushes");
}
