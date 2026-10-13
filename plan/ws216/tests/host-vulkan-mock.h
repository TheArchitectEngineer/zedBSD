/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws216-p004: a model of a Vulkan device for Notes' renderer (render.c) on
 * the host.  It defines every Vulkan function render.c calls: objects are
 * numbered handles, memory is the host's, and the command buffer's draws
 * are kept for the test to read.  The device can be set to refuse
 * allocations larger than a size (i915 refuses one of more than 16 MiB) and
 * to refuse the pipelines of the grain or of the Multiply (as the
 * Raspberry Pi 4's driver does).
 */

#ifndef HOST_VULKAN_MOCK_H
#define HOST_VULKAN_MOCK_H

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

/* The kinds of the model's objects. */
#define MOCK_FREE	0
#define MOCK_BUFFER	1
#define MOCK_IMAGE	2
#define MOCK_MEMORY	3
#define MOCK_PIPELINE	4
#define MOCK_MODULE	5
#define MOCK_VIEW	6
#define MOCK_SAMPLER	7
#define MOCK_SET	8
#define MOCK_OTHER	9

/* One object of the model, by its handle's number. */
struct mock_object {
	int kind;

	/* A buffer's or a memory's size, a memory's bytes, and the memory a buffer or an image is bound to. */
	size_t size;
	unsigned char *bytes;
	size_t memory;

	/* An image's size, usage and tiling, and its layout after the last barrier; a view's image. */
	VkImageLayout layout;
	uint32_t width;
	uint32_t height;
	VkImageUsageFlags usage;
	VkImageTiling tiling;
	size_t image;

	/* A module's SPIR-V (and its bytes); a pipeline's fragment module's SPIR-V, colour blend factors and stencil test. */
	const uint32_t *code;
	size_t code_size;
	const uint32_t *fragment;
	size_t fragment_size;
	VkBlendFactor src_color;
	VkBlendFactor dst_color;
	VkBlendFactor src_alpha;
	VkBlendFactor dst_alpha;
	VkCompareOp compare;
	VkStencilOp pass_op;
	int stencil;
	uint32_t write_mask;

	/* A sampler's filter and address mode; a set's view and sampler. */
	VkFilter filter;
	VkSamplerAddressMode address;
	size_t view;
	size_t sampler;
};

/* One draw recorded: its pipeline, vertex buffer and set (handle numbers), its vertices, the push constants and the pass. */
struct mock_draw {
	size_t pipeline;
	size_t buffer;
	size_t set;
	uint32_t first;
	uint32_t count;
	float push[8];
	unsigned pass;
};

/* The model's settings and what it saw. */
struct mock_device {
	/* Allocations of buffers' memory larger than this are refused (0: none), and so are images' larger than image_limit. */
	size_t buffer_limit;
	size_t image_limit;

	/* The pipelines through the grain's shader, or with the Multiply's blend, are refused. */
	int refuse_grain;
	int refuse_multiply;
	const uint32_t *grain_code;
	size_t grain_size;

	/* The largest allocation given, the allocations refused, the pipelines made and refused. */
	size_t largest;
	unsigned refused;
	unsigned pipelines_made;
	unsigned pipelines_refused;

	/*
	 * ws216-p005: the copies of an image to a buffer since the start, the
	 * passes made that end an attachment in a layout the Raspberry Pi 4's
	 * driver refuses (review-1 S-1), and a function the test sets that fills the buffer's bytes at the copy
	 * (the model draws nothing), given the image's size.
	 */
	unsigned copies;
	unsigned bad_final_layouts;
	void (*copy_hook)(unsigned char *bytes, uint32_t width, uint32_t height);

	/* The draws of the command buffer since it was reset, and the passes begun. */
	struct mock_draw *draws;
	size_t draw_count;
	size_t draw_capacity;
	unsigned passes;
};

/* The model. */
extern struct mock_device mock;

/* The objects, by number (a handle's number is its pointer's value / 16 - 1). */
struct mock_object *mock_get(const void *handle);
size_t mock_number(const void *handle);
size_t mock_live(int kind);
void mock_reset(void);
int mock_same_code(const uint32_t *code, size_t size, const uint32_t *other, size_t other_size);

#endif
