/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Objects of the executor's graphics path (see gfx.h).  Every command here is decoded exactly as
 * libvulkan encodes it: the records through the generated codec (vkc.h), the framing around them as
 * read from the library's own senders (objects.c, resources.c, memory.c, descriptors.c, pipeline.c,
 * sync.c).  The framing of a generic create is
 *   [device][present][create info][pAllocator = 0][present][identity] -> [result][present][identity]
 * and of a generic destroy
 *   [device][identity][pAllocator = 0] -> (the echoed opcode alone).
 */

#include "vkc.h"
#include "gfx.h"
#include "vk.h"

#include "../internal.h"

#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>

#include <errno.h>

#include "codec-generated.inc"

/* Every live VkDeviceMemory, so that a blob can find the allocation it is the storage of. */
static struct gfx_memory *gfx_memories;

static uint32_t
gfx_result(int error)
{
	if (error == 0)
		return 0U;
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (error == ENOTSUP)
		return (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT;
	return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
}

/* The tail every generic create shares: [pAllocator][present][identity]. */
static uint64_t
gfx_create_tail(struct i915_vk_reader *reader)
{
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	return i915_vk_read_u64(reader);
}

/* Publishes `object` under `identity` and writes the create reply; frees the object on failure. */
static int
gfx_create_reply(struct i915_vk_session *session, struct i915_vk_writer *reply,
	enum i915_vk_object_kind kind, uint64_t identity, void *object, int error)
{
	if (error == 0 && object == NULL)
		error = ENOMEM;
	if (error == 0) {
		error = i915_vk_obj_insert(session->vk, kind, identity, object);
		if (error != 0)
			kern_free(object);
	} else if (object != NULL) {
		kern_free(object);
	}

	i915_vk_reply_u32(reply, gfx_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);
		i915_vk_reply_u64(reply, identity);
	}
	return 0;
}

/* A generic destroy of an object that owns nothing but itself. */
static int
gfx_destroy_plain(struct i915_vk_session *session, struct i915_vk_reader *reader,
	enum i915_vk_object_kind kind)
{
	uint64_t identity;
	void *object;

	(void)i915_vk_read_u64(reader);			/* device */
	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator */
	if (reader->error != 0)
		return EINVAL;

	object = i915_vk_obj_lookup(session->vk, kind, identity);
	if (object != NULL) {
		i915_vk_obj_remove(session->vk, kind, identity);
		kern_free(object);
	}
	return 0;
}

/* ---------------- memory ---------------- */

uint8_t *
i915_vk_gfx_memory_cpu(struct gfx_memory *memory, uint64_t offset, uint64_t bytes)
{
	if (memory == NULL || memory->object == NULL || offset > memory->object->bytes ||
	    bytes > memory->object->bytes - offset)
		return NULL;
	return (uint8_t *)kern_pmem_to_kernel(memory->object->run.paddr) + offset;
}

uint64_t
i915_vk_gfx_memory_va(struct gfx_memory *memory, uint64_t offset)
{
	if (memory == NULL || memory->object == NULL || memory->object->va == 0U)
		return 0U;
	return memory->object->va + offset;
}

/*
 * The blob libvulkan creates for an allocation names it by `blob_id` (memory.c memory_export):
 * that blob IS the allocation's storage -- the application maps it, the GPU addresses it.
 */
int
drv_i915_vk_blob_attach(struct i915_vk_device *vk, uint64_t blob_id, struct i915_gem_object *object)
{
	struct gfx_memory *memory;

	for (memory = gfx_memories; memory != NULL; memory = memory->next) {
		if (memory->vk != vk || memory->identity != blob_id)
			continue;
		if (memory->object != NULL || object->bytes < memory->size)
			return EINVAL;
		memory->object = object;
		return 0;
	}
	return ENOENT;
}

/* The blob is going away: whatever still names it has no storage from here on. */
void
drv_i915_vk_blob_detach(struct i915_vk_device *vk, struct i915_gem_object *object)
{
	struct gfx_memory *memory;

	for (memory = gfx_memories; memory != NULL; memory = memory->next)
		if (memory->vk == vk && memory->object == object)
			memory->object = NULL;
}

/*
 * vkAllocateMemory (memory.c): [device][present][sType][chain present]{[sType][pNext = 0][u32]}
 * [allocationSize][memoryTypeIndex][pAllocator][present][identity] -> [result][present][identity].
 */
