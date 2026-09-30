/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The compositor's operating-system boundary for client GPU buffers (WS103).
 *
 * A client's GPU buffer arrives as an fd and a description of its image
 * (keiland_gpu_buffer_v1.create_buffer).  What the fd is and how the
 * description is written belong to the operating system: on zedBSD the fd is
 * a kernel image capability and the description the kernel's own record of
 * it (gpu-zedbsd.c).  Everything the compositor needs from them is the layout
 * below and the Vulkan handle type the fd is imported as; the rest of the
 * compositor uses only these.
 */
#ifndef ZWL_GPU_H
#define ZWL_GPU_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_external.h>

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

/*
 * What the compositor's Vulkan device can take, against which a description
 * is checked before any of its values reaches Vulkan.
 */
struct zwl_gpu_limits {
	uint32_t max_dimension;
	uint32_t memory_type_count;
};

size_t zwl_gpu_buffer_wire_bytes(void);
int zwl_gpu_buffer_decode(const unsigned char *bytes, size_t size, const struct zwl_gpu_limits *limits, struct zwl_buffer_layout *layout);
VkExternalMemoryHandleTypeFlagBits zwl_gpu_buffer_handle_type(void);

#endif
