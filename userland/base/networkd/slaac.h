/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * IPv6 stateless address autoconfiguration for networkd (ws130-p006,
 * plan/ws130/phase001/phase.md section 4): what a Router Advertisement says
 * (RFC 4861; its prefixes, router lifetime, M and O flags, and RDNSS and
 * DNSSL of RFC 8106), a stable interface identifier (RFC 7217) and a
 * temporary one's lifetimes (RFC 8981), and an address from a prefix and
 * an identifier.  Pure: the host tests build this file alone.
 */

#ifndef NETWORKD_SLAAC_H
#define NETWORKD_SLAAC_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

/* The most prefixes and DNS servers one advertisement gives, and the longest search list kept. */
#define SLAAC_PREFIXES_MAX	8U
#define SLAAC_DNS_MAX		3U
#define SLAAC_SEARCH_MAX	256U

/* The advertisement's flags: addresses (M) and other information (O) from DHCPv6. */
#define SLAAC_RA_MANAGED	0x80U
#define SLAAC_RA_OTHER		0x40U

/* A prefix of the advertisement: its length, on-link (L) and autonomous (A), and its lifetimes in seconds. */
struct slaac_prefix {
	struct in6_addr prefix;
	unsigned length;
	int onlink;
	int autonomous;
	uint32_t valid;
	uint32_t preferred;
};

/*
 * What an advertisement says: its M and O flags, its router lifetime
 * (seconds; 0 is not a default router), its prefixes, its DNS servers
 * (RDNSS) with their lifetime, and its search list (DNSSL, the names
 * separated by spaces).
 */
struct slaac_ra {
	unsigned flags;
	uint32_t router_lifetime;
	struct slaac_prefix prefixes[SLAAC_PREFIXES_MAX];
	unsigned prefix_count;
	struct in6_addr dns[SLAAC_DNS_MAX];
	unsigned dns_count;
	uint32_t dns_lifetime;
	char search[SLAAC_SEARCH_MAX];
};

/* The longest a temporary address lives (RFC 8981: two days valid, one day preferred), in seconds. */
#define SLAAC_TEMPORARY_VALID		(2U * 86400U)
#define SLAAC_TEMPORARY_PREFERRED	86400U

int slaac_parse(const uint8_t *message, size_t length, struct slaac_ra *ra);
void slaac_stable_iid(const uint8_t *secret, size_t secret_length, const struct in6_addr *prefix, const char *interface,
    const char *network, unsigned dad_counter, uint8_t *iid);
void slaac_address(const struct in6_addr *prefix, const uint8_t *iid, struct in6_addr *address);
void slaac_temporary_lifetimes(uint32_t valid, uint32_t preferred, uint32_t desync, uint32_t *temporary_valid,
    uint32_t *temporary_preferred);

#endif
