/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * networkd's IPv6 (ws130-p006, plan/ws130/phase001/phase.md section 4).
 *
 * When a wired interface comes up (or is up when networkd starts), its
 * IPv6 is turned on or off as net.conf says (on by default, H3); when on,
 * it gets its link-local address with a stable interface identifier
 * (RFC 7217, H4: no MAC address shown) and the static addresses of its
 * "ipv6:" section.  The kernel then does duplicate address detection and
 * solicits a router.  Each Router Advertisement the kernel publishes
 * (RTM_ROUTERADV) gives, as net.conf allows: for each autonomous /64
 * prefix a stable address (RFC 7217, or EUI-64 with stable-address false)
 * and a temporary one (RFC 8981), with the prefix's lifetimes; the router
 * as the default route for its lifetime; and its DNS servers (RDNSS) in
 * resolv.conf after the IPv4 ones (H5).  The kernel removes what runs out.
 */

#include "userland/base/networkd/ipv6.h"
#include "userland/base/networkd/slaac.h"
#include "userland/base/net/netconf.h"
#include "userland/base/net/netutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* The secret of the stable identifiers, and where it is kept (made the first time, 0600). */
#define IPV6_SECRET_DIRECTORY	"/var/db/networkd"
#define IPV6_SECRET_PATH	"/var/db/networkd/ipv6-secret"
#define IPV6_SECRET_LENGTH	32U

/* How many temporary addresses networkd keeps the identifiers of (an interface and a prefix each). */
#define IPV6_TEMPORARIES	16U

/* The resolver's file, its temporary twin, the most servers it lists, and the longest line read. */
#define IPV6_RESOLV_PATH	"/etc/resolv.conf"
#define IPV6_RESOLV_TEMPORARY	"/etc/resolv.conf.ipv6"
#define IPV6_RESOLV_SERVERS	3U
#define IPV6_RESOLV_LINE	256U

/* The longest text of a name server with its zone. */
#define IPV6_SERVER_TEXT	(INET6_ADDRSTRLEN + IF_NAMESIZE + 1)

/* A temporary address's identifier, kept so that each advertisement renews it rather than making another. */
struct ipv6_temporary {
	int used;
	unsigned ifindex;
	struct in6_addr prefix;
	uint8_t iid[8];
};

static uint8_t ipv6_secret[IPV6_SECRET_LENGTH];
static int ipv6_secret_ready;
static struct ipv6_temporary ipv6_temporaries[IPV6_TEMPORARIES];
static struct netconf ipv6_configuration;

static int ipv6_secret_load(void);
static void ipv6_config(const char *name, struct netconf_interface *item, int *dns_dynamic);
static int ipv6_request(unsigned long command, void *argument);
static int ipv6_add(const char *name, const struct in6_addr *address, unsigned prefix, unsigned flags, uint32_t valid,
    uint32_t preferred);
static void ipv6_setup(const char *name);
static void ipv6_advertisement(const struct rtm_routeradv *record, const uint8_t *message);
static void ipv6_prefix(const char *name, unsigned ifindex, const struct netconf_interface *item,
    const struct slaac_prefix *prefix);
static int ipv6_eui64(const char *name, uint8_t *iid);
static const uint8_t *ipv6_temporary(unsigned ifindex, const struct in6_addr *prefix);
static void ipv6_route(unsigned ifindex, const struct in6_addr *router, uint32_t lifetime);
static void ipv6_resolver(const char *name, const struct slaac_ra *ra);

/*
 * Opens the route socket of IPv6's events (RTM_ROUTERADV, RTM_ADDRINFO,
 * and RTM_IFINFO).  Returns the descriptor, or -1 with errno set.
 */
int
networkd_ipv6_open(void)
{
	int descriptor;

	/* Not blocking, closed across exec. */
	descriptor = socket(PF_ROUTE, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, AF_INET6);
	return descriptor;
}

