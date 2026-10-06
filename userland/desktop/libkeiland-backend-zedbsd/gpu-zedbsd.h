/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* The image description used only inside the zedBSD GPU module (libkeiland-backend, WS131 p009). */
#ifndef KWL_GPU_ZEDBSD_H
#define KWL_GPU_ZEDBSD_H

#include "userland/desktop/libkeiland-backend/keiland-backend-gpu.h"

/*
 * What the compositor's Vulkan device can take, against which a description
 * is checked before any of its values reaches Vulkan (from the device the
 * protocol host lends, for one request).
 */
struct kwl_gpu_limits {
	uint32_t max_dimension;
	uint32_t memory_type_count;
};

/*
 * The layout of a client's GPU buffer as its description states it.
 *
 * One per wl_buffer, filled when the buffer is created and not changed
 * afterwards.  The image the compositor makes of the buffer has this size,
 * format and row layout; the import checks that the description is the
 * allocation's real one.
 */
struct kwl_buffer_layout {
	uint32_t width;
	uint32_t height;
	VkFormat format;
	uint32_t stride;
	uint64_t offset;
	uint64_t allocation_bytes;
	uint32_t memory_type;
};

/*
 * The backend's record of one client GPU buffer (the resource's private
 * pointer, from its creation to kl_backend_gpu_resource_free): a duplicate
 * of the image capability's fd and the description as the client sent it,
 * kept for the direct scanout (scanout-zedbsd.c, ws122-p005b), and the
 * scanout's handle of the imported capability (0 for none) with the claim
 * it belongs to.
 */
#define KWL_GPU_DESCRIPTION_MAX	64U
struct kwl_gpu_buffer_record {
	int descriptor;
	unsigned char description[KWL_GPU_DESCRIPTION_MAX];
	size_t description_size;
	uint32_t width;
	uint32_t height;
	uint64_t handle;
	uint64_t claim;
};

/* Forgets a buffer's scanout handle when the buffer goes (scanout-zedbsd.c). */
void kwl_scanout_forget(struct kwl_gpu_buffer_record *record);

/* Reports the zedBSD description's wire length. */
size_t kwl_gpu_buffer_wire_bytes(void);
/* Decodes and checks every field before it reaches Vulkan. */
int kwl_gpu_buffer_decode(const unsigned char *bytes, size_t size, const struct kwl_gpu_limits *limits, struct kwl_buffer_layout *layout);
/* Reports the memory handle type of a zedBSD image capability. */
VkExternalMemoryHandleTypeFlagBits kwl_gpu_buffer_handle_type(void);

#endif
