/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws197-p007a2: the SCO pieces of the Bluetooth HCI class's pure part on
 * the host (src/drivers/generic/bt-hci-proto.c): the written packet's
 * check, the links' check and alternate (design S2, S3), the reassembly of
 * isochronous IN bytes with gaps and leading noise (A5), the ring that
 * drops its oldest (B2) and the OUT queue of bytes (A4).  Prints a line
 * for each failed check and "bt-sco-proto: all PASS" at the end.
 */

#include <drivers/generic/bt-hci.h>

#include <uapi/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The packets the assembler gave, their handles' sequence bytes. */
struct delivered {
	unsigned count;
	uint8_t first[64];
	size_t length[64];
};

static unsigned failures;

static void check(int good, const char *what);
static void link_one(struct bt_sco *links, uint16_t handle, uint16_t voice, uint16_t length);
static void take(void *context, const uint8_t *packet, size_t length);
static size_t make_packet(uint8_t *out, uint16_t handle, unsigned length, uint8_t mark);
static void test_write(void);
static void test_links(void);
static void test_assembler(void);
static void test_ring(void);
static void test_fifo(void);

/* Records one check. */
static void
check(
	int good,
	const char *what)
{
	/* A failure is printed and counted. */
	if (!good) {
		printf("FAIL %s\n", what);
		failures++;
	}
}

/* Sets one link (and only it) in a struct bt_sco. */
static void
link_one(
	struct bt_sco *links,
	uint16_t handle,
	uint16_t voice,
	uint16_t length)
{
	/* The one link. */
	memset(links, 0, sizeof(*links));
	links->count = 1U;
	links->handle[0] = handle;
	links->voice_setting[0] = voice;
	links->packet_length[0] = length;
}

/* Keeps what the assembler gives: each packet's first data byte (its mark) and length. */
static void
take(
	void *context,
	const uint8_t *packet,
	size_t length)
{
	struct delivered *seen;

	/* Up to 64 of them. */
	seen = context;
	if (seen->count < 64U) {
		seen->first[seen->count] = packet[BT_SCO_HEADER];
		seen->length[seen->count] = length;
	}

	/* Counted. */
	seen->count++;
}

/* Makes an SCO packet (header, then length bytes of mark); returns its size. */
static size_t
make_packet(
	uint8_t *out,
	uint16_t handle,
	unsigned length,
	uint8_t mark)
{
	/* The header: handle low first, length. */
	out[0] = (uint8_t)(handle & 0xffU);
	out[1] = (uint8_t)(handle >> 8);
	out[2] = (uint8_t)length;
	memset(out + BT_SCO_HEADER, mark, length);

	/* Succeeded: its size. */
	return BT_SCO_HEADER + length;
}

/* A written SCO packet: its type, header and length. */
static void
test_write(
	void)
{
	uint8_t packet[1U + BT_SCO_HEADER + 48U];
	int error;

	/* A whole one. */
	packet[0] = BT_PACKET_SCO;
	(void)make_packet(packet + 1, 0x0001U, 48U, 7U);
	error = bt_hci_check_sco(packet, sizeof(packet));
	check(error == 0, "a whole SCO packet");

	/* Short of its data, another type, no header. */
	error = bt_hci_check_sco(packet, sizeof(packet) - 1U);
	check(error == EINVAL, "an SCO packet short of its data");
	packet[0] = BT_PACKET_ACL;
	error = bt_hci_check_sco(packet, sizeof(packet));
	check(error == EINVAL, "an ACL packet is not SCO");
	packet[0] = BT_PACKET_SCO;
	error = bt_hci_check_sco(packet, 3U);
	check(error == EINVAL, "no header");

	/* The commands' and ACL check still refuses SCO. */
	error = bt_hci_check_write(packet, sizeof(packet), BT_ACL_DATA_DEFAULT);
	check(error == EINVAL, "the ACL check refuses SCO");
}

