/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI driver's checks on words: fence values, the ring's room, the
 * extended capability list and the native command stream of the test
 * kernels.  Nothing here touches the kernel or the hardware.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "ghci-words.h"
#include "hcr.h"
#include "packets.h"

/*
 * The fields of a class 0 packet's first dword, and the job tag's place.
 *
 * Values referenced: GHCI 1.0 (draft) section 7.1 (the packet envelope).
 */
#define PACKET_OP_SHIFT			24U
#define PACKET_OP_MASK			0x3fU
#define PACKET_FLAGS_SHIFT		8U
#define PACKET_FLAGS_MASK		0xffU
#define PACKET_PAYLOAD_MASK		0xffU
#define PACKET_TAG_SHIFT		16U
#define PACKET_TAG_MASK			0x00ff0000U

/*
 * The flags each checked micro-operation may carry, and the units a
 * WAIT_IDLE may name (the STATUS busy bits 0 to 6).
 *
 * Values referenced: GHCI 1.0 (draft) sections 3.3 (STATUS) and 7.3
 * (the standard micro-operations).
 */
#define BLIT_FLAGS			0x01U
#define FENCE_FLAGS			0x01U
#define WAIT_IDLE_FLAGS			0x7fU
#define INVALIDATE_FLAGS		0x0fU

/*
 * The blitter's BLT_OP word: the operation in [2:0], blend over in [4],
 * the pixel size in [9:8] and the reverse direction in [12].  A native
 * command may only fill and copy, with 32-bit or 16-bit pixels.
 *
 * Values referenced: GHCI 1.0 (draft) section 8.2 (the blitter range).
 */
#define BLT_OP_KIND_MASK		0x7U
#define BLT_OP_PIXEL_SHIFT		8U
#define BLT_OP_PIXEL_MASK		0x3U
#define BLT_OP_DEFINED			0x1317U

/* The bit of each blitter state word in the mask of the words a native command has set. */
#define BLT_WORD(index)			(1U << (index))

static int check_packet(uint32_t header, const uint32_t *payload, const struct ghci_native_check *check, uint32_t *blt_state, uint32_t *blt_set);
static int check_set_reg(const uint32_t *payload, uint32_t pairs, uint32_t *blt_state, uint32_t *blt_set);
static int check_blit(const struct ghci_native_check *check, const uint32_t *blt_state, uint32_t blt_set);
static int check_rectangle(const struct ghci_native_check *check, uint32_t base, uint32_t stride, uint32_t corner, uint32_t size, uint32_t pixel_bytes);
static int check_owned(const struct ghci_native_check *check, uint32_t address, uint32_t bytes, uint32_t alignment);
static void copy_packet(const uint32_t *packet, uint32_t length, uint32_t tag, uint32_t *out);

/*
 * Reports whether a fence value in memory has reached a value.
 */
int
ghci_fence_passed(
	uint32_t memory,
	uint32_t value)
{
	int32_t distance;

	/* The values are a circle: memory has reached value when it is less than half a turn ahead. */
	distance = (int32_t)(memory - value);
	if (distance >= 0)
		return 1;

	/* Succeeded: the fence has not been reached yet. */
	return 0;
}

/*
 * Plans where the next run of packets goes in the ring.
 */
int
ghci_ring_plan(
	uint32_t wptr,
	uint32_t rptr,
	uint32_t ring_dwords,
	uint32_t need,
	uint32_t *pad)
{
	uint32_t free_dwords;
	uint32_t to_end;
	uint32_t padding;

	/* Refuses a ring of no size and positions outside the ring. */
	if (ring_dwords == 0U)
		return EINVAL;
	if (wptr >= ring_dwords)
		return EINVAL;
	if (rptr >= ring_dwords)
		return EINVAL;

	/* Refuses an empty run, and one that could never fit beside the unused dword. */
	if (need == 0U)
		return EINVAL;
	if (need >= ring_dwords)
		return EINVAL;

	/* The room is the distance back to the reader, less the one dword the ring leaves unused. */
	free_dwords = (rptr + ring_dwords - wptr - 1U) % ring_dwords;

	/* A packet never crosses the end: the rest of the ring is padded when it does not hold the run. */
	to_end = ring_dwords - wptr;
	padding = 0U;
	if (need > to_end)
		padding = to_end;

	/* Waits for the reader while the padding and the run do not both fit. */
	if (free_dwords < padding + need)
		return EAGAIN;

	/* Succeeded: the padding, if any, and the run fit. */
	*pad = padding;
	return 0;
}

/*
 * Walks the extended capability list for the entry of an id.
 */
