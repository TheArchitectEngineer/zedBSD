/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The copy engine's methods the executor writes into the work channel's
 * push buffers: a region of a block-linear plane copied out to a buffer's
 * rows (a video picture read back), on the copy object's subchannel.
 * Touches no hardware (a host test runs it).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-gsp.h"
#include "drivers/gpu/nvrtx/nvrtx-work.h"
#include "drivers/gpu/nvrtx/vk/nvrtx-exec.h"

/*
 * A method header: incrementing, then the count, the subchannel and the
 * method's word address.
 *
 * Values referenced: NVIDIA open-gpu-doc classes/host/clc46f.h
 * (NVC46F_DMA_INCR_OPCODE, NVC46F_DMA_INCR_COUNT,
 * NVC46F_DMA_INCR_SUBCHANNEL, NVC46F_DMA_INCR_ADDRESS).
 */
#define COPY_METHOD_INCREMENT		(1U << 29)
#define COPY_METHOD_COUNT_SHIFT		16U
#define COPY_METHOD_SUBCHANNEL_SHIFT	13U

/*
 * The copy engine's methods: the start, the source and destination
 * addresses, the pitches, the line's bytes and count, the block-linear
 * source's block size, width, height, depth and layer (one run of five
 * words) and its origin (two words); and the start's fields: a non-pipelined
 * transfer, flushed, from block linear to pitch, of several lines, between
 * virtual addresses.
 *
 * Values referenced: NVIDIA open-gpu-doc classes/dma-copy/clc5b5.h
 * (NVC5B5_LAUNCH_DMA and its DATA_TRANSFER_TYPE, FLUSH_ENABLE,
 * SRC_MEMORY_LAYOUT, DST_MEMORY_LAYOUT and MULTI_LINE_ENABLE fields,
 * NVC5B5_OFFSET_IN_UPPER, NVC5B5_OFFSET_OUT_UPPER, NVC5B5_PITCH_IN,
 * NVC5B5_PITCH_OUT, NVC5B5_LINE_LENGTH_IN, NVC5B5_LINE_COUNT,
 * NVC5B5_SET_SRC_BLOCK_SIZE and its WIDTH, HEIGHT, DEPTH and GOB_HEIGHT
 * fields, NVC5B5_SET_SRC_WIDTH to NVC5B5_SET_SRC_LAYER, NVC5B5_SRC_ORIGIN_X
 * and NVC5B5_SRC_ORIGIN_Y).
 */
#define COPY_LAUNCH			0x300U
#define COPY_OFFSET_IN_UPPER		0x400U
#define COPY_OFFSET_OUT_UPPER		0x408U
#define COPY_PITCH_IN			0x410U
#define COPY_SOURCE_BLOCK		0x728U
#define COPY_SOURCE_ORIGIN_X		0x744U
#define COPY_LAUNCH_NON_PIPELINED	0x2U
#define COPY_LAUNCH_FLUSH		(1U << 2)
#define COPY_LAUNCH_DESTINATION_PITCH	(1U << 8)
#define COPY_LAUNCH_MULTI_LINE		(1U << 9)
#define COPY_BLOCK_HEIGHT_SHIFT		4U
#define COPY_BLOCK_GOB_HEIGHT_SHIFT	12U
#define COPY_BLOCK_TWO_GOBS		1U
#define COPY_GOB_HEIGHT_FERMI_8		1U

static size_t copy_method(uint8_t *push, size_t at, uint32_t method, uint32_t count);

/*
 * Writes the copy of one region of a block-linear plane (blocks two GOBs
 * high) out to a buffer's rows, on the copy object's subchannel.
 *
 * Returns where the push buffer goes on; it takes at most
 * NVRTX_EXEC_PLANE_COPY_BYTES.
 *
 * Procedure referenced: Mesa main 921d028a5fff2beb4ec90451913b5fcc05fb1e6a
 * src/nouveau/vulkan/nvk_cmd_copy.c (nouveau_copy_rect: the addresses, the
 * pitches and lines, the block-linear source's surface with the row
 * stride as its width, its origin by the separate origin methods, then
 * the launch).
 */
size_t
nvrtx_exec_copy_plane(
	uint8_t *push,
	size_t at,
	const struct nvrtx_exec_plane_copy *copy)
{
	uint32_t block;

	/* The plane's first byte, then the region's first byte in the buffer. */
	at = copy_method(push, at, COPY_OFFSET_IN_UPPER, 4U);
	nvrtx_put32(push, at, (uint32_t)(copy->source >> 32));
	nvrtx_put32(push, at + 4U, (uint32_t)(copy->source & 0xffffffffU));
	nvrtx_put32(push, at + 8U, (uint32_t)(copy->destination >> 32));
	nvrtx_put32(push, at + 12U, (uint32_t)(copy->destination & 0xffffffffU));
	at += 16U;

	/* The pitches (the plane's, the buffer's), the region's row bytes and its rows. */
	at = copy_method(push, at, COPY_PITCH_IN, 4U);
	nvrtx_put32(push, at, copy->source_pitch);
	nvrtx_put32(push, at + 4U, copy->destination_pitch);
	nvrtx_put32(push, at + 8U, copy->line_bytes);
	nvrtx_put32(push, at + 12U, copy->lines);
	at += 16U;

	/* The plane: blocks one GOB wide and two high, its bytes a row (no tile width), its rows, one layer. */
	block = (COPY_BLOCK_TWO_GOBS << COPY_BLOCK_HEIGHT_SHIFT) | (COPY_GOB_HEIGHT_FERMI_8 << COPY_BLOCK_GOB_HEIGHT_SHIFT);
	at = copy_method(push, at, COPY_SOURCE_BLOCK, 5U);
	nvrtx_put32(push, at, block);
	nvrtx_put32(push, at + 4U, copy->source_pitch);
	nvrtx_put32(push, at + 8U, copy->source_rows);
	nvrtx_put32(push, at + 12U, 1U);
	nvrtx_put32(push, at + 16U, 0U);
	at += 20U;

	/* The region's origin in the plane: bytes across, rows down. */
	at = copy_method(push, at, COPY_SOURCE_ORIGIN_X, 2U);
	nvrtx_put32(push, at, copy->x_bytes);
	nvrtx_put32(push, at + 4U, copy->y);
	at += 8U;

	/* Starts the copy: not pipelined, flushed, block linear to pitch, several lines. */
	at = copy_method(push, at, COPY_LAUNCH, 1U);
	nvrtx_put32(push, at, COPY_LAUNCH_NON_PIPELINED | COPY_LAUNCH_FLUSH | COPY_LAUNCH_DESTINATION_PITCH | COPY_LAUNCH_MULTI_LINE);
	at += 4U;

	/* Succeeded: the copy is written. */
	return at;
}

/* Writes an incrementing method header on the copy object's subchannel and returns where its data goes. */
static size_t
copy_method(
	uint8_t *push,
	size_t at,
	uint32_t method,
	uint32_t count)
{
	uint32_t header;

	/* The header's fields. */
	header = COPY_METHOD_INCREMENT |
	    (count << COPY_METHOD_COUNT_SHIFT) |
	    (NVRTX_WORK_SUBCHANNEL_COPY << COPY_METHOD_SUBCHANNEL_SHIFT) |
	    (method >> 2);
	nvrtx_put32(push, at, header);

	/* Succeeded: the data follows the header. */
	return at + 4U;
}
