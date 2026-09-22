/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * netinet
 */

#ifndef KERN_UAPI_NETINET_H
#define KERN_UAPI_NETINET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <uapi/socket.h>
#include <stdint.h>

#define IPPROTO_IP		0
#define IPPROTO_ICMP		1
#define IPPROTO_TCP		6
#define IPPROTO_UDP		17
#define IPPROTO_IPV6		41

/*
 * Options at IPPROTO_TCP.  TCP_NODELAY is the one POSIX names.
 */
#define TCP_NODELAY		1

#define INADDR_ANY		0x00000000U
#define INADDR_BROADCAST	0xffffffffU
#define INADDR_LOOPBACK		0x7f000001U

/* The network the loopback address belongs to. */
#define IN_LOOPBACKNET		127

/*
 * Ports below this one are for a program that had to be privileged to bind
 * them, which is the whole of what the number means.
 */
#define IPPORT_RESERVED		1024

/*
 * POSIX names for the address and port types carried in struct sockaddr_in.
 * Portable software declares variables with them rather than with uint32_t.
 */
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
	in_addr_t s_addr;
};

struct sockaddr_in {
	sa_family_t sin_family;
	uint16_t sin_port;
	struct in_addr sin_addr;
	uint8_t sin_zero[8];
};

/*
 * The second family's address, and the socket address that carries it.
 *
 * Nothing routes this protocol yet; these are here so that software which
 * knows both families can be compiled, and can then be told at run time
 * which one this system carries.  The flow and scope words are part of the
 * address as it is written down, and are kept even though nothing reads
 * them.
 */
struct in6_addr {
	union {
		uint8_t __u6_addr8[16];
		uint16_t __u6_addr16[8];
		uint32_t __u6_addr32[4];
	} __u6_addr;
};

#define s6_addr		__u6_addr.__u6_addr8
#define s6_addr16	__u6_addr.__u6_addr16
#define s6_addr32	__u6_addr.__u6_addr32

struct sockaddr_in6 {
	sa_family_t sin6_family;
	uint16_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

/* Joining and leaving a group, for software that names the structure. */
struct ipv6_mreq {
	struct in6_addr ipv6mr_multiaddr;
	unsigned ipv6mr_interface;
};

/* The two addresses every implementation publishes by name. */
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

#define IN6ADDR_ANY_INIT	{ { { 0, 0, 0, 0, 0, 0, 0, 0, \
				      0, 0, 0, 0, 0, 0, 0, 0 } } }
#define IN6ADDR_LOOPBACK_INIT	{ { { 0, 0, 0, 0, 0, 0, 0, 0, \
				      0, 0, 0, 0, 0, 0, 0, 1 } } }

/*
 * Room for the longest written form of an address of the second family.
 * The first family keeps its own in <arpa/inet.h>, where it already was.
 */
#define INET6_ADDRSTRLEN	46

uint16_t
htons(
	uint16_t value);

uint16_t
ntohs(
	uint16_t value);

uint32_t
htonl(
	uint32_t value);

uint32_t
ntohl(
	uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