/* The links' check and the alternate chosen. */
static void
test_links(
	void)
{
	struct bt_sco links;
	unsigned alternate;
	int error;

	/* One 16-bit CVSD link: alternate 2. */
	link_one(&links, 0x0003U, 0x0060U, 48U);
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 2U, "one 16-bit CVSD link: alternate 2");

	/* Two and three: 4 and 5. */
	links.count = 2U;
	links.handle[1] = 0x0004U;
	links.voice_setting[1] = 0x0060U;
	links.packet_length[1] = 48U;
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 4U, "two 16-bit links: alternate 4");
	links.count = 3U;
	links.handle[2] = 0x0005U;
	links.voice_setting[2] = 0x0060U;
	links.packet_length[2] = 48U;
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 5U, "three 16-bit links: alternate 5");

	/* Mixed sample sizes, a repeated handle. */
	links.voice_setting[2] = 0x0040U;
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "mixed sample sizes refused");
	links.voice_setting[2] = 0x0060U;
	links.handle[2] = 0x0003U;
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "a handle twice refused");

	/* 8-bit links: one alternate a link. */
	link_one(&links, 0x0003U, 0x0040U, 24U);
	links.count = 2U;
	links.handle[1] = 0x0006U;
	links.voice_setting[1] = 0x0040U;
	links.packet_length[1] = 24U;
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 2U, "two 8-bit links: alternate 2");

	/* Transparent: alone, alternate 6 or 1. */
	link_one(&links, 0x0003U, 0x0063U, 60U);
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 6U, "transparent: alternate 6");
	alternate = bt_hci_sco_alternate(&links, 0);
	check(alternate == 1U, "transparent without alternate 6: 1");
	links.count = 2U;
	links.handle[1] = 0x0004U;
	links.voice_setting[1] = 0x0060U;
	links.packet_length[1] = 48U;
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "transparent with CVSD refused");

	/* Out of range: handle, length, count, reserved; none: alternate 0. */
	link_one(&links, 0x0f00U, 0x0060U, 48U);
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "a handle out of range refused");
	link_one(&links, 0x0003U, 0x0060U, 0U);
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "a length of 0 refused");
	link_one(&links, 0x0003U, 0x0060U, 48U);
	links.count = 4U;
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "four links refused");
	link_one(&links, 0x0003U, 0x0060U, 48U);
	links.reserved[1] = 1U;
	error = bt_hci_sco_check(&links);
	check(error == EINVAL, "reserved refused");
	memset(&links, 0, sizeof(links));
	error = bt_hci_sco_check(&links);
	alternate = bt_hci_sco_alternate(&links, 1);
	check(error == 0 && alternate == 0U, "no link: alternate 0");
}

/* The reassembly from 17-byte isochronous packets, with a gap, noise and the Packet Status Flag. */
static void
test_assembler(
	void)
{
	struct bt_hci_sco_assembler assembler;
	struct delivered seen;
	struct bt_sco links;
	uint8_t stream[51U * 6U];
	size_t used;
	size_t offset;
	size_t piece;
	unsigned index;
	int good;

	/* Six packets of 48 bytes, marked 1 to 6, the fourth with the Packet Status Flag set. */
	link_one(&links, 0x0001U, 0x0060U, 48U);
	used = 0U;
	for (index = 0U; index < 6U; index++) {
		if (index == 3U)
			used += make_packet(stream + used, 0x3001U, 48U, (uint8_t)(index + 1U));
		else
			used += make_packet(stream + used, 0x0001U, 48U, (uint8_t)(index + 1U));
	}

	/* Fed 17 bytes at a time: all six. */
	bt_hci_sco_assembler_init(&assembler, &links);
	memset(&seen, 0, sizeof(seen));
	for (offset = 0U; offset < used; offset += piece) {
		piece = 17U;
		if (piece > used - offset)
			piece = used - offset;
		bt_hci_sco_feed(&assembler, stream + offset, piece, take, &seen);
	}

	/* Each in order, whole. */
	good = seen.count == 6U;
	for (index = 0U; index < 6U && good; index++) {
		if (seen.first[index] != index + 1U || seen.length[index] != 51U)
			good = 0;
	}

	/* And no packet lost. */
	check(good && assembler.resyncs == 0U, "six packets in 17-byte pieces");

	/* A gap after 34 bytes (a missed isochronous packet): the first is lost, the rest found again. */
	bt_hci_sco_assembler_init(&assembler, &links);
	memset(&seen, 0, sizeof(seen));
	bt_hci_sco_feed(&assembler, stream, 34U, take, &seen);
	bt_hci_sco_gap(&assembler);
	bt_hci_sco_feed(&assembler, stream + 51U, used - 51U, take, &seen);
	check(seen.count == 5U && seen.first[0] == 2U && assembler.resyncs == 1U, "a gap loses one packet");

	/* A gap inside the stream's second packet, the rest fed from its middle: noise until the third. */
	bt_hci_sco_assembler_init(&assembler, &links);
	memset(&seen, 0, sizeof(seen));
	bt_hci_sco_feed(&assembler, stream + 51U + 20U, used - 51U - 20U, take, &seen);
	check(seen.count == 4U && seen.first[0] == 3U && assembler.resyncs == 1U, "noise before the next header");

	/* A link of another length is not taken. */
	link_one(&links, 0x0001U, 0x0060U, 60U);
	bt_hci_sco_assembler_init(&assembler, &links);
	memset(&seen, 0, sizeof(seen));
	bt_hci_sco_feed(&assembler, stream, used, take, &seen);
	check(seen.count == 0U, "a header of another length is not ours");
}

