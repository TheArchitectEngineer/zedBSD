/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's shader modules, layouts and descriptors: shader modules,
 * descriptor set layouts, pipeline layouts, pipeline caches, descriptor pools
 * and their sets, and the updates of the sets.
 *
 * A set's binding names the image view and the sampler of a combined image
 * sampler, the one kind of descriptor the compositor uses; other kinds are
 * read and left unbound (a draw that samples one is refused).  A set
 * holds what it names, so a view or sampler destroyed while a set names it
 * lives on until the set lets go.
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

#include "drivers/gpu/i915/render/vulkan-codec.inc"

/* The largest SPIR-V module, the most sets one allocation hands out, and the most items of one update. */
#define LAYOUT_SHADER_BYTES		(256U * 1024U)
#define LAYOUT_ALLOCATED_SETS		64U
#define LAYOUT_UPDATE_ITEMS		64U

static int layout_create_shader(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_create_set_layout(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_create_pipeline_layout(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_create_cache(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_destroy_cache(struct i915_wire_reader *reader);
static int layout_create_pool(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_destroy_pool(struct amdrdna_vk_session *session, struct i915_wire_reader *reader);
static int layout_reset_pool(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_pool_sets_free(struct amdrdna_vk_session *session, const void *pool);
static int layout_allocate_sets(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_free_sets(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int layout_update_sets(struct amdrdna_vk_session *session, struct i915_wire_reader *reader);
static int layout_update_write(struct amdrdna_vk_session *session, struct i915_wire_reader *reader);
static int layout_slot_set(struct amdrdna_vk_descriptor_slot *slot, struct amdrdna_vk_object *view, struct amdrdna_vk_object *sampler);
static int layout_slot_buffer(struct amdrdna_vk_descriptor_slot *slot, struct amdrdna_vk_object *buffer, uint64_t offset, uint64_t range);
static int layout_slot_clear(struct amdrdna_vk_descriptor_slot *slot);
static int layout_set_destroy(struct amdrdna_vk_session *session, void *payload);

/*
 * Runs a shader module, layout, cache or descriptor command; *handled
 * stays clear for an opcode of another handler's.
 */
int
amdrdna_vk_layout_route(
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
	case GPU_OP_CREATE_SHADER_MODULE:
		error = layout_create_shader(session, reader, reply);
		break;
	case GPU_OP_DESTROY_SHADER_MODULE:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_SHADER_MODULE);
		break;
	case GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT:
		error = layout_create_set_layout(session, reader, reply);
		break;
	case GPU_OP_DESTROY_DESCRIPTOR_SET_LAYOUT:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT);
		break;
	case GPU_OP_CREATE_PIPELINE_LAYOUT:
		error = layout_create_pipeline_layout(session, reader, reply);
		break;
	case GPU_OP_DESTROY_PIPELINE_LAYOUT:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_PIPELINE_LAYOUT);
		break;
	case GPU_OP_CREATE_PIPELINE_CACHE:
		error = layout_create_cache(session, reader, reply);
		break;
	case GPU_OP_DESTROY_PIPELINE_CACHE:
		error = layout_destroy_cache(reader);
		break;
	case GPU_OP_CREATE_DESCRIPTOR_POOL:
		error = layout_create_pool(session, reader, reply);
		break;
	case GPU_OP_DESTROY_DESCRIPTOR_POOL:
		error = layout_destroy_pool(session, reader);
		break;
	case GPU_OP_RESET_DESCRIPTOR_POOL:
		error = layout_reset_pool(session, reader, reply);
		break;
	case GPU_OP_ALLOCATE_DESCRIPTOR_SETS:
		error = layout_allocate_sets(session, reader, reply);
		break;
	case GPU_OP_FREE_DESCRIPTOR_SETS:
		error = layout_free_sets(session, reader, reply);
		break;
	case GPU_OP_UPDATE_DESCRIPTOR_SETS:
		error = layout_update_sets(session, reader);
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
 * vkCreateShaderModule: [device][present][VkShaderModuleCreateInfo] and
 * the create's tail -> the create's reply.  The module keeps a copy of its
 * words; one not of whole words, empty or too large is refused.
 */
static int
layout_create_shader(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkShaderModuleCreateInfo info;
	struct amdrdna_vk_shader *shader;
	uint64_t identity;
	uint32_t words;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkShaderModuleCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Whole words of code, some of them, within the largest module. */
	error = 0;
	if (info.pCode == NULL || info.codeSize == 0U) {
		error = EINVAL;
	} else if ((info.codeSize & 3U) != 0U) {
		error = EINVAL;
	} else if (info.codeSize > LAYOUT_SHADER_BYTES) {
		error = ENOTSUP;
	}

	/* The module with its words behind it (the command's scratch is emptied after the command). */
	shader = NULL;
	if (error == 0) {
		words = (uint32_t)(info.codeSize / 4U);
		shader = kern_calloc(1U, sizeof(*shader) + (size_t)words * 4U);
		if (shader != NULL) {
			shader->words = (uint32_t *)(shader + 1);
			shader->word_count = words;
			kern_memcpy(shader->words, info.pCode, (size_t)words * 4U);
		}
	}

	/* Publishes the module and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_SHADER_MODULE, identity, shader, amdrdna_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * vkCreateDescriptorSetLayout: [device][present]
 * [VkDescriptorSetLayoutCreateInfo] and the create's tail -> the create's
 * reply.  Binding numbers past the ones a set holds are refused.
 */
static int
layout_create_set_layout(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkDescriptorSetLayoutCreateInfo info;
	struct amdrdna_vk_set_layout *layout;
	uint64_t identity;
	uint32_t index;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkDescriptorSetLayoutCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* No more bindings, and no higher binding number, than a set holds. */
	error = 0;
	if (info.bindingCount > AMDRDNA_VK_BINDINGS_MAX)
		error = ENOTSUP;
	if (error == 0 && info.bindingCount != 0U && info.pBindings == NULL)
		error = EINVAL;
	for (index = 0U; error == 0 && index < info.bindingCount; index++) {
		if (info.pBindings[index].binding >= AMDRDNA_VK_BINDINGS_MAX)
			error = ENOTSUP;
	}

	/* The layout keeps each binding's number, type and count. */
	layout = NULL;
	if (error == 0) {
		layout = kern_calloc(1U, sizeof(*layout));
		if (layout != NULL) {
			layout->count = info.bindingCount;
			for (index = 0U; index < info.bindingCount; index++) {
				layout->bindings[index].binding = info.pBindings[index].binding;
				layout->bindings[index].type = info.pBindings[index].descriptorType;
				layout->bindings[index].count = info.pBindings[index].descriptorCount;
			}
		}
	}

	/* Publishes the layout and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT, identity, layout, amdrdna_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * vkCreatePipelineLayout: [device][present][VkPipelineLayoutCreateInfo]
 * and the create's tail -> the create's reply.  The layout keeps its set
 * count and the bytes its push constant ranges reach.
 */
static int
layout_create_pipeline_layout(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkPipelineLayoutCreateInfo info;
	struct amdrdna_vk_pipeline_layout *layout;
	uint64_t identity;
	uint32_t index;
	uint32_t end;
	uint32_t push_bytes;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkPipelineLayoutCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The bytes the push constant ranges reach. */
	push_bytes = 0U;
	for (index = 0U; index < info.pushConstantRangeCount && info.pPushConstantRanges != NULL; index++) {
		end = info.pPushConstantRanges[index].offset + info.pPushConstantRanges[index].size;
		if (end > push_bytes)
			push_bytes = end;
	}

	/* No more sets, and no more push constant bytes, than a pipeline binds. */
	error = 0;
	if (info.setLayoutCount > AMDRDNA_VK_SETS_MAX) {
		error = ENOTSUP;
	} else if (push_bytes > AMDRDNA_VK_PUSH_BYTES) {
		error = ENOTSUP;
	}

	/* The layout. */
	layout = NULL;
	if (error == 0) {
		layout = kern_calloc(1U, sizeof(*layout));
		if (layout != NULL) {
			layout->set_count = info.setLayoutCount;
			layout->push_bytes = push_bytes;
		}
	}

	/* Publishes the layout and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_PIPELINE_LAYOUT, identity, layout, amdrdna_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * vkCreatePipelineCache: [device][present][VkPipelineCacheCreateInfo] and
 * the create's tail -> the create's reply.  The executor keeps no cache:
 * the identity is answered and not recorded, so its destroy finds nothing.
 */
static int
layout_create_cache(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkPipelineCacheCreateInfo info;
	uint64_t identity;

	/* The create info behind the device and its presence word; its data is not kept. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkPipelineCacheCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Success with the identity. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);
	drv_i915_wire_reply_u64(reply, 1U);
	drv_i915_wire_reply_u64(reply, identity);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* vkDestroyPipelineCache: [device][identity][allocator], no reply body; nothing was kept. */
static int
layout_destroy_cache(
	struct i915_wire_reader *reader)
{
	/* The device, the identity and the allocator are read. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: there is nothing to destroy. */
	return 0;
}

/*
 * vkCreateDescriptorPool: [device][present][VkDescriptorPoolCreateInfo]
 * and the create's tail -> the create's reply.
 */
static int
layout_create_pool(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkDescriptorPoolCreateInfo info;
	struct amdrdna_vk_descriptor_pool *pool;
	uint64_t identity;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkDescriptorPoolCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The pool keeps its maxSets. */
	pool = kern_calloc(1U, sizeof(*pool));
	if (pool != NULL)
		pool->max_sets = info.maxSets;

	/* Publishes the pool and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_DESCRIPTOR_POOL, identity, pool, amdrdna_vk_free_payload, 0);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * vkDestroyDescriptorPool: [device][identity][allocator], no reply body.
 * The pool's sets go with it.
 */
static int
layout_destroy_pool(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader)
{
	const struct amdrdna_vk_descriptor_pool *pool;
	uint64_t identity;
	int error;

	/* The identity between the device and the allocator. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An unknown pool is already gone. */
	pool = amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_POOL, identity);
	if (pool == NULL)
		return 0;

	/* The pool's sets, then the pool. */
	error = layout_pool_sets_free(session, pool);
	if (error == 0)
		error = amdrdna_vk_object_remove(session, I915_VK_OBJ_DESCRIPTOR_POOL, identity);
	if (error != 0)
		return error;

	/* Succeeded: the pool and its sets are gone. */
	return 0;
}

/*
 * vkResetDescriptorPool: [device][identity][flags] -> [result].  The
 * pool's sets go; the pool stays.
 */
static int
layout_reset_pool(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	const struct amdrdna_vk_descriptor_pool *pool;
	uint64_t identity;
	uint32_t result;
	int error;

	/* The identity between the device and the flags. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The sets of a known pool go. */
	error = 0;
	pool = amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_POOL, identity);
	if (pool != NULL)
		error = layout_pool_sets_free(session, pool);

	/* The reset's result. */
	result = amdrdna_vk_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * Removes every published set of a pool.  The list changes with each
 * removal, so the walk starts over after one.
 */
static int
layout_pool_sets_free(
	struct amdrdna_vk_session *session,
	const void *pool)
{
	struct amdrdna_vk_object *object;
	const struct amdrdna_vk_descriptor_set *set;
	bool removed;
	int error;

	/* Until a walk finds no set of the pool. */
	do {
		removed = false;
		for (object = session->objects; object != NULL; object = object->next) {
			if (object->kind != I915_VK_OBJ_DESCRIPTOR_SET)
				continue;
			set = object->payload;
			if (set->pool != pool)
				continue;

			/* Unpublishes it; whatever still holds it keeps it. */
			error = amdrdna_vk_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, object->identity);
			if (error != 0)
				return error;
			removed = true;
			break;
		}
	} while (removed);

	/* Succeeded: the pool has no set. */
	return 0;
}

/*
 * vkAllocateDescriptorSets: [device][present][VkDescriptorSetAllocateInfo]
 * [count][identities] -> [result][count][identities].  Every set of the
 * batch is published, or none.
 */
static int
layout_allocate_sets(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkDescriptorSetAllocateInfo info;
	uint64_t identities[LAYOUT_ALLOCATED_SETS];
	const struct amdrdna_vk_descriptor_pool *pool;
	struct amdrdna_vk_descriptor_set *set;
	struct amdrdna_vk_object *object;
	uint64_t count;
	uint64_t index;
	uint64_t published;
	uint32_t result;
	int error;

	/* The allocate info behind the device and its presence word, then the identities. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkDescriptorSetAllocateInfo(reader, &session->arena, &info);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count > LAYOUT_ALLOCATED_SETS || count != info.descriptorSetCount)
		return EINVAL;

	/* The identity libvulkan chose for each set. */
	for (index = 0U; index < count; index++)
		identities[index] = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Sets of a known pool. */
	error = 0;
	pool = amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_POOL, (uint64_t)(uintptr_t)info.descriptorPool);
	if (pool == NULL)
		error = EINVAL;

	/* Publishes each set, up to the first failure. */
	published = 0U;
	for (index = 0U; error == 0 && index < count; index++) {
		set = kern_calloc(1U, sizeof(*set));
		if (set == NULL) {
			error = ENOMEM;
			break;
		}

		set->pool = pool;
		error = amdrdna_vk_object_publish(session, I915_VK_OBJ_DESCRIPTOR_SET, identities[index], set, layout_set_destroy, &object);
		if (error != 0) {
			kern_free(set);
			break;
		}

		published++;
	}

	/* A batch that failed part-way leaves no set. */
	if (error != 0) {
		for (index = 0U; index < published; index++)
			(void)amdrdna_vk_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, identities[index]);
	}

	/* The result, and on success every set's identity. */
	result = amdrdna_vk_result(error);
	drv_i915_wire_reply_u32(reply, result);
	if (error == 0) {
		drv_i915_wire_reply_u64(reply, count);
		for (index = 0U; index < count; index++)
			drv_i915_wire_reply_u64(reply, identities[index]);
	}

	/* Succeeded: the reply carries the allocation's result. */
	return 0;
}

/*
 * vkFreeDescriptorSets: [device][pool][count][count]{identity} ->
 * [result].  Each named set of the session goes.
 */
static int
layout_free_sets(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint64_t count;
	uint64_t index;
	uint64_t identity;
	uint32_t result;
	int error;
	int failed;

	/* The count behind the device, the pool and the count's first form. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count > LAYOUT_ALLOCATED_SETS)
		return EINVAL;

	/* Removes each set; an unknown one is already gone. */
	failed = 0;
	for (index = 0U; index < count; index++) {
		identity = drv_i915_wire_read_u64(reader);
		if (reader->error != 0)
			return EINVAL;

		error = amdrdna_vk_object_remove(session, I915_VK_OBJ_DESCRIPTOR_SET, identity);
		if (error != 0 && error != ENOENT && failed == 0)
			failed = error;
	}

	/* The free's result. */
	result = amdrdna_vk_result(failed);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * vkUpdateDescriptorSets: [device][n][n]{write}[m][m]{copy}, no reply
 * body.  Copies are read and not applied (the compositor makes none).
 */
static int
layout_update_sets(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader)
{
	VkCopyDescriptorSet copy;
	uint64_t writes;
	uint64_t copies;
	uint64_t index;
	int error;

	/* How many writes follow, behind the device and the count's first form. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	writes = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (writes > LAYOUT_UPDATE_ITEMS)
		return EINVAL;

	/* Applies each write. */
	for (index = 0U; index < writes; index++) {
		error = layout_update_write(session, reader);
		if (error != 0)
			return error;
	}

	/* How many copies follow; each is read. */
	(void)drv_i915_wire_read_u32(reader);
	copies = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (copies > LAYOUT_UPDATE_ITEMS)
		return EINVAL;
	for (index = 0U; index < copies; index++)
		i915_vkc_dec_VkCopyDescriptorSet(reader, &session->arena, &copy);
	if (reader->error != 0)
		return EINVAL;

	/* Says so when copies were not applied. */
	if (copies != 0U)
		kern_logf("amdrdna: vk: vkUpdateDescriptorSets: %u descriptor copies are not applied\n", (unsigned)copies);

	/* Succeeded: the writes are applied. */
	return 0;
}

/*
 * Reads one descriptor write and applies its first combined image sampler,
 * uniform buffer or storage buffer to the set's binding: [sType][pNext]
 * [set][binding][element][count][type][images]{[sampler][view][layout]}
 * [buffers]{VkDescriptorBufferInfo}[texel views]{identity}.
 */
static int
layout_update_write(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader)
{
	VkDescriptorBufferInfo buffer;
	struct amdrdna_vk_descriptor_set *set;
	struct amdrdna_vk_object *view;
	struct amdrdna_vk_object *sampler;
	struct amdrdna_vk_object *bound;
	uint64_t set_id;
	uint64_t count;
	uint64_t item;
	uint64_t sampler_id;
	uint64_t view_id;
	uint32_t binding;
	uint32_t element;
	uint32_t type;
	int error;

	/* The set, the binding, the element and the type behind sType and pNext. */
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	set_id = drv_i915_wire_read_u64(reader);
	binding = drv_i915_wire_read_u32(reader);
	element = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	type = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count > LAYOUT_UPDATE_ITEMS)
		return EINVAL;
	set = amdrdna_vk_payload(session, I915_VK_OBJ_DESCRIPTOR_SET, set_id);

	/* The image descriptors; the first combined image sampler of element 0 is bound. */
	for (item = 0U; item < count; item++) {
		sampler_id = drv_i915_wire_read_u64(reader);
		view_id = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		if (reader->error != 0)
			return EINVAL;

		/* Only element 0 of a known set's binding, as a combined image sampler. */
		if (item != 0U || element != 0U || set == NULL || binding >= AMDRDNA_VK_BINDINGS_MAX)
			continue;
		if (type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
			kern_logf("amdrdna: vk: vkUpdateDescriptorSets: descriptors of type %u are not bound\n", (unsigned)type);
			continue;
		}

		/* The binding samples this view with this sampler from now on. */
		view = amdrdna_vk_object_find(session, I915_VK_OBJ_IMAGE_VIEW, view_id);
		sampler = amdrdna_vk_object_find(session, I915_VK_OBJ_SAMPLER, sampler_id);
		error = layout_slot_set(&set->slots[binding], view, sampler);
		if (error != 0)
			return error;
	}

	/* The buffer descriptors; the first uniform or storage buffer of element 0 is bound. */
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count > LAYOUT_UPDATE_ITEMS)
		return EINVAL;
	for (item = 0U; item < count; item++) {
		kern_memset(&buffer, 0, sizeof(buffer));
		i915_vkc_dec_VkDescriptorBufferInfo(reader, &session->arena, &buffer);
		if (reader->error != 0)
			return EINVAL;

		/* Only element 0 of a known set's binding, as a uniform or storage buffer. */
		if (item != 0U || element != 0U || set == NULL || binding >= AMDRDNA_VK_BINDINGS_MAX)
			continue;
		if (type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
			kern_logf("amdrdna: vk: vkUpdateDescriptorSets: buffer descriptors of type %u are not bound\n", (unsigned)type);
			continue;
		}

		/* The binding reads this buffer's range from now on. */
		bound = amdrdna_vk_object_find(session, I915_VK_OBJ_BUFFER, (uint64_t)(uintptr_t)buffer.buffer);
		error = layout_slot_buffer(&set->slots[binding], bound, buffer.offset, buffer.range);
		if (error != 0)
			return error;
	}

	/* The texel buffer views are read; none is bound. */
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	if (count > LAYOUT_UPDATE_ITEMS)
		return EINVAL;
	for (item = 0U; item < count; item++)
		(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the write is read and its combined image sampler bound. */
	return 0;
}

/*
 * Points a binding at a view and a sampler (either may be NULL): the
 * binding holds the new ones and lets the old ones go.
 */
static int
layout_slot_set(
	struct amdrdna_vk_descriptor_slot *slot,
	struct amdrdna_vk_object *view,
	struct amdrdna_vk_object *sampler)
{
	int error;

	/* Holds the new ones first: one of them may be the old one. */
	if (view != NULL) {
		error = amdrdna_vk_object_retain(view);
		if (error != 0)
			return error;
	}

	if (sampler != NULL) {
		error = amdrdna_vk_object_retain(sampler);
		if (error != 0) {
			(void)amdrdna_vk_object_release(view);
			return error;
		}
	}

	/* Lets the old ones go. */
	error = layout_slot_clear(slot);

	/* The binding names the new ones. */
	slot->view = view;
	slot->sampler = sampler;

	/* Reports an old one whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the binding names the view and the sampler. */
	return 0;
}

/*
 * Points a binding at a buffer's range (the buffer may be NULL): the
 * binding holds the new buffer and lets what it named before go.
 */
static int
layout_slot_buffer(
	struct amdrdna_vk_descriptor_slot *slot,
	struct amdrdna_vk_object *buffer,
	uint64_t offset,
	uint64_t range)
{
	int error;

	/* Holds the new one first: it may be the old one. */
	if (buffer != NULL) {
		error = amdrdna_vk_object_retain(buffer);
		if (error != 0)
			return error;
	}

	/* Lets the old ones go. */
	error = layout_slot_clear(slot);

	/* The binding names the buffer's range. */
	slot->buffer = buffer;
	slot->offset = offset;
	slot->range = range;

	/* Reports an old one whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the binding names the buffer. */
	return 0;
}

/* Lets go of a binding's view, sampler and buffer. */
static int
layout_slot_clear(
	struct amdrdna_vk_descriptor_slot *slot)
{
	int first_error;
	int error;

	/* The view, the sampler, then the buffer; the first failure is reported. */
	first_error = amdrdna_vk_object_release(slot->view);
	error = amdrdna_vk_object_release(slot->sampler);
	if (first_error == 0)
		first_error = error;
	error = amdrdna_vk_object_release(slot->buffer);
	if (first_error == 0)
		first_error = error;
	slot->view = NULL;
	slot->sampler = NULL;
	slot->buffer = NULL;
	slot->offset = 0U;
	slot->range = 0U;

	/* Reports a destruction that failed. */
	if (first_error != 0)
		return first_error;

	/* Succeeded: the binding names nothing. */
	return 0;
}

/* Destroys a set nothing holds: its bindings let their views and samplers go. */
static int
layout_set_destroy(
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_descriptor_set *set;
	uint32_t binding;
	int first_error;
	int error;

	UNUSED_PARAMETER(session);

	set = payload;

	/* Every binding. */
	first_error = 0;
	for (binding = 0U; binding < AMDRDNA_VK_BINDINGS_MAX; binding++) {
		error = layout_slot_clear(&set->slots[binding]);
		if (error != 0 && first_error == 0)
			first_error = error;
	}

	kern_free(set);

	/* Reports a destruction that failed. */
	if (first_error != 0)
		return first_error;

	/* Succeeded: the set is gone. */
	return 0;
}