static int
gfx_allocate_memory(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	struct gfx_memory *memory;
	uint64_t chain;
	uint64_t size;
	uint64_t identity;
	uint32_t type;

	(void)i915_vk_read_u64(reader);			/* device */
	(void)i915_vk_read_u64(reader);			/* pAllocateInfo present */
	(void)i915_vk_read_u32(reader);			/* sType */
	chain = i915_vk_read_u64(reader);
	if (chain != 0U) {
		/* XXX: export / import declarations are read and not acted on (no sharing between contexts). */
		(void)i915_vk_read_u32(reader);
		(void)i915_vk_read_u64(reader);
		(void)i915_vk_read_u32(reader);
		kern_logf("i915: vk: XXX vkAllocateMemory external-memory declaration ignored\n");
	}
	size = i915_vk_read_u64(reader);
	type = i915_vk_read_u32(reader);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	memory = NULL;
	if (size != 0U && type == 0U)
		memory = kern_calloc(1U, sizeof(*memory));
	if (memory != NULL) {
		memory->vk = session->vk;
		memory->identity = identity;
		memory->size = size;
	}
	(void)gfx_create_reply(session, reply, I915_VK_OBJ_MEMORY, identity, memory,
		size == 0U || type != 0U ? EINVAL : 0);
	if (memory != NULL && i915_vk_obj_lookup(session->vk, I915_VK_OBJ_MEMORY, identity) == memory) {
		memory->next = gfx_memories;
		gfx_memories = memory;
	}
	return 0;
}

static int
gfx_free_memory(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	struct gfx_memory *memory;
	struct gfx_memory **link;
	uint64_t identity;

	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	memory = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_MEMORY, identity);
	if (memory == NULL)
		return 0;
	i915_vk_obj_remove(session->vk, I915_VK_OBJ_MEMORY, identity);
	for (link = &gfx_memories; *link != NULL; link = &(*link)->next)
		if (*link == memory) {
			*link = memory->next;
			break;
		}
	/* XXX: buffers and images bound to it keep a dangling pointer; the application frees them first. */
	kern_free(memory);
	return 0;
}

/* vkBind{Buffer,Image}Memory: [device][resource][memory][offset] -> [result]. */
static int
gfx_bind(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply, int image)
{
	struct gfx_memory *memory;
	uint64_t resource;
	uint64_t memory_id;
	uint64_t offset;
	int error;

	(void)i915_vk_read_u64(reader);
	resource = i915_vk_read_u64(reader);
	memory_id = i915_vk_read_u64(reader);
	offset = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	error = EINVAL;
	memory = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_MEMORY, memory_id);
	if (memory != NULL && image != 0) {
		struct gfx_image *target = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE, resource);

		if (target != NULL && offset <= memory->size && target->bytes <= memory->size - offset) {
			target->memory = memory;
			target->offset = offset;
			error = 0;
		}
	} else if (memory != NULL) {
		struct gfx_buffer *target = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_BUFFER, resource);

		if (target != NULL && offset <= memory->size && target->size <= memory->size - offset) {
			target->memory = memory;
			target->offset = offset;
			error = 0;
		}
	}
	i915_vk_reply_u32(reply, gfx_result(error));
	return 0;
}

/* vkGet{Buffer,Image}MemoryRequirements: [device][resource][present] -> [present][VkMemoryRequirements]. */
static int
gfx_requirements(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply, int image)
{
	VkMemoryRequirements requirements;
	uint64_t resource;
	uint64_t bytes;

	(void)i915_vk_read_u64(reader);
	resource = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	bytes = 0U;
	if (image != 0) {
		struct gfx_image *target = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE, resource);

		if (target != NULL)
			bytes = target->bytes;
	} else {
		struct gfx_buffer *target = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_BUFFER, resource);

		if (target != NULL)
			bytes = target->size;
	}

	/* Whole pages: the storage of an allocation is a blob, and a blob is pages. */
	memset(&requirements, 0, sizeof(requirements));
	requirements.size = (bytes + 4095U) & ~(uint64_t)4095U;
	requirements.alignment = 4096U;
	requirements.memoryTypeBits = bytes != 0U ? 1U : 0U;
	i915_vk_reply_u64(reply, 1U);
	i915_vkc_enc_VkMemoryRequirements(reply, &requirements);
	return 0;
}

