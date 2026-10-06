/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws130-p006: the host test of networkd's SLAAC (userland/base/networkd/slaac.c
 * with the host's headers): a Router Advertisement read (its flags, router
 * lifetime, a prefix, RDNSS and DNSSL), the refused ones, the stable
 * interface identifier (the same inputs the same, any input changed
 * another), an address from a prefix, and a temporary address's lifetimes.
 */

#include "userland/base/networkd/slaac.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int test_failures;

static void check(int condition, const char *what);

/* An advertisement: O, router lifetime 1800; a prefix 2001:db8:1::/64 (L, A, 86400, 14400); RDNSS of two (600); DNSSL. */
static const uint8_t test_ra[] = {
	134, 0, 0, 0, 64, 0x40, 0x07, 0x08, 0, 0, 0, 0, 0, 0, 0, 0,
	3, 4, 64, 0xc0, 0x00, 0x01, 0x51, 0x80, 0x00, 0x00, 0x38, 0x40, 0, 0, 0, 0,
	0x20, 0x01, 0x0d, 0xb8, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	25, 5, 0, 0, 0, 0, 0x02, 0x58,
	0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x53,
	0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	31, 4, 0, 0, 0, 0, 0x02, 0x58,
	7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0,
	4, 'c', 'o', 'r', 'p', 3, 'n', 'e', 't', 0, 0
};

/* Runs the checks; the exit status is 1 when one failed. */
int
main(void)
{
	static const uint8_t secret[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	struct slaac_ra ra;
	struct in6_addr prefix;
	struct in6_addr other;
	struct in6_addr address;
	uint8_t iid[8];
	uint8_t again[8];
	uint8_t different[8];
	uint8_t bad[sizeof(test_ra)];
	char text[INET6_ADDRSTRLEN];
	uint32_t valid;
	uint32_t preferred;
	int status;

	/* The advertisement read. */
	status = slaac_parse(test_ra, sizeof(test_ra), &ra);
	check(status == 0, "read");
	check(ra.flags == SLAAC_RA_OTHER && ra.router_lifetime == 1800U, "the O flag and the router lifetime");
	check(ra.prefix_count == 1U && ra.prefixes[0].length == 64U && ra.prefixes[0].onlink && ra.prefixes[0].autonomous, "the prefix: /64, L, A");
	check(ra.prefixes[0].valid == 86400U && ra.prefixes[0].preferred == 14400U, "the prefix's lifetimes");
	(void)inet_ntop(AF_INET6, &ra.prefixes[0].prefix, text, sizeof(text));
	check(strcmp(text, "2001:db8:1::") == 0, "the prefix's address");
	(void)inet_ntop(AF_INET6, &ra.dns[1], text, sizeof(text));
	check(ra.dns_count == 2U && ra.dns_lifetime == 600U && strcmp(text, "fe80::1") == 0, "RDNSS: two servers, 600 s");
	check(strcmp(ra.search, "example.com corp.net") == 0, "DNSSL: the two names");

	/* Refused: not an advertisement, and an option past the end. */
	memcpy(bad, test_ra, sizeof(bad));
	bad[0] = 133;
	status = slaac_parse(bad, sizeof(bad), &ra);
	check(status != 0, "refused: a solicitation");
	memcpy(bad, test_ra, sizeof(bad));
	bad[17] = 40;
	status = slaac_parse(bad, sizeof(bad), &ra);
	check(status != 0, "refused: an option past the end");

	/* The stable identifier: the same inputs the same; another prefix, counter or network another. */
	(void)inet_pton(AF_INET6, "2001:db8:1::", &prefix);
	(void)inet_pton(AF_INET6, "2001:db8:2::", &other);
	slaac_stable_iid(secret, sizeof(secret), &prefix, "ue0", "", 0, iid);
	slaac_stable_iid(secret, sizeof(secret), &prefix, "ue0", "", 0, again);
	check(memcmp(iid, again, 8U) == 0, "stable: the same inputs, the same identifier");
	slaac_stable_iid(secret, sizeof(secret), &other, "ue0", "", 0, different);
	check(memcmp(iid, different, 8U) != 0, "stable: another prefix, another identifier");
	slaac_stable_iid(secret, sizeof(secret), &prefix, "ue0", "", 1, different);
	check(memcmp(iid, different, 8U) != 0, "stable: another DAD counter, another identifier");
	slaac_stable_iid(secret, sizeof(secret), &prefix, "ue0", "home", 0, different);
	check(memcmp(iid, different, 8U) != 0, "stable: another network, another identifier");

	/* An address: the prefix's 64 bits and the identifier's. */
	slaac_address(&prefix, iid, &address);
	check(memcmp(address.s6_addr, prefix.s6_addr, 8U) == 0 && memcmp(address.s6_addr + 8, iid, 8U) == 0, "the address");

	/* A temporary address's lifetimes: at most two days valid, a day less the desync preferred, no longer than the prefix's. */
	slaac_temporary_lifetimes(259200U, 200000U, 600U, &valid, &preferred);
	check(valid == 172800U && preferred == 85800U, "temporary: capped at two days and a day less the desync");
	slaac_temporary_lifetimes(3600U, 1800U, 600U, &valid, &preferred);
	check(valid == 3600U && preferred == 1800U, "temporary: the prefix's when shorter");

	/* The result. */
	if (test_failures != 0) {
		printf("host-slaac: %d failed\n", test_failures);
		return 1;
	}

	/* Succeeded. */
	printf("host-slaac: PASS\n");
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
