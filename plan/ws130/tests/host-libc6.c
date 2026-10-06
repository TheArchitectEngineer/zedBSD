/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws130-p004: the host test of the resolver's IPv6 parts
 * (userland/base/libc/resolver-dns.c, built alone with the host's
 * headers): an AAAA question built and its answer read (two addresses, a
 * CNAME before them, an answer without one), an address's PTR name, and
 * whether an IPv6 destination comes before IPv4 by its source.
 */

#include "userland/base/libc/resolver-internal.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);
static size_t put_answer(uint8_t *message, size_t at, uint16_t type, const uint8_t *data, uint16_t length);
static int preferred(const char *destination, const char *source);

/* Runs the checks; the exit status is the count of failures. */
int
main(void)
{
	static const uint8_t first[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	static const uint8_t second[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 };
	static const uint8_t alias[] = { 3, 'w', 'w', 'w', 0xc0, 0x0c };
	struct resolver_result result;
	uint8_t message[512];
	uint8_t address[16];
	char name[128];
	size_t length;
	size_t at;
	int truncated;
	int error;

	/* An AAAA question: its type and class after the name. */
	error = resolver_dns_build_query(message, sizeof(message), 0x1234U, "example.org", DNS_TYPE_AAAA, &length);
	check(error == 0 && length == 29U && message[25] == 0 && message[26] == 28U && message[28] == 1U, "AAAA question built");

	/* Its answer: a CNAME, then two AAAA records. */
	message[2] = 0x81;
	message[3] = 0x80;
	message[7] = 3;
	at = put_answer(message, length, DNS_TYPE_CNAME, alias, sizeof(alias));
	at = put_answer(message, at, DNS_TYPE_AAAA, first, 16U);
	at = put_answer(message, at, DNS_TYPE_AAAA, second, 16U);
	memset(&result, 0, sizeof(result));
	error = resolver_dns_parse(message, at, 0x1234U, "example.org", DNS_TYPE_AAAA, &result, &truncated);
	check(error == 0 && result.address6_count == 2U, "AAAA answer: two addresses");
	check(memcmp(result.addresses6[0].s6_addr, first, 16U) == 0 && memcmp(result.addresses6[1].s6_addr, second, 16U) == 0, "AAAA answer: in their order");
	check(strcmp(result.canonical, "www.example.org") == 0 && result.address_count == 0U, "AAAA answer: the CNAME, no IPv4 address");

	/* An answer with the CNAME only: no address. */
	message[7] = 1;
	at = put_answer(message, length, DNS_TYPE_CNAME, alias, sizeof(alias));
	memset(&result, 0, sizeof(result));
	error = resolver_dns_parse(message, at, 0x1234U, "example.org", DNS_TYPE_AAAA, &result, &truncated);
	check(error == EAI_NONAME && strcmp(result.canonical, "www.example.org") == 0, "AAAA answer without an address: no name, the CNAME kept");

	/* The PTR name: the nibbles from the last, then ip6.arpa. */
	(void)inet_pton(AF_INET6, "2001:db8::567:89ab", address);
	error = resolver_inet6_ptr_name(address, name, sizeof(name));
	check(error == 0 && strcmp(name, "b.a.9.8.7.6.5.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.b.d.0.1.0.0.2.ip6.arpa") == 0, "PTR name of 2001:db8::567:89ab");
	error = resolver_inet6_ptr_name(address, name, 72U);
	check(error == EAI_OVERFLOW, "PTR name too long for 72 bytes");

	/* IPv6 first only from a source that reaches the destination. */
	check(preferred("2001:db8::1", "2001:db8::100") == 1, "global source to a global destination: IPv6 first");
	check(preferred("2001:db8::1", "fe80::1") == 0, "link-local source to a global destination: IPv4 first");
	check(preferred("fe80::2", "fe80::1") == 1, "link-local source to a link-local destination: IPv6 first");
	check(preferred("2001:db8::1", "::") == 0, "no source: IPv4 first");
	check(preferred("::1", "::1") == 1, "the loopback from the loopback");
	check(preferred("2001:db8::1", "::1") == 0, "the loopback as a source beyond the host: IPv4 first");

	/* The result. */
	if (test_failures != 0) {
		printf("host-libc6: %d failed\n", test_failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-libc6: PASS\n");
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

/* Writes an answer record (its name a pointer to the question's) at an offset; returns the offset after it. */
static size_t
put_answer(
	uint8_t *message,
	size_t at,
	uint16_t type,
	const uint8_t *data,
	uint16_t length)
{
	/* The name, the type, the class, the time to live and the length. */
	message[at++] = 0xc0;
	message[at++] = 0x0c;
	message[at++] = (uint8_t)(type >> 8);
	message[at++] = (uint8_t)type;
	message[at++] = 0;
	message[at++] = 1;
	message[at++] = 0;
	message[at++] = 0;
	message[at++] = 0x0e;
	message[at++] = 0x10;
	message[at++] = (uint8_t)(length >> 8);
	message[at++] = (uint8_t)length;

	/* The data. */
	memcpy(message + at, data, length);
	return at + length;
}

/* Tells whether an IPv6 destination comes first from a source, both as text. */
static int
preferred(
	const char *destination,
	const char *source)
{
	uint8_t to[16];
	uint8_t from[16];
	int answer;

	/* The two addresses. */
	(void)inet_pton(AF_INET6, destination, to);
	(void)inet_pton(AF_INET6, source, from);

	/* The resolver's answer. */
	answer = resolver_inet6_preferred(to, from);
	return answer;
}