/* ---------------- buffer, image, view, sampler ---------------- */

static int
gfx_create_buffer(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkBufferCreateInfo info;
	struct gfx_buffer *buffer;
	uint64_t identity;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkBufferCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	buffer = kern_calloc(1U, sizeof(*buffer));
	if (buffer != NULL) {
		buffer->size = info.size;
		buffer->usage = info.usage;
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_BUFFER, identity, buffer, 0);
}

/* Bytes to a texel of the formats this executor lays out; 0 = not one of them. */
static uint32_t
gfx_format_bytes(uint32_t format)
{
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_D32_SFLOAT:
		return 4U;
	default:
		return 0U;
	}
}

static int
gfx_create_image(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkImageCreateInfo info;
	struct gfx_image *image;
	uint64_t identity;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkImageCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/*
	 * XXX: one layout -- 2D, one level, one layer, one sample, linear rows of width * 4 bytes.
	 * Anything else is refused here, by name, rather than laid out wrongly.
	 */
	image = NULL;
	error = 0;
	if (info.imageType != VK_IMAGE_TYPE_2D || info.mipLevels != 1U || info.arrayLayers != 1U ||
	    info.samples != VK_SAMPLE_COUNT_1_BIT || info.extent.depth != 1U || info.extent.width == 0U ||
	    info.extent.height == 0U || info.extent.width > 16384U || info.extent.height > 16384U ||
	    gfx_format_bytes(info.format) == 0U) {
		kern_logf("i915: vk: XXX vkCreateImage refused: type %u format %u %ux%ux%u levels %u layers %u samples %u\n",
			(unsigned)info.imageType, (unsigned)info.format, info.extent.width, info.extent.height,
			info.extent.depth, info.mipLevels, info.arrayLayers, (unsigned)info.samples);
		error = ENOTSUP;
	} else {
		image = kern_calloc(1U, sizeof(*image));
	}
	if (image != NULL) {
		image->format = info.format;
		image->width = info.extent.width;
		image->height = info.extent.height;
		image->usage = info.usage;
		image->pitch = info.extent.width * gfx_format_bytes(info.format);
		image->bytes = (uint64_t)image->pitch * info.extent.height;
		if (info.format == VK_FORMAT_D32_SFLOAT) {
			/*
			 * A Gen9+ depth buffer is always Y-tiled (isl_emit_depth_stencil.c): whole 4 KiB tiles of
			 * 128 bytes by 32 rows.  Nothing reads depth texel by texel here (the clear is one value),
			 * so only the extent matters.  XXX: a depth image can not be copied or sampled.
			 */
			image->pitch = (image->pitch + 127U) & ~127U;
			image->bytes = (uint64_t)image->pitch * ((info.extent.height + 31U) & ~31U);
		}
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_IMAGE, identity, image, error);
}

static int
gfx_create_image_view(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkImageViewCreateInfo info;
	struct gfx_view *view;
	struct gfx_image *image;
	uint64_t identity;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkImageViewCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* XXX: a view is the whole image in the image's own format; swizzles and sub-ranges are not applied. */
	view = NULL;
	image = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE, (uint64_t)(uintptr_t)info.image);
	if (image != NULL)
		view = kern_calloc(1U, sizeof(*view));
	if (view != NULL) {
		view->image = image;
		view->format = info.format;
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_IMAGE_VIEW, identity, view, image == NULL ? EINVAL : 0);
}

static int
gfx_create_sampler(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkSamplerCreateInfo info;
	struct gfx_sampler *sampler;
	uint64_t identity;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkSamplerCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	sampler = kern_calloc(1U, sizeof(*sampler));
	if (sampler != NULL) {
		sampler->mag_filter = info.magFilter;
		sampler->min_filter = info.minFilter;
		sampler->address_u = info.addressModeU;
		sampler->address_v = info.addressModeV;
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_SAMPLER, identity, sampler, 0);
}

/* ---------------- descriptors and layouts ---------------- */

