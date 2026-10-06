/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Declares the zedBSD C library resolver internal interface.
 */

#ifndef KERN_RESOLVER_INTERNAL_H
#define KERN_RESOLVER_INTERNAL_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define DNS_TYPE_A 1U
#define DNS_TYPE_CNAME 5U
#define DNS_TYPE_PTR 12U
#define DNS_TYPE_AAAA 28U
#define DNS_MAX_ADDRESSES 8U
#define DNS_MAX_NAMESERVERS 3U

struct resolver_result {
	struct in_addr addresses[DNS_MAX_ADDRESSES];
	unsigned address_count;
	struct in6_addr addresses6[DNS_MAX_ADDRESSES];	/* AAAA (ws130-p004) */
	unsigned address6_count;
	char canonical[254];
	char ptr_name[254];
	char cname_chain[8][254];
	unsigned cname_count;
	uint32_t ttl;
	struct in_addr server;
	uint16_t port;
};

/*
 * A name server of resolv.conf in its order: an IPv4 one, or an IPv6 one with
 * the interface of a link-local address (ws130-p004).
 */
struct resolver_server {
	int family;
	struct in_addr address;
	struct in6_addr address6;
	uint32_t scope;
};

/* The IPv4 name servers (servers, count; nslookup's), and every name server in resolv.conf's order. */
struct resolver_config {
	struct in_addr servers[DNS_MAX_NAMESERVERS];
	unsigned count;
	struct resolver_server list[DNS_MAX_NAMESERVERS];
	unsigned list_count;
};

int resolver_dns_build_query(uint8_t *, size_t, uint16_t, const char *,
			     uint16_t, size_t *);
int resolver_dns_parse(const uint8_t *, size_t, uint16_t, const char *,
		       uint16_t, struct resolver_result *, int *);
int resolver_load_config(struct resolver_config *);
int resolver_query_server(const char *, uint16_t, const struct in_addr *,
			  uint16_t, struct resolver_result *);
int resolver_query(const char *, uint16_t, struct resolver_result *);

/* IPv6 (ws130-p004): an address's PTR name, and whether a destination comes before IPv4 by its source (RFC 6724). */
int resolver_inet6_ptr_name(const uint8_t *address, char *output, size_t capacity);
int resolver_inet6_preferred(const uint8_t *destination, const uint8_t *source);

#endif