int
ghci_extcap_find(
	uint32_t (*read)(void *context, uint32_t offset),
	void *context,
	uint32_t first,
	uint32_t window_bytes,
	uint32_t id,
	uint32_t *registers,
	uint32_t *version)
{
	uint32_t entry;
	uint32_t word;
	uint32_t next;
	unsigned walked;

	/* A list that starts nowhere has no entries. */
	if (first == 0U)
		return ENOENT;

	/* A window too small for one entry holds no list. */
	if (window_bytes < 8U)
		return EINVAL;

	/* Follows the entries, a bounded number of them, inside the window. */
	entry = first;
	for (walked = 0U; walked < GHCI_EXTCAP_MAX; walked++) {
		/* Refuses an entry that is not aligned to a word. */
		if ((entry % 4U) != 0U)
			return EINVAL;

		/* Refuses an entry whose two words leave the window. */
		if (entry > window_bytes - 8U)
			return EINVAL;

		/* Reads the entry's first word: its id, its version and the distance to the next. */
		word = read(context, entry);

		/* The entry asked for: its registers' offset is the second word. */
		if (GHCI_XCAP_ID(word) == id) {
			*registers = read(context, entry + 4U);
			*version = GHCI_XCAP_VERSION(word);
			return 0;
		}

		/* The last entry ends the list without the id. */
		next = GHCI_XCAP_NEXT(word);
		if (next == 0U)
			return ENOENT;

		/* Refuses a step that leaves the window. */
		if (next > (window_bytes - entry) / 4U)
			return EINVAL;

		/* Moves on to the next entry. */
		entry += next * 4U;
	}

	/* Failed: a list that does not end within its bound. */
	return EINVAL;
}

/*
 * Checks a native command and copies its packets with the caller's tag.
 */
int
ghci_native_prepare(
	const uint32_t *stream,
	uint32_t stream_dwords,
	const struct ghci_native_check *check,
	uint32_t *out,
	uint32_t capacity,
	uint32_t *dwords)
{
	uint32_t blt_state[GHCI_BLIT_STATE_WORDS];
	uint32_t blt_set;
	uint32_t position;
	uint32_t packets;
	uint32_t header;
	uint32_t length;
	uint32_t written;
	uint32_t index;
	int error;

	/* Refuses a stream shorter than its head and one packet. */
	if (stream_dwords < GHCI_NATIVE_HEAD_DWORDS + 1U)
		return EINVAL;

	/* Refuses another gpu-op and any flags. */
	if (stream[0] != GPU_OP_GHCI_NATIVE)
		return EINVAL;
	if (stream[1] != 0U)
		return EINVAL;

	/* Refuses a count of packets that is empty or beyond the bound. */
	packets = stream[2];
	if (packets == 0U)
		return EINVAL;
	if (packets > GHCI_NATIVE_PACKETS_MAX)
		return EINVAL;

	/*
	 * No blitter state word is known at the start: a BLIT is taken only
	 * after the stream itself has set every word the blit reads, so the
	 * check sees exactly what the blitter will.
	 */
	for (index = 0U; index < GHCI_BLIT_STATE_WORDS; index++)
		blt_state[index] = 0U;
	blt_set = 0U;

	/* Checks and copies each packet in turn. */
	position = GHCI_NATIVE_HEAD_DWORDS;
	written = 0U;
	for (index = 0U; index < packets; index++) {
		/* Refuses a count that runs past the stream. */
		if (position >= stream_dwords)
			return EINVAL;

		/* Refuses a packet whose payload runs past the stream. */
		header = stream[position];
		length = 1U + (header & PACKET_PAYLOAD_MASK);
		if (length > stream_dwords - position)
			return EINVAL;

		/* Checks the micro-operation and its operands. */
		error = check_packet(header, &stream[position + 1U], check, blt_state, &blt_set);
		if (error != 0)
			return error;

		/* Refuses packets past the room the caller gave. */
		if (length > capacity - written)
			return E2BIG;

		/* Copies the packet with the session's tag in place of the caller's. */
		copy_packet(&stream[position], length, check->tag, &out[written]);
		written += length;
		position += length;
	}

	/* Refuses words after the last packet: the count and the stream disagree. */
	if (position != stream_dwords)
		return EINVAL;

	/* Succeeded: every packet is checked and copied. */
	*dwords = written;
	return 0;
}

