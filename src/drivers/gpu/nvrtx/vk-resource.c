/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's buffers, images, image views and samplers (ws037-p007
 * stage D3).
 *
 * An image is 2D, of one level, one layer and one sample, in one of the
 * four-byte colour formats, laid out pitch linear: its rows are the width
 * times four bytes rounded up to NVRTX_VK_PITCH_ALIGN, and it is pitch
 * times height bytes.  The same layout serves the display, the texture
 * header and the colour target, so an image is never converted.  Anything
 * else is refused by name (VK_ERROR_FEATURE_NOT_PRESENT) rather than laid
 * out wrongly.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/nvrtx/nvrtx-vk-objects.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

static int resource_create_buffer(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_create_image(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_image_supported(const VkImageCreateInfo *info);
static int resource_layout(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_create_view(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static uint32_t resource_swizzle(VkComponentSwizzle swizzle, uint32_t identity);
static int resource_create_sampler(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_buffer_destroy(struct nvrtx_vk_session *session, void *payload);
static int resource_image_destroy(struct nvrtx_vk_session *session, void *payload);
static int resource_view_destroy(struct nvrtx_vk_session *session, void *payload);
static int resource_free(struct nvrtx_vk_session *session, void *payload);

/*
 * Runs a buffer, image, view or sampler command; *handled stays clear for
 * an opcode of another handler's.
 */
int
nvrtx_vk_resource_route(
	struct nvrtx_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	UNUSED_PARAMETER(requested);

	/* Picks the command, or leaves it to another handler. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_BUFFER:
		error = resource_create_buffer(session, reader, reply);
		break;
	case GPU_OP_DESTROY_BUFFER:
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_BUFFER);
		break;
	case GPU_OP_CREATE_IMAGE:
		error = resource_create_image(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE:
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE);
		break;
	case GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT:
		error = resource_layout(session, reader, reply);
		break;
	case GPU_OP_CREATE_IMAGE_VIEW:
		error = resource_create_view(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE_VIEW:
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE_VIEW);
		break;
	case GPU_OP_CREATE_SAMPLER:
		error = resource_create_sampler(session, reader, reply);
		break;
	case GPU_OP_DESTROY_SAMPLER:
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_SAMPLER);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* Reports a command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/*
 * Reports whether a format is one of the four-byte colour formats every
 * image of the executor is in.
 */
bool
nvrtx_vk_format_colour(
	uint32_t format)
{
	/* B8G8R8A8 and R8G8B8A8, as UNORM and as SRGB. */
	switch (format) {
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return true;
	default:
		break;
	}

	/* Any other format is not one. */
	return false;
}

/* vkCreateBuffer: [device][present][VkBufferCreateInfo] and the create's tail -> the create's reply. */
static int
resource_create_buffer(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkBufferCreateInfo info;
	struct nvrtx_vk_buffer *buffer;
	uint64_t identity;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkBufferCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A buffer of some bytes. */
	error = 0;
	if (info.size == 0U)
		error = EINVAL;

	/* The buffer keeps its size and usage; it has no storage until it is bound. */
	buffer = NULL;
	if (error == 0) {
		buffer = kern_calloc(1U, sizeof(*buffer));
		if (buffer != NULL) {
			buffer->size = info.size;
			buffer->usage = info.usage;
		}
	}

	/* Publishes the buffer and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_BUFFER, identity, buffer, resource_buffer_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* vkCreateImage: [device][present][VkImageCreateInfo] and the create's tail -> the create's reply. */
static int
resource_create_image(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageCreateInfo info;
	struct nvrtx_vk_image *image;
	uint64_t identity;
	uint32_t row;
	int supported;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Refuses by name an image the executor does not lay out. */
	error = 0;
	supported = resource_image_supported(&info);
	if (supported == 0) {
		kern_logf("nvrtx: vk: vkCreateImage refused: type %u format %u %ux%ux%u levels %u layers %u samples %u\n",
			  (unsigned)info.imageType,
			  (unsigned)info.format,
			  info.extent.width,
			  info.extent.height,
			  info.extent.depth,
			  info.mipLevels,
			  info.arrayLayers,
			  (unsigned)info.samples);
		error = ENOTSUP;
	}

	/* Lays the image out: rows of the width's pixels on the pitch boundary. */
	image = NULL;
	if (error == 0) {
		image = kern_calloc(1U, sizeof(*image));
		if (image != NULL) {
			row = info.extent.width * NVRTX_VK_PIXEL_BYTES;
			image->format = info.format;
			image->width = info.extent.width;
			image->height = info.extent.height;
			image->usage = info.usage;
			image->pitch = (row + NVRTX_VK_PITCH_ALIGN - 1U) & ~(NVRTX_VK_PITCH_ALIGN - 1U);
			image->bytes = (uint64_t)image->pitch * image->height;
		}
	}

	/* Publishes the image and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE, identity, image, resource_image_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Reports whether an image is one the executor lays out: 2D, one level,
 * one layer, one sample, a colour format, within the largest side.
 */
static int
resource_image_supported(
	const VkImageCreateInfo *info)
{
	bool colour;

	/* A 2D image of one level, one layer and one sample. */
	if (info->imageType != VK_IMAGE_TYPE_2D)
		return 0;
	if (info->mipLevels != 1U)
		return 0;
	if (info->arrayLayers != 1U)
		return 0;
	if (info->samples != VK_SAMPLE_COUNT_1_BIT)
		return 0;
	if (info->extent.depth != 1U)
		return 0;

	/* Of a size the colour target and the texture header take. */
	if (info->extent.width == 0U || info->extent.width > NVRTX_VK_IMAGE_SIDE_MAX)
		return 0;
	if (info->extent.height == 0U || info->extent.height > NVRTX_VK_IMAGE_SIDE_MAX)
		return 0;

	/* In one of the colour formats. */
	colour = nvrtx_vk_format_colour(info->format);
	if (!colour)
		return 0;

	/* Succeeded: the image is laid out pitch linear. */
	return 1;
}

/*
 * vkGetImageSubresourceLayout: [device][image][present]
 * [VkImageSubresource][present] -> [present][VkSubresourceLayout].  The one
 * subresource starts at 0 with the image's pitch; an unknown image or
 * subresource reports an empty layout.
 */
static int
resource_layout(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	const struct nvrtx_vk_image *image;
	uint64_t identity;
	uint64_t present;

	/* The image behind the device, then the subresource when present (level 0 when absent). */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	kern_memset(&subresource, 0, sizeof(subresource));
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkImageSubresource(reader, &session->arena, &subresource);

	/* Skips the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Level 0 of layer 0 is the whole image, rows pitch bytes apart. */
	kern_memset(&layout, 0, sizeof(layout));
	image = nvrtx_vk_payload(session, I915_VK_OBJ_IMAGE, identity);
	if (image != NULL && subresource.mipLevel == 0U && subresource.arrayLayer == 0U) {
		layout.offset = 0U;
		layout.size = image->bytes;
		layout.rowPitch = image->pitch;
		layout.arrayPitch = image->bytes;
		layout.depthPitch = image->bytes;
	}

	/* The layout behind its presence word. */
	drv_i915_wire_reply_u64(reply, 1U);
	i915_vkc_enc_VkSubresourceLayout(reply, &layout);

	/* Succeeded: the reply carries the layout. */
	return 0;
}

/*
 * vkCreateImageView: [device][present][VkImageViewCreateInfo] and the
 * create's tail -> the create's reply.  A 2D view of the one level and
 * layer of a known image, in a colour format.
 */
static int
resource_create_view(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageViewCreateInfo info;
	struct nvrtx_vk_object *image;
	struct nvrtx_vk_view *view;
	uint64_t identity;
	bool colour;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageViewCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A view of a known image, 2D, of its level 0 and layer 0, in a colour format. */
	error = 0;
	image = nvrtx_vk_object_find(session, I915_VK_OBJ_IMAGE, (uint64_t)(uintptr_t)info.image);
	colour = nvrtx_vk_format_colour(info.format);
	if (image == NULL) {
		error = EINVAL;
	} else if (info.viewType != VK_IMAGE_VIEW_TYPE_2D) {
		error = ENOTSUP;
	} else if (info.subresourceRange.baseMipLevel != 0U || info.subresourceRange.baseArrayLayer != 0U) {
		error = EINVAL;
	} else if (!colour) {
		error = ENOTSUP;
	}

	/* The view holds its image. */
	view = NULL;
	if (error == 0)
		error = nvrtx_vk_object_retain(image);
	if (error == 0) {
		view = kern_calloc(1U, sizeof(*view));
		if (view == NULL)
			(void)nvrtx_vk_object_release(image);
	}

	/* The image, the format, and where each channel comes from. */
	if (view != NULL) {
		view->image = image;
		view->format = info.format;
		view->swizzle[0] = resource_swizzle(info.components.r, VK_COMPONENT_SWIZZLE_R);
		view->swizzle[1] = resource_swizzle(info.components.g, VK_COMPONENT_SWIZZLE_G);
		view->swizzle[2] = resource_swizzle(info.components.b, VK_COMPONENT_SWIZZLE_B);
		view->swizzle[3] = resource_swizzle(info.components.a, VK_COMPONENT_SWIZZLE_A);
	}

	/* Publishes the view and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE_VIEW, identity, view, resource_view_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* Reports where a channel of a view comes from: the identity swizzle is the channel's own. */
static uint32_t
resource_swizzle(
	VkComponentSwizzle swizzle,
	uint32_t identity)
{
	/* The identity names the channel itself. */
	if (swizzle == VK_COMPONENT_SWIZZLE_IDENTITY)
		return identity;

	/* Succeeded: the channel the view names. */
	return (uint32_t)swizzle;
}

/* vkCreateSampler: [device][present][VkSamplerCreateInfo] and the create's tail -> the create's reply. */
static int
resource_create_sampler(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkSamplerCreateInfo info;
	struct nvrtx_vk_sampler *sampler;
	uint64_t identity;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkSamplerCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The sampler keeps what a texture read uses. */
	sampler = kern_calloc(1U, sizeof(*sampler));
	if (sampler != NULL) {
		sampler->mag_filter = info.magFilter;
		sampler->min_filter = info.minFilter;
		sampler->mipmap_mode = info.mipmapMode;
		sampler->address_u = info.addressModeU;
		sampler->address_v = info.addressModeV;
		sampler->address_w = info.addressModeW;
		sampler->border_color = info.borderColor;
		sampler->unnormalized = info.unnormalizedCoordinates;
	}

	/* Publishes the sampler and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_SAMPLER, identity, sampler, resource_free, 0);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* Destroys a buffer nothing holds: it lets its memory go. */
static int
resource_buffer_destroy(
	struct nvrtx_vk_session *session,
	void *payload)
{
	struct nvrtx_vk_buffer *buffer;
	int error;

	UNUSED_PARAMETER(session);

	buffer = payload;

	/* The memory may go with it when nothing else holds the memory. */
	error = nvrtx_vk_object_release(buffer->memory);
	kern_free(buffer);

	/* Reports a memory whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the buffer is gone. */
	return 0;
}

/* Destroys an image nothing holds: it lets its memory go. */
static int
resource_image_destroy(
	struct nvrtx_vk_session *session,
	void *payload)
{
	struct nvrtx_vk_image *image;
	int error;

	UNUSED_PARAMETER(session);

	image = payload;

	/* The memory may go with it when nothing else holds the memory. */
	error = nvrtx_vk_object_release(image->memory);
	kern_free(image);

	/* Reports a memory whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the image is gone. */
	return 0;
}

/* Destroys a view nothing holds: it lets its image go. */
static int
resource_view_destroy(
	struct nvrtx_vk_session *session,
	void *payload)
{
	struct nvrtx_vk_view *view;
	int error;

	UNUSED_PARAMETER(session);

	view = payload;

	/* The image may go with it when nothing else holds the image. */
	error = nvrtx_vk_object_release(view->image);
	kern_free(view);

	/* Reports an image whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the view is gone. */
	return 0;
}

/* Destroys a payload that holds nothing but its own storage. */
static int
resource_free(
	struct nvrtx_vk_session *session,
	void *payload)
{
	UNUSED_PARAMETER(session);

	/* Its storage. */
	kern_free(payload);

	/* Succeeded: the payload is gone. */
	return 0;
}