/*
 * Reads every record waiting on the IPv6 route socket and acts on it: an
 * interface's arrival or carrier sets its IPv6 up, an advertisement is
 * applied.  Returns 0, or -1 when the socket failed.
 */
int
networkd_ipv6_events(
	int descriptor)
{
	static uint8_t record[sizeof(struct rtm_routeradv) + RTM_ROUTERADV_MESSAGE_MAX];
	const struct rtm_header *header;
	const struct rtm_ifinfo *info;
	const struct rtm_routeradv *advert;
	char name[IF_NAMESIZE];
	const char *named;
	ssize_t count;

	/* Each record until none waits. */
	for (;;) {
		count = read(descriptor, record, sizeof(record));
		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return 0;
		if (count <= 0)
			return -1;
		if ((size_t)count < sizeof(*header))
			continue;
		header = (const struct rtm_header *)record;

		/* An interface that arrived or got its carrier: its IPv6 set up. */
		if (header->rtm_type == RTM_IFINFO && (size_t)count >= sizeof(*info)) {
			info = (const struct rtm_ifinfo *)record;
			if (info->rtm_transition != RTM_IFINFO_CARRIER_UP && info->rtm_transition != RTM_IFINFO_ARRIVAL)
				continue;
			named = if_indextoname(info->rtm_ifindex, name);
			if (named != NULL)
				ipv6_setup(name);
			continue;
		}

		/* A router's advertisement. */
		if (header->rtm_type == RTM_ROUTERADV && (size_t)count >= sizeof(*advert)) {
			advert = (const struct rtm_routeradv *)record;
			if (sizeof(*advert) + advert->rtm_message_length <= (size_t)count)
				ipv6_advertisement(advert, record + sizeof(*advert));
		}
	}
}

/*
 * Sets up the IPv6 of each wired interface that is up when networkd
 * starts (the loopback keeps ::1, which the kernel gives it).
 */
void
networkd_ipv6_start(void)
{
	struct ifreq *interfaces;
	struct ifreq flags;
	unsigned count;
	unsigned index;
	int descriptor;
	int status;

	/* The interfaces. */
	descriptor = socket(AF_INET, SOCK_DGRAM, 0);
	if (descriptor < 0)
		return;
	status = netutil_interfaces(descriptor, &interfaces, &count);
	if (status != 0) {
		close(descriptor);
		return;
	}

	/* Each one up, not the loopback. */
	for (index = 0; index < count; index++) {
		status = netutil_ifreq(&flags, interfaces[index].ifr_name);
		if (status == 0)
			status = ioctl(descriptor, SIOCGIFFLAGS, &flags);
		if (status != 0)
			continue;
		if ((flags.ifr_flags & IFF_UP) == 0 || (flags.ifr_flags & IFF_LOOPBACK) != 0)
			continue;
		ipv6_setup(interfaces[index].ifr_name);
	}
	free(interfaces);
	close(descriptor);
}

