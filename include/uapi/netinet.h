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

/*
 * Options at IPPROTO_TCP.  TCP_NODELAY is the one POSIX names.
 */
#define TCP_NODELAY		1

#define INADDR_ANY		0x00000000U
#define INADDR_BROADCAST	0xffffffffU
#define INADDR_LOOPBACK		0x7f000001U

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
