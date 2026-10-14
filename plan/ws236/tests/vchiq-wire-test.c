/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws236-p002: the VCHIQ shared-memory layout, message framing, slot
 * recycling and page lists (rpi4-vchiq-wire.c) on the host, against a
 * simulated master (the VPU's side) written here from the protocol's rules.
 */

#include "drivers/platform/rpi4/rpi4-vchiq-wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/errno.h>

/* The checks that failed. */
static unsigned failures;

/* The simulated master's read and write positions in the two streams. */
static uint32_t master_rx_pos;
static uint32_t master_tx_pos;

static void check(int condition, const char *what);
static uint8_t *slot(struct drv_rpi4_vchiq_ring *ring, uint32_t index);
static void master_init(struct drv_rpi4_vchiq_ring *ring);
static unsigned master_read(struct drv_rpi4_vchiq_ring *ring, uint32_t *expected_id, unsigned expected_count);
static int master_write(struct drv_rpi4_vchiq_ring *ring, uint32_t msgid, uint32_t size, uint8_t fill);
static void test_layout(void);
static void test_stream_to_master(void);
static void test_stream_from_master(void);
static void test_pagelist(void);
static void test_helpers(void);

/* The kernel's barriers and memset, as the wire file uses them. */
void kern_io_barrier(void) { __sync_synchronize(); }
void kern_io_read_barrier(void) { __sync_synchronize(); }
void kern_io_write_barrier(void) { __sync_synchronize(); }
void *kern_memset(void *destination, int value, size_t bytes) { return memset(destination, value, bytes); }

int
main(void)
{
	test_helpers();
	test_layout();
	test_stream_to_master();
	test_stream_from_master();
	test_pagelist();
	if (failures != 0) {
		printf("vchiq-wire: %u check(s) failed\n", failures);
		return 1;
	}
	printf("vchiq-wire: all PASS\n");
	return 0;
}

/* Counts and reports a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* The CPU address of a slot. */
static uint8_t *
slot(
	struct drv_rpi4_vchiq_ring *ring,
	uint32_t index)
{
	return ring->base + (size_t)index * DRV_RPI4_VCHIQ_SLOT_SIZE;
}

/* Lays out the master's half as the firmware would. */
static void
master_init(
	struct drv_rpi4_vchiq_ring *ring)
{
	struct drv_rpi4_vchiq_shared *master;
	uint32_t index;
	uint32_t count;

	master = &ring->zero->master;
	count = 0;
	for (index = master->slot_first; index <= master->slot_last; index++) {
		master->slot_queue[count] = index;
		count++;
	}
	master->slot_queue_recycle = count;
	master->initialised = 1;
	master_rx_pos = 0;
	master_tx_pos = 0;
}

/*
 * Reads the ARM's stream as the master would, checking each data message's
 * id and payload, and recycles each slot read to its end.  Reports the
 * messages read.
 */
static unsigned
master_read(
	struct drv_rpi4_vchiq_ring *ring,
	uint32_t *expected_id,
	unsigned expected_count)
{
	struct drv_rpi4_vchiq_shared *slave;
	uint8_t *data;
	uint32_t msgid;
	uint32_t size;
	uint32_t index;
	unsigned count;
	char text[96];

	slave = &ring->zero->slave;
	count = 0;
	while (master_rx_pos != slave->tx_pos) {
		data = slot(ring, slave->slot_queue[(master_rx_pos / DRV_RPI4_VCHIQ_SLOT_SIZE) & 63U]);
		data += master_rx_pos % DRV_RPI4_VCHIQ_SLOT_SIZE;
		msgid = ((uint32_t *)data)[0];
		size = ((uint32_t *)data)[1];
		if (drv_rpi4_vchiq_msgid_type(msgid) != DRV_RPI4_VCHIQ_MSG_PADDING) {
			snprintf(text, sizeof(text), "master reads message %u in order", count);
			check(count < expected_count && msgid == expected_id[count], text);
			for (index = 0; index < size; index++) {
				if (data[8U + index] != (uint8_t)(msgid + index)) {
					check(0, "master reads the payload written");
					break;
				}
			}
			count++;
		}
		master_rx_pos += drv_rpi4_vchiq_stride(size);
		if (master_rx_pos % DRV_RPI4_VCHIQ_SLOT_SIZE == 0) {
			slave->slot_queue[slave->slot_queue_recycle & 63U] =
				slave->slot_queue[((master_rx_pos / DRV_RPI4_VCHIQ_SLOT_SIZE) - 1U) & 63U];
			slave->slot_queue_recycle++;
		}
	}
	return count;
}

/* Writes one message into the master's stream; EAGAIN when it has no free slot. */
static int
master_write(
	struct drv_rpi4_vchiq_ring *ring,
	uint32_t msgid,
	uint32_t size,
	uint8_t fill)
{
	struct drv_rpi4_vchiq_shared *master;
	uint32_t stride;
	uint32_t space;
	uint32_t position;
	uint8_t *data;

	master = &ring->zero->master;
	stride = drv_rpi4_vchiq_stride(size);
	position = master_tx_pos;
	space = DRV_RPI4_VCHIQ_SLOT_SIZE - position % DRV_RPI4_VCHIQ_SLOT_SIZE;
	if (position % DRV_RPI4_VCHIQ_SLOT_SIZE != 0 && stride > space)
		position += space;
	if (position % DRV_RPI4_VCHIQ_SLOT_SIZE == 0 &&
	    (int32_t)(master->slot_queue_recycle - position / DRV_RPI4_VCHIQ_SLOT_SIZE) <= 0)
		return EAGAIN;
	if (position != master_tx_pos) {
		data = slot(ring, master->slot_queue[(master_tx_pos / DRV_RPI4_VCHIQ_SLOT_SIZE) & 63U]);
		data += master_tx_pos % DRV_RPI4_VCHIQ_SLOT_SIZE;
		((uint32_t *)data)[0] = 0;
		((uint32_t *)data)[1] = space - 8U;
	}
	data = slot(ring, master->slot_queue[(position / DRV_RPI4_VCHIQ_SLOT_SIZE) & 63U]);
	data += position % DRV_RPI4_VCHIQ_SLOT_SIZE;
	memset(data + 8, fill, size);
	((uint32_t *)data)[0] = msgid;
	((uint32_t *)data)[1] = size;
	master_tx_pos = position + stride;
	master->tx_pos = master_tx_pos;
	return 0;
}

/* Slot zero carries the configuration and the ARM's initial state. */
static void
test_layout(void)
{
	struct drv_rpi4_vchiq_ring ring;
	uint8_t *memory;
	size_t bytes;
	int error;

	bytes = drv_rpi4_vchiq_slot_bytes();
	check(bytes == 65U * 4096U, "slot memory is slot zero and 64 data slots");
	memory = aligned_alloc(4096, bytes);
	memset(memory, 0xa5, bytes);
	error = drv_rpi4_vchiq_ring_init(&ring, memory + 8, bytes, 0, 0);
	check(error == EINVAL, "unaligned memory is refused");
	error = drv_rpi4_vchiq_ring_init(&ring, memory, bytes - 1U, 0, 0);
	check(error == EINVAL, "short memory is refused");
	error = drv_rpi4_vchiq_ring_init(&ring, memory, bytes, 0xc1234000U, 64U);
	check(error == 0, "slot zero is laid out");
	check(ring.zero->magic == 0x56434849U, "magic is VCHI");
	check(ring.zero->version == 8U && ring.zero->version_min == 3U, "versions 8 and 3");
	check(ring.zero->slot_zero_size == 1288U, "slot zero size 1288");
	check(ring.zero->slot_size == 4096U, "slot size 4096");
	check(ring.zero->max_slots == 128U && ring.zero->max_slots_per_side == 64U, "slot counts 128 and 64");
	check(ring.zero->platform_data[0] == 0xc1234000U && ring.zero->platform_data[1] == 64U, "fragment area");
	check(ring.zero->master.slot_sync == 1U, "master sync slot 1");
	check(ring.zero->master.slot_first == 2U && ring.zero->master.slot_last == 32U, "master slots 2-32");
	check(ring.zero->slave.slot_sync == 33U, "slave sync slot 33");
	check(ring.zero->slave.slot_first == 34U && ring.zero->slave.slot_last == 64U, "slave slots 34-64");
	check(ring.local == &ring.zero->slave && ring.remote == &ring.zero->master, "the ARM is the slave");
	check(ring.slot_queue_available == 31U && ring.local->slot_queue_recycle == 31U, "31 ARM slots queued");
	check(ring.local->slot_queue[0] == 34U && ring.local->slot_queue[30] == 64U, "ARM queue holds its slots in order");
	check(ring.local->initialised == 1U && ring.zero->master.initialised == 0U, "only the ARM is initialised");
	check(ring.local->debug[0] == 11U, "debug count 11");
	check(ring.local->sync_release.fired == 1U && ring.local->trigger.fired == 0U, "sync slot free, no trigger");
	check(((uint32_t *)slot(&ring, 33))[0] == 0U, "sync slot holds padding");
	check(offsetof(struct drv_rpi4_vchiq_slot_zero, master) == 32U, "master state at 32");
	check(offsetof(struct drv_rpi4_vchiq_slot_zero, slave) == 404U, "slave state at 404");
	check(offsetof(struct drv_rpi4_vchiq_slot_zero, slots) == 776U, "slot infos at 776");
	check(offsetof(struct drv_rpi4_vchiq_shared, tx_pos) == 28U, "tx_pos at 28");
	check(offsetof(struct drv_rpi4_vchiq_shared, slot_queue) == 72U, "slot queue at 72");
	free(memory);
}

/*
 * The ARM writes far more than its slots hold; the master reads and
 * recycles, and every message arrives once, in order, with its payload.
 */
static void
test_stream_to_master(void)
{
	struct drv_rpi4_vchiq_ring ring;
	uint32_t expected[4000];
	uint8_t *memory;
	uint8_t *payload;
	uint32_t size;
	uint32_t msgid;
	uint32_t index;
	unsigned sent;
	unsigned read;
	unsigned stalls;
	int error;

	memory = aligned_alloc(4096, drv_rpi4_vchiq_slot_bytes());
	memset(memory, 0, drv_rpi4_vchiq_slot_bytes());
	(void)drv_rpi4_vchiq_ring_init(&ring, memory, drv_rpi4_vchiq_slot_bytes(), 0, 0);
	master_init(&ring);
	sent = 0;
	read = 0;
	stalls = 0;
	error = drv_rpi4_vchiq_ring_reserve(&ring, DRV_RPI4_VCHIQ_MAX_PAYLOAD + 1U, &payload);
	check(error == EMSGSIZE, "an oversized message is refused");
	while (sent < 4000U) {
		size = (sent * 37U) % 700U;
		if (sent % 97U == 0)
			size = DRV_RPI4_VCHIQ_MAX_PAYLOAD;
		msgid = drv_rpi4_vchiq_msgid(DRV_RPI4_VCHIQ_MSG_DATA, 1U + sent % 7U, sent % 4096U);
		error = drv_rpi4_vchiq_ring_reserve(&ring, size, &payload);
		if (error == EAGAIN) {
			stalls++;
			read += master_read(&ring, expected + read, sent - read);
			drv_rpi4_vchiq_ring_recycle(&ring);
			continue;
		}
		check(error == 0, "a reservation succeeds");
		for (index = 0; index < size; index++)
			payload[index] = (uint8_t)(msgid + index);
		drv_rpi4_vchiq_ring_commit(&ring, msgid, size);
		expected[sent] = msgid;
		sent++;
	}
	read += master_read(&ring, expected + read, sent - read);
	check(read == 4000U, "the master read every message");
	check(stalls > 0, "the ARM ran out of slots and waited for recycling");
	free(memory);
}

/*
 * The master writes; the ARM reads in order and hands each slot back into
 * the master's queue at the slot's end.
 */
static void
test_stream_from_master(void)
{
	struct drv_rpi4_vchiq_ring ring;
	uint8_t *memory;
	const uint8_t *payload;
	uint32_t msgid;
	uint32_t size;
	uint32_t index;
	uint32_t written;
	uint32_t received;
	uint32_t released;
	uint32_t recycle_before;
	int error;
	bool back;

	memory = aligned_alloc(4096, drv_rpi4_vchiq_slot_bytes());
	memset(memory, 0, drv_rpi4_vchiq_slot_bytes());
	(void)drv_rpi4_vchiq_ring_init(&ring, memory, drv_rpi4_vchiq_slot_bytes(), 0, 0);
	master_init(&ring);
	error = drv_rpi4_vchiq_ring_next(&ring, &msgid, &size, &payload);
	check(error == EAGAIN, "nothing to read at first");
	written = 0;
	received = 0;
	released = 0;
	recycle_before = ring.remote->slot_queue_recycle;
	while (written < 3000U) {
		error = master_write(&ring, drv_rpi4_vchiq_msgid(DRV_RPI4_VCHIQ_MSG_DATA, 3, written % 8U + 1U),
				     (written * 53U) % 900U, (uint8_t)written);
		if (error == 0) {
			written++;
			continue;
		}
		for (;;) {
			error = drv_rpi4_vchiq_ring_next(&ring, &msgid, &size, &payload);
			if (error != 0)
				break;
			if (drv_rpi4_vchiq_msgid_type(msgid) == DRV_RPI4_VCHIQ_MSG_DATA) {
				check(drv_rpi4_vchiq_msgid_destination(msgid) == received % 8U + 1U, "ARM reads in order");
				check(size == (received * 53U) % 900U, "ARM reads the size written");
				for (index = 0; index < size; index++) {
					if (payload[index] != (uint8_t)received) {
						check(0, "ARM reads the payload written");
						break;
					}
				}
				received++;
			}
			back = drv_rpi4_vchiq_ring_advance(&ring, size);
			if (back)
				released++;
		}
		check(error == EAGAIN, "the master's stream is well formed");
	}
	for (;;) {
		error = drv_rpi4_vchiq_ring_next(&ring, &msgid, &size, &payload);
		if (error != 0)
			break;
		if (drv_rpi4_vchiq_msgid_type(msgid) == DRV_RPI4_VCHIQ_MSG_DATA)
			received++;
		back = drv_rpi4_vchiq_ring_advance(&ring, size);
		if (back)
			released++;
	}
	check(written == 3000U && received == written, "the ARM read every message");
	check(released > 31U, "slots went back to the master many times");
	check(ring.remote->slot_queue_recycle - recycle_before == released, "each release moved the master's recycle count");
	free(memory);
}

/* Page lists in the Pi 4's long bulk form. */
static void
test_pagelist(void)
{
	uint32_t words[32];
	size_t used;
	int error;

	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x12345000ULL, 0x3000U, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == 0 && used == 12U, "three aligned pages make one word");
	check(words[0] == 0x3000U && words[1] == 1U, "length, type READ, offset 0");
	check(words[2] == ((0x12345U << 8) | 2U), "page number and two more pages");

	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x1000100ULL, 0x2000U, DRV_RPI4_VCHIQ_PAGELIST_WRITE, &used);
	check(error == 0 && used == 12U, "an unaligned start touches three pages");
	check(words[1] == (0x100U << 16), "type WRITE and offset 0x100");
	check(words[2] == ((0x1000U << 8) | 2U), "the unaligned run");

	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x40000000ULL, 600U * 4096U, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == 0 && used == 20U, "600 pages make three words");
	check(words[2] == ((0x40000U << 8) | 255U), "first run of 256");
	check(words[3] == ((0x40100U << 8) | 255U), "second run of 256");
	check(words[4] == ((0x40200U << 8) | 87U), "last run of 88");

	error = drv_rpi4_vchiq_pagelist(words, 16U, 0x40000000ULL, 600U * 4096U, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == ENOSPC, "a list too long for its room is refused");
	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x1000000000ULL, 4096U, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == ERANGE, "a page above 64 GiB is refused");
	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x1000ULL, 0, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == EINVAL, "an empty transfer is refused");
	error = drv_rpi4_vchiq_pagelist(words, sizeof(words), 0x200000000ULL, 4096U, DRV_RPI4_VCHIQ_PAGELIST_READ, &used);
	check(error == 0 && words[2] == (0x200000U << 8), "a page above 4 GiB (8 GiB Pi 4)");
}

