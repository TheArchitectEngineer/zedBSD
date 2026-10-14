/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The records of the display channels (stage P8b) that touch no hardware.
 *
 * A display channel (the core channel and the window channels of the
 * Turing display) reads methods from a push buffer; this header names the
 * methods the driver sends, the objects it writes into the display's
 * instance memory (the hash table and the context DMA objects), and the
 * parameters the resource manager takes to make a channel.  The method
 * offsets and fields are those of the core (0xc57d) and window (0xc57e)
 * classes in NVIDIA's open-gpu-doc (MIT); the instance-memory objects and
 * the order of the methods follow nouveau's display (MIT).  Everything here
 * builds bytes in memory the caller owns, so it is tested on the host.
 */

#ifndef DRIVERS_GPU_NVRTX_DISP_H
#define DRIVERS_GPU_NVRTX_DISP_H

#include <stddef.h>
#include <stdint.h>

/* The channel classes and their handles: class in the high half, instance in the low half. */
#define NVRTX_DISP_CLASS_CORE		0xc57dU
#define NVRTX_DISP_CLASS_WINDOW		0xc57eU

/* The channel numbers the hash table knows a channel by: the core is 0, window w is 1 + w. */
#define NVRTX_DISP_CHID_CORE		0U
#define NVRTX_DISP_CHID_WINDOW_FIRST	1U

/* The handles of the two context DMA objects every channel has: the notifiers and the whole VRAM. */
#define NVRTX_DISP_HANDLE_SYNC		0xf0000000U
#define NVRTX_DISP_HANDLE_VRAM		0xf0000001U

/* The hash table at the start of the instance memory: 1024 entries of 8 bytes. */
#define NVRTX_DISP_RAMHT_BYTES		0x2000U
#define NVRTX_DISP_RAMHT_ENTRIES	1024U
#define NVRTX_DISP_RAMHT_BITS		10U
#define NVRTX_DISP_RAMHT_ENTRY_BYTES	8U

/* One context DMA object: 24 bytes on a 16-byte step, after the hash table. */
#define NVRTX_DISP_CONTEXT_DMA_BYTES	24U
#define NVRTX_DISP_CONTEXT_DMA_STEP	32U

/* The resource manager's parameters: the push buffer's registration and a channel's allocation. */
#define NVRTX_DISP_PUSHBUFFER_BYTES	56U
#define NVRTX_DISP_CHANNEL_BYTES	40U

/* The longest run of data one method header carries. */
#define NVRTX_DISP_METHOD_COUNT_MAX	1023U

/* The notifier's status (bits 31:30 of its first word): finished is 2, not begun is 0. */
#define NVRTX_DISP_NOTIFIER_STATUS_SHIFT	30U
#define NVRTX_DISP_NOTIFIER_FINISHED		2U

/* The core channel's methods. */
#define NVRTX_CORE_UPDATE			0x0200U
#define NVRTX_CORE_SET_CONTEXT_DMA_NOTIFIER	0x0208U
#define NVRTX_CORE_SET_NOTIFIER_CONTROL		0x020cU
#define NVRTX_CORE_SET_INTERLOCK_FLAGS		0x0218U
#define NVRTX_CORE_SOR_SET_CONTROL		0x0300U
#define NVRTX_CORE_SOR_STRIDE			0x0020U
#define NVRTX_CORE_WINDOW_SET_CONTROL		0x1000U
#define NVRTX_CORE_WINDOW_FORMAT_BOUNDS		0x1004U
#define NVRTX_CORE_WINDOW_USAGE_BOUNDS		0x1010U
#define NVRTX_CORE_WINDOW_STRIDE		0x0080U
#define NVRTX_CORE_HEAD_FIRST			0x2000U
#define NVRTX_CORE_HEAD_STRIDE			0x0400U

/* The head methods the takeover sets, as offsets inside one head's block. */
#define NVRTX_HEAD_PROCAMP			0x000U
#define NVRTX_HEAD_CONTROL_OUTPUT_RESOURCE	0x004U
#define NVRTX_HEAD_CONTROL			0x008U
#define NVRTX_HEAD_PIXEL_CLOCK			0x00cU
#define NVRTX_HEAD_DITHER_CONTROL		0x018U
#define NVRTX_HEAD_DISPLAY_ID			0x020U
#define NVRTX_HEAD_PIXEL_CLOCK_MAX		0x028U
#define NVRTX_HEAD_USAGE_BOUNDS			0x030U
#define NVRTX_HEAD_VIEWPORT_SIZE_IN		0x04cU
#define NVRTX_HEAD_VIEWPORT_SIZE_OUT		0x058U
#define NVRTX_HEAD_RASTER_SIZE			0x064U
#define NVRTX_HEAD_RASTER_SYNC_END		0x068U
#define NVRTX_HEAD_RASTER_BLANK_END		0x06cU
#define NVRTX_HEAD_RASTER_BLANK_START		0x070U
#define NVRTX_HEAD_RASTER_VERT_BLANK2		0x074U
#define NVRTX_HEAD_CONTEXT_DMA_CURSOR		0x088U
#define NVRTX_HEAD_CONTROL_CURSOR		0x09cU
#define NVRTX_HEAD_CONTEXT_DMA_OLUT		0x288U

