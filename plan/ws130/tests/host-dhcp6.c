/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws130-p007: the host test of the DHCPv6 messages of `dhcpc -6`
 * (userland/base/net/dhcp6.c with the host's headers): a DUID-UUID and an
 * IAID; a Solicit, a Request, an Information-Request built byte by byte and
 * the refused ones; an Advertise and a Reply read (identifiers, preference,
 * IA_NA, address, DNS, search list, refresh time, statuses) and the
 * refused ones.
 */

#include "userland/base/net/dhcp6.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);
static void test_identity(struct dhcp6_duid *client);
static void test_build(const struct dhcp6_duid *client);
static void test_parse(const struct dhcp6_duid *client);
static size_t put_option(uint8_t *at, unsigned code, const void *data, size_t length);

/* A server's DUID (DUID-LL of 00:00:5e:00:53:01). */
static const uint8_t test_server[] = { 0, 3, 0, 1, 0x00, 0x00, 0x5e, 0x00, 0x53, 0x01 };

/* Runs the checks; the exit status is 1 when one failed. */
int
main(void)
{
	struct dhcp6_duid client;

	/* Each part. */
	test_identity(&client);
	test_build(&client);
	test_parse(&client);

	/* The verdict. */
	printf("host-dhcp6: %s\n", test_failures == 0 ? "PASS" : "FAIL");
	return test_failures == 0 ? 0 : 1;
}

/* A DUID-UUID from random bytes, and IAIDs. */
static void
test_identity(
	struct dhcp6_duid *client)
{
	uint8_t random[16];
	uint32_t first;
	uint32_t again;
	uint32_t other;

	/* The DUID: type 4, the UUID's version 4 and variant 10. */
	memset(random, 0xff, sizeof(random));
	dhcp6_duid_uuid(random, client);
	check(client->length == 18U && client->bytes[0] == 0 && client->bytes[1] == 4, "duid: DUID-UUID, 18 bytes");
	check((client->bytes[2 + 6] & 0xf0U) == 0x40U && (client->bytes[2 + 8] & 0xc0U) == 0x80U,
	    "duid: version 4, variant 10");

	/* The IAID: the same name the same, another another. */
	first = dhcp6_iaid("ue0");
	again = dhcp6_iaid("ue0");
	other = dhcp6_iaid("em0");
	check(first == again && first != other, "iaid: from the interface's name");
}

/* Messages built byte by byte, and the ones refused. */
static void
test_build(
	const struct dhcp6_duid *client)
{
	struct dhcp6_request request;
	struct dhcp6_duid server;
	uint8_t buffer[256];
	size_t length;
	int status;

	/* A Solicit: type and transaction; client id, IA_NA, ORO (23, 24, 82), elapsed time. */
	memset(&request, 0, sizeof(request));
	request.type = DHCP6_SOLICIT;
	request.xid = 0x12abcdefU;
	request.client = client;
	request.with_ia = 1;
	request.iaid = 0x01020304U;
	request.elapsed = 150;
	status = dhcp6_build(buffer, sizeof(buffer), &length, &request);
	check(status == 0 && length == 58U, "solicit: built, 58 bytes");
	check(buffer[0] == 1 && buffer[1] == 0xab && buffer[2] == 0xcd && buffer[3] == 0xef, "solicit: type and 24-bit xid");
	check(buffer[4] == 0 && buffer[5] == 1 && buffer[7] == 18 && memcmp(buffer + 8, client->bytes, 18U) == 0,
	    "solicit: client id");
	check(buffer[26] == 0 && buffer[27] == 3 && buffer[29] == 12 && buffer[30] == 1 && buffer[33] == 4,
	    "solicit: IA_NA with the IAID, no address");
	check(buffer[43] == 6 && buffer[45] == 6 && buffer[47] == 23 && buffer[49] == 24 && buffer[51] == 82,
	    "solicit: ORO of DNS, domains, SOL_MAX_RT");
	check(buffer[53] == 8 && buffer[55] == 2 && buffer[56] == 0 && buffer[57] == 150, "solicit: elapsed time");

	/* A Request needs the server's identifier. */
	request.type = DHCP6_REQUEST;
	status = dhcp6_build(buffer, sizeof(buffer), &length, &request);
	check(status != 0, "request: refused without the server");

	/* A Request: server id, and the IA_NA with the address offered. */
	memcpy(server.bytes, test_server, sizeof(test_server));
	server.length = sizeof(test_server);
	request.server = &server;
	request.with_address = 1;
	(void)inet_pton(AF_INET6, "2001:db8::100", &request.address);
	status = dhcp6_build(buffer, sizeof(buffer), &length, &request);
	check(status == 0 && buffer[0] == 3, "request: built");
	check(buffer[27] == 2 && buffer[29] == sizeof(test_server) && memcmp(buffer + 30, test_server, sizeof(test_server)) == 0,
	    "request: server id");
	check(buffer[41] == 3 && buffer[43] == 40 && buffer[57] == 5 && buffer[59] == 24 && buffer[60] == 0x20 &&
	    buffer[75] == 0x00 && buffer[74] == 0x01, "request: IA_NA with the IAADDR");

	/* An Information-Request: no IA_NA, ORO (23, 24, 32, 83). */
	request.type = DHCP6_INFORMATION;
	request.server = NULL;
	status = dhcp6_build(buffer, sizeof(buffer), &length, &request);
	check(status == 0 && buffer[0] == 11 && length == 4U + 22U + 12U + 6U, "information-request: no IA_NA");
	check(buffer[27] == 6 && buffer[29] == 8 && buffer[35] == 32 && buffer[37] == 83, "information-request: ORO");

	/* Too small a buffer, and a type a client does not send. */
	request.type = DHCP6_SOLICIT;
	status = dhcp6_build(buffer, 40U, &length, &request);
	check(status != 0, "solicit: refused in too small a buffer");
	request.type = DHCP6_REPLY;
	status = dhcp6_build(buffer, sizeof(buffer), &length, &request);
	check(status != 0, "reply: not a client's message");
}

/* Advertise and Reply read, and the ones refused. */
static void
test_parse(
	const struct dhcp6_duid *client)
{
	static const uint8_t domains[] = { 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 3, 'l', 'a', 'n', 0 };
	static const uint8_t bad_domains[] = { 3, 'a', '\n', 'b', 0 };
	struct dhcp6_reply reply;
	struct in6_addr address;
	uint8_t message[512];
	uint8_t ia[64];
	uint8_t dns[32];
	uint8_t value[4];
	char text[INET6_ADDRSTRLEN];
	size_t length;
	size_t ia_length;
	size_t no_server;
	size_t with_server;
	int status;

	/* An Advertise: client id, server id, preference 255, IA_NA (T1 1800, T2 2880; 2001:db8::100 3600/7200), DNS, domains. */
	message[0] = 2;
	message[1] = 0xab;
	message[2] = 0xcd;
	message[3] = 0xef;
	length = 4;
	length += put_option(message + length, 1, client->bytes, client->length);
	no_server = length;
	length += put_option(message + length, 2, test_server, sizeof(test_server));
	with_server = length;
	value[0] = 255;
	length += put_option(message + length, 7, value, 1);
	memset(ia, 0, sizeof(ia));
	ia[3] = 9;
	ia[6] = 0x07;
	ia[7] = 0x08;
	ia[10] = 0x0b;
	ia[11] = 0x40;
	ia[12] = 0;
	ia[13] = 5;
	ia[14] = 0;
	ia[15] = 24;
	(void)inet_pton(AF_INET6, "2001:db8::100", ia + 16);
	ia[34] = 0x0e;
	ia[35] = 0x10;
	ia[38] = 0x1c;
	ia[39] = 0x20;
	ia_length = 40;
	length += put_option(message + length, 3, ia, ia_length);
	(void)inet_pton(AF_INET6, "2001:db8::53", dns);
	(void)inet_pton(AF_INET6, "fe80::53", dns + 16);
	length += put_option(message + length, 23, dns, sizeof(dns));
	length += put_option(message + length, 24, domains, sizeof(domains));
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status == 0 && reply.type == 2 && reply.preference == 255U, "advertise: read, preference 255");
	check(reply.server.length == sizeof(test_server) && memcmp(reply.server.bytes, test_server, sizeof(test_server)) == 0,
	    "advertise: server id");
	(void)inet_ntop(AF_INET6, &reply.address, text, sizeof(text));
	check(reply.has_ia && reply.iaid == 9U && reply.t1 == 1800U && reply.t2 == 2880U, "advertise: IA_NA and its times");
	check(reply.has_address && strcmp(text, "2001:db8::100") == 0 && reply.preferred == 3600U && reply.valid == 7200U,
	    "advertise: the address and its lifetimes");
	check(reply.status == DHCP6_STATUS_NONE && reply.ia_status == DHCP6_STATUS_NONE, "advertise: no status");
	(void)inet_pton(AF_INET6, "fe80::53", &address);
	check(reply.dns_count == 2U && memcmp(&reply.dns[1], &address, sizeof(address)) == 0, "advertise: two DNS servers");
	check(strcmp(reply.search, "example.com lan") == 0, "advertise: the search list");

	/* Another transaction, another client. */
	status = dhcp6_parse(message, length, 0x00abcdeeU, client, &reply);
	check(status != 0, "advertise: refused for another transaction");
	message[9] ^= 0xffU;
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status != 0, "advertise: refused for another client");
	message[9] ^= 0xffU;

	/* An option past the end. */
	status = dhcp6_parse(message, length - 1U, 0x00abcdefU, client, &reply);
	check(status != 0, "advertise: refused when an option runs past the end");

	/* Without the server id: an Advertise refused, a Reply taken. */
	memmove(message + no_server, message + with_server, length - with_server);
	length -= with_server - no_server;
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status != 0, "advertise: refused without a server id");
	message[0] = 7;
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status == 0 && reply.type == 7 && reply.server.length == 0U, "reply: taken without a server id");

	/* A status, and an IA_NA's NoAddrsAvail. */
	length = 4;
	length += put_option(message + length, 1, client->bytes, client->length);
	value[0] = 0;
	value[1] = 2;
	length += put_option(message + length, 13, value, 2);
	memset(ia, 0, sizeof(ia));
	ia[12] = 0;
	ia[13] = 13;
	ia[15] = 2;
	ia[17] = 2;
	length += put_option(message + length, 3, ia, 18);
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status == 0 && reply.status == DHCP6_STATUS_NO_ADDRS && reply.ia_status == DHCP6_STATUS_NO_ADDRS &&
	    !reply.has_address, "reply: statuses read, no address");

	/* An address whose preferred lifetime passes its valid one is not taken; the refresh time. */
	length = 4;
	length += put_option(message + length, 1, client->bytes, client->length);
	memset(ia, 0, sizeof(ia));
	ia[13] = 5;
	ia[15] = 24;
	ia[16] = 0x20;
	ia[35] = 9;
	ia[39] = 8;
	length += put_option(message + length, 3, ia, 40);
	value[0] = 0;
	value[1] = 0;
	value[2] = 0x0e;
	value[3] = 0x10;
	length += put_option(message + length, 32, value, 4);
	length += put_option(message + length, 24, bad_domains, sizeof(bad_domains));
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status == 0 && reply.has_ia && !reply.has_address, "reply: preferred past valid not taken");
	check(reply.refresh == 3600U, "reply: information refresh time");
	check(reply.search[0] == '\0', "reply: a label with a newline not taken");

	/* Not a server's message. */
	message[0] = 1;
	status = dhcp6_parse(message, length, 0x00abcdefU, client, &reply);
	check(status != 0, "solicit: not read as an answer");
}

/* Writes an option; its whole length. */
static size_t
put_option(
	uint8_t *at,
	unsigned code,
	const void *data,
	size_t length)
{
	/* The code, the length, the data. */
	at[0] = (uint8_t)(code >> 8);
	at[1] = (uint8_t)code;
	at[2] = (uint8_t)(length >> 8);
	at[3] = (uint8_t)length;
	memcpy(at + 4, data, length);
	return 4U + length;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *what)
{
	/* The line, and the count. */
	printf("%s: %s\n", condition ? "ok" : "FAIL", what);
	if (!condition)
		test_failures++;
}