/* Checks one packet of a native command: its class, operation, length, flags and addresses. */
static int
check_packet(
	uint32_t header,
	const uint32_t *payload,
	const struct ghci_native_check *check,
	uint32_t *blt_state,
	uint32_t *blt_set)
{
	uint32_t op;
	uint32_t flags;
	uint32_t count;
	int error;

	/* Only the standard micro-operations are taken. */
	if (GHCI_PKT_GET_CLASS(header) != GHCI_PKT_CLASS_STD)
		return EINVAL;

	/* The operation, its flags and the dwords of its payload. */
	op = (header >> PACKET_OP_SHIFT) & PACKET_OP_MASK;
	flags = (header >> PACKET_FLAGS_SHIFT) & PACKET_FLAGS_MASK;
	count = header & PACKET_PAYLOAD_MASK;

	/* Selects the rules of the operation. */
	switch (op) {
	case GHCI_OP_NOP:
		/* Skipped words of any number, and no flags. */
		if (flags != 0U)
			return EINVAL;
		break;

	case GHCI_OP_SET_REG:
		/* Pairs of a blitter register and a value, and no flags. */
		if (flags != 0U)
			return EINVAL;
		if (count == 0U)
			return EINVAL;
		if ((count % 2U) != 0U)
			return EINVAL;

		/* Records the blitter words the pairs set. */
		error = check_set_reg(payload, count / 2U, blt_state, blt_set);
		if (error != 0)
			return error;
		break;

	case GHCI_OP_BLIT:
		/* No operands; the one flag says whether the processor waits for the end. */
		if (count != 0U)
			return EINVAL;
		if ((flags & ~BLIT_FLAGS) != 0U)
			return EINVAL;

		/* The rectangles the blit reads and writes must be the session's. */
		error = check_blit(check, blt_state, *blt_set);
		if (error != 0)
			return error;
		break;

	case GHCI_OP_FENCE:
		/* The address and the value; the one flag raises the interrupt. */
		if (count != 2U)
			return EINVAL;
		if ((flags & ~FENCE_FLAGS) != 0U)
			return EINVAL;

		/* The fence word is written by the GPU, so it must be the session's. */
		error = check_owned(check, payload[0], 4U, 4U);
		if (error != 0)
			return error;
		break;

	case GHCI_OP_WAIT_IDLE:
		/* No operands; the flags are a mask of the units waited for. */
		if (count != 0U)
			return EINVAL;
		if ((flags & ~WAIT_IDLE_FLAGS) != 0U)
			return EINVAL;
		break;

	case GHCI_OP_INVALIDATE:
		/* No operands; the flags name the caches. */
		if (count != 0U)
			return EINVAL;
		if ((flags & ~INVALIDATE_FLAGS) != 0U)
			return EINVAL;
		break;

	default:
		/* Every other operation reaches units or memory the check does not follow. */
		return EINVAL;
	}

	/* Succeeded: the packet is one a test may send. */
	return 0;
}

/* Records the (register, value) pairs of a SET_REG, which may only name the blitter's words. */
static int
check_set_reg(
	const uint32_t *payload,
	uint32_t pairs,
	uint32_t *blt_state,
	uint32_t *blt_set)
{
	uint32_t index;
	uint32_t pair;

	/* Records each pair in turn. */
	for (pair = 0U; pair < pairs; pair++) {
		/* Refuses a register outside BLT_OP to BLT_CLIP_MAX. */
		index = payload[2U * pair];
		if (index >= GHCI_S_BLT + GHCI_BLIT_STATE_WORDS)
			return EINVAL;

		/* The value the blitter will see in the word. */
		blt_state[index - GHCI_S_BLT] = payload[2U * pair + 1U];
		*blt_set |= BLT_WORD(index - GHCI_S_BLT);
	}

	/* Succeeded: every pair names a blitter word. */
	return 0;
}

