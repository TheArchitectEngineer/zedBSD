/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* The image description used only inside the zedBSD GPU module. */
#ifndef ZWL_GPU_ZEDBSD_H
#define ZWL_GPU_ZEDBSD_H

#include "userland/desktop/wayland/zwl-gpu.h"

/*
 * The layout of a client's GPU buffer as its description states it.
 *
 * One per wl_buffer, filled when the buffer is created and not changed
 * afterwards.  The image the compositor makes of the buffer has this size,
 * format and row layout; the import checks that the description is the
 * allocation's real one.
 */
struct zwl_buffer_layout {
	uint32_t width;
	uint32_t height;
	VkFormat format;
	uint32_t stride;
	uint64_t offset;
	uint64_t allocation_bytes;
	uint32_t memory_type;
};

/* Reports the zedBSD description's wire length. */
size_t zwl_gpu_buffer_wire_bytes(void);
/* Decodes and checks every field before it reaches Vulkan. */
int zwl_gpu_buffer_decode(const unsigned char *bytes, size_t size, const struct zwl_gpu_limits *limits, struct zwl_buffer_layout *layout);
/* Reports the memory handle type of a zedBSD image capability. */
VkExternalMemoryHandleTypeFlagBits zwl_gpu_buffer_handle_type(void);

#endif
