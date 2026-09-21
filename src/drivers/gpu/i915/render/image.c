/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's images, image views and samplers (see image.h).
 *
 * Every command is decoded exactly as libvulkan encodes it: the records
 * through the generated codec, the framing around them as read from the
 * library's own senders (objects.c and resources.c of libvulkan).
 */

#include "image.h"
#include "codec.h"
#include "gfx.h"
#include "internal.h"
#include "object.h"
#include "reply.h"

#include <kern/klog.h>
#include <kern/kmem.h>

#include <vulkan/vulkan_core.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vulkan-codec.inc"

/* The largest width and height of an image the executor lays out. */
#define I915_GFX_IMAGE_MAX_EXTENT	16384U

static uint32_t i915_gfx_format_bytes(uint32_t format);
static int i915_gfx_image_supported(const VkImageCreateInfo *info);

/*
 * Creates a VkImage: vkCreateImage, a generic create.
 *
 * XXX: one layout -- 2D, one level, one layer, one sample, linear rows of
 * width * 4 bytes.  Anything else is refused here, by name, rather than laid
 * out wrongly.  A depth image is laid out in whole Y tiles.
 */
int
drv_i915_gfx_create_image(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageCreateInfo info;
	struct i915_gfx_image *image;
	uint64_t identity;
	uint32_t texel_bytes;
	int supported;
	int error;

	/* Decodes the create info behind the device and its presence marker. */
	memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = drv_i915_gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Decides whether the image has the one layout the executor supports. */
	supported = i915_gfx_image_supported(&info);

	/* Refuses any other image by name, and allocates a supported one. */
	image = NULL;
	error = 0;
	if (supported == 0) {
		kern_logf("i915: vk: XXX vkCreateImage refused: type %u format %u %ux%ux%u levels %u layers %u samples %u\n",
			  (unsigned)info.imageType,
			  (unsigned)info.format,
			  info.extent.width,
			  info.extent.height,
			  info.extent.depth,
			  info.mipLevels,
			  info.arrayLayers,
			  (unsigned)info.samples);
		error = ENOTSUP;
	} else {
		image = kern_calloc(1U, sizeof(*image));
	}

	/* Lays the image out as linear rows of whole texels. */
	if (image != NULL) {
		texel_bytes = i915_gfx_format_bytes(info.format);
		image->format = info.format;
		image->width = info.extent.width;
		image->height = info.extent.height;
		image->usage = info.usage;
		image->pitch = info.extent.width * texel_bytes;
		image->bytes = (uint64_t)image->pitch * info.extent.height;

		/*
		 * A Gen9+ depth buffer is always Y-tiled (isl_emit_depth_stencil.c):
		 * whole 4 KiB tiles of 128 bytes by 32 rows.  Nothing reads depth
		 * texel by texel here (the clear is one value), so only the extent
		 * matters.  XXX: a depth image can not be copied or sampled.
		 */
		if (info.format == VK_FORMAT_D32_SFLOAT) {
			image->pitch = (image->pitch + 127U) & ~127U;
			image->bytes = (uint64_t)image->pitch * ((info.extent.height + 31U) & ~31U);
		}
	}

	/* Publishes the image and answers; a refused or failed image is reported there. */
	drv_i915_gfx_create_reply(session, reply, I915_VK_OBJ_IMAGE, identity, image, error);

	/* Succeeded: the reply carries the result of the create. */
	return 0;
}

/*
 * Creates a VkImageView: vkCreateImageView, a generic create.
 *
 * XXX: a view is the whole image in the image's own format; swizzles and
 * sub-ranges are not applied.  A view of an unknown image fails.
 */
int
drv_i915_gfx_create_image_view(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageViewCreateInfo info;
	struct i915_gfx_view *view;
	struct i915_gfx_image *image;
	uint64_t identity;
	int error;

	/* Decodes the create info behind the device and its presence marker. */
	memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageViewCreateInfo(reader, &session->arena, &info);
	identity = drv_i915_gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Resolves the image the view shows; the view of an unknown image fails. */
	view = NULL;
	error = 0;
	image = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, (uint64_t)(uintptr_t)info.image);
	if (image == NULL) {
		error = EINVAL;
	} else {
		view = kern_calloc(1U, sizeof(*view));
	}

	/* Records the image and the format the view was created with. */
	if (view != NULL) {
		view->image = image;
		view->format = info.format;
	}

	/* Publishes the view and answers; a failed view is reported there. */
	drv_i915_gfx_create_reply(session, reply, I915_VK_OBJ_IMAGE_VIEW, identity, view, error);

	/* Succeeded: the reply carries the result of the create. */
	return 0;
}