/* Message ids, strides and events. */
static void
test_helpers(void)
{
	struct drv_rpi4_vchiq_event event;
	uint32_t msgid;
	bool wake;

	msgid = drv_rpi4_vchiq_msgid(DRV_RPI4_VCHIQ_MSG_BULK_RX, 0x123U, 0xfffU);
	check(msgid == 0x06123fffU, "message id layout");
	check(drv_rpi4_vchiq_msgid_type(msgid) == 6U, "type read back");
	check(drv_rpi4_vchiq_msgid_source(msgid) == 0x123U, "source read back");
	check(drv_rpi4_vchiq_msgid_destination(msgid) == 0xfffU, "destination read back");
	check(drv_rpi4_vchiq_stride(0) == 8U, "stride of an empty message");
	check(drv_rpi4_vchiq_stride(1) == 16U, "stride of one byte");
	check(drv_rpi4_vchiq_stride(8) == 16U, "stride of eight bytes");
	check(drv_rpi4_vchiq_stride(9) == 24U, "stride of nine bytes");
	check(drv_rpi4_vchiq_stride(DRV_RPI4_VCHIQ_MAX_PAYLOAD) == 4096U, "the largest message fills a slot");
	memset(&event, 0, sizeof(event));
	wake = drv_rpi4_vchiq_event_fire(&event);
	check(event.fired == 1U && !wake, "an unarmed event fires without a doorbell");
	event.armed = 1U;
	wake = drv_rpi4_vchiq_event_fire(&event);
	check(wake, "an armed event needs the doorbell");
}