static int
gfx_create_dsl(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkDescriptorSetLayoutCreateInfo info;
	struct gfx_dsl *dsl;
	uint64_t identity;
	uint32_t index;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkDescriptorSetLayoutCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	dsl = NULL;
	error = 0;
	if (info.bindingCount > GFX_MAX_BINDINGS)
		error = ENOTSUP;
	else
		dsl = kern_calloc(1U, sizeof(*dsl));
	if (dsl != NULL) {
		dsl->count = info.bindingCount;
		for (index = 0U; index < info.bindingCount; index++) {
			dsl->bindings[index].binding = info.pBindings[index].binding;
			dsl->bindings[index].type = info.pBindings[index].descriptorType;
			dsl->bindings[index].stages = info.pBindings[index].stageFlags;
		}
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT, identity, dsl, error);
}

/* A descriptor pool bounds nothing here: sets are small host objects. */
static int
gfx_create_dpool(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkDescriptorPoolCreateInfo info;
	uint64_t identity;
	uint32_t *pool;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkDescriptorPoolCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	pool = kern_calloc(1U, sizeof(*pool));
	if (pool != NULL)
		*pool = info.maxSets;
	return gfx_create_reply(session, reply, I915_VK_OBJ_DESCRIPTOR_POOL, identity, pool, 0);
}

/*
 * vkAllocateDescriptorSets (descriptors.c): [device][present][VkDescriptorSetAllocateInfo]
 * [count][identities] -> [result][count][identities].
 */
static int
gfx_allocate_dsets(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkDescriptorSetAllocateInfo info;
	uint64_t identities[8];
	struct gfx_dset *dset;
	uint64_t count;
	uint64_t index;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkDescriptorSetAllocateInfo(reader, &session->arena, &info);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 8U || count != info.descriptorSetCount)
		return EINVAL;
	for (index = 0U; index < count; index++)
		identities[index] = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	error = 0;
	for (index = 0U; index < count && error == 0; index++) {
		dset = kern_calloc(1U, sizeof(*dset));
		if (dset == NULL) {
			error = ENOMEM;
			break;
		}
		dset->layout = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT,
			(uint64_t)(uintptr_t)info.pSetLayouts[index]);
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_DESCRIPTOR_SET, identities[index], dset);
		if (error != 0)
			kern_free(dset);
	}
	/* XXX: a batch that fails part-way leaves its earlier sets allocated (happy path only). */

	i915_vk_reply_u32(reply, gfx_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, count);
		for (index = 0U; index < count; index++)
			i915_vk_reply_u64(reply, identities[index]);
	}
	return 0;
}

/*
 * vkUpdateDescriptorSets (descriptors.c descriptor_write): [device][n][n]{write}[m][m]{copy};
 * write = [sType][pNext][set][binding][element][count][type][images]{[sampler][view][layout]}
 * [buffers]{VkDescriptorBufferInfo}[texel views]{identity}.  No reply body.
 */
static int
gfx_update_dsets(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	VkDescriptorBufferInfo buffer_info;
	VkCopyDescriptorSet copy;
	struct gfx_dset *dset;
	uint64_t writes;
	uint64_t count;
	uint64_t index;
	uint64_t item;
	uint64_t sampler;
	uint64_t view;
	uint32_t binding;
	uint32_t type;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	writes = i915_vk_read_u64(reader);
	if (reader->error != 0 || writes > 64U)
		return EINVAL;

	for (index = 0U; index < writes; index++) {
		(void)i915_vk_read_u32(reader);
		(void)i915_vk_read_u64(reader);
		dset = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_DESCRIPTOR_SET, i915_vk_read_u64(reader));
		binding = i915_vk_read_u32(reader);
		(void)i915_vk_read_u32(reader);		/* dstArrayElement: XXX arrays of descriptors are not laid out */
		(void)i915_vk_read_u32(reader);
		type = i915_vk_read_u32(reader);

		count = i915_vk_read_u64(reader);
		if (reader->error != 0 || count > 64U)
			return EINVAL;
		for (item = 0U; item < count; item++) {
			sampler = i915_vk_read_u64(reader);
			view = i915_vk_read_u64(reader);
			(void)i915_vk_read_u32(reader);
			if (item != 0U || dset == NULL || binding >= GFX_MAX_BINDINGS)
				continue;
			dset->slots[binding].sampler = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_SAMPLER, sampler);
			dset->slots[binding].view = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE_VIEW, view);
		}

		count = i915_vk_read_u64(reader);
		if (reader->error != 0 || count > 64U)
			return EINVAL;
		for (item = 0U; item < count; item++)
			i915_vkc_dec_VkDescriptorBufferInfo(reader, &session->arena, &buffer_info);
		if (count != 0U)
			kern_logf("i915: vk: XXX vkUpdateDescriptorSets: buffer descriptors (type %u) are not bound to anything\n", type);

		count = i915_vk_read_u64(reader);
		if (reader->error != 0 || count > 64U)
			return EINVAL;
		for (item = 0U; item < count; item++)
			(void)i915_vk_read_u64(reader);
	}

	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;
	for (item = 0U; item < count; item++)
		i915_vkc_dec_VkCopyDescriptorSet(reader, &session->arena, &copy);
	if (count != 0U)
		kern_logf("i915: vk: XXX vkUpdateDescriptorSets: descriptor copies are not applied\n");
	return reader->error != 0 ? EINVAL : 0;
}

