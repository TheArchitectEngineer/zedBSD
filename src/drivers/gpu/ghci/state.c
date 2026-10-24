/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The packets the GHCI driver writes into the ring itself: fences,
 * padding, the waits and invalidations that order its own work, and the
 * standard state words of one blitter operation.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "ghci-words.h"
#include "hcr.h"
#include "packets.h"

/* The flags of INVALIDATE that name a cache (data, texture, constants, instructions). */
#define INVALIDATE_CACHES		0x0fU

/* The flags of WAIT_IDLE that name a unit (the STATUS busy bits 0 to 6). */
#define WAIT_IDLE_UNITS			0x7fU

/*
 * Writes a complete fence packet.
 */
void
ghci_packet_fence(
	uint32_t tag,
	uint32_t address,
	uint32_t value,
	int irq,
	uint32_t *out)
{
	uint32_t flags;

	/* The fence interrupt is raised once the value is in memory, when asked for. */
	flags = 0U;
	if (irq != 0)
		flags = GHCI_FENCE_IRQ;

	/* The header, the fence word's address and the value written there. */
	out[0] = GHCI_PKT_N(GHCI_PKT_CLASS_STD, GHCI_OP_FENCE, tag, flags, 2U);
	out[1] = address;
	out[2] = value;
}

/*
 * Writes a padding packet that fills dwords dwords.
 */
void
ghci_packet_pad(
	uint32_t dwords,
	uint32_t *out)
{
	uint32_t index;

	/* The header counts the dwords after it, which the processor skips. */
	out[0] = GHCI_PKT_PAD(dwords - 1U);

	/* The skipped dwords are written as zero, so the ring holds no stale words. */
	for (index = 1U; index < dwords; index++)
		out[index] = 0U;
}

/*
 * Writes a WAIT_IDLE packet.
 */
void
ghci_packet_wait_idle(
	uint32_t tag,
	uint32_t units,
	uint32_t *out)
{
	/* The units waited for travel in the flags; zero waits for every unit. */
	out[0] = GHCI_PKT_N(GHCI_PKT_CLASS_STD, GHCI_OP_WAIT_IDLE, tag, units & WAIT_IDLE_UNITS, 0U);
}

/*
 * Writes an INVALIDATE packet.
 */
void
ghci_packet_invalidate(
	uint32_t tag,
	uint32_t caches,
	uint32_t *out)
{
	/* The caches dropped travel in the flags. */
	out[0] = GHCI_PKT_N(GHCI_PKT_CLASS_STD, GHCI_OP_INVALIDATE, tag, caches & INVALIDATE_CACHES, 0U);
}

/*
 * Writes the packets of one blitter operation.
 */
int
ghci_packet_blit(
	const struct ghci_blit *blit,
	uint32_t tag,
	uint32_t *out,
	uint32_t capacity,
	uint32_t *dwords)
{
	uint32_t values[GHCI_BLIT_STATE_WORDS];
	uint32_t index;

	/* One SET_REG of every state word and one BLIT must fit. */
	if (capacity < GHCI_BLIT_PACKET_DWORDS)
		return ENOSPC;

	/* The state words in their register order; the clip rectangle is off. */
	values[GHCI_S_BLT_OP] = blit->op;
	values[GHCI_S_BLT_DST_BASE] = blit->dst_base;
	values[GHCI_S_BLT_DST_STRIDE] = blit->dst_stride;
	values[GHCI_S_BLT_SRC_BASE] = blit->src_base;
	values[GHCI_S_BLT_SRC_STRIDE] = blit->src_stride;
	values[GHCI_S_BLT_DST_XY] = GHCI_XY(blit->dst_x, blit->dst_y);
	values[GHCI_S_BLT_SRC_XY] = GHCI_XY(blit->src_x, blit->src_y);
	values[GHCI_S_BLT_SIZE] = GHCI_XY(blit->width, blit->height);
	values[GHCI_S_BLT_COLOR] = blit->color;
	values[GHCI_S_BLT_CLIP_MIN] = 0U;
	values[GHCI_S_BLT_CLIP_MAX] = 0U;

	/* The SET_REG header and its (index, value) pairs. */
	out[0] = GHCI_PKT_N(GHCI_PKT_CLASS_STD, GHCI_OP_SET_REG, tag, 0U, 2U * GHCI_BLIT_STATE_WORDS);
	for (index = 0U; index < GHCI_BLIT_STATE_WORDS; index++) {
		out[1U + 2U * index] = GHCI_S_BLT + index;
		out[2U + 2U * index] = values[index];
	}

	/* The BLIT, which runs on its own while later packets are parsed. */
	out[GHCI_BLIT_PACKET_DWORDS - 1U] = GHCI_PKT_N(GHCI_PKT_CLASS_STD, GHCI_OP_BLIT, tag, 0U, 0U);

	/* Succeeded: the packets are written. */
	*dwords = GHCI_BLIT_PACKET_DWORDS;
	return 0;
}
