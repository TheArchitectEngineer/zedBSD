/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's images: VkImage, its binding to memory, what it
 * requires and the layout of a linear one, VkImageView with the texture
 * unit's words of the view, and VkSampler with the words of the sampler.
 *
 * An image is laid out when it is made (image-state.c) and takes the
 * storage of the memory it is bound to.  A view is made of a bound image
 * and keeps it; its words name the image's address, so they are built
 * once, when the view is made.  An image that declares external memory
 * (libvulkan's declaration for a window system's shared image, of a
 * handle type the session shares by) is made as any other; its memory is
 * the exported or imported allocation it is bound to (vk-memory.c).
 *
 * Procedure referenced: zedBSD src/drivers/gpu/bcm2711/vulkan-resource.c
 * and vulkan-input.c (the wire of each command).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/powervr/render-private.h"
#include "drivers/gpu/powervr/vk.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/* The usages an image may have. */
#define IMAGE_USAGES		(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | \
				 VK_IMAGE_USAGE_TRANSFER_DST_BIT | \
				 VK_IMAGE_USAGE_SAMPLED_BIT | \
				 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | \
				 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)

/*
 * Whether a sampler has asked for a border colour other than transparent
 * black.  Set once, by the first such sampler of any session, so the
 * kernel log says it once a boot; a lost race only prints twice.
 */
static bool border_noted;