/* A pipeline layout carries nothing a draw needs beyond what the pipeline and the sets say. */
static int
gfx_create_pipeline_layout(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkPipelineLayoutCreateInfo info;
	uint64_t identity;
	uint32_t *layout;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkPipelineLayoutCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	layout = kern_calloc(1U, sizeof(*layout));
	if (layout != NULL)
		*layout = info.setLayoutCount;
	return gfx_create_reply(session, reply, I915_VK_OBJ_PIPELINE_LAYOUT, identity, layout, 0);
}

/* ---------------- render pass, framebuffer ---------------- */

static int
gfx_create_render_pass(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkRenderPassCreateInfo info;
	const VkSubpassDescription *subpass;
	struct gfx_pass *pass;
	uint64_t identity;
	uint32_t index;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkRenderPassCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* XXX: one subpass, at most one colour attachment and one depth attachment. */
	pass = NULL;
	error = 0;
	if (info.subpassCount != 1U || info.attachmentCount > GFX_MAX_ATTACHMENTS ||
	    info.pSubpasses[0].colorAttachmentCount > 1U) {
		kern_logf("i915: vk: XXX vkCreateRenderPass refused: %u subpasses, %u attachments, %u colour attachments\n",
			info.subpassCount, info.attachmentCount,
			info.subpassCount != 0U ? info.pSubpasses[0].colorAttachmentCount : 0U);
		error = ENOTSUP;
	} else {
		pass = kern_calloc(1U, sizeof(*pass));
	}
	if (pass != NULL) {
		subpass = &info.pSubpasses[0];
		pass->attachment_count = info.attachmentCount;
		for (index = 0U; index < info.attachmentCount; index++) {
			pass->attachments[index].format = info.pAttachments[index].format;
			pass->attachments[index].load_op = info.pAttachments[index].loadOp;
		}
		pass->color_attachment = subpass->colorAttachmentCount != 0U ?
			subpass->pColorAttachments[0].attachment : VK_ATTACHMENT_UNUSED;
		pass->depth_attachment = subpass->pDepthStencilAttachment != NULL ?
			subpass->pDepthStencilAttachment->attachment : VK_ATTACHMENT_UNUSED;
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_RENDER_PASS, identity, pass, error);
}

static int
gfx_create_framebuffer(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkFramebufferCreateInfo info;
	struct gfx_framebuffer *framebuffer;
	uint64_t identity;
	uint32_t index;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkFramebufferCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	framebuffer = NULL;
	error = 0;
	if (info.attachmentCount > GFX_MAX_ATTACHMENTS)
		error = ENOTSUP;
	else
		framebuffer = kern_calloc(1U, sizeof(*framebuffer));
	if (framebuffer != NULL) {
		framebuffer->width = info.width;
		framebuffer->height = info.height;
		framebuffer->view_count = info.attachmentCount;
		for (index = 0U; index < info.attachmentCount; index++) {
			uint64_t view;

			memcpy(&view, (const char *)info.pAttachments + index * 8U, sizeof(view));
			framebuffer->views[index] = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE_VIEW, view);
		}
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_FRAMEBUFFER, identity, framebuffer, error);
}

/* ---------------- shader module, pipeline ---------------- */

