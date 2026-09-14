/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Window system integration: swapchain images scanned out through the display
 * module.  Contract for p010; see external-design.md section 4.10.
 */

#ifndef I915_VK_WSI_H
#define I915_VK_WSI_H

#include "vk-internal.h"

/* Routes surface/display/swapchain opcodes. */
int
i915_vk_wsi_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Swapchain lifetime and presentation. */
int
i915_vk_swapchain_create(
	struct i915_vk_session *session,
	uint32_t width,
	uint32_t height,
	uint32_t image_count,
	struct i915_vk_swapchain **out);

void
i915_vk_swapchain_destroy(
	struct i915_vk_swapchain *swapchain);

int
i915_vk_swapchain_acquire(
	struct i915_vk_swapchain *swapchain,
	uint32_t *image_index);

int
i915_vk_swapchain_present(
	struct i915_vk_swapchain *swapchain,
	uint32_t image_index,
	struct i915_vk_fence *wait);

#endif /* I915_VK_WSI_H */
