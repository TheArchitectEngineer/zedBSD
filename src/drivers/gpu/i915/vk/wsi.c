/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Window system integration: swapchain images the display module scans out.
 *
 * A swapchain is a small ring of images backed by memory that the render target
 * draws into and the display flips to.  The present path waits for the draw
 * fence and flips; the display register programming behind the flip is completed
 * on hardware.
 */

#include "vk-internal.h"
#include "wsi.h"
#include "res.h"
#include "sync.h"
#include "display.h"

#include <kern/kmem.h>

#include <errno.h>

/* The most images a baseline swapchain rings through. */
#define WSI_MAX_IMAGES 8U

/* A hardware color format for scanout surfaces (isl.h ISL_FORMAT). */
#define WSI_FORMAT_B8G8R8A8_UNORM 192U

/* A swapchain owns its images and the memory that backs them. */
struct i915_vk_swapchain {
	struct i915_vk_session *session;
	uint32_t count;
	uint32_t width;
	uint32_t height;
	uint32_t acquired;
	struct i915_vk_memory *memory[WSI_MAX_IMAGES];
	struct i915_vk_image *images[WSI_MAX_IMAGES];
};

/* Routes surface/display/swapchain opcodes; decode lands at integration. */
int
i915_vk_wsi_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)session;
	(void)opcode;
	(void)reader;
	(void)reply;
	return EINVAL;
}

/* Creates a swapchain of memory-backed images sized to the request. */
int
i915_vk_swapchain_create(
	struct i915_vk_session *session,
	uint32_t width,
	uint32_t height,
	uint32_t image_count,
	struct i915_vk_swapchain **out)
{
	struct i915_vk_image_info info;
	struct i915_vk_swapchain *swapchain;
	uint32_t index;
	int error;

	/* The caller receives nothing on failure. */
	*out = NULL;

	swapchain = kern_calloc(1U, sizeof(*swapchain));
	if (swapchain == NULL)
		return ENOMEM;
	swapchain->session = session;
	swapchain->width = width;
	swapchain->height = height;
	swapchain->count = image_count > WSI_MAX_IMAGES ? WSI_MAX_IMAGES : image_count;

	/* Every image is a linear scanout surface bound to its own memory. */
	info.width = width;
	info.height = height;
	info.format = WSI_FORMAT_B8G8R8A8_UNORM;
	info.tiling = 0U;
	for (index = 0U; index < swapchain->count; index++) {
		error = i915_vk_memory_alloc(session, (uint64_t)width * height * 4U, 0U, &swapchain->memory[index]);
		if (error != 0) {
			i915_vk_swapchain_destroy(swapchain);
			return error;
		}
		error = i915_vk_image_create(session, &info, &swapchain->images[index]);
		if (error != 0) {
			i915_vk_swapchain_destroy(swapchain);
			return error;
		}
		error = i915_vk_image_bind(swapchain->images[index], swapchain->memory[index], 0U);
		if (error != 0) {
			i915_vk_swapchain_destroy(swapchain);
			return error;
		}
	}

	/* Succeeded: the swapchain can be acquired and presented. */
	*out = swapchain;
	return 0;
}

/* Releases a swapchain's images and memory. */
void
i915_vk_swapchain_destroy(
	struct i915_vk_swapchain *swapchain)
{
	uint32_t index;

	/* A swapchain that never created is nothing to release. */
	if (swapchain == NULL)
		return;

	/* Images retire before the memory that backs them. */
	for (index = 0U; index < swapchain->count; index++) {
		if (swapchain->images[index] != NULL)
			i915_vk_image_destroy(swapchain->images[index]);
		if (swapchain->memory[index] != NULL)
			i915_vk_memory_free(swapchain->memory[index]);
	}

	kern_free(swapchain);
}

/* Returns the next image index to render into. */
int
i915_vk_swapchain_acquire(
	struct i915_vk_swapchain *swapchain,
	uint32_t *image_index)
{
	/* An empty swapchain has no image to acquire. */
	if (swapchain->count == 0U)
		return EINVAL;

	/* The ring hands out images in order and wraps around. */
	*image_index = swapchain->acquired;
	swapchain->acquired = (swapchain->acquired + 1U) % swapchain->count;
	return 0;
}

/* Presents an image once its draw fence signals. */
int
i915_vk_swapchain_present(
	struct i915_vk_swapchain *swapchain,
	uint32_t image_index,
	struct i915_vk_fence *wait)
{
	int error;

	/* A present names an image the swapchain owns. */
	if (image_index >= swapchain->count)
		return EINVAL;

	/* The draw must complete before the image is scanned out. */
	if (wait != NULL) {
		error = i915_vk_fence_wait(wait, ~0ULL);
		if (error != 0)
			return error;
	}

	/* The display flips its primary plane to the presented image. */
	return i915_vk_display_flip(swapchain->session->vk, swapchain->images[image_index]);
}