/*
 * Creates a VkSampler: vkCreateSampler, a generic create.
 *
 * The sampler keeps the filters and the address modes along u and v.
 */
int
drv_i915_gfx_create_sampler(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkSamplerCreateInfo info;
	struct i915_gfx_sampler *sampler;
	uint64_t identity;

	/* Decodes the create info behind the device and its presence marker. */
	memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkSamplerCreateInfo(reader, &session->arena, &info);
	identity = drv_i915_gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Allocates the sampler and keeps what a texture read uses. */
	sampler = kern_calloc(1U, sizeof(*sampler));
	if (sampler != NULL) {
		sampler->mag_filter = info.magFilter;
		sampler->min_filter = info.minFilter;
		sampler->address_u = info.addressModeU;
		sampler->address_v = info.addressModeV;
	}

	/* Publishes the sampler and answers; a failed allocation is reported there. */
	drv_i915_gfx_create_reply(session, reply, I915_VK_OBJ_SAMPLER, identity, sampler, 0);

	/* Succeeded: the reply carries the result of the create. */
	return 0;
}

/*
 * Reports the layout of an image's one subresource:
 * vkGetImageSubresourceLayout.
 *
 * The command is [device][image][present][VkImageSubresource][present] and
 * the reply [present][VkSubresourceLayout].  Every image is one linear
 * level, so the subresource asked for is not consulted; an unknown image
 * reports an empty layout.
 */
int
drv_i915_gfx_subresource_layout(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	struct i915_gfx_image *image;
	uint64_t image_id;
	uint64_t present;

	/* Reads the image behind the device and resolves it. */
	(void)drv_i915_wire_read_u64(reader);
	image_id = drv_i915_wire_read_u64(reader);
	image = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, image_id);

	/* Decodes the subresource when it is present; it is not consulted. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkImageSubresource(reader, &session->arena, &subresource);

	/* Skips the output's presence marker. */
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Describes the image's one linear level. */
	memset(&layout, 0, sizeof(layout));
	if (image != NULL) {
		layout.size = image->bytes;
		layout.rowPitch = image->pitch;
		layout.arrayPitch = image->bytes;
		layout.depthPitch = image->bytes;
	}

	/* Writes the layout behind its presence marker. */
	drv_i915_wire_reply_u64(reply, 1U);
	i915_vkc_enc_VkSubresourceLayout(reply, &layout);

	/* Succeeded: the reply carries the layout. */
	return 0;
}

/* Reports the bytes to a texel of a format the executor lays out; 0 for any other. */
static uint32_t
i915_gfx_format_bytes(
	uint32_t format)
{
	/* Only these formats are laid out. */
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_D32_SFLOAT:
		/* A four-byte texel. */
		return 4U;
	default:
		/* Not a format the executor lays out. */
		return 0U;
	}
}

/* Decides whether an image has the one layout the executor supports. */
static int
i915_gfx_image_supported(
	const VkImageCreateInfo *info)
{
	uint32_t texel_bytes;

	/* Only a 2D image. */
	if (info->imageType != VK_IMAGE_TYPE_2D)
		return 0;

	/* Only one mip level. */
	if (info->mipLevels != 1U)
		return 0;

	/* Only one array layer. */
	if (info->arrayLayers != 1U)
		return 0;

	/* Only one sample. */
	if (info->samples != VK_SAMPLE_COUNT_1_BIT)
		return 0;

	/* Only a depth of one. */
	if (info->extent.depth != 1U)
		return 0;

	/* Refuses an empty image. */
	if (info->extent.width == 0U || info->extent.height == 0U)
		return 0;

	/* Refuses an image wider or taller than the executor lays out. */
	if (info->extent.width > I915_GFX_IMAGE_MAX_EXTENT || info->extent.height > I915_GFX_IMAGE_MAX_EXTENT)
		return 0;

	/* Only a format the executor lays out. */
	texel_bytes = i915_gfx_format_bytes(info->format);
	if (texel_bytes == 0U)
		return 0;

	/* Succeeded: the image has the supported layout. */
	return 1;
}