/* How many head methods the takeover copies from the armed state, and where three of them are in the copy. */
#define NVRTX_DISP_HEAD_COPIED			15U
#define NVRTX_DISP_ARMED_VIEWPORT_IN		0U
#define NVRTX_DISP_ARMED_RASTER_SIZE		2U
#define NVRTX_DISP_ARMED_PIXEL_CLOCK		8U

/* The window channel's methods. */
#define NVRTX_WINDOW_UPDATE			0x200U
#define NVRTX_WINDOW_SET_CONTEXT_DMA_NOTIFIER	0x21cU
#define NVRTX_WINDOW_SET_NOTIFIER_CONTROL	0x220U
#define NVRTX_WINDOW_SET_SIZE			0x224U
#define NVRTX_WINDOW_SET_STORAGE		0x228U
#define NVRTX_WINDOW_SET_PARAMS			0x22cU
#define NVRTX_WINDOW_SET_PLANAR_STORAGE		0x230U
#define NVRTX_WINDOW_SET_CONTEXT_DMA_ISO	0x240U
#define NVRTX_WINDOW_SET_OFFSET			0x260U
#define NVRTX_WINDOW_SET_POINT_IN		0x290U
#define NVRTX_WINDOW_SET_SIZE_IN		0x298U
#define NVRTX_WINDOW_SET_SIZE_OUT		0x2a4U
#define NVRTX_WINDOW_SET_COMPOSITION_CONTROL	0x2ecU
#define NVRTX_WINDOW_SET_PRESENT_CONTROL	0x308U
#define NVRTX_WINDOW_SET_INTERLOCK_FLAGS	0x370U
#define NVRTX_WINDOW_SET_FMT_COEFFICIENT	0x400U
#define NVRTX_WINDOW_SET_CONTEXT_DMA_ILUT	0x444U

/*
 * A push buffer being written: the memory, how many words fit, how many are written.
 *
 * The words are written in the GPU's (little-endian) order.  A write past
 * the end writes nothing and sets the overflow flag, so a builder never
 * returns an error and the caller checks once before it moves the channel's
 * put.
 */
struct nvrtx_push {
	uint8_t *bytes;
	uint32_t capacity;
	uint32_t count;
	int overflow;
};

/*
 * One surface a window shows: linear A8R8G8B8 pixels in the VRAM.
 *
 * The offset is the surface's VRAM address (a multiple of 256), the pitch
 * the bytes from one row to the next (a multiple of 64), the handle the
 * context DMA object the window reads it through.
 */
struct nvrtx_disp_surface {
	uint64_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
};

/* The head methods the takeover copies, in the order nvrtx_disp_core_head() sends them. */
extern const uint32_t nvrtx_disp_head_copied[NVRTX_DISP_HEAD_COPIED];

void nvrtx_push_init(struct nvrtx_push *push, uint8_t *bytes, uint32_t capacity);
void nvrtx_push_method(struct nvrtx_push *push, uint32_t method, const uint32_t *values, uint32_t count);
void nvrtx_push_one(struct nvrtx_push *push, uint32_t method, uint32_t value);

uint32_t nvrtx_disp_ramht_hash(uint32_t chid, uint32_t handle);
void nvrtx_disp_ramht_entry(uint8_t *entry, uint32_t handle, uint32_t chid, uint32_t client, uint32_t object_offset);
void nvrtx_disp_context_dma(uint8_t *object, uint64_t start, uint64_t limit);
void nvrtx_disp_pushbuffer_params(uint8_t *params, uint64_t bus_address, uint32_t size, uint32_t class_number, uint32_t instance);
void nvrtx_disp_channel_params(uint8_t *params, uint32_t instance);

void nvrtx_disp_core_init(struct nvrtx_push *push, uint32_t window_mask);
void nvrtx_disp_core_head(struct nvrtx_push *push, uint32_t head, const uint32_t *armed);
void nvrtx_disp_core_sor(struct nvrtx_push *push, uint32_t sor, uint32_t control);
void nvrtx_disp_core_owners(struct nvrtx_push *push, uint32_t window_mask);
void nvrtx_disp_core_head_planes_off(struct nvrtx_push *push, uint32_t head);
void nvrtx_disp_core_update(struct nvrtx_push *push, uint32_t notifier_offset, uint32_t window_interlock);

void nvrtx_disp_window_image(struct nvrtx_push *push, const struct nvrtx_disp_surface *surface);
void nvrtx_disp_window_defaults(struct nvrtx_push *push);
void nvrtx_disp_window_flip(struct nvrtx_push *push, uint64_t offset, uint32_t notifier_offset);
void nvrtx_disp_window_update(struct nvrtx_push *push, int with_core, uint32_t window);

uint32_t nvrtx_disp_band_color(uint32_t band);

#endif
