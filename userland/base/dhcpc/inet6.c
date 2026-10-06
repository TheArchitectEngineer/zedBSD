/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * `dhcpc -6` (ws130-p007): DHCPv6 on one interface, once, over a UDP
 * socket on port 546 bound to the interface, to All_DHCP_Relay_Agents_and_
 * Servers (ff02::1:2) from the interface's link-local address.  No Rapid
 * Commit.  Each exchange sends again after 1, 2, 4... seconds until the
 * time given runs out.  The address taken is /128 with the lease's
 * lifetimes (the kernel removes it when they run out); the DNS servers go
 * in resolv.conf after those there (H5: DHCPv4's, then the RDNSS ones).
 */

#include "userland/base/dhcpc/inet6.h"
#include "userland/base/net/dhcp6.h"
#include "userland/base/net/netutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Where dhcpc keeps its identifier and each interface's record (/var/db made too: images lack it). */
#define INET6_STATE_PARENT	"/var/db"
#define INET6_STATE_DIRECTORY	"/var/db/dhcpc"
#define INET6_DUID_PATH		"/var/db/dhcpc/duid"
#define INET6_PATH_MAX		64U

/* The resolver's file, the most servers it lists, the most other lines kept, and the longest line. */
#define INET6_RESOLV_PATH	"/etc/resolv.conf"
#define INET6_RESOLV_SERVERS	3U
#define INET6_RESOLV_LINES	32U
#define INET6_LINE		256U

/* The longest text of a name server with its zone. */
#define INET6_SERVER_TEXT	(INET6_ADDRSTRLEN + IF_NAMESIZE + 1)

/* The largest message read, and the most a DUID's text takes. */
#define INET6_PACKET		1500U
#define INET6_DUID_TEXT		(DHCP6_DUID_MAX * 2U + 1U)

/* The first wait for answers, the longest, and the wait for a source address (the link-local one's DAD). */
#define INET6_FIRST_ROUND_US	1000000U
#define INET6_LONGEST_ROUND_US	8000000U
#define INET6_SOURCE_WAIT_US	200000U

/* The highest preference: a server with it is taken at once (RFC 8415 section 18.2.1). */
#define INET6_PREFERENCE_MAX	255U

/* An elapsed time is in hundredths of a second, at most 0xffff. */
#define INET6_ELAPSED_UNIT_US	10000U
#define INET6_ELAPSED_MAX	0xffffU

/* One run: the interface and its index, the client's identifier, the socket, and when it gives up. */
struct inet6_session {
	const char *interface;
	unsigned ifindex;
	struct dhcp6_duid client;
	int socket;
	uint64_t deadline;
	int verbose;
};

static int inet6_duid(struct dhcp6_duid *duid);
static int inet6_open(struct inet6_session *session);
static int inet6_exchange(struct inet6_session *session, struct dhcp6_request *request, unsigned expected, int collect,
    uint64_t deadline, struct dhcp6_reply *reply);
static int inet6_send(struct inet6_session *session, const uint8_t *packet, size_t length, uint64_t deadline);
static ssize_t inet6_receive(struct inet6_session *session, uint8_t *packet, size_t capacity, uint64_t until);
static int inet6_usable(const struct dhcp6_reply *reply);
static int inet6_finish(struct inet6_session *session, const struct dhcp6_reply *reply, int stateful, int resolver);
static int inet6_address(const char *interface, const struct dhcp6_reply *reply);
static int inet6_resolver(const char *interface, const struct dhcp6_reply *reply);
static int inet6_record(const char *interface, int stateful, uint32_t renew, const struct dhcp6_reply *reply);
static int inet6_recorded(const char *interface, struct dhcp6_duid *server, struct in6_addr *address);
static int inet6_replace(const char *path, const char *text);
static void inet6_hex(const struct dhcp6_duid *duid, char *text);
static int inet6_unhex(const char *text, struct dhcp6_duid *duid);
static int inet6_digit(char letter);
static void inet6_server(const char *interface, const struct in6_addr *address, char *text);

/*
 * Runs DHCPv6 on an interface: with information, an Information-Request;
 * otherwise a Renew of the lease recorded before (in half the time), then
 * Solicit and Request.  With resolver, the DNS servers are written.
 * Returns 0, or 1 when nothing was obtained.
 */
int
dhcpc_inet6(
	const char *interface,
	int information,
	int resolver,
	unsigned timeout_seconds,
	int verbose)
{
	struct inet6_session session;
	struct dhcp6_request request;
	struct dhcp6_reply advertise;
	struct dhcp6_reply reply;
	struct dhcp6_duid server;
	struct in6_addr previous;
	uint64_t now;
	uint64_t renew_deadline;
	int status;
	int bound;

	/* The interface, the client's identifier, and the socket. */
	memset(&session, 0, sizeof(session));
	session.interface = interface;
	session.verbose = verbose;
	session.socket = -1;
	session.ifindex = if_nametoindex(interface);
	if (session.ifindex == 0U) {
		fprintf(stderr, "dhcpc: %s: %s\n", interface, strerror(errno));
		return 1;
	}
	status = inet6_duid(&session.client);
	if (status != 0) {
		fprintf(stderr, "dhcpc: DUID: %s\n", strerror(errno));
		return 1;
	}
	now = netutil_monotonic_us();
	session.deadline = now + (uint64_t)timeout_seconds * 1000000U;
	status = inet6_open(&session);
	if (status != 0) {
		fprintf(stderr, "dhcpc: %s: DHCPv6 socket: %s\n", interface, strerror(errno));
		return 1;
	}
	memset(&request, 0, sizeof(request));
	request.client = &session.client;
	request.iaid = dhcp6_iaid(interface);

	/* Only the information: Information-Request and Reply. */
	if (information) {
		request.type = DHCP6_INFORMATION;
		status = inet6_exchange(&session, &request, DHCP6_REPLY, 0, session.deadline, &reply);
		if (status == 0 && reply.status != DHCP6_STATUS_NONE && reply.status != DHCP6_STATUS_SUCCESS) {
			errno = EACCES;
			status = -1;
		}
		if (status == 0)
			status = inet6_finish(&session, &reply, 0, resolver);
		else
			fprintf(stderr, "dhcpc: %s: information-request: %s\n", interface, strerror(errno));
		close(session.socket);
		return status == 0 ? 0 : 1;
	}

	/* The lease recorded before, renewed within half the time. */
	bound = 0;
	status = inet6_recorded(interface, &server, &previous);
	if (status == 0) {
		request.type = DHCP6_RENEW;
		request.server = &server;
		request.with_ia = 1;
		request.with_address = 1;
		request.address = previous;
		renew_deadline = now + (session.deadline - now) / 2U;
		status = inet6_exchange(&session, &request, DHCP6_REPLY, 0, renew_deadline, &reply);
		bound = status == 0 && inet6_usable(&reply);
		if (verbose)
			printf("dhcpc: %s: renew %s\n", interface, bound ? "taken" : "not taken");
	}

	/* Otherwise Solicit and Advertise, then Request and Reply. */
	if (!bound) {
		request.type = DHCP6_SOLICIT;
		request.server = NULL;
		request.with_ia = 1;
		request.with_address = 0;
		status = inet6_exchange(&session, &request, DHCP6_ADVERTISE, 1, session.deadline, &advertise);
		if (status == 0) {
			request.type = DHCP6_REQUEST;
			request.server = &advertise.server;
			request.with_address = 1;
			request.address = advertise.address;
			status = inet6_exchange(&session, &request, DHCP6_REPLY, 0, session.deadline, &reply);
			bound = status == 0 && inet6_usable(&reply);
			if (status == 0 && !bound)
				errno = EACCES;
		}
	}
	if (!bound) {
		fprintf(stderr, "dhcpc: %s: DHCPv6 lease: %s\n", interface, strerror(errno));
		close(session.socket);
		return 1;
	}

	/* Succeeded: the address, the resolver and the record. */
	status = inet6_finish(&session, &reply, 1, resolver);
	close(session.socket);
	return status == 0 ? 0 : 1;
}

/* Reads the client's DUID, making a DUID-UUID and keeping it the first time (H7); 0, or -1. */
static int
inet6_duid(
	struct dhcp6_duid *duid)
{
	uint8_t random[16];
	ssize_t count;
	int descriptor;
	int status;

	/* The one kept. */
	descriptor = open(INET6_DUID_PATH, O_RDONLY | O_CLOEXEC);
	if (descriptor >= 0) {
		count = read(descriptor, duid->bytes, sizeof(duid->bytes));
		close(descriptor);
		if (count > 2) {
			duid->length = (size_t)count;
			return 0;
		}
	}

	/* A new one, kept for the next time (a run that cannot keep it still uses it). */
	status = getentropy(random, sizeof(random));
	if (status != 0)
		return -1;
	dhcp6_duid_uuid(random, duid);
	(void)mkdir(INET6_STATE_PARENT, 0755);
	(void)mkdir(INET6_STATE_DIRECTORY, 0755);
	descriptor = open(INET6_DUID_PATH, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (descriptor < 0) {
		fprintf(stderr, "dhcpc: %s: %s\n", INET6_DUID_PATH, strerror(errno));
		return 0;
	}
	count = write(descriptor, duid->bytes, duid->length);
	if (count != (ssize_t)duid->length || fsync(descriptor) != 0)
		fprintf(stderr, "dhcpc: %s: %s\n", INET6_DUID_PATH, strerror(errno));
	close(descriptor);

	/* Succeeded. */
	return 0;
}

/* Opens the UDP socket: the interface's, on port 546; 0, or -1. */
static int
inet6_open(
	struct inet6_session *session)
{
	struct sockaddr_in6 client;
	int status;

	/* The socket, bound to the interface. */
	session->socket = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
	if (session->socket < 0)
		return -1;
	status = setsockopt(session->socket, SOL_SOCKET, SO_BINDTODEVICE, session->interface,
	    (socklen_t)strlen(session->interface) + 1U);
	if (status != 0)
		goto fail;

	/* The client's port. */
	memset(&client, 0, sizeof(client));
	client.sin6_family = AF_INET6;
	client.sin6_port = htons(DHCP6_CLIENT_PORT);
	status = bind(session->socket, (const struct sockaddr *)&client, sizeof(client));
	if (status != 0)
		goto fail;

	/* Succeeded. */
	return 0;

fail:
	status = errno;
	close(session->socket);
	session->socket = -1;
	errno = status;
	return -1;
}

/*
 * Sends a message and waits for the answer of the type expected, sending
 * again after 1, 2, 4... seconds until the deadline.  With collect (a
 * Solicit), every Advertise of a round is read and the most preferred one
 * that offers an address is taken.  Returns 0, or -1 with errno set.
 */
static int
inet6_exchange(
	struct inet6_session *session,
	struct dhcp6_request *request,
	unsigned expected,
	int collect,
	uint64_t deadline,
	struct dhcp6_reply *reply)
{
	uint8_t packet[INET6_PACKET];
	struct dhcp6_reply answer;
	uint64_t started;
	uint64_t now;
	uint64_t round;
	uint64_t round_end;
	uint64_t elapsed;
	ssize_t received;
	size_t length;
	int found;
	int status;

	/* A new transaction. */
	status = getentropy(&request->xid, sizeof(request->xid));
	if (status != 0)
		return -1;
	started = netutil_monotonic_us();
	round = INET6_FIRST_ROUND_US;
	found = 0;

	/* Each round until the deadline. */
	for (now = started; now < deadline; now = netutil_monotonic_us()) {
		/* The message, with the time since the first in hundredths of a second. */
		elapsed = (now - started) / INET6_ELAPSED_UNIT_US;
		request->elapsed = (uint16_t)(elapsed > INET6_ELAPSED_MAX ? INET6_ELAPSED_MAX : elapsed);
		status = dhcp6_build(packet, sizeof(packet), &length, request);
		if (status != 0) {
			errno = EINVAL;
			return -1;
		}
		status = inet6_send(session, packet, length, deadline);
		if (status != 0)
			return -1;
		if (session->verbose)
			printf("dhcpc: %s: sent type %u xid %06x length %zu\n", session->interface, request->type,
			    (unsigned)(request->xid & 0x00ffffffU), length);

		/* The answers until the round ends. */
		round_end = netutil_monotonic_us() + round;
		if (round_end > deadline)
			round_end = deadline;
		for (;;) {
			received = inet6_receive(session, packet, sizeof(packet), round_end);
			if (received < 0)
				break;
			status = dhcp6_parse(packet, (size_t)received, request->xid, &session->client, &answer);
			if (session->verbose)
				printf("dhcpc: %s: received type %u length %zd %s\n", session->interface, packet[0], received,
				    status == 0 ? "read" : "not ours");
			if (status != 0 || answer.type != expected)
				continue;
			if (!collect) {
				*reply = answer;
				return 0;
			}

			/* An Advertise: the most preferred that offers an address. */
			if (!inet6_usable(&answer))
				continue;
			if (!found || answer.preference > reply->preference) {
				*reply = answer;
				found = 1;
			}
			if (answer.preference == INET6_PREFERENCE_MAX)
				return 0;
		}
		if (found)
			return 0;

		/* The next round, twice as long. */
		round *= 2U;
		if (round > INET6_LONGEST_ROUND_US)
			round = INET6_LONGEST_ROUND_US;
	}

	/* No answer in time. */
	errno = ETIMEDOUT;
	return -1;
}

/*
 * Sends a message to ff02::1:2 on the interface.  While the interface has
 * no address to send from yet (its link-local one in DAD), it waits and
 * tries again until the deadline.  Returns 0, or -1 with errno set.
 */
static int
inet6_send(
	struct inet6_session *session,
	const uint8_t *packet,
	size_t length,
	uint64_t deadline)
{
	struct sockaddr_in6 servers;
	struct timespec pause;
	ssize_t sent;
	int status;

	/* All_DHCP_Relay_Agents_and_Servers on the interface. */
	memset(&servers, 0, sizeof(servers));
	servers.sin6_family = AF_INET6;
	servers.sin6_port = htons(DHCP6_SERVER_PORT);
	servers.sin6_scope_id = session->ifindex;
	status = inet_pton(AF_INET6, "ff02::1:2", &servers.sin6_addr);
	if (status != 1) {
		errno = EINVAL;
		return -1;
	}

	/* Sent, or tried again while there is no source address. */
	for (;;) {
		sent = sendto(session->socket, packet, length, 0, (const struct sockaddr *)&servers, sizeof(servers));
		if (sent == (ssize_t)length)
			return 0;
		if (sent >= 0)
			errno = EIO;
		if (errno != EADDRNOTAVAIL && errno != ENETUNREACH && errno != EINTR)
			return -1;
		if (netutil_monotonic_us() + INET6_SOURCE_WAIT_US >= deadline)
			return -1;
		pause.tv_sec = 0;
		pause.tv_nsec = (long)INET6_SOURCE_WAIT_US * 1000L;
		(void)nanosleep(&pause, NULL);
	}
}

/* Reads a message from a server's port until a time; its length, or -1 when none came. */
static ssize_t
inet6_receive(
	struct inet6_session *session,
	uint8_t *packet,
	size_t capacity,
	uint64_t until)
{
	struct sockaddr_in6 source;
	struct timeval timeout;
	socklen_t source_length;
	uint64_t now;
	ssize_t received;
	int status;

	/* Until one comes from port 547, or the time is up. */
	for (;;) {
		now = netutil_monotonic_us();
		if (now >= until)
			return -1;
		timeout.tv_sec = (time_t)((until - now) / 1000000U);
		timeout.tv_usec = (long)((until - now) % 1000000U);
		status = setsockopt(session->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		if (status != 0)
			return -1;
		source_length = sizeof(source);
		received = recvfrom(session->socket, packet, capacity, 0, (struct sockaddr *)&source, &source_length);
		if (received < 0 && errno == EINTR)
			continue;
		if (received < 0)
			return -1;
		if (source.sin6_family == AF_INET6 && source.sin6_port == htons(DHCP6_SERVER_PORT))
			return received;
		if (session->verbose)
			printf("dhcpc: %s: passed over a datagram from port %u\n", session->interface,
			    (unsigned)ntohs(source.sin6_port));
	}
}

/* Tells whether an Advertise or a Reply gives an address: no failing status, and an address with a valid lifetime. */
static int
inet6_usable(
	const struct dhcp6_reply *reply)
{
	/* The message's status, the IA_NA's, and its address. */
	if (reply->status != DHCP6_STATUS_NONE && reply->status != DHCP6_STATUS_SUCCESS)
		return 0;
	if (!reply->has_ia || (reply->ia_status != DHCP6_STATUS_NONE && reply->ia_status != DHCP6_STATUS_SUCCESS))
		return 0;
	return reply->has_address && reply->valid != 0U;
}

/*
 * Applies what a Reply gave: the address (stateful), the DNS servers when
 * the resolver is dhcpc's to write, and the record of when to run again
 * (T1, or the information refresh time).  Returns 0, or -1.
 */
static int
inet6_finish(
	struct inet6_session *session,
	const struct dhcp6_reply *reply,
	int stateful,
	int resolver)
{
	char text[INET6_ADDRSTRLEN];
	char server[INET6_SERVER_TEXT];
	const char *written;
	uint32_t renew;
	unsigned index;
	int status;

	/* The address, and T1: the server's, or half the preferred lifetime (RFC 8415 section 21.4). */
	if (stateful) {
		status = inet6_address(session->interface, reply);
		if (status != 0) {
			fprintf(stderr, "dhcpc: %s: address: %s\n", session->interface, strerror(errno));
			return -1;
		}
		renew = reply->t1;
		if (renew == 0U || (reply->t2 != 0U && renew > reply->t2))
			renew = reply->preferred / 2U;
		written = inet_ntop(AF_INET6, &reply->address, text, sizeof(text));
		printf("dhcpc: %s: address %s/128 valid %u preferred %u\n", session->interface,
		    written != NULL ? text : "?", (unsigned)reply->valid, (unsigned)reply->preferred);
	} else {
		/* The information refresh time: the server's, at least the least (RFC 8415 section 21.23). */
		renew = reply->refresh;
		if (renew == 0U)
			renew = DHCP6_REFRESH_DEFAULT;
		if (renew < DHCP6_REFRESH_MINIMUM)
			renew = DHCP6_REFRESH_MINIMUM;
	}
	if (renew == DHCP6_INFINITE)
		renew = 0U;

	/* The DNS servers and the search list. */
	for (index = 0; index < reply->dns_count; index++) {
		inet6_server(session->interface, &reply->dns[index], server);
		printf("dhcpc: %s: dns %s\n", session->interface, server);
	}
	if (reply->search[0] != '\0')
		printf("dhcpc: %s: search %s\n", session->interface, reply->search);
	if (resolver && (reply->dns_count != 0U || reply->search[0] != '\0')) {
		status = inet6_resolver(session->interface, reply);
		if (status != 0)
			fprintf(stderr, "dhcpc: %s: %s\n", INET6_RESOLV_PATH, strerror(errno));
	}

	/* Succeeded: when to run again recorded for networkd. */
	status = inet6_record(session->interface, stateful, renew, reply);
	if (status != 0)
		fprintf(stderr, "dhcpc: %s: record: %s\n", session->interface, strerror(errno));
	printf("dhcpc: %s: DHCPv6 %s renew %u\n", session->interface, stateful ? "bound" : "information", (unsigned)renew);
	fflush(stdout);
	return 0;
}

/* Puts the lease's address on the interface (/128, its lifetimes), or takes it off when its valid lifetime is 0; 0, or -1. */
static int
inet6_address(
	const char *interface,
	const struct dhcp6_reply *reply)
{
	struct in6_aliasreq request;
	unsigned long command;
	int descriptor;
	int status;
	int saved;

	/* The request. */
	memset(&request, 0, sizeof(request));
	(void)snprintf(request.ifra_name, sizeof(request.ifra_name), "%s", interface);
	request.ifra_addr.sin6_family = AF_INET6;
	request.ifra_addr.sin6_addr = reply->address;
	request.ifra_prefixlen = 128U;
	request.ifra_flags = IN6_IFF_DHCP;
	request.ifra_valid = reply->valid;
	request.ifra_preferred = reply->preferred;
	command = SIOCAIFADDR_IN6;
	if (reply->valid == 0U)
		command = SIOCDIFADDR_IN6;

	/* Succeeded when the kernel takes it. */
	descriptor = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (descriptor < 0)
		return -1;
	status = ioctl(descriptor, command, &request);
	saved = errno;
	close(descriptor);
	errno = saved;
	return status == 0 ? 0 : -1;
}

/*
 * Puts the DHCPv6 DNS servers in resolv.conf after the servers it lists
 * (H5: DHCPv4's and the RDNSS ones come first), three at most, and the
 * search list when it has none.  The other lines are kept; a file without
 * a first comment gets one.  Returns 0, or -1 with errno set.
 */
static int
inet6_resolver(
	const char *interface,
	const struct dhcp6_reply *reply)
{
	char lines[INET6_RESOLV_LINES][INET6_LINE];
	char servers[INET6_RESOLV_SERVERS][INET6_SERVER_TEXT];
	char line[INET6_LINE];
	char text[INET6_RESOLV_LINES * INET6_LINE + INET6_RESOLV_SERVERS * (INET6_SERVER_TEXT + 12U) + 64U];
	char server[INET6_SERVER_TEXT];
	unsigned line_count;
	unsigned count;
	unsigned index;
	unsigned prior;
	size_t used;
	int has_search;
	int changed;
	int fresh;
	FILE *input;
	char *value;
	char *end;

	/* The file's lines, and its name servers in their order. */
	line_count = 0;
	count = 0;
	has_search = 0;
	input = fopen(INET6_RESOLV_PATH, "r");
	while (input != NULL && fgets(line, sizeof(line), input) != NULL) {
		if (strncmp(line, "nameserver ", 11) == 0) {
			value = line + 11;
			end = strpbrk(value, " \t\r\n");
			if (end != NULL)
				*end = '\0';
			if (count < INET6_RESOLV_SERVERS)
				(void)snprintf(servers[count++], sizeof(servers[0]), "%s", value);
			continue;
		}
		if (line_count == INET6_RESOLV_LINES)
			continue;
		if (strncmp(line, "search ", 7) == 0)
			has_search = 1;
		end = strchr(line, '\n');
		if (end == NULL)
			(void)snprintf(lines[line_count++], sizeof(lines[0]), "%.*s\n", (int)(INET6_LINE - 2U), line);
		else
			(void)snprintf(lines[line_count++], sizeof(lines[0]), "%s", line);
	}
	if (input != NULL)
		fclose(input);

	/* DHCPv6's after them, each once. */
	changed = 0;
	for (index = 0; index < reply->dns_count && count < INET6_RESOLV_SERVERS; index++) {
		inet6_server(interface, &reply->dns[index], server);
		fresh = 1;
		for (prior = 0; prior < count; prior++) {
			if (strcmp(servers[prior], server) == 0)
				fresh = 0;
		}
		if (!fresh)
			continue;
		(void)snprintf(servers[count++], sizeof(servers[0]), "%s", server);
		changed = 1;
	}

	/* The search list when the file has none. */
	if (!has_search && reply->search[0] != '\0' && line_count < INET6_RESOLV_LINES) {
		(void)snprintf(lines[line_count++], sizeof(lines[0]), "search %.*s\n", (int)(INET6_LINE - 9U), reply->search);
		changed = 1;
	}
	if (!changed)
		return 0;

	/* The file anew: a first comment, the lines, the servers. */
	used = 0;
	if (line_count == 0U || lines[0][0] != '#')
		used += (size_t)snprintf(text + used, sizeof(text) - used, "# Generated by dhcpc -6 for %s\n", interface);
	for (index = 0; index < line_count; index++)
		used += (size_t)snprintf(text + used, sizeof(text) - used, "%s", lines[index]);
	for (index = 0; index < count; index++)
		used += (size_t)snprintf(text + used, sizeof(text) - used, "nameserver %s\n", servers[index]);
	if (used >= sizeof(text)) {
		errno = EOVERFLOW;
		return -1;
	}

	/* Succeeded when it is in place. */
	return inet6_replace(INET6_RESOLV_PATH, text);
}

/* Records when networkd runs dhcpc -6 again, and a lease's server and address for the Renew; 0, or -1. */
static int
inet6_record(
	const char *interface,
	int stateful,
	uint32_t renew,
	const struct dhcp6_reply *reply)
{
	char path[INET6_PATH_MAX];
	char duid[INET6_DUID_TEXT];
	char address[INET6_ADDRSTRLEN];
	char text[INET6_DUID_TEXT + INET6_ADDRSTRLEN + 64U];
	const char *written;
	int length;

	/* The file of the interface. */
	length = snprintf(path, sizeof(path), "%s/%s.dhcp6", INET6_STATE_DIRECTORY, interface);
	if (length < 0 || (size_t)length >= sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* What it says: the mode and the renewal, and a lease's server and address. */
	if (!stateful) {
		(void)snprintf(text, sizeof(text), "mode stateless\nrenew %u\n", (unsigned)renew);
	} else {
		inet6_hex(&reply->server, duid);
		written = inet_ntop(AF_INET6, &reply->address, address, sizeof(address));
		if (written == NULL)
			return -1;
		(void)snprintf(text, sizeof(text), "mode stateful\nrenew %u\nserver %s\naddress %s\n", (unsigned)renew, duid,
		    address);
	}

	/* Succeeded when it is in place. */
	(void)mkdir(INET6_STATE_PARENT, 0755);
	(void)mkdir(INET6_STATE_DIRECTORY, 0755);
	return inet6_replace(path, text);
}

/* Reads the record of a stateful lease: its server and its address; 0, or -1 when there is none. */
static int
inet6_recorded(
	const char *interface,
	struct dhcp6_duid *server,
	struct in6_addr *address)
{
	char path[INET6_PATH_MAX];
	char line[INET6_LINE];
	char *end;
	FILE *input;
	int stateful;
	int found;
	int status;

	/* The file of the interface. */
	status = snprintf(path, sizeof(path), "%s/%s.dhcp6", INET6_STATE_DIRECTORY, interface);
	if (status < 0 || (size_t)status >= sizeof(path))
		return -1;
	input = fopen(path, "r");
	if (input == NULL)
		return -1;

	/* Its lines: the mode, the server, the address. */
	stateful = 0;
	found = 0;
	server->length = 0;
	while (fgets(line, sizeof(line), input) != NULL) {
		end = strchr(line, '\n');
		if (end != NULL)
			*end = '\0';
		if (strcmp(line, "mode stateful") == 0)
			stateful = 1;
		else if (strncmp(line, "server ", 7) == 0)
			(void)inet6_unhex(line + 7, server);
		else if (strncmp(line, "address ", 8) == 0)
			found = inet_pton(AF_INET6, line + 8, address) == 1;
	}
	fclose(input);

	/* Succeeded when it has all three. */
	return stateful && found && server->length != 0U ? 0 : -1;
}

/* Puts a file's text in place: a temporary twin written and synced, then renamed over it; 0, or -1. */
static int
inet6_replace(
	const char *path,
	const char *text)
{
	char temporary[INET6_PATH_MAX + 24U];
	FILE *output;
	int status;
	int saved;

	/* The twin. */
	status = snprintf(temporary, sizeof(temporary), "%s.dhcpc.%ld", path, (long)getpid());
	if (status < 0 || (size_t)status >= sizeof(temporary)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	output = fopen(temporary, "w");
	if (output == NULL)
		return -1;
	status = fputs(text, output) < 0 ? -1 : 0;
	if (status == 0)
		status = fflush(output);
	if (status == 0)
		status = fsync(fileno(output));
	saved = errno;
	if (fclose(output) != 0 && status == 0) {
		status = -1;
		saved = errno;
	}

	/* Succeeded when it is renamed over the file. */
	if (status == 0)
		status = rename(temporary, path);
	if (status != 0) {
		saved = errno;
		(void)unlink(temporary);
		errno = saved;
		return -1;
	}
	return 0;
}

/* Writes a DUID as hexadecimal text. */
static void
inet6_hex(
	const struct dhcp6_duid *duid,
	char *text)
{
	static const char digits[] = "0123456789abcdef";
	size_t index;

	/* Two digits a byte. */
	for (index = 0; index < duid->length; index++) {
		text[index * 2U] = digits[duid->bytes[index] >> 4];
		text[index * 2U + 1U] = digits[duid->bytes[index] & 0x0fU];
	}
	text[duid->length * 2U] = '\0';
}

/* Reads a DUID's hexadecimal text; 0, or -1 when it is not one. */
static int
inet6_unhex(
	const char *text,
	struct dhcp6_duid *duid)
{
	size_t length;
	size_t index;
	int high;
	int low;

	/* An even number of digits that fits. */
	length = strlen(text);
	if (length == 0U || length % 2U != 0U || length / 2U > DHCP6_DUID_MAX)
		return -1;

	/* Each pair. */
	for (index = 0; index < length / 2U; index++) {
		high = inet6_digit(text[index * 2U]);
		low = inet6_digit(text[index * 2U + 1U]);
		if (high < 0 || low < 0)
			return -1;
		duid->bytes[index] = (uint8_t)(high << 4 | low);
	}

	/* Succeeded. */
	duid->length = length / 2U;
	return 0;
}

/* Gives a hexadecimal digit's value, or -1. */
static int
inet6_digit(
	char letter)
{
	/* The digits, then the small letters. */
	if (letter >= '0' && letter <= '9')
		return letter - '0';
	if (letter >= 'a' && letter <= 'f')
		return letter - 'a' + 10;
	return -1;
}

/* Writes a name server's text: a link-local one with its interface. */
static void
inet6_server(
	const char *interface,
	const struct in6_addr *address,
	char *text)
{
	char number[INET6_ADDRSTRLEN];
	const char *written;

	/* The address, and the zone of one on the link. */
	written = inet_ntop(AF_INET6, address, number, sizeof(number));
	if (written == NULL) {
		(void)snprintf(text, INET6_SERVER_TEXT, "?");
		return;
	}
	if (IN6_IS_ADDR_LINKLOCAL(address))
		(void)snprintf(text, INET6_SERVER_TEXT, "%s%%%s", number, interface);
	else
		(void)snprintf(text, INET6_SERVER_TEXT, "%s", number);
}
