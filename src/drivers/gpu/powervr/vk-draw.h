/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The data of the draw-state records the Venus executor keeps
 * (vk-draw.c writes them, vk-pass.c reads them into the render jobs).
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_VK_DRAW_H
#define KERN_DRIVERS_GPU_POWERVR_VK_DRAW_H

#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/powervr/render-pass.h"

/* The data of a vertex buffer bind: the first binding, the buffers' offsets. */
struct powervr_vk_vertex_bind {
	uint32_t first;
	uint32_t count;
	uint64_t offsets[POWERVR_DRAW_VERTEX_BUFFERS];
};

/* The data of a viewport or scissor set and of a push constant update. */
struct powervr_vk_state_record {
	VkViewport viewport;
	VkRect2D scissor;
	uint32_t push_offset;
	uint32_t push_bytes;
	uint8_t push[POWERVR_DRAW_PUSH_BYTES];
};

#endif /* KERN_DRIVERS_GPU_POWERVR_VK_DRAW_H */