static int
gfx_create_shader(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkShaderModuleCreateInfo info;
	struct gfx_shader *shader;
	uint64_t identity;
	uint32_t words;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkShaderModuleCreateInfo(reader, &session->arena, &info);
	identity = gfx_create_tail(reader);
	if (reader->error != 0 || info.pCode == NULL || (info.codeSize & 3U) != 0U || info.codeSize > (1U << 20))
		return EINVAL;

	words = (uint32_t)(info.codeSize / 4U);
	shader = kern_calloc(1U, sizeof(*shader) + (size_t)words * 4U);
	if (shader != NULL) {
		shader->words = (uint32_t *)(shader + 1);
		shader->word_count = words;
		memcpy(shader->words, info.pCode, (size_t)words * 4U);
	}
	return gfx_create_reply(session, reply, I915_VK_OBJ_SHADER_MODULE, identity, shader, 0);
}

static void
gfx_float_bits(uint32_t *destination, const float *source)
{
	memcpy(destination, source, sizeof(*destination));
}

/*
 * One VkGraphicsPipelineCreateInfo as pipeline.c pipeline_encode_graphics sends it: the head, the
 * stages, then each state record behind its own presence marker (the rasterization record is
 * always present), then [layout][renderPass][subpass][base pipeline][base index].
 */
static int
gfx_decode_pipeline(struct i915_vk_session *session, struct i915_vk_reader *reader, struct gfx_pipeline *pipeline)
{
	VkPipelineShaderStageCreateInfo stage;
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineTessellationStateCreateInfo tessellation;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic;
	struct i915_vk_arena *arena;
	uint64_t stages;
	uint64_t index;

	arena = &session->arena;
	(void)i915_vk_read_u32(reader);			/* sType */
	(void)i915_vk_read_u64(reader);			/* pNext */
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u32(reader);			/* stageCount */
	stages = i915_vk_read_u64(reader);
	if (reader->error != 0 || stages > 8U)
		return EINVAL;
	for (index = 0U; index < stages; index++) {
		struct gfx_shader *shader;

		memset(&stage, 0, sizeof(stage));
		i915_vkc_dec_VkPipelineShaderStageCreateInfo(reader, arena, &stage);
		shader = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_SHADER_MODULE, (uint64_t)(uintptr_t)stage.module);
		if (stage.stage == VK_SHADER_STAGE_VERTEX_BIT)
			pipeline->vertex = shader;
		else if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT)
			pipeline->fragment = shader;
		else
			kern_logf("i915: vk: XXX pipeline stage 0x%x is not run\n", (unsigned)stage.stage);
	}

	if (i915_vk_read_u64(reader) != 0U) {
		memset(&vertex_input, 0, sizeof(vertex_input));
		i915_vkc_dec_VkPipelineVertexInputStateCreateInfo(reader, arena, &vertex_input);
		if (reader->error == 0 && (vertex_input.vertexBindingDescriptionCount > GFX_MAX_VERTEX_BINDINGS ||
		    vertex_input.vertexAttributeDescriptionCount > GFX_MAX_VERTEX_ATTRIBUTES))
			return ENOTSUP;
		pipeline->binding_count = vertex_input.vertexBindingDescriptionCount;
		for (index = 0U; reader->error == 0 && index < pipeline->binding_count; index++) {
			pipeline->bindings[index].binding = vertex_input.pVertexBindingDescriptions[index].binding;
			pipeline->bindings[index].stride = vertex_input.pVertexBindingDescriptions[index].stride;
		}
		pipeline->attribute_count = vertex_input.vertexAttributeDescriptionCount;
		for (index = 0U; reader->error == 0 && index < pipeline->attribute_count; index++) {
			pipeline->attributes[index].location = vertex_input.pVertexAttributeDescriptions[index].location;
			pipeline->attributes[index].binding = vertex_input.pVertexAttributeDescriptions[index].binding;
			pipeline->attributes[index].format = vertex_input.pVertexAttributeDescriptions[index].format;
			pipeline->attributes[index].offset = vertex_input.pVertexAttributeDescriptions[index].offset;
		}
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&assembly, 0, sizeof(assembly));
		i915_vkc_dec_VkPipelineInputAssemblyStateCreateInfo(reader, arena, &assembly);
		pipeline->topology = assembly.topology;
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&tessellation, 0, sizeof(tessellation));
		i915_vkc_dec_VkPipelineTessellationStateCreateInfo(reader, arena, &tessellation);
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&viewport, 0, sizeof(viewport));
		i915_vkc_dec_VkPipelineViewportStateCreateInfo(reader, arena, &viewport);
		if (reader->error == 0 && viewport.pViewports != NULL && viewport.viewportCount != 0U) {
			gfx_float_bits(&pipeline->viewport[0], &viewport.pViewports[0].x);
			gfx_float_bits(&pipeline->viewport[1], &viewport.pViewports[0].y);
			gfx_float_bits(&pipeline->viewport[2], &viewport.pViewports[0].width);
			gfx_float_bits(&pipeline->viewport[3], &viewport.pViewports[0].height);
			gfx_float_bits(&pipeline->viewport[4], &viewport.pViewports[0].minDepth);
			gfx_float_bits(&pipeline->viewport[5], &viewport.pViewports[0].maxDepth);
		} else if (reader->error == 0) {
			kern_logf("i915: vk: XXX pipeline has a dynamic viewport; vkCmdSetViewport is not implemented\n");
		}
		if (reader->error == 0 && viewport.pScissors != NULL && viewport.scissorCount != 0U)
			pipeline->scissor = viewport.pScissors[0];
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&raster, 0, sizeof(raster));
		i915_vkc_dec_VkPipelineRasterizationStateCreateInfo(reader, arena, &raster);
		pipeline->cull_mode = raster.cullMode;
		pipeline->front_face = raster.frontFace;
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&multisample, 0, sizeof(multisample));
		i915_vkc_dec_VkPipelineMultisampleStateCreateInfo(reader, arena, &multisample);
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&depth, 0, sizeof(depth));
		i915_vkc_dec_VkPipelineDepthStencilStateCreateInfo(reader, arena, &depth);
		pipeline->depth_test = depth.depthTestEnable;
		pipeline->depth_write = depth.depthWriteEnable;
		pipeline->depth_compare = depth.depthCompareOp;
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&blend, 0, sizeof(blend));
		i915_vkc_dec_VkPipelineColorBlendStateCreateInfo(reader, arena, &blend);
		if (reader->error == 0 && blend.attachmentCount != 0U && blend.pAttachments[0].blendEnable != VK_FALSE)
			kern_logf("i915: vk: XXX pipeline asks for blending; the draw writes the colour unblended\n");
	}
	if (i915_vk_read_u64(reader) != 0U) {
		memset(&dynamic, 0, sizeof(dynamic));
		i915_vkc_dec_VkPipelineDynamicStateCreateInfo(reader, arena, &dynamic);
		if (reader->error == 0 && dynamic.dynamicStateCount != 0U)
			kern_logf("i915: vk: XXX pipeline declares %u dynamic states; none of the vkCmdSet* commands is implemented\n",
				dynamic.dynamicStateCount);
	}
	(void)i915_vk_read_u64(reader);			/* layout */
	(void)i915_vk_read_u64(reader);			/* renderPass */
	(void)i915_vk_read_u32(reader);			/* subpass */
	(void)i915_vk_read_u64(reader);			/* basePipelineHandle */
	(void)i915_vk_read_u32(reader);			/* basePipelineIndex */
	return reader->error != 0 ? EINVAL : 0;
}

