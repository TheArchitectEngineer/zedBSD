/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Packet buffer
 */

#ifndef KERN_KERN_NET_PACKET_BUF_H
#define KERN_KERN_NET_PACKET_BUF_H

#include <kern/atomic.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The packets of the whole stack, in two classes of one pool: standard
 * packets of PACKET_BUF_STORAGE_SIZE, and on the 64-bit machines a few
 * jumbo packets large enough for a 9000-byte frame with its headroom.  A
 * 64-bit machine's TCP offers a window of tens of kilobytes, a packet for
 * each segment of it; the 32-bit boards keep the small pool their memory
 * map was laid out for and have no jumbo packets.
 *
 * PACKET_BUF_JUMBO_RESERVE jumbo packets are kept for the drivers'
 * receive (PACKET_BUF_ALLOC_RECEIVE): a sender or a copy that would take
 * one of the last of them is refused, so the frames that arrive still
 * find a packet while sockets hold the others.
 */
#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64) || \
    defined(HAL_ARCH_RISCV64)
#define PACKET_BUF_POOL_COUNT		256U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	9216U
#define PACKET_BUF_JUMBO_COUNT		32U
#define PACKET_BUF_JUMBO_RESERVE	8U
#else
#define PACKET_BUF_POOL_COUNT		32U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	2048U
#define PACKET_BUF_JUMBO_COUNT		0U
#define PACKET_BUF_JUMBO_RESERVE	0U
#endif
#define PACKET_BUF_STORAGE_SIZE	2048U

/* The largest storage any packet has: what one frame and its headroom may take. */
#define PACKET_BUF_MAX_STORAGE_SIZE	PACKET_BUF_JUMBO_STORAGE_SIZE

/* packet_buf_alloc_for: the caller is a driver's receive, which may take the reserved jumbo packets. */
#define PACKET_BUF_ALLOC_RECEIVE	0x1U
#define PACKET_BUF_DEFAULT_HEADROOM	64U
#define PACKET_OFFSET_NONE	UINT16_MAX

struct net_device;

/*
 * The packet pool's use at one moment (packet_buf_pool_usage): for the
 * standard packets and then the jumbo ones, the class's size, the packets
 * taken now and the most ever taken, and how many allocations found none
 * free (for the jumbo class, none outside the reserve), and the jumbo
 * packets kept for the drivers' receive.
 */
struct packet_buf_pool_usage {
	unsigned total;
	unsigned used;
	unsigned used_max;
	uint64_t alloc_failures;
	unsigned jumbo_total;
	unsigned jumbo_used;
	unsigned jumbo_used_max;
	unsigned jumbo_reserve;
	uint64_t jumbo_alloc_failures;
};

struct packet_buf {
	uint8_t *storage;
	uint8_t *data;
	size_t capacity;
	size_t length;
	uint16_t l2_offset;
	uint16_t l3_offset;
	uint16_t l4_offset;
	uint16_t l3_length;
	uint16_t protocol;
	uint32_t flags;
	struct net_device *device;
	struct packet_buf *next;
	refcount_t refcount;
	/*
	 * Large enough for sockaddr_storage.  Network drivers may use only the

	 * * leading bytes; AF_UNIX datagrams retain their complete source name.
	 */
	uint8_t source_address[128];
	uint8_t source_length;
	/*
	 * The hop limit an IPv6 datagram came with, kept on the copy queued
	 * for a socket that asked for it (IPV6_RECVHOPLIMIT, ws177-p044);
	 * hop_limit_known says it was kept.
	 */
	uint8_t hop_limit;
	uint8_t hop_limit_known;
	void *control;
	void (
		*control_release)(
		void *);
};

void
packet_buf_pool_init(void);
struct packet_buf *
packet_buf_alloc(
	size_t headroom);
struct packet_buf *
packet_buf_alloc_for(
	size_t headroom,
	size_t length,
	unsigned flags,
	int *error);
void
packet_buf_ref(
	struct packet_buf *packet);
void
packet_buf_free(
	struct packet_buf *packet);
size_t
packet_buf_headroom(
	const struct packet_buf *packet);
size_t
packet_buf_tailroom(
	const struct packet_buf *packet);
void *
packet_buf_push(
	struct packet_buf *packet,
	size_t length);
void *
packet_buf_pull(
	struct packet_buf *packet,
	size_t length);
void *
packet_buf_append(
	struct packet_buf *packet,
	size_t length);
int
packet_buf_trim(
	struct packet_buf *packet,
	size_t length);
struct packet_buf *
packet_buf_copy(
	const struct packet_buf *packet);
struct packet_buf *
packet_buf_copy_region(
	const struct packet_buf *packet,
	size_t offset,
	size_t length);
unsigned
packet_buf_in_use(void);
void
packet_buf_pool_usage(
	struct packet_buf_pool_usage *usage);

#endif