/* Checks that a BLIT reads and writes only the session's memory, from the words the stream set. */
static int
check_blit(
	const struct ghci_native_check *check,
	const uint32_t *blt_state,
	uint32_t blt_set)
{
	uint32_t needed;
	uint32_t op;
	uint32_t kind;
	uint32_t pixel;
	uint32_t pixel_bytes;
	int error;

	/* Every blit needs its operation, destination, rectangle and size from this stream. */
	needed = BLT_WORD(GHCI_S_BLT_OP) |
	    BLT_WORD(GHCI_S_BLT_DST_BASE) |
	    BLT_WORD(GHCI_S_BLT_DST_STRIDE) |
	    BLT_WORD(GHCI_S_BLT_DST_XY) |
	    BLT_WORD(GHCI_S_BLT_SIZE);
	if ((blt_set & needed) != needed)
		return EINVAL;

	/* Refuses bits the blitter does not define. */
	op = blt_state[GHCI_S_BLT_OP];
	if ((op & ~BLT_OP_DEFINED) != 0U)
		return EINVAL;

	/* Only a fill and a copy are taken; the tiled and resolving copies are not. */
	kind = op & BLT_OP_KIND_MASK;
	if (kind != GHCI_BLT_FILL && kind != GHCI_BLT_COPY)
		return EINVAL;

	/* The pixel is 32 bits or 16 bits. */
	pixel = (op >> BLT_OP_PIXEL_SHIFT) & BLT_OP_PIXEL_MASK;
	if (pixel == 0U) {
		pixel_bytes = 4U;
	} else if (pixel == 1U) {
		pixel_bytes = 2U;
	} else {
		return EINVAL;
	}

	/* The destination rectangle must be the session's. */
	error = check_rectangle(check,
	    blt_state[GHCI_S_BLT_DST_BASE],
	    blt_state[GHCI_S_BLT_DST_STRIDE],
	    blt_state[GHCI_S_BLT_DST_XY],
	    blt_state[GHCI_S_BLT_SIZE],
	    pixel_bytes);
	if (error != 0)
		return error;

	/* A fill reads no memory. */
	if (kind == GHCI_BLT_FILL)
		return 0;

	/* A copy also needs its source from this stream. */
	needed = BLT_WORD(GHCI_S_BLT_SRC_BASE) |
	    BLT_WORD(GHCI_S_BLT_SRC_STRIDE) |
	    BLT_WORD(GHCI_S_BLT_SRC_XY);
	if ((blt_set & needed) != needed)
		return EINVAL;

	/* The source rectangle, of the same size, must be the session's too. */
	error = check_rectangle(check,
	    blt_state[GHCI_S_BLT_SRC_BASE],
	    blt_state[GHCI_S_BLT_SRC_STRIDE],
	    blt_state[GHCI_S_BLT_SRC_XY],
	    blt_state[GHCI_S_BLT_SIZE],
	    pixel_bytes);
	if (error != 0)
		return error;

	/* Succeeded: both rectangles are the session's. */
	return 0;
}

/*
 * Checks that every pixel of a rectangle of an image lies inside the
 * session's memory: the bytes from the first pixel of its first row to
 * the last pixel of its last row.  The clip rectangle only takes pixels
 * away, so it is not needed for the bound.
 */
static int
check_rectangle(
	const struct ghci_native_check *check,
	uint32_t base,
	uint32_t stride,
	uint32_t corner,
	uint32_t size,
	uint32_t pixel_bytes)
{
	uint64_t x;
	uint64_t y;
	uint64_t width;
	uint64_t height;
	uint64_t first;
	uint64_t end;
	int error;

	/* Bases and strides are multiples of the pixel's size. */
	if ((base % pixel_bytes) != 0U)
		return EINVAL;
	if ((stride % pixel_bytes) != 0U)
		return EINVAL;

	/* The rectangle's corner and size, in pixels. */
	x = corner & 0xffffU;
	y = corner >> 16;
	width = size & 0xffffU;
	height = size >> 16;

	/* A rectangle without pixels touches no memory. */
	if (width == 0U || height == 0U)
		return 0;

	/* The first byte of the first row and the end of the last row. */
	first = (uint64_t)base + y * stride + x * pixel_bytes;
	end = (uint64_t)base + (y + height - 1U) * stride + (x + width) * pixel_bytes;

	/* Refuses a rectangle that runs past the 32-bit address space. */
	if (end > 0x100000000ULL)
		return EINVAL;

	/* The whole run must be one of the session's allocations. */
	error = check_owned(check, (uint32_t)first, (uint32_t)(end - first), 1U);
	if (error != 0)
		return error;

	/* Succeeded: the rectangle is the session's. */
	return 0;
}

/* Checks that a run of GPU addresses is aligned and inside the session's memory. */
static int
check_owned(
	const struct ghci_native_check *check,
	uint32_t address,
	uint32_t bytes,
	uint32_t alignment)
{
	int owned;

	/* Refuses a misaligned address. */
	if ((address % alignment) != 0U)
		return EINVAL;

	/* Asks the caller whether the session owns the run. */
	owned = check->owned(check->context, address, bytes);
	if (owned == 0)
		return EINVAL;

	/* Succeeded: the run is the session's. */
	return 0;
}

/* Copies one packet, giving its header the job tag. */
static void
copy_packet(
	const uint32_t *packet,
	uint32_t length,
	uint32_t tag,
	uint32_t *out)
{
	uint32_t index;

	/* The header names the session, whatever the caller wrote there. */
	out[0] = (packet[0] & ~PACKET_TAG_MASK) | ((tag << PACKET_TAG_SHIFT) & PACKET_TAG_MASK);

	/* Copies the payload unchanged. */
	for (index = 1U; index < length; index++)
		out[index] = packet[index];
}
