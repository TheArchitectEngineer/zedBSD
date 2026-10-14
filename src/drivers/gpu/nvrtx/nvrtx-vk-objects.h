/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's objects (ws037-p007 stage D3): what a published identity
 * of each kind stands for, and the handlers that make them.
 *
 * Every image is pitch linear, four bytes a pixel: the display, the
 * texture header and the colour target all read that layout, so nothing
 * is converted between them.  A buffer or an image names the memory it is
 * bound to by holding that memory's object; the memory's storage arrives
 * with the blob libvulkan creates for it (vk-memory.c).
 */

#ifndef DRIVERS_GPU_NVRTX_VK_OBJECTS_H
#define DRIVERS_GPU_NVRTX_VK_OBJECTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/*
 * An image's rows are this many bytes apart at least (a multiple of it),
 * and an image or a buffer starts on this boundary of its memory: the
 * display's offset (256) and stride (64), the texture header's pitch (32)
 * and the colour target's pitch (128) all divide it.
 */
#define NVRTX_VK_PITCH_ALIGN		256U
#define NVRTX_VK_RESOURCE_ALIGN		256U

/* The bytes of one pixel of every image format the executor takes. */
#define NVRTX_VK_PIXEL_BYTES		4U

/* The widest and tallest image (the colour target's and the texture header's limit). */
#define NVRTX_VK_IMAGE_SIDE_MAX		16384U

/* The memory type the executor reports (one: device local, host visible and coherent). */
#define NVRTX_VK_MEMORY_TYPE		0U

/*
 * A VkDeviceMemory.
 *
 * bytes is the size libvulkan asked for (whole pages).  Once the blob for
 * it is attached, cpu is the CPU's view of its first byte (through the
 * BAR1 window), va the GPU's address of it in the work channel's space,
 * and backing the node's record the memory holds a reference to.
 */
struct nvrtx_vk_memory {
	uint64_t bytes;
	uint8_t *cpu;
	uint64_t va;
	void *backing;
	bool attached;
};

/*
 * A VkBuffer: its size and usage, and once bound the memory object it
 * holds and its offset in that memory.
 */
struct nvrtx_vk_buffer {
	uint64_t size;
	uint32_t usage;
	struct nvrtx_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImage: a 2D image of one level, one layer and one sample, pitch
 * linear with rows pitch bytes apart, bytes long; once bound the memory
 * object it holds and its offset in that memory.
 */
struct nvrtx_vk_image {
	uint32_t format;
	uint32_t width;
	uint32_t height;
	uint32_t usage;
	uint32_t pitch;
	uint64_t bytes;
	struct nvrtx_vk_object *memory;
	uint64_t offset;
};

/*
 * A VkImageView: the image object it holds, the format it reads the image
 * as, and where each of red, green, blue and alpha comes from
 * (VkComponentSwizzle, identity resolved).
 */
struct nvrtx_vk_view {
	struct nvrtx_vk_object *image;
	uint32_t format;
	uint32_t swizzle[4];
};

/*
 * A VkSampler: the filters and the address modes a texture read uses, and
 * whether coordinates are in pixels.
 */
struct nvrtx_vk_sampler {
	uint32_t mag_filter;
	uint32_t min_filter;
	uint32_t mipmap_mode;
	uint32_t address_u;
	uint32_t address_v;
	uint32_t address_w;
	uint32_t border_color;
	uint32_t unnormalized;
};

int nvrtx_vk_memory_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);
int nvrtx_vk_resource_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

uint32_t nvrtx_vk_result(int error);
uint64_t nvrtx_vk_create_tail(struct i915_wire_reader *reader);
void nvrtx_vk_create_reply(struct nvrtx_vk_session *session, struct i915_wire_writer *reply, enum i915_vk_object_kind kind, uint64_t identity, void *payload, int (*destroy)(struct nvrtx_vk_session *, void *), int error);
int nvrtx_vk_destroy_plain(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, enum i915_vk_object_kind kind);
void *nvrtx_vk_payload(struct nvrtx_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);

bool nvrtx_vk_format_colour(uint32_t format);
int nvrtx_vk_buffer_address(const struct nvrtx_vk_buffer *buffer, uint64_t *va, uint8_t **cpu);
int nvrtx_vk_image_address(const struct nvrtx_vk_image *image, uint64_t *va, uint8_t **cpu);

#endif /* DRIVERS_GPU_NVRTX_VK_OBJECTS_H */