/* Reads the secret of the stable identifiers, making and keeping it the first time; 0, or -1. */
static int
ipv6_secret_load(void)
{
	ssize_t count;
	int descriptor;
	int status;

	/* Read once. */
	if (ipv6_secret_ready)
		return 0;

	/* The kept one. */
	descriptor = open(IPV6_SECRET_PATH, O_RDONLY | O_CLOEXEC);
	if (descriptor >= 0) {
		count = read(descriptor, ipv6_secret, sizeof(ipv6_secret));
		close(descriptor);
		if (count != (ssize_t)sizeof(ipv6_secret))
			return -1;
		ipv6_secret_ready = 1;
		return 0;
	}

	/* A new one, kept for the next boot. */
	status = getentropy(ipv6_secret, sizeof(ipv6_secret));
	if (status != 0)
		return -1;
	(void)mkdir(IPV6_SECRET_DIRECTORY, 0700);
	descriptor = open(IPV6_SECRET_PATH, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (descriptor >= 0) {
		count = write(descriptor, ipv6_secret, sizeof(ipv6_secret));
		(void)fsync(descriptor);
		close(descriptor);
		if (count != (ssize_t)sizeof(ipv6_secret))
			fprintf(stderr, "networkd: %s: cannot keep the IPv6 secret\n", IPV6_SECRET_PATH);
	}

	/* Succeeded: this run uses it in any case. */
	ipv6_secret_ready = 1;
	return 0;
}

/*
 * Finds an interface's entry of net.conf (the defaults when net.conf or the
 * entry is missing) and whether the DNS servers are the network's (dhcp or
 * merge mode) rather than static.
 */
static void
ipv6_config(
	const char *name,
	struct netconf_interface *item,
	int *dns_dynamic)
{
	char error[128];
	size_t index;
	int status;
	int same;

	/* The defaults. */
	memset(item, 0, sizeof(*item));
	*dns_dynamic = 1;

	/* net.conf's entry. */
	status = netconf_load(NETCONF_PATH, &ipv6_configuration, error, sizeof(error));
	if (status != 0)
		return;
	*dns_dynamic = ipv6_configuration.dns_mode != NETCONF_DNS_STATIC;
	for (index = 0; index < ipv6_configuration.interface_count; index++) {
		same = strcmp(ipv6_configuration.interfaces[index].name, name);
		if (same == 0) {
			*item = ipv6_configuration.interfaces[index];
			return;
		}
	}
}

/* Makes one IPv6 ioctl on a socket of its own; 0, or -1 with errno set. */
static int
ipv6_request(
	unsigned long command,
	void *argument)
{
	int descriptor;
	int status;
	int saved;

	/* The socket, the request, and the socket closed. */
	descriptor = socket(AF_INET6, SOCK_DGRAM, 0);
	if (descriptor < 0)
		return -1;
	status = ioctl(descriptor, command, argument);
	saved = errno;
	close(descriptor);
	errno = saved;
	return status;
}

/* Adds (or renews) an address of an interface with its flags and lifetimes. */
static int
ipv6_add(
	const char *name,
	const struct in6_addr *address,
	unsigned prefix,
	unsigned flags,
	uint32_t valid,
	uint32_t preferred)
{
	struct in6_aliasreq request;
	char text[INET6_ADDRSTRLEN];
	const char *written;
	int status;

	/* The request. */
	memset(&request, 0, sizeof(request));
	(void)snprintf(request.ifra_name, sizeof(request.ifra_name), "%s", name);
	request.ifra_addr.sin6_family = AF_INET6;
	request.ifra_addr.sin6_addr = *address;
	request.ifra_prefixlen = prefix;
	request.ifra_flags = flags;
	request.ifra_valid = valid;
	request.ifra_preferred = preferred;
	status = ipv6_request(SIOCAIFADDR_IN6, &request);

	/* A failure is said in the log; the next event tries again. */
	if (status != 0) {
		written = inet_ntop(AF_INET6, address, text, sizeof(text));
		fprintf(stderr, "networkd: %s: IPv6 address %s/%u: %s\n", name, written != NULL ? text : "?", prefix,
		    strerror(errno));
	}
	return status;
}

/* Sets an interface's IPv6 up: on or off as net.conf says, and when on its link-local and static addresses. */
static void
ipv6_setup(
	const char *name)
{
	struct netconf_interface item;
	struct ifreq request;
	struct in6_addr link;
	struct in6_addr address;
	uint8_t iid[8];
	size_t index;
	int dns_dynamic;
	int on;
	int status;

	/* On or off. */
	ipv6_config(name, &item, &dns_dynamic);
	on = netconf_ipv6_enabled(&item);
	status = netutil_ifreq(&request, name);
	if (status != 0)
		return;
	request.ifr_flags = on;
	status = ipv6_request(SIOCSIFINET6, &request);
	if (status != 0 || !on)
		return;

	/* The link-local address: fe80::/64 with the stable identifier. */
	status = ipv6_secret_load();
	if (status != 0)
		return;
	memset(&link, 0, sizeof(link));
	link.s6_addr[0] = 0xfe;
	link.s6_addr[1] = 0x80;
	slaac_stable_iid(ipv6_secret, sizeof(ipv6_secret), &link, name, "", 0, iid);
	slaac_address(&link, iid, &address);
	(void)ipv6_add(name, &address, 64U, 0U, IN6_LIFETIME_INFINITE, IN6_LIFETIME_INFINITE);

	/* The static addresses. */
	for (index = 0; index < item.ipv6.address_count; index++) {
		status = inet_pton(AF_INET6, item.ipv6.addresses[index].address, &address);
		if (status != 1)
			continue;
		(void)ipv6_add(name, &address, item.ipv6.addresses[index].prefix_length, 0U, IN6_LIFETIME_INFINITE,
		    IN6_LIFETIME_INFINITE);
	}
	printf("networkd: %s: IPv6 on\n", name);
	fflush(stdout);
}

/* Applies a Router Advertisement of an interface: its prefixes' addresses, the default route, the DNS servers. */
static void
ipv6_advertisement(
	const struct rtm_routeradv *record,
	const uint8_t *message)
{
	struct netconf_interface item;
	struct slaac_ra ra;
	char name[IF_NAMESIZE];
	const char *named;
	unsigned index;
	int dns_dynamic;
	int status;

	/* The interface and what net.conf says of it. */
	named = if_indextoname(record->rtm_ifindex, name);
	if (named == NULL)
		return;
	ipv6_config(name, &item, &dns_dynamic);
	if (!netconf_ipv6_enabled(&item))
		return;

	/* The advertisement. */
	status = slaac_parse(message, record->rtm_message_length, &ra);
	if (status != 0)
		return;

	/* Each prefix's addresses, the router, and the DNS servers. */
	if (netconf_ipv6_autoconf(&item)) {
		for (index = 0; index < ra.prefix_count; index++)
			ipv6_prefix(name, record->rtm_ifindex, &item, &ra.prefixes[index]);
	}
	if (ra.router_lifetime != 0U)
		ipv6_route(record->rtm_ifindex, &record->rtm_source, ra.router_lifetime);
	if (ra.dns_count != 0U && dns_dynamic)
		ipv6_resolver(name, &ra);
	printf("networkd: %s: router advertisement prefixes=%u router=%u dns=%u\n", name, ra.prefix_count,
	    (unsigned)ra.router_lifetime, ra.dns_count);
	fflush(stdout);
}

/* Makes (or renews) a prefix's addresses: the stable one (or EUI-64) and the temporary one, as net.conf allows. */
static void
ipv6_prefix(
	const char *name,
	unsigned ifindex,
	const struct netconf_interface *item,
	const struct slaac_prefix *prefix)
{
	struct in6_addr address;
	const uint8_t *temporary;
	uint8_t iid[8];
	uint32_t valid;
	uint32_t preferred;
	int status;

	/* An autonomous /64 with lifetimes that make sense (RFC 4862 section 5.5.3). */
	if (!prefix->autonomous || prefix->length != 64U)
		return;
	if (prefix->valid == 0U || prefix->preferred > prefix->valid)
		return;

	/* The stable address, or the EUI-64 one. */
	status = 0;
	if (netconf_ipv6_stable_address(item))
		slaac_stable_iid(ipv6_secret, sizeof(ipv6_secret), &prefix->prefix, name, "", 0, iid);
	else
		status = ipv6_eui64(name, iid);
	if (status == 0) {
		slaac_address(&prefix->prefix, iid, &address);
		(void)ipv6_add(name, &address, 64U, IN6_IFF_AUTOCONF, prefix->valid, prefix->preferred);
	}

	/* The temporary one, while the prefix is preferred. */
	if (!netconf_ipv6_temporary(item) || prefix->preferred == 0U)
		return;
	temporary = ipv6_temporary(ifindex, &prefix->prefix);
	if (temporary == NULL)
		return;
	slaac_temporary_lifetimes(prefix->valid, prefix->preferred, 0U, &valid, &preferred);
	slaac_address(&prefix->prefix, temporary, &address);
	(void)ipv6_add(name, &address, 64U, IN6_IFF_AUTOCONF | IN6_IFF_TEMPORARY, valid, preferred);
}

/* Makes an interface's modified EUI-64 identifier from its MAC address (stable-address false); 0, or -1. */
static int
ipv6_eui64(
	const char *name,
	uint8_t *iid)
{
	struct ifreq request;
	int descriptor;
	int status;

	/* The MAC address. */
	status = netutil_ifreq(&request, name);
	if (status != 0)
		return -1;
	descriptor = socket(AF_INET, SOCK_DGRAM, 0);
	if (descriptor < 0)
		return -1;
	status = ioctl(descriptor, SIOCGIFHWADDR, &request);
	close(descriptor);
	if (status != 0)
		return -1;

	/* Succeeded: its halves around ff:fe, the universal bit flipped. */
	iid[0] = (uint8_t)(request.ifr_hwaddr[0] ^ 0x02U);
	iid[1] = (uint8_t)request.ifr_hwaddr[1];
	iid[2] = (uint8_t)request.ifr_hwaddr[2];
	iid[3] = 0xff;
	iid[4] = 0xfe;
	iid[5] = (uint8_t)request.ifr_hwaddr[3];
	iid[6] = (uint8_t)request.ifr_hwaddr[4];
	iid[7] = (uint8_t)request.ifr_hwaddr[5];
	return 0;
}

/* Finds the temporary identifier of an interface's prefix, making a random one the first time; NULL when the table is full. */
static const uint8_t *
ipv6_temporary(
	unsigned ifindex,
	const struct in6_addr *prefix)
{
	struct ipv6_temporary *entry;
	unsigned index;
	int same;
	int status;

	/* The one made before. */
	for (index = 0; index < IPV6_TEMPORARIES; index++) {
		entry = &ipv6_temporaries[index];
		if (!entry->used || entry->ifindex != ifindex)
			continue;
		same = memcmp(entry->prefix.s6_addr, prefix->s6_addr, 8U);
		if (same == 0)
			return entry->iid;
	}

	/* A new one in a free slot. */
	for (index = 0; index < IPV6_TEMPORARIES; index++) {
		entry = &ipv6_temporaries[index];
		if (entry->used)
			continue;
		status = getentropy(entry->iid, sizeof(entry->iid));
		if (status != 0)
			return NULL;
		entry->iid[0] &= 0xfdU;
		entry->used = 1;
		entry->ifindex = ifindex;
		entry->prefix = *prefix;
		return entry->iid;
	}

	/* The table is full. */
	return NULL;
}

/* Puts the router as the default route of its interface for its lifetime (the kernel removes it when it runs out). */
static void
ipv6_route(
	unsigned ifindex,
	const struct in6_addr *router,
	uint32_t lifetime)
{
	struct in6_rtentry route;
	int status;

	/* The route to ::/0 through the router; one already there is replaced. */
	memset(&route, 0, sizeof(route));
	route.rt6_gateway = *router;
	route.rt6_flags = RTF_UP | RTF_GATEWAY | RTF_DYNAMIC;
	route.rt6_ifindex = ifindex;
	route.rt6_lifetime = lifetime;
	(void)ipv6_request(SIOCDELRT_IN6, &route);
	status = ipv6_request(SIOCADDRT_IN6, &route);
	if (status != 0)
		fprintf(stderr, "networkd: IPv6 default route: %s\n", strerror(errno));
}

/*
 * Puts an advertisement's DNS servers in resolv.conf after the IPv4 ones
 * (H5: DHCPv4's first), three at most; a link-local server with its
 * interface.  The search list is added when the file has none.
 */
static void
ipv6_resolver(
	const char *name,
	const struct slaac_ra *ra)
{
	char servers[IPV6_RESOLV_SERVERS][IPV6_SERVER_TEXT];
	char existing[IPV6_RESOLV_SERVERS][IPV6_SERVER_TEXT];
	char line[IPV6_RESOLV_LINE];
	char search[SLAAC_SEARCH_MAX + 8];
	char text[INET6_ADDRSTRLEN];
	struct in_addr address4;
	const char *written;
	FILE *input;
	FILE *output;
	unsigned count;
	unsigned listed;
	unsigned index;
	unsigned prior;
	int status;
	int changed;
	int same;
	char *value;
	char *end;

	/* The IPv4 servers the file has, every server it lists, and its search list. */
	count = 0;
	listed = 0;
	search[0] = '\0';
	input = fopen(IPV6_RESOLV_PATH, "r");
	while (input != NULL && fgets(line, sizeof(line), input) != NULL) {
		if (strncmp(line, "search ", 7) == 0) {
			(void)snprintf(search, sizeof(search), "%s", line);
			continue;
		}
		if (strncmp(line, "nameserver ", 11) != 0)
			continue;
		value = line + 11;
		end = strpbrk(value, " \t\r\n");
		if (end != NULL)
			*end = '\0';
		if (listed < IPV6_RESOLV_SERVERS)
			(void)snprintf(existing[listed++], sizeof(existing[0]), "%s", value);
		status = inet_aton(value, &address4);
		if (status == 0 || count == IPV6_RESOLV_SERVERS)
			continue;
		(void)snprintf(servers[count++], sizeof(servers[0]), "%s", value);
	}
	if (input != NULL)
		fclose(input);

	/* The advertisement's after them, a link-local one with its interface. */
	for (index = 0; index < ra->dns_count && count < IPV6_RESOLV_SERVERS; index++) {
		written = inet_ntop(AF_INET6, &ra->dns[index], text, sizeof(text));
		if (written == NULL)
			continue;
		if (IN6_IS_ADDR_LINKLOCAL(&ra->dns[index]))
			(void)snprintf(servers[count], sizeof(servers[0]), "%s%%%s", text, name);
		else
			(void)snprintf(servers[count], sizeof(servers[0]), "%s", text);
		same = 1;
		for (prior = 0; prior < count; prior++) {
			if (strcmp(servers[prior], servers[count]) == 0)
				same = 0;
		}
		if (same)
			count++;
	}

	/* Nothing to write when the file lists the same servers and has a search list or none is given. */
	changed = count != listed;
	for (index = 0; index < count && !changed; index++)
		changed = strcmp(servers[index], existing[index]) != 0;
	if (search[0] == '\0' && ra->search[0] != '\0') {
		(void)snprintf(search, sizeof(search), "search %s\n", ra->search);
		changed = 1;
	}
	if (!changed)
		return;

	/* The file written anew, then put in place. */
	output = fopen(IPV6_RESOLV_TEMPORARY, "w");
	if (output == NULL)
		return;
	fprintf(output, "# Generated by networkd\n");
	if (search[0] != '\0')
		fputs(search, output);
	for (index = 0; index < count; index++)
		fprintf(output, "nameserver %s\n", servers[index]);
	status = fflush(output);
	if (status == 0)
		status = fsync(fileno(output));
	fclose(output);
	if (status == 0)
		status = rename(IPV6_RESOLV_TEMPORARY, IPV6_RESOLV_PATH);
	if (status != 0)
		fprintf(stderr, "networkd: %s: %s\n", IPV6_RESOLV_PATH, strerror(errno));
}