static int create_image(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int image_chain(const struct i915_wire_reader *reader, bool *present);
static int image_check(const VkImageCreateInfo *info, const struct powervr_format *format);
static int bind_image(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int image_requirements(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int subresource_layout(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int create_view(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int view_build(struct powervr_vk_session *session, const VkImageViewCreateInfo *info, struct powervr_vk_image_view *view);
static int create_sampler(struct powervr_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int image_destroy(struct powervr_vk_session *session, void *payload);
static int view_destroy(struct powervr_vk_session *session, void *payload);

/*
 * Runs an image, image view or sampler command; *handled stays clear for
 * another handler's opcode.
 */
int
powervr_vk_image_route(
	struct powervr_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another handler. */
	(void)requested;
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_IMAGE:
		error = create_image(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE);
		break;
	case GPU_OP_BIND_IMAGE_MEMORY:
		error = bind_image(session, reader, reply);
		break;
	case GPU_OP_GET_IMAGE_MEMORY_REQUIREMENTS:
		error = image_requirements(session, reader, reply);
		break;
	case GPU_OP_GET_IMAGE_SUBRESOURCE_LAYOUT:
		error = subresource_layout(session, reader, reply);
		break;
	case GPU_OP_CREATE_IMAGE_VIEW:
		error = create_view(session, reader, reply);
		break;
	case GPU_OP_DESTROY_IMAGE_VIEW:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_IMAGE_VIEW);
		break;
	case GPU_OP_CREATE_SAMPLER:
		error = create_sampler(session, reader, reply);
		break;
	case GPU_OP_DESTROY_SAMPLER:
		error = powervr_vk_destroy_plain(session, reader, I915_VK_OBJ_SAMPLER);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/* Reports a bound image's device address; EIO when it is not bound. */
int
powervr_vk_image_address(
	const struct powervr_vk_image *image,
	uint64_t *address)
{
	const struct powervr_vk_memory *memory;

	/* An image bound to memory. */
	if (image->memory == NULL)
		return EIO;
	memory = image->memory->payload;

	/* The memory's address moved by the image's offset. */
	*address = memory->storage->address + image->offset;

	/* Succeeded: the image's address. */
	return 0;
}

/*
 * vkCreateImage: [device][present][VkImageCreateInfo] and the create's
 * tail -> the create's reply.  A two-dimensional image of one sample in a
 * format of the table; one that declares external memory names a
 * handle type the session shares by.
 */
static int
create_image(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageCreateInfo info;
	struct powervr_vk_image *image;
	const struct powervr_format *format;
	uint64_t identity;
	bool external;
	int declared;
	int error;

	/* Whether the record chains an external memory declaration the executor takes, then the record and the identity. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	declared = image_chain(reader, &external);
	if (declared == EINVAL)
		return EINVAL;
	i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A declaration the executor takes, then a shape and format it takes. */
	format = powervr_format_find(info.format);
	error = declared;
	if (error == 0)
		error = image_check(&info, format);

	/* The record with its layout. */
	image = NULL;
	if (error == 0) {
		image = kern_calloc(1, sizeof(*image));
		if (image == NULL)
			error = ENOMEM;
	}
	if (error == 0) {
		image->format = format;
		image->usage = info.usage;
		image->external = external;
		image->shape.width = info.extent.width;
		image->shape.height = info.extent.height;
		image->shape.depth = info.extent.depth;
		image->shape.levels = info.mipLevels;
		image->shape.layers = info.arrayLayers;
		image->shape.linear = info.tiling == VK_IMAGE_TILING_LINEAR;
		image->shape.volume = false;
		error = powervr_image_lay_out(&image->shape, format, &image->layout);
	}

	/* An image no allocation can hold is out of device memory. */
	if (error == 0 && image->layout.bytes > POWERVR_RENDER_MAX_BYTES)
		error = ENOMEM;

	/* Publishes the image and answers; a refused one is destroyed there. */
	powervr_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE, identity, image, image_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Reports in *present whether an image's create record chains libvulkan's
 * external memory declaration ([sType][u64 present], then [sType][u64 0]
 * [u32 handle types] ahead of the rest), without moving the reader: the
 * record's decoder steps over it.  ENOTSUP for another extension, one
 * that chains more, or a handle type the session does not share by.
 *
 * Procedure referenced: zedBSD src/drivers/gpu/bcm2711/vulkan-resource.c
 * (external_declaration).
 */
static int
image_chain(
	const struct i915_wire_reader *reader,
	bool *present)
{
	struct i915_wire_reader peek;
	uint64_t chained;
	uint64_t next;
	uint32_t type;
	uint32_t handles;

	/* A copy of the reader steps over sType to the chain's presence word. */
	*present = false;
	peek = *reader;
	(void)drv_i915_wire_read_u32(&peek);
	chained = drv_i915_wire_read_u64(&peek);
	if (peek.error != 0 || chained > 1)
		return EINVAL;
	if (chained == 0)
		return 0;

	/* The one declaration, of handle types the session shares by. */
	type = drv_i915_wire_read_u32(&peek);
	next = drv_i915_wire_read_u64(&peek);
	handles = drv_i915_wire_read_u32(&peek);
	if (peek.error != 0)
		return EINVAL;
	if (type != VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO || next != 0)
		return ENOTSUP;
	if (handles == 0 || (handles & ~POWERVR_VK_EXTERNAL_HANDLES) != 0)
		return ENOTSUP;

	/* Succeeded: a declaration follows. */
	*present = true;
	return 0;
}

/*
 * Checks an image's create against what the executor makes: ENOTSUP for
 * what it does not take, EINVAL for a create Vulkan does not allow.
 */
static int
image_check(
	const VkImageCreateInfo *info,
	const struct powervr_format *format)
{
	uint32_t features;

	/* A plain two-dimensional image of one sample. */
	if (info->flags != 0 || info->imageType != VK_IMAGE_TYPE_2D || info->samples != VK_SAMPLE_COUNT_1_BIT)
		return ENOTSUP;
	if (info->extent.depth != 1)
		return EINVAL;

	/* No larger than the device reports. */
	if (info->extent.width > POWERVR_VK_IMAGE_EDGE || info->extent.height > POWERVR_VK_IMAGE_EDGE || info->arrayLayers > POWERVR_VK_IMAGE_LAYERS)
		return ENOTSUP;

	/* A usage the executor knows. */
	if (info->usage == 0)
		return EINVAL;
	if ((info->usage & ~(uint32_t)IMAGE_USAGES) != 0)
		return ENOTSUP;

	/*
	 * A format of the table with features on the tiling.  A linear image
	 * is checked against what the format will have once linear images
	 * are reported, so it is made while the queries still say nothing.
	 */
	if (format == NULL)
		return ENOTSUP;
	if (info->tiling == VK_IMAGE_TILING_OPTIMAL) {
		features = format->optimal_features;
	} else if (info->tiling == VK_IMAGE_TILING_LINEAR) {
		features = format->linear_features;
	} else {
		return ENOTSUP;
	}
	if (features == 0)
		return ENOTSUP;

	/* Only a linear image starts with its contents defined by the host. */
	if (info->initialLayout == VK_IMAGE_LAYOUT_UNDEFINED)
		return 0;
	if (info->initialLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && info->tiling == VK_IMAGE_TILING_LINEAR)
		return 0;

	/* Any other initial layout is not allowed. */
	return EINVAL;
}

/*
 * vkBindImageMemory: [device][image][memory][offset] -> [result].  An
 * image is bound once, on the image boundary, wholly inside the
 * allocation; it holds the allocation from then on.
 */
static int
bind_image(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct powervr_vk_object *memory;
	struct powervr_vk_image *image;
	const struct powervr_vk_memory *storage;
	uint64_t image_id;
	uint64_t memory_id;
	uint64_t offset;
	uint32_t result;
	int error;

	/* The image, the allocation and the offset behind the device. */
	(void)drv_i915_wire_read_u64(reader);
	image_id = drv_i915_wire_read_u64(reader);
	memory_id = drv_i915_wire_read_u64(reader);
	offset = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An unbound image and an allocation that exist. */
	error = 0;
	image = powervr_vk_payload(session, I915_VK_OBJ_IMAGE, image_id);
	memory = powervr_vk_object_find(session, I915_VK_OBJ_MEMORY, memory_id);
	if (image == NULL || memory == NULL || image->memory != NULL)
		error = EINVAL;

	/* On the boundary, wholly inside the allocation. */
	if (error == 0) {
		storage = memory->payload;
		if ((offset % POWERVR_IMAGE_ALIGN) != 0 || offset > storage->bytes || image->layout.bytes > storage->bytes - offset)
			error = EINVAL;
	}

	/* The image holds the allocation. */
	if (error == 0)
		error = powervr_vk_object_retain(memory);
	if (error == 0) {
		image->memory = memory;
		image->offset = offset;
	}

	/* The bind's result. */
	result = powervr_vk_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * vkGetImageMemoryRequirements: [device][image][present] -> [present]
 * [VkMemoryRequirements].  The image's bytes in whole pages on the page
 * boundary, of either type.
 */
static int
image_requirements(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkMemoryRequirements record;
	const struct powervr_vk_image *image;
	uint64_t identity;
	uint64_t bytes;

	/* The image between the device and the presence word. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* How many bytes the image occupies; an unknown one needs no memory of any type. */
	bytes = 0;
	image = powervr_vk_payload(session, I915_VK_OBJ_IMAGE, identity);
	if (image != NULL)
		bytes = image->layout.bytes;

	/* The requirements. */
	kern_memset(&record, 0, sizeof(record));
	record.size = (bytes + POWERVR_IMAGE_ALIGN - 1U) & ~(uint64_t)(POWERVR_IMAGE_ALIGN - 1U);
	record.alignment = POWERVR_IMAGE_ALIGN;
	record.memoryTypeBits = 0;
	if (bytes != 0)
		record.memoryTypeBits = (1U << POWERVR_VK_MEMORY_DEVICE) | (1U << POWERVR_VK_MEMORY_HOST);

	/* The requirements behind their presence word. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkMemoryRequirements(reply, &record);

	/* Succeeded: the reply carries the requirements. */
	return 0;
}

/*
 * vkGetImageSubresourceLayout: [device][image][present]
 * [VkImageSubresource][present] -> [present][VkSubresourceLayout].  A
 * linear image's one subresource starts at 0 with its rows; any other
 * subresource answers all zeros, since Vulkan asks only of linear ones.
 */
static int
subresource_layout(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageSubresource subresource;
	VkSubresourceLayout layout;
	const struct powervr_vk_image *image;
	uint64_t identity;

	/* The image and the subresource behind their presence words. */
	kern_memset(&subresource, 0, sizeof(subresource));
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageSubresource(reader, &session->arena, &subresource);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The colour of a linear image's first level and layer. */
	kern_memset(&layout, 0, sizeof(layout));
	image = powervr_vk_payload(session, I915_VK_OBJ_IMAGE, identity);
	if (image != NULL &&
	    image->shape.linear &&
	    subresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
	    subresource.mipLevel == 0 &&
	    subresource.arrayLayer == 0) {
		layout.size = image->layout.bytes;
		layout.rowPitch = image->layout.row_pitch;
		layout.arrayPitch = image->layout.bytes;
		layout.depthPitch = image->layout.bytes;
	}

	/* The layout behind its presence word. */
	drv_i915_wire_reply_u64(reply, 1);
	i915_vkc_enc_VkSubresourceLayout(reply, &layout);

	/* Succeeded: the reply carries the layout. */
	return 0;
}

/*
 * vkCreateImageView: [device][present][VkImageViewCreateInfo] and the
 * create's tail -> the create's reply.  A two-dimensional view (or array
 * of them) of a bound image in the image's own format.
 */
static int
create_view(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkImageViewCreateInfo info;
	struct powervr_vk_image_view *view;
	uint64_t identity;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkImageViewCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The view, its words, and its hold on the image. */
	error = 0;
	view = kern_calloc(1, sizeof(*view));
	if (view == NULL)
		error = ENOMEM;
	if (error == 0)
		error = view_build(session, &info, view);

	/* Publishes the view and answers. */
	powervr_vk_create_reply(session, reply, I915_VK_OBJ_IMAGE_VIEW, identity, view, view_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Fills a view from its create info: checks it against the image, builds
 * the texture unit's words and takes a hold on the image.
 */
static int
view_build(
	struct powervr_vk_session *session,
	const VkImageViewCreateInfo *info,
	struct powervr_vk_image_view *view)
{
	struct powervr_view_shape shape;
	struct powervr_vk_object *object;
	const struct powervr_vk_image *image;
	uint32_t aspect;
	uint32_t levels;
	uint32_t layers;
	int error;

	/* A bound image of the session. */
	object = powervr_vk_object_find(session, I915_VK_OBJ_IMAGE, (uint64_t)(uintptr_t)info->image);
	if (object == NULL)
		return EINVAL;
	image = object->payload;
	kern_memset(&shape, 0, sizeof(shape));
	error = powervr_vk_image_address(image, &shape.address);
	if (error != 0)
		return EINVAL;

	/* A plain view in the image's format, of the image's one aspect. */
	if (info->flags != 0 || info->format != image->format->vk_format)
		return ENOTSUP;
	if (info->viewType != VK_IMAGE_VIEW_TYPE_2D && info->viewType != VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		return ENOTSUP;
	aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if ((image->format->kind & POWERVR_FORMAT_DEPTH) != 0)
		aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	if (info->subresourceRange.aspectMask != aspect)
		return EINVAL;

	/* Levels and layers inside the image (the remaining ones when so asked). */
	levels = info->subresourceRange.levelCount;
	if (levels == VK_REMAINING_MIP_LEVELS && info->subresourceRange.baseMipLevel < image->shape.levels)
		levels = image->shape.levels - info->subresourceRange.baseMipLevel;
	layers = info->subresourceRange.layerCount;
	if (layers == VK_REMAINING_ARRAY_LAYERS && info->subresourceRange.baseArrayLayer < image->shape.layers)
		layers = image->shape.layers - info->subresourceRange.baseArrayLayer;
	if (levels == 0 || info->subresourceRange.baseMipLevel >= image->shape.levels || levels > image->shape.levels - info->subresourceRange.baseMipLevel)
		return EINVAL;
	if (layers == 0 || info->subresourceRange.baseArrayLayer >= image->shape.layers || layers > image->shape.layers - info->subresourceRange.baseArrayLayer)
		return EINVAL;
	if (info->viewType == VK_IMAGE_VIEW_TYPE_2D && layers != 1)
		return EINVAL;

	/* The words of the view. */
	shape.format = image->format;
	shape.layout = &image->layout;
	shape.width = image->shape.width;
	shape.height = image->shape.height;
	shape.image_levels = image->shape.levels;
	shape.base_level = info->subresourceRange.baseMipLevel;
	shape.level_count = levels;
	shape.base_layer = info->subresourceRange.baseArrayLayer;
	shape.layer_count = layers;
	shape.linear = image->shape.linear;
	shape.mapping[0] = info->components.r;
	shape.mapping[1] = info->components.g;
	shape.mapping[2] = info->components.b;
	shape.mapping[3] = info->components.a;
	error = powervr_view_words(&shape, view->words);
	if (error != 0)
		return error;

	/* The view holds the image. */
	error = powervr_vk_object_retain(object);
	if (error != 0)
		return error;
	view->image = object;
	view->aspect = aspect;
	view->base_level = info->subresourceRange.baseMipLevel;
	view->base_layer = info->subresourceRange.baseArrayLayer;

	/* Succeeded: the view is filled. */
	return 0;
}

/*
 * vkCreateSampler: [device][present][VkSamplerCreateInfo] and the
 * create's tail -> the create's reply.  Nearest and linear filters, the
 * four core address modes, no anisotropy and no comparison; a border is
 * always transparent black.
 */
static int
create_sampler(
	struct powervr_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkSamplerCreateInfo info;
	struct powervr_sampler_shape shape;
	struct powervr_vk_sampler *sampler;
	uint64_t identity;
	bool border;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkSamplerCreateInfo(reader, &session->arena, &info);
	identity = powervr_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A plain sampler. */
	error = 0;
	if (info.flags != 0 || info.anisotropyEnable != VK_FALSE || info.compareEnable != VK_FALSE)
		error = ENOTSUP;

	/* A border other than transparent black is noted once, then sampled as transparent black. */
	border = false;
	if (info.addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
	    info.addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
	    info.addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)
		border = true;
	if (error == 0 && border && info.borderColor != VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK && info.borderColor != VK_BORDER_COLOR_INT_TRANSPARENT_BLACK && !border_noted) {
		border_noted = true;
		kern_logf("powervr: a sampler's border colour %u reads as transparent black\n", (unsigned)info.borderColor);
	}

	/* The sampler's words, from its floats' bits. */
	sampler = NULL;
	if (error == 0) {
		kern_memset(&shape, 0, sizeof(shape));
		shape.mag_filter = info.magFilter;
		shape.min_filter = info.minFilter;
		shape.mipmap_mode = info.mipmapMode;
		shape.address_u = info.addressModeU;
		shape.address_v = info.addressModeV;
		shape.address_w = info.addressModeW;
		kern_memcpy(&shape.lod_bias_bits, &info.mipLodBias, sizeof(shape.lod_bias_bits));
		kern_memcpy(&shape.min_lod_bits, &info.minLod, sizeof(shape.min_lod_bits));
		kern_memcpy(&shape.max_lod_bits, &info.maxLod, sizeof(shape.max_lod_bits));
		shape.unnormalized = info.unnormalizedCoordinates != VK_FALSE;
		sampler = kern_calloc(1, sizeof(*sampler));
		if (sampler == NULL)
			error = ENOMEM;
	}
	if (error == 0)
		error = powervr_sampler_words(&shape, sampler->words);

	/* Publishes the sampler and answers. */
	powervr_vk_create_reply(session, reply, I915_VK_OBJ_SAMPLER, identity, sampler, powervr_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* Destroys an image nothing holds: it lets its allocation go. */
static int
image_destroy(
	struct powervr_vk_session *session,
	void *payload)
{
	struct powervr_vk_image *image;
	int error;

	/* The allocation, then the record. */
	(void)session;
	image = payload;
	error = powervr_vk_object_release(image->memory);
	kern_free(image);
	if (error != 0)
		return error;

	/* Succeeded: the image is gone. */
	return 0;
}

/* Destroys a view nothing holds: it lets its image go. */
static int
view_destroy(
	struct powervr_vk_session *session,
	void *payload)
{
	struct powervr_vk_image_view *view;
	int error;

	/* The image, then the record. */
	(void)session;
	view = payload;
	error = powervr_vk_object_release(view->image);
	kern_free(view);
	if (error != 0)
		return error;

	/* Succeeded: the view is gone. */
	return 0;
}