/*
 * vkCreateGraphicsPipelines (pipeline.c): [device][cache][count][count]{create info}[pAllocator]
 * [count][identities] -> [result][count][identities].
 */
static int
gfx_create_pipelines(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	struct gfx_pipeline *pipelines[4];
	uint64_t identities[4];
	uint64_t count;
	uint64_t index;
	int error;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count == 0U || count > 4U)
		return EINVAL;

	memset(pipelines, 0, sizeof(pipelines));
	error = 0;
	for (index = 0U; index < count; index++) {
		pipelines[index] = kern_calloc(1U, sizeof(*pipelines[index]));
		if (pipelines[index] == NULL) {
			error = ENOMEM;
			break;
		}
		/* A record that does not decode leaves the rest of the stream unreadable: the command fails. */
		session->arena.used = 0U;
		error = gfx_decode_pipeline(session, reader, pipelines[index]);
		if (error != 0)
			break;
	}
	if (error != 0) {
		for (index = 0U; index < count; index++)
			kern_free(pipelines[index]);
		return error;
	}

	(void)i915_vk_read_u64(reader);			/* pAllocator */
	if (i915_vk_read_u64(reader) != count)
		reader->error = 1;
	for (index = 0U; index < count; index++)
		identities[index] = i915_vk_read_u64(reader);
	if (reader->error != 0) {
		for (index = 0U; index < count; index++)
			kern_free(pipelines[index]);
		return EINVAL;
	}

	for (index = 0U; index < count && error == 0; index++)
		error = i915_vk_gfx_pipeline_prepare(session, pipelines[index]);
	for (index = 0U; index < count && error == 0; index++)
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_PIPELINE, identities[index], pipelines[index]);
	if (error != 0) {
		/* XXX: pipelines already published by this batch stay published (happy path only). */
		kern_logf("i915: vk: vkCreateGraphicsPipelines failed: %d\n", error);
	}

	i915_vk_reply_u32(reply, gfx_result(error));
	i915_vk_reply_u64(reply, count);
	for (index = 0U; index < count; index++)
		i915_vk_reply_u64(reply, error == 0 ? identities[index] : 0U);
	return 0;
}

