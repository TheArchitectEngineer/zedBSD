/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The zedBSD side of the compositor's GPU buffer boundary (zwl-gpu.h, WS103).
 *
 * On zedBSD a client's GPU buffer is a kernel image capability fd with the
 * kernel's description of the image (struct gpu_image_descriptor, 64 bytes,
 * as keiland_gpu_buffer_v1.create_buffer's array).  The fd is imported as
 * Vulkan OPAQUE_FD memory; libvulkan checks the description against the
 * kernel's own record of the capability when the import names the image
 * (a dedicated import).  This file is the only one of the compositor that
 * reads the kernel's types.
 */

#include "zwl-gpu.h"

#include <uapi/gpu.h>
#include <errno.h>
#include <string.h>

/*
 * Reports how many bytes a buffer's description takes on the wire.
 */
size_t
zwl_gpu_buffer_wire_bytes(
	void)
{
	/* Succeeded: the kernel's image description. */
	return sizeof(struct gpu_image_descriptor);
}

/*
 * Decodes a buffer's description and checks every value a client chose.
 *
 * Nothing of the description reaches Vulkan before it is known to be an
 * image Vulkan can make: a nonzero size within the device's largest image,
 * one of the two four-channel formats in linear rows, rows of at least the
 * width that start inside and end inside the allocation, and a memory type
 * the device has.  Returns EINVAL for anything else.
 */
int
zwl_gpu_buffer_decode(
	const unsigned char *bytes,
	size_t size,
	const struct zwl_gpu_limits *limits,
	struct zwl_buffer_layout *layout)
{
	struct gpu_image_descriptor image;
	VkFormat format;
	uint64_t row_bytes;
	uint64_t rows_bytes;
	uint64_t end;

	/* The description is exactly the kernel's record. */
	if (size != sizeof(image))
		return EINVAL;

	/* Copies the record out of the (possibly unaligned) wire bytes. */
	memcpy(&image, bytes, sizeof(image));

	/* A record of this interface's revision and length. */
	if (image.version != GPU_ABI_VERSION)
		return EINVAL;
	if (image.size != sizeof(image))
		return EINVAL;

	/* A size Vulkan can make an image of. */
	if (image.width == 0U || image.width > limits->max_dimension)
		return EINVAL;
	if (image.height == 0U || image.height > limits->max_dimension)
		return EINVAL;

	/* Only linear rows are shared. */
	if (image.tiling != GPU_IMAGE_LINEAR)
		return EINVAL;

	/* One of the two four-channel formats, in its byte order. */
	if (image.format == GPU_PIXEL_BGRA8888) {
		format = VK_FORMAT_B8G8R8A8_UNORM;
	} else if (image.format == GPU_PIXEL_RGBA8888) {
		format = VK_FORMAT_R8G8B8A8_UNORM;
	} else {
		return EINVAL;
	}

	/* Rows of whole pixels, at least as long as a row of the image. */
	row_bytes = (uint64_t)image.width * 4U;
	if (image.stride < row_bytes)
		return EINVAL;
	if ((image.stride % 4U) != 0U)
		return EINVAL;

	/* The last row ends inside the allocation (no value here can overflow 64 bits but the offset). */
	rows_bytes = (uint64_t)image.stride * (uint64_t)(image.height - 1U) + row_bytes;
	if (image.offset > UINT64_MAX - rows_bytes)
		return EINVAL;
	end = image.offset + rows_bytes;
	if (end > image.allocation_bytes)
		return EINVAL;

	/* A memory type the device has. */
	if (image.memory_type >= limits->memory_type_count)
		return EINVAL;

	/* The layout the compositor makes its image of. */
	layout->width = image.width;
	layout->height = image.height;
	layout->format = format;
	layout->stride = image.stride;
	layout->offset = image.offset;
	layout->allocation_bytes = image.allocation_bytes;
	layout->memory_type = image.memory_type;

	/* Succeeded: every value is one Vulkan can take. */
	return 0;
}

/*
 * Reports the Vulkan handle type a buffer's fd is imported as.
 */
VkExternalMemoryHandleTypeFlagBits
zwl_gpu_buffer_handle_type(
	void)
{
	/* Succeeded: a kernel image capability is libvulkan's OPAQUE_FD. */
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
}
