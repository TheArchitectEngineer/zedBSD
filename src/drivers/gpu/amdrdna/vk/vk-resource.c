/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's buffers, images, image views and samplers.
 *
 * An image is 2D, of one level, one layer and one sample, in one of the
 * four-byte colour formats, laid out pitch linear: its rows are the width
 * times four bytes rounded up to AMDRDNA_VK_PITCH_ALIGN, and it is pitch
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

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"
#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

static int resource_create_buffer(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_create_image(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_image_supported(const VkImageCreateInfo *info);
static void resource_lay_out(struct amdrdna_vk_image *image);
static uint32_t resource_level_side(uint32_t side, uint32_t level);
static int resource_layout(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_create_view(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static uint32_t resource_swizzle(VkComponentSwizzle swizzle, uint32_t identity);
static int resource_create_sampler(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int resource_buffer_destroy(struct amdrdna_vk_session *session, void *payload);
static int resource_image_destroy(struct amdrdna_vk_session *session, void *payload);
static int resource_view_destroy(struct amdrdna_vk_session *session, void *payload);

/*
 * Runs a buffer, image, view or sampler command; *handled stays clear for
 * an opcode of another handler's.
 */
int
amdrdna_vk_resource_route(
	struct amdrdna_vk_session *session,
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
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_BUFFER);
		break;
	case GPU_OP_CREATE_IMAGE:
		error = resource_create_image(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE);
		break;
	case GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT:
		error = resource_layout(session, reader, reply);
		break;
	case GPU_OP_CREATE_IMAGE_VIEW:
		error = resource_create_view(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE_VIEW:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE_VIEW);
		break;
	case GPU_OP_CREATE_SAMPLER:
		error = resource_create_sampler(session, reader, reply);
		break;
	case GPU_OP_DESTROY_SAMPLER:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_SAMPLER);
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
amdrdna_vk_format_colour(
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

/* Reports whether a format is the one depth format the executor draws into: D32_SFLOAT. */
bool
amdrdna_vk_format_depth(
	uint32_t format)
{
	/* A 32-bit float depth, no stencil. */
	if (format == VK_FORMAT_D32_SFLOAT)
		return true;

	/* Any other format is not one. */
	return false;
}

/*
 * Gives one level of a colour image as an image of its own: the level's
 * size and pitch, its offset in the memory, one level.  The levels lie in
 * GFX10's linear mip chain: the smallest first, each level's rows its
 * width's bytes rounded up to 256, its rows not padded.  Returns 0, or
 * EINVAL for a level the image does not have or a depth image.
 *
 * Procedure referenced: Mesa 25.0.7
 * src/amd/addrlib/src/gfx10/gfx10addrlib.cpp
 * (Gfx10Lib::HwlComputeSurfaceInfoLinear: the levels from the last to the
 * first, each at the sum of the ones before, its pitch the width aligned
 * to 256 bytes) and src/amd/addrlib/src/core/addrlib2.h (GetMipSize: a
 * side halved per level, at least 1).
 */
int
amdrdna_vk_image_level(
	const struct amdrdna_vk_image *image,
	uint32_t level,
	struct amdrdna_vk_image *part)
{
	uint64_t offset;
	uint32_t smaller;
	uint32_t width;
	uint32_t height;

	/* A level the colour image has. */
	if (image->depth || level >= image->levels)
		return EINVAL;

	/* The levels smaller than it lie before it. */
	offset = 0U;
	for (smaller = image->levels - 1U; smaller > level; smaller--) {
		width = resource_level_side(image->width, smaller);
		height = resource_level_side(image->height, smaller);
		offset += (uint64_t)amdrdna_vk_pitch(width) * height;
	}

	/* The level: its size, its pitch, where it starts, one level. */
	*part = *image;
	part->width = resource_level_side(image->width, level);
	part->height = resource_level_side(image->height, level);
	part->pitch = amdrdna_vk_pitch(part->width);
	part->bytes = (uint64_t)part->pitch * part->height;
	part->offset = image->offset + offset;
	part->levels = 1U;

	/* Succeeded: the level is an image of its own. */
	return 0;
}

/* vkCreateBuffer: [device][present][VkBufferCreateInfo] and the create's tail -> the create's reply. */
static int
resource_create_buffer(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkBufferCreateInfo info;
	struct amdrdna_vk_buffer *buffer;
	uint64_t identity;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkBufferCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
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
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_BUFFER, identity, buffer, resource_buffer_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* vkCreateImage: [device][present][VkImageCreateInfo] and the create's tail -> the create's reply. */
static int
resource_create_image(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageCreateInfo info;
	struct amdrdna_vk_image *image;
	uint64_t identity;
	int supported;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Refuses by name an image the executor does not lay out. */
	error = 0;
	supported = resource_image_supported(&info);
	if (supported == 0) {
		kern_logf("amdrdna: vk: vkCreateImage refused: type %u format %u %ux%ux%u levels %u layers %u samples %u\n",
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

	/* Lays the image out: a colour image's levels pitch linear, a depth image in the depth unit's blocks. */
	image = NULL;
	if (error == 0) {
		image = kern_calloc(1U, sizeof(*image));
		if (image != NULL) {
			image->format = info.format;
			image->width = info.extent.width;
			image->height = info.extent.height;
			image->usage = info.usage;
			image->levels = info.mipLevels;
			image->depth = amdrdna_vk_format_depth(info.format);
			resource_lay_out(image);
		}
	}

	/* Publishes the image and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE, identity, image, resource_image_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Reports whether an image is one the executor lays out: 2D, one layer,
 * one sample, within the largest side; a colour format with levels down to
 * one pixel at most, or the depth format of one level only drawn into.
 */
static int
resource_image_supported(
	const VkImageCreateInfo *info)
{
	uint32_t largest;
	bool colour;
	bool depth;

	/* A 2D image of one layer and one sample, with at least one level. */
	if (info->imageType != VK_IMAGE_TYPE_2D)
		return 0;
	if (info->mipLevels == 0U || info->mipLevels > AMDRDNA_VK_LEVELS_MAX)
		return 0;
	largest = info->extent.width;
	if (info->extent.height > largest)
		largest = info->extent.height;
	if ((largest >> (info->mipLevels - 1U)) == 0U)
		return 0;
	if (info->arrayLayers != 1U)
		return 0;
	if (info->samples != VK_SAMPLE_COUNT_1_BIT)
		return 0;
	if (info->extent.depth != 1U)
		return 0;

	/* Of a size the colour target and the texture header take. */
	if (info->extent.width == 0U || info->extent.width > AMDRDNA_VK_IMAGE_SIDE_MAX)
		return 0;
	if (info->extent.height == 0U || info->extent.height > AMDRDNA_VK_IMAGE_SIDE_MAX)
		return 0;

	/* The depth format: one level, only a depth attachment. */
	depth = amdrdna_vk_format_depth(info->format);
	if (depth) {
		if (info->mipLevels != 1U)
			return 0;
		if ((info->usage & ~(uint32_t)VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U)
			return 0;
		return 1;
	}

	/* Otherwise one of the colour formats. */
	colour = amdrdna_vk_format_colour(info->format);
	if (!colour)
		return 0;

	/* Succeeded: the image is laid out pitch linear. */
	return 1;
}

/*
 * Lays an image out: a depth image whole blocks of the depth unit's
 * swizzle (its pitch the block-aligned width's bytes), a colour image's
 * levels in the linear mip chain (its pitch level 0's).
 */
static void
resource_lay_out(
	struct amdrdna_vk_image *image)
{
	struct amdrdna_vk_image first;
	uint32_t width;
	uint32_t height;
	int error;

	/* A depth image: the width and the height rounded up to the 128-pixel blocks. */
	if (image->depth) {
		width = (image->width + AMDRDNA_VK_DEPTH_BLOCK_SIDE - 1U) & ~(AMDRDNA_VK_DEPTH_BLOCK_SIDE - 1U);
		height = (image->height + AMDRDNA_VK_DEPTH_BLOCK_SIDE - 1U) & ~(AMDRDNA_VK_DEPTH_BLOCK_SIDE - 1U);
		image->pitch = width * AMDRDNA_VK_PIXEL_BYTES;
		image->bytes = (uint64_t)image->pitch * height;
		return;
	}

	/* A colour image: level 0 lies after every smaller level, and ends the chain. */
	image->pitch = amdrdna_vk_pitch(image->width);
	error = amdrdna_vk_image_level(image, 0U, &first);
	if (error != 0) {
		image->bytes = (uint64_t)image->pitch * image->height;
		return;
	}

	/* Succeeded: the chain's bytes. */
	image->bytes = first.offset - image->offset + first.bytes;
}

/* Reports a side of a level: the side halved per level, at least one pixel. */
static uint32_t
resource_level_side(
	uint32_t side,
	uint32_t level)
{
	uint32_t halved;

	/* Halved per level. */
	halved = side >> level;
	if (halved == 0U)
		return 1U;

	/* Succeeded: the level's side. */
	return halved;
}

/*
 * vkGetImageSubresourceLayout: [device][image][present]
 * [VkImageSubresource][present] -> [present][VkSubresourceLayout].  A level
 * of a colour image starts where the mip chain has it, with its own pitch;
 * an unknown image or subresource, or a depth image, reports an empty
 * layout.
 */
static int
resource_layout(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	const struct amdrdna_vk_image *image;
	struct amdrdna_vk_image part;
	uint64_t identity;
	uint64_t present;
	int level;

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

	/* A level of layer 0 of a colour image: where it starts in the image, its rows pitch bytes apart. */
	kern_memset(&layout, 0, sizeof(layout));
	image = amdrdna_vk_payload(session, I915_VK_OBJ_IMAGE, identity);
	level = EINVAL;
	if (image != NULL && subresource.arrayLayer == 0U)
		level = amdrdna_vk_image_level(image, subresource.mipLevel, &part);
	if (level == 0) {
		layout.offset = part.offset - image->offset;
		layout.size = part.bytes;
		layout.rowPitch = part.pitch;
		layout.arrayPitch = part.bytes;
		layout.depthPitch = part.bytes;
	}

	/* The layout behind its presence word. */
	drv_i915_wire_reply_u64(reply, 1U);
	i915_vkc_enc_VkSubresourceLayout(reply, &layout);

	/* Succeeded: the reply carries the layout. */
	return 0;
}

/*
 * vkCreateImageView: [device][present][VkImageViewCreateInfo] and the
 * create's tail -> the create's reply.  A 2D view of levels and layer 0 of
 * a known image, in a colour format or the depth format.
 */
static int
resource_create_view(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageViewCreateInfo info;
	struct amdrdna_vk_object *image;
	struct amdrdna_vk_view *view;
	uint64_t identity;
	uint32_t levels;
	uint32_t count;
	bool colour;
	bool depth;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageViewCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A view of a known image, 2D, of levels it has and layer 0, in a colour format or the depth format. */
	error = 0;
	image = amdrdna_vk_object_find(session, I915_VK_OBJ_IMAGE, (uint64_t)(uintptr_t)info.image);
	colour = amdrdna_vk_format_colour(info.format);
	depth = amdrdna_vk_format_depth(info.format);
	levels = 0U;
	if (image != NULL)
		levels = ((const struct amdrdna_vk_image *)image->payload)->levels;
	count = info.subresourceRange.levelCount;
	if (count == VK_REMAINING_MIP_LEVELS && info.subresourceRange.baseMipLevel < levels)
		count = levels - info.subresourceRange.baseMipLevel;
	if (image == NULL) {
		error = EINVAL;
	} else if (info.viewType != VK_IMAGE_VIEW_TYPE_2D) {
		error = ENOTSUP;
	} else if (info.subresourceRange.baseArrayLayer != 0U) {
		error = EINVAL;
	} else if (info.subresourceRange.baseMipLevel >= levels || count == 0U || count > levels - info.subresourceRange.baseMipLevel) {
		error = EINVAL;
	} else if (!colour && !depth) {
		error = ENOTSUP;
	}

	/* The view holds its image. */
	view = NULL;
	if (error == 0)
		error = amdrdna_vk_object_retain(image);
	if (error == 0) {
		view = kern_calloc(1U, sizeof(*view));
		if (view == NULL)
			(void)amdrdna_vk_object_release(image);
	}

	/* The image, the format, and where each channel comes from. */
	if (view != NULL) {
		view->image = image;
		view->format = info.format;
		view->swizzle[0] = resource_swizzle(info.components.r, VK_COMPONENT_SWIZZLE_R);
		view->swizzle[1] = resource_swizzle(info.components.g, VK_COMPONENT_SWIZZLE_G);
		view->swizzle[2] = resource_swizzle(info.components.b, VK_COMPONENT_SWIZZLE_B);
		view->swizzle[3] = resource_swizzle(info.components.a, VK_COMPONENT_SWIZZLE_A);
		view->base_level = info.subresourceRange.baseMipLevel;
		view->level_count = count;
	}

	/* Publishes the view and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE_VIEW, identity, view, resource_view_destroy, error);

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
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkSamplerCreateInfo info;
	struct amdrdna_vk_sampler *sampler;
	uint64_t identity;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkSamplerCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
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
		sampler->compare_enable = info.compareEnable;
		sampler->compare_op = info.compareOp;
	}

	/* Publishes the sampler and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_SAMPLER, identity, sampler, amdrdna_vk_free_payload, 0);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* Destroys a buffer nothing holds: it lets its memory go. */
static int
resource_buffer_destroy(
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_buffer *buffer;
	int error;

	UNUSED_PARAMETER(session);

	buffer = payload;

	/* The memory may go with it when nothing else holds the memory. */
	error = amdrdna_vk_object_release(buffer->memory);
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
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_image *image;
	int error;

	UNUSED_PARAMETER(session);

	image = payload;

	/* The memory may go with it when nothing else holds the memory. */
	error = amdrdna_vk_object_release(image->memory);
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
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_view *view;
	int error;

	UNUSED_PARAMETER(session);

	view = payload;

	/* The image may go with it when nothing else holds the image. */
	error = amdrdna_vk_object_release(view->image);
	kern_free(view);

	/* Reports an image whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the view is gone. */
	return 0;
}