/* vkDestroyPipeline: a generic destroy; the pipeline owns its compiled kernels. */
static int
gfx_destroy_pipeline(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	struct gfx_pipeline *pipeline;
	uint64_t identity;

	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	pipeline = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_PIPELINE, identity);
	if (pipeline != NULL) {
		i915_vk_obj_remove(session->vk, I915_VK_OBJ_PIPELINE, identity);
		i915_vk_gfx_pipeline_release(pipeline);
		kern_free(pipeline);
	}
	return 0;
}

/* ---------------- semaphore ---------------- */

/*
 * vkCreateSemaphore (sync.c sync_create): [device][present][sType][pNext][flags][pAllocator]
 * [present][identity].  XXX: one queue that runs every submission to its end before the next is
 * decoded: a semaphore has nothing to order, so it is an identity and nothing else.
 */
static int
gfx_create_semaphore(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	uint64_t identity;
	uint32_t *semaphore;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	identity = gfx_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	semaphore = kern_calloc(1U, sizeof(*semaphore));
	return gfx_create_reply(session, reply, I915_VK_OBJ_SEMAPHORE, identity, semaphore, 0);
}

/* ---------------- routing ---------------- */

int
i915_vk_gfx_obj_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled)
{
	*handled = 1;
	switch (opcode) {
	case 21U: return gfx_allocate_memory(session, reader, reply);
	case 22U: return gfx_free_memory(session, reader);
	case 28U: return gfx_bind(session, reader, reply, 0);
	case 29U: return gfx_bind(session, reader, reply, 1);
	case 30U: return gfx_requirements(session, reader, reply, 0);
	case 31U: return gfx_requirements(session, reader, reply, 1);
	case 40U: return gfx_create_semaphore(session, reader, reply);
	case 41U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_SEMAPHORE);
	case 50U: return gfx_create_buffer(session, reader, reply);
	case 51U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_BUFFER);
	case 54U: return gfx_create_image(session, reader, reply);
	case 55U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_IMAGE);
	case 57U: return gfx_create_image_view(session, reader, reply);
	case 58U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_IMAGE_VIEW);
	case 59U: return gfx_create_shader(session, reader, reply);
	case 60U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_SHADER_MODULE);
	case 65U: return gfx_create_pipelines(session, reader, reply);
	case 67U: return gfx_destroy_pipeline(session, reader);
	case 68U: return gfx_create_pipeline_layout(session, reader, reply);
	case 69U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_PIPELINE_LAYOUT);
	case 70U: return gfx_create_sampler(session, reader, reply);
	case 71U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_SAMPLER);
	case 72U: return gfx_create_dsl(session, reader, reply);
	case 73U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT);
	case 74U: return gfx_create_dpool(session, reader, reply);
	case 75U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_DESCRIPTOR_POOL);
	case 77U: return gfx_allocate_dsets(session, reader, reply);
	case 79U: return gfx_update_dsets(session, reader);
	case 80U: return gfx_create_framebuffer(session, reader, reply);
	case 81U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_FRAMEBUFFER);
	case 82U: return gfx_create_render_pass(session, reader, reply);
	case 83U: return gfx_destroy_plain(session, reader, I915_VK_OBJ_RENDER_PASS);
	default:
		*handled = 0;
		return 0;
	}
}
