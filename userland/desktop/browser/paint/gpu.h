/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU renderer (plan/ws074/design.md §8.2): a display list drawn with
 * Vulkan, one instanced quad per rectangle or glyph, into a framebuffer the
 * caller gives (a swapchain image of the window) or into an offscreen image
 * that is read back (the headless --render-gpu mode and the tests).
 *
 * It follows the CPU reference renderer's coverage rules (paint.h), so the
 * two pictures can be compared pixel by pixel.  Glyph bitmaps are copied
 * into an atlas image the first time they are drawn.
 */

#ifndef ZDESKTOP_BROWSER_PAINT_GPU_H
#define ZDESKTOP_BROWSER_PAINT_GPU_H

#include "paint/paint.h"

#include <vulkan/vulkan.h>

/* The glyph atlas's width and height, in texels. */
#define PAINT_GPU_ATLAS_SIZE	1024U

struct paint_gpu_slot;

/*
 * The Vulkan objects of the GPU renderer.
 *
 * The instance, the device and its queue are the caller's (the window's)
 * unless owns_device says the renderer made them itself (the headless
 * mode).  One frame is drawn at a time: each draw waits for its fence, so
 * the host may write the atlas and the instances between frames.
 */
struct paint_gpu {
	/* The device the renderer draws with, and whether it made the device and the instance. */
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t family;
	int owns_device;

	/* The pass (its color format and the layout it leaves the image in), the pipeline and what it binds. */
	VkFormat format;
	VkRenderPass pass;
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet set;
	VkSampler sampler;

	/* The unit square's six corners. */
	VkBuffer corners;
	VkDeviceMemory corner_memory;

	/* The frame's items, host-visible and mapped, with room for capacity of them. */
	VkBuffer instances;
	VkDeviceMemory instance_memory;
	void *instance_map;
	size_t instance_capacity;
	struct wb_vector staged;

	/* The atlas: a linear, host-written image, mapped for good, and whether it left its first layout. */
	VkImage atlas;
	VkDeviceMemory atlas_memory;
	VkImageView atlas_view;
	unsigned char *atlas_map;
	size_t atlas_pitch;
	int atlas_ready;

	/*
	 * The atlas's packing: glyphs go left to right on shelves as tall as
	 * their tallest glyph.  The table maps a glyph's bitmap (which the text
	 * system keeps for its life) to its place; full says a glyph did not fit
	 * and the atlas starts over before the next frame.
	 */
	uint32_t shelf_x;
	uint32_t shelf_y;
	uint32_t shelf_height;
	struct paint_gpu_slot *slots;
	size_t slot_capacity;
	size_t slot_count;
	int full;

	/* One command buffer and the fence its submission signals. */
	VkCommandPool pool;
	VkCommandBuffer command;
	VkFence fence;

	/* The Vulkan call that failed last, for the error line. */
	const char *operation;
};

/* The GPU renderer (vulkan.c). */
VkResult paint_gpu_open(struct paint_gpu *gpu, VkInstance instance, VkPhysicalDevice physical, uint32_t family, VkDevice device, VkFormat format, VkImageLayout final_layout);
VkResult paint_gpu_draw(struct paint_gpu *gpu, const struct paint_list *list, struct text_system *text, layout_unit scroll_y, VkFramebuffer framebuffer, VkExtent2D extent, VkSemaphore wait, VkSemaphore signal);
void paint_gpu_close(struct paint_gpu *gpu);
VkResult paint_gpu_render(const struct paint_list *list, struct text_system *text, layout_unit scroll_y, struct paint_bitmap *bitmap, const char **failed);

#endif