/* The ring that drops its oldest. */
static void
test_ring(
	void)
{
	struct bt_hci_ring ring;
	uint8_t bytes[200];
	uint8_t body[51];
	uint8_t type;
	uint32_t sequence;
	size_t length;
	unsigned dropped;
	unsigned total;
	unsigned index;
	int error;

	/* Room for three records of 51 bytes (58 each). */
	bt_hci_ring_init(&ring, bytes, sizeof(bytes), 0U);
	memset(body, 0, sizeof(body));
	total = 0U;
	for (index = 0U; index < 5U; index++) {
		error = bt_hci_ring_push_drop(&ring, BT_PACKET_SCO, index, body, sizeof(body), &dropped);
		total += dropped;
		check(error == 0, "a packet always fits");
	}

	/* Two dropped, the oldest kept is the third. */
	(void)bt_hci_ring_peek(&ring, &type, &sequence, &length);
	check(total == 2U && ring.count == 3U && sequence == 2U && type == BT_PACKET_SCO, "the oldest dropped");

	/* A record longer than the ring is refused. */
	error = bt_hci_ring_push_drop(&ring, BT_PACKET_SCO, 9U, bytes, sizeof(bytes), &dropped);
	check(error == EINVAL && ring.count == 0U, "a record too long for the ring");
}

/* The OUT queue of bytes: whole puts, partial gets, the wrap. */
static void
test_fifo(
	void)
{
	struct bt_hci_fifo fifo;
	uint8_t bytes[100];
	uint8_t in[60];
	uint8_t out[60];
	size_t got;
	unsigned index;
	int error;
	int good;

	/* 60 in, 40 out, 60 in (wraps), 80 out. */
	bt_hci_fifo_init(&fifo, bytes, sizeof(bytes));
	for (index = 0U; index < 60U; index++)
		in[index] = (uint8_t)index;
	error = bt_hci_fifo_put(&fifo, in, 60U);
	check(error == 0, "put 60");
	error = bt_hci_fifo_put(&fifo, in, 60U);
	check(error == EAGAIN, "a put that does not fit");
	got = bt_hci_fifo_get(&fifo, out, 40U);
	check(got == 40U && out[39] == 39U, "get 40");
	error = bt_hci_fifo_put(&fifo, in, 60U);
	check(error == 0 && bt_hci_fifo_room(&fifo) == 20U, "put 60 across the end");
	got = bt_hci_fifo_get(&fifo, out, 20U);
	check(got == 20U && out[0] == 40U && out[19] == 59U, "the rest of the first");
	got = bt_hci_fifo_get(&fifo, out, 60U);
	good = got == 60U;
	for (index = 0U; index < 60U && good; index++) {
		if (out[index] != index)
			good = 0;
	}

	/* All of it, nothing left. */
	check(good && fifo.used == 0U, "the second across the end");
	bt_hci_fifo_clear(&fifo);
	check(bt_hci_fifo_room(&fifo) == 100U, "cleared");
}

/* Runs every group. */
int
main(
	void)
{
	/* Each group. */
	test_write();
	test_links();
	test_assembler();
	test_ring();
	test_fifo();

	/* The summary. */
	if (failures != 0U) {
		printf("bt-sco-proto: %u FAIL\n", failures);
		return EXIT_FAILURE;
	}

	/* Succeeded: every check passed. */
	printf("bt-sco-proto: all PASS\n");
	return EXIT_SUCCESS;
}
