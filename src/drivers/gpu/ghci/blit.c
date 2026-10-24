/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The driver's own blits: clearing memory before a session sees it, and
 * copying between the processor and memory it does not reach, through
 * the device's bounce buffer.  Each blit is a submission of the driver's
 * that the caller waits for; the device's lock is held throughout, which
 * also keeps the bounce buffer the caller's.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "ghci.h"
#include "hcr.h"
#include "packets.h"

/*
 * A clear writes rows of one page of 32-bit pixels (1024 pixels, 4 KiB),
 * at most 16384 rows (64 MiB) per blit, within the blitter's 16-bit sizes.
 */
#define CLEAR_ROW_PIXELS		1024U
#define CLEAR_ROW_BYTES			(CLEAR_ROW_PIXELS * 4U)
#define CLEAR_ROWS_MAX			16384U

/* The dwords of one blit and the invalidation before it. */
#define BLIT_RUN_DWORDS			(GHCI_BARE_PACKET_DWORDS + GHCI_BLIT_PACKET_DWORDS)

/* The cache a blit's source may be read through, dropped before the blit reads rewritten memory. */
#define DATA_CACHE			0x01U

/*
 * Fills a run of GPU memory with zero through the blitter and waits for it.
 */
int
ghci_blit_clear(
	struct ghci_device *device,
	uint32_t gpu_address,
	uint32_t bytes)
{
	struct ghci_blit blit;
	uint32_t rows;
	uint32_t done_rows;
	uint32_t chunk;
	int error;

	/* Clears whole pages only, as the allocator hands them out. */
	if ((bytes % CLEAR_ROW_BYTES) != 0U)
		return EINVAL;

	/* The fill of every chunk of rows: the pages as an image of 4 KiB rows. */
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_FILL;
	blit.dst_stride = CLEAR_ROW_BYTES;
	blit.width = CLEAR_ROW_PIXELS;
	blit.color = 0U;

	/* Fills one chunk of rows at a time. */
	rows = bytes / CLEAR_ROW_BYTES;
	done_rows = 0U;
	while (done_rows < rows) {
		chunk = rows - done_rows;
		if (chunk > CLEAR_ROWS_MAX)
			chunk = CLEAR_ROWS_MAX;

		/* The chunk's rows, then the wait for them. */
		blit.dst_base = gpu_address + done_rows * CLEAR_ROW_BYTES;
		blit.height = chunk;
		error = ghci_blit_run(device, &blit);
		if (error != 0)
			return error;

		done_rows += chunk;
	}

	/* Succeeded: the memory reads zero. */
	return 0;
}

/*
 * Copies between the processor and memory it does not reach, through the
 * bounce buffer, in whole 32-bit words.
 */
int
ghci_blit_transfer(
	struct ghci_device *device,
	struct ghci_allocation *allocation,
	uint64_t offset,
	void *bytes,
	uint32_t length,
	int to_gpu)
{
	struct ghci_blit blit;
	uint8_t *cursor;
	uint32_t done;
	uint32_t chunk;
	uint32_t target;
	int error;

	/* The blitter moves whole 32-bit pixels: offsets and lengths are word multiples. */
	if ((offset % 4U) != 0U)
		return EINVAL;
	if ((length % 4U) != 0U)
		return EINVAL;

	/* A device without its bounce buffer cannot copy. */
	if (device->bounce == NULL)
		return EIO;

	/* Every chunk is one row of pixels between the bounce buffer and the allocation. */
	kern_memset(&blit, 0, sizeof(blit));
	blit.op = GHCI_BLT_COPY;
	blit.height = 1U;

	/* Copies one bounce buffer's worth at a time. */
	cursor = bytes;
	done = 0U;
	while (done < length) {
		chunk = length - done;
		if (chunk > GHCI_BOUNCE_BYTES)
			chunk = GHCI_BOUNCE_BYTES;
		target = allocation->gpu_address + (uint32_t)offset + done;

		/* The chunk's row, from the bounce buffer or into it. */
		blit.width = chunk / 4U;
		blit.dst_stride = chunk;
		blit.src_stride = chunk;
		if (to_gpu) {
			kern_memcpy(device->bounce->view, cursor + done, chunk);
			blit.src_base = device->bounce->gpu_address;
			blit.dst_base = target;
		} else {
			blit.src_base = target;
			blit.dst_base = device->bounce->gpu_address;
		}

		/* The copy, then the wait for it. */
		error = ghci_blit_run(device, &blit);
		if (error != 0)
			return error;

		/* A read takes the chunk out of the bounce buffer. */
		if (!to_gpu)
			kern_memcpy(cursor + done, device->bounce->view, chunk);

		done += chunk;
	}

	/* Succeeded: every chunk is copied. */
	return 0;
}

/*
 * Submits one blit of the driver's, after dropping the data cache, and
 * waits for its fence.
 */
int
ghci_blit_run(
	struct ghci_device *device,
	const struct ghci_blit *blit)
{
	struct ghci_ticket ticket;
	uint32_t packets[BLIT_RUN_DWORDS];
	uint32_t dwords;
	int error;

	/* The blit may read memory the processor rewrote: the data cache goes first. */
	ghci_packet_invalidate(GHCI_TAG_DRIVER, DATA_CACHE, &packets[0]);

	/* The blitter's state and the BLIT. */
	error = ghci_packet_blit(blit, GHCI_TAG_DRIVER, &packets[1], GHCI_BLIT_PACKET_DWORDS, &dwords);
	if (error != 0)
		return error;

	/* The driver's submission, followed by its fence. */
	error = ghci_cp_submit(device, NULL, packets, 1U + dwords, &ticket);
	if (error != 0)
		return error;

	/* The fence is complete: the blit has written its memory when it passes. */
	error = ghci_cp_wait(device, &ticket, GHCI_FENCE_WAIT_MS);
	if (error != 0)
		return error;

	/* Succeeded: the blit is done. */
	return 0;
}
