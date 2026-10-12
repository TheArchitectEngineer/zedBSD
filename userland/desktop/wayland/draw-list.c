/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display list of a frame (ws213-p003, plan/ws213/phase001/phase.md
 * 2 and 4.4): the drawing (compose_record and everything it calls) writes
 * each Vulkan command it would record as an operation of the list, with
 * the arrays it was given copied into the list, and the list is replayed
 * into the frame's command buffer when the drawing ends.  The replay calls
 * the same commands with the same arguments in the same order, so the
 * command buffer is the one the drawing recorded directly before.
 *
 * The walk that writes the list and the replay that encodes it are timed
 * apart (KWL PERF walk), and a later step replays the list on a thread of
 * its own while the event loop goes on.
 */

#include "draw-list.h"

#include <stdlib.h>
#include <string.h>

/* The operations a list first makes room for, and the bytes of its data and of its vertices. */
#define LIST_FIRST_OPS		1024U
#define LIST_FIRST_DATA		(64U * 1024U)
#define LIST_FIRST_VERTICES	(64U * 1024U)
#define LIST_FIRST_COPIES	16U

/* The alignment of every array in a list's data: the largest of the Vulkan structures it holds. */
#define LIST_ALIGN		8U

static struct kwl_list_op *list_op(struct kwl_list *list, enum kwl_list_kind kind);
static int list_data(struct kwl_list *list, const void *source, size_t bytes, size_t *offset);
static int list_room(void **memory, size_t *capacity, size_t needed, size_t first);
static const void *list_at(const struct kwl_list *list, size_t offset, uint32_t count);
static void list_replay_op(const struct kwl_list *list, const struct kwl_list_op *op, VkCommandBuffer command);
static void list_replay_copy(const struct kwl_list *list, const struct kwl_list_op *op, VkCommandBuffer command);

/*
 * Empties a list for a new drawing, keeping its memory.
 *
 * A list never written is all zero and is reset like any other.
 */
void
kwl_list_reset(
	struct kwl_list *list)
{
	/* No operation, array, vertex or copy, and nothing counted. */
	list->op_count = 0U;
	list->data_used = 0U;
	list->vertex_used = 0U;
	list->copy_count = 0U;
	list->draws = 0U;
	list->joined = 0U;
	list->joined_draws = 0U;
	list->failed = 0U;

	/* A new list has no scissor set yet: whatever was known of the last one's is not of this one (ws211-p009). */
	list->scissor_serial++;
}

/* Frees a list's memory; the list is empty and may be written again. */
void
kwl_list_free(
	struct kwl_list *list)
{
	/* Each of its arrays. */
	free(list->ops);
	free(list->data);
	free(list->vertices);
	free(list->copies);
	memset(list, 0, sizeof(*list));
}

/*
 * Replays a list's operations into a command buffer that is recording.
 *
 * The vertices of the quads drawn together and the host images' copies
 * are not part of it: the caller writes the vertices where the draws read
 * them and records the copies in the command buffer that runs first.
 */
void
kwl_list_replay(
	const struct kwl_list *list,
	VkCommandBuffer command)
{
	size_t index;

	/* Each operation, in the order the drawing wrote it. */
	for (index = 0U; index < list->op_count; index++)
		list_replay_op(list, &list->ops[index], command);
}

/*
 * Reports how many things a list draws: its draws, less those that drew
 * quads together, and every quad so drawn (the count KWL DRAWS's draws
 * gives).
 */
unsigned
kwl_list_things(
	const struct kwl_list *list)
{
	unsigned things;

	/* The draws of one thing each, then the quads drawn together. */
	things = list->draws;
	if (things >= list->joined_draws)
		things -= list->joined_draws;
	things += list->joined;

	/* The number of things drawn. */
	return things;
}

/* Appends vertices of the quads drawn together, at the next byte of the list's vertices. */
void
kwl_list_add_vertices(
	struct kwl_list *list,
	const void *vertices,
	uint32_t bytes)
{
	size_t capacity;
	void *memory;
	int error;

	/* Room for them, grown when the frame draws more than before. */
	memory = list->vertices;
	capacity = list->vertex_capacity;
	error = list_room(&memory, &capacity, (size_t)list->vertex_used + bytes, LIST_FIRST_VERTICES);
	if (error != 0) {
		list->failed = 1U;
		return;
	}

	/* The grown array is the list's. */
	list->vertices = memory;
	list->vertex_capacity = (uint32_t)capacity;

	/* The vertices after those before them. */
	memcpy(list->vertices + list->vertex_used, vertices, bytes);
	list->vertex_used += bytes;
}

/* Appends a host image whose staging the frame copies into its sampled image first. */
void
kwl_list_add_copy(
	struct kwl_list *list,
	struct kwl_import *import)
{
	size_t capacity;
	void *memory;
	int error;

	/* Room for one more. */
	memory = list->copies;
	capacity = (size_t)list->copy_capacity * sizeof(*list->copies);
	error = list_room(&memory, &capacity, ((size_t)list->copy_count + 1U) * sizeof(*list->copies), LIST_FIRST_COPIES * sizeof(*list->copies));
	if (error != 0) {
		list->failed = 1U;
		return;
	}

	/* The grown array is the list's. */
	list->copies = memory;
	list->copy_capacity = (unsigned)(capacity / sizeof(*list->copies));

	/* The image after those before it. */
	list->copies[list->copy_count] = import;
	list->copy_count++;
}

/* Writes the beginning of a render pass (vkCmdBeginRenderPass). */
void
kwl_list_begin_render_pass(
	struct kwl_list *list,
	const VkRenderPassBeginInfo *begin,
	VkSubpassContents contents)
{
	struct kwl_list_op *op;

	/* A pass begun leaves the scissor to be set again (ws211-p009). */
	list->scissor_serial++;

	/* The operation. */
	op = list_op(list, KWL_LIST_BEGIN_PASS);
	if (op == NULL)
		return;

	/* The pass, its framebuffer and area, and how it is drawn. */
	op->args.begin.pass = begin->renderPass;
	op->args.begin.framebuffer = begin->framebuffer;
	op->args.begin.area = begin->renderArea;
	op->args.begin.contents = contents;
	op->args.begin.clear_count = begin->clearValueCount;

	/* Its clear values. */
	(void)list_data(list, begin->pClearValues, (size_t)begin->clearValueCount * sizeof(VkClearValue), &op->args.begin.clears);
}

/* Writes the end of a render pass (vkCmdEndRenderPass). */
void
kwl_list_end_render_pass(
	struct kwl_list *list)
{
	/* The operation, which has no arguments (a failure leaves the list failed). */
	(void)list_op(list, KWL_LIST_END_PASS);
}

/* Writes viewports (vkCmdSetViewport). */
void
kwl_list_set_viewport(
	struct kwl_list *list,
	uint32_t first,
	uint32_t count,
	const VkViewport *viewports)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_VIEWPORT);
	if (op == NULL)
		return;

	/* The first viewport's place. */
	op->args.range.first = first;
	op->args.range.count = count;

	/* The viewports. */
	(void)list_data(list, viewports, (size_t)count * sizeof(*viewports), &op->args.range.items);
}

/* Writes scissors (vkCmdSetScissor). */
void
kwl_list_set_scissor(
	struct kwl_list *list,
	uint32_t first,
	uint32_t count,
	const VkRect2D *scissors)
{
	struct kwl_list_op *op;

	/* Another scissor set: one known before is not the list's any more (ws211-p009). */
	list->scissor_serial++;

	/* The operation. */
	op = list_op(list, KWL_LIST_SCISSOR);
	if (op == NULL)
		return;

	/* The first scissor's place. */
	op->args.range.first = first;
	op->args.range.count = count;

	/* The scissors. */
	(void)list_data(list, scissors, (size_t)count * sizeof(*scissors), &op->args.range.items);
}

/* Writes a pipeline's bind (vkCmdBindPipeline). */
void
kwl_list_bind_pipeline(
	struct kwl_list *list,
	VkPipelineBindPoint point,
	VkPipeline pipeline)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_PIPELINE);
	if (op == NULL)
		return;

	/* The bind point and the pipeline. */
	op->args.pipeline.point = point;
	op->args.pipeline.pipeline = pipeline;
}

/* Writes descriptor sets' bind (vkCmdBindDescriptorSets). */
void
kwl_list_bind_descriptor_sets(
	struct kwl_list *list,
	VkPipelineBindPoint point,
	VkPipelineLayout layout,
	uint32_t first,
	uint32_t count,
	const VkDescriptorSet *sets,
	uint32_t dynamic_count,
	const uint32_t *dynamic_offsets)
{
	struct kwl_list_op *op;
	int error;

	/* The operation. */
	op = list_op(list, KWL_LIST_SETS);
	if (op == NULL)
		return;

	/* The bind point, the layout and the first set's place. */
	op->args.sets.point = point;
	op->args.sets.layout = layout;
	op->args.sets.first = first;
	op->args.sets.count = count;
	op->args.sets.dynamic_count = dynamic_count;

	/* The sets. */
	error = list_data(list, sets, (size_t)count * sizeof(*sets), &op->args.sets.sets);
	if (error != 0)
		return;

	/* Their dynamic offsets. */
	(void)list_data(list, dynamic_offsets, (size_t)dynamic_count * sizeof(*dynamic_offsets), &op->args.sets.dynamic_offsets);
}

/* Writes push constants with their values (vkCmdPushConstants). */
void
kwl_list_push_constants(
	struct kwl_list *list,
	VkPipelineLayout layout,
	VkShaderStageFlags stages,
	uint32_t offset,
	uint32_t size,
	const void *values)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_PUSH);
	if (op == NULL)
		return;

	/* The range of the constants. */
	op->args.push.layout = layout;
	op->args.push.stages = stages;
	op->args.push.offset = offset;
	op->args.push.size = size;

	/* The values, kept in the list (the caller's are on its stack). */
	(void)list_data(list, values, size, &op->args.push.values);
}

/* Writes vertex buffers' bind (vkCmdBindVertexBuffers). */
void
kwl_list_bind_vertex_buffers(
	struct kwl_list *list,
	uint32_t first,
	uint32_t count,
	const VkBuffer *buffers,
	const VkDeviceSize *offsets)
{
	struct kwl_list_op *op;
	int error;

	/* The operation. */
	op = list_op(list, KWL_LIST_VERTEX_BUFFERS);
	if (op == NULL)
		return;

	/* The first binding's place. */
	op->args.vertex_buffers.first = first;
	op->args.vertex_buffers.count = count;

	/* The buffers. */
	error = list_data(list, buffers, (size_t)count * sizeof(*buffers), &op->args.vertex_buffers.buffers);
	if (error != 0)
		return;

	/* Their offsets. */
	(void)list_data(list, offsets, (size_t)count * sizeof(*offsets), &op->args.vertex_buffers.offsets);
}

/* Writes a draw (vkCmdDraw), counted for the walk's timing. */
void
kwl_list_draw(
	struct kwl_list *list,
	uint32_t vertices,
	uint32_t instances,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_DRAW);
	if (op == NULL)
		return;

	/* The counts and the first of each. */
	op->args.draw.vertices = vertices;
	op->args.draw.instances = instances;
	op->args.draw.first_vertex = first_vertex;
	op->args.draw.first_instance = first_instance;

	/* One draw more in the frame (KWL PERF walk sorts the frames by it). */
	list->draws++;
}

/* Writes a pipeline barrier with its barriers (vkCmdPipelineBarrier). */
void
kwl_list_pipeline_barrier(
	struct kwl_list *list,
	VkPipelineStageFlags source,
	VkPipelineStageFlags destination,
	VkDependencyFlags dependency,
	uint32_t memory_count,
	const VkMemoryBarrier *memory,
	uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers,
	uint32_t image_count,
	const VkImageMemoryBarrier *images)
{
	struct kwl_list_op *op;
	int error;

	/* The operation. */
	op = list_op(list, KWL_LIST_BARRIER);
	if (op == NULL)
		return;

	/* Its stages and its counts. */
	op->args.barrier.source = source;
	op->args.barrier.destination = destination;
	op->args.barrier.dependency = dependency;
	op->args.barrier.memory_count = memory_count;
	op->args.barrier.buffer_count = buffer_count;
	op->args.barrier.image_count = image_count;

	/* The memory barriers. */
	error = list_data(list, memory, (size_t)memory_count * sizeof(*memory), &op->args.barrier.memory);
	if (error != 0)
		return;

	/* The buffer barriers. */
	error = list_data(list, buffers, (size_t)buffer_count * sizeof(*buffers), &op->args.barrier.buffers);
	if (error != 0)
		return;

	/* The image barriers. */
	(void)list_data(list, images, (size_t)image_count * sizeof(*images), &op->args.barrier.images);
}

/* Writes an image's copy into another (vkCmdCopyImage). */
void
kwl_list_copy_image(
	struct kwl_list *list,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageCopy *regions)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_COPY_IMAGE);
	if (op == NULL)
		return;

	/* The two images. */
	op->args.images.source = source;
	op->args.images.source_layout = source_layout;
	op->args.images.destination = destination;
	op->args.images.destination_layout = destination_layout;
	op->args.images.count = count;

	/* The regions. */
	(void)list_data(list, regions, (size_t)count * sizeof(*regions), &op->args.images.regions);
}

/* Writes a buffer's copy into an image (vkCmdCopyBufferToImage). */
void
kwl_list_copy_buffer_to_image(
	struct kwl_list *list,
	VkBuffer buffer,
	VkImage image,
	VkImageLayout layout,
	uint32_t count,
	const VkBufferImageCopy *regions)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_COPY_BUFFER_TO_IMAGE);
	if (op == NULL)
		return;

	/* The buffer and the image. */
	op->args.buffer_image.buffer = buffer;
	op->args.buffer_image.image = image;
	op->args.buffer_image.layout = layout;
	op->args.buffer_image.count = count;

	/* The regions. */
	(void)list_data(list, regions, (size_t)count * sizeof(*regions), &op->args.buffer_image.regions);
}

/* Writes an image's copy into a buffer (vkCmdCopyImageToBuffer). */
void
kwl_list_copy_image_to_buffer(
	struct kwl_list *list,
	VkImage image,
	VkImageLayout layout,
	VkBuffer buffer,
	uint32_t count,
	const VkBufferImageCopy *regions)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_COPY_IMAGE_TO_BUFFER);
	if (op == NULL)
		return;

	/* The image and the buffer. */
	op->args.buffer_image.buffer = buffer;
	op->args.buffer_image.image = image;
	op->args.buffer_image.layout = layout;
	op->args.buffer_image.count = count;

	/* The regions. */
	(void)list_data(list, regions, (size_t)count * sizeof(*regions), &op->args.buffer_image.regions);
}

/* Writes an image's scaled copy into another (vkCmdBlitImage). */
void
kwl_list_blit_image(
	struct kwl_list *list,
	VkImage source,
	VkImageLayout source_layout,
	VkImage destination,
	VkImageLayout destination_layout,
	uint32_t count,
	const VkImageBlit *regions,
	VkFilter filter)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_BLIT);
	if (op == NULL)
		return;

	/* The two images and the filter. */
	op->args.images.source = source;
	op->args.images.source_layout = source_layout;
	op->args.images.destination = destination;
	op->args.images.destination_layout = destination_layout;
	op->args.images.count = count;
	op->args.images.filter = filter;

	/* The regions. */
	(void)list_data(list, regions, (size_t)count * sizeof(*regions), &op->args.images.regions);
}

/* Writes an image's clear outside a pass (vkCmdClearColorImage). */
void
kwl_list_clear_color_image(
	struct kwl_list *list,
	VkImage image,
	VkImageLayout layout,
	const VkClearColorValue *color,
	uint32_t count,
	const VkImageSubresourceRange *ranges)
{
	struct kwl_list_op *op;

	/* The operation. */
	op = list_op(list, KWL_LIST_CLEAR);
	if (op == NULL)
		return;

	/* The image and the colour. */
	op->args.clear.image = image;
	op->args.clear.layout = layout;
	op->args.clear.color = *color;
	op->args.clear.count = count;

	/* The ranges. */
	(void)list_data(list, ranges, (size_t)count * sizeof(*ranges), &op->args.clear.ranges);
}

/*
 * Appends an operation of a kind, all its arguments zero; returns NULL,
 * with the list failed, when there is no room and none can be made.
 */
static struct kwl_list_op *
list_op(
	struct kwl_list *list,
	enum kwl_list_kind kind)
{
	struct kwl_list_op *op;
	size_t capacity;
	void *memory;
	int error;

	/* A failed list takes nothing more: it is not replayed. */
	if (list->failed)
		return NULL;

	/* Room for one more operation. */
	memory = list->ops;
	capacity = list->op_capacity * sizeof(*list->ops);
	error = list_room(&memory, &capacity, (list->op_count + 1U) * sizeof(*list->ops), LIST_FIRST_OPS * sizeof(*list->ops));
	if (error != 0) {
		list->failed = 1U;
		return NULL;
	}

	/* The grown array is the list's. */
	list->ops = memory;
	list->op_capacity = capacity / sizeof(*list->ops);

	/* The new operation, after those before it. */
	op = &list->ops[list->op_count];
	memset(op, 0, sizeof(*op));
	op->kind = kind;
	list->op_count++;

	/* Succeeded: the caller fills its arguments. */
	return op;
}

/*
 * Copies an array of a command into the list's data and gives its offset
 * there; an empty array takes no room.  Returns -1, with the list failed,
 * when there is no room and none can be made.
 */
static int
list_data(
	struct kwl_list *list,
	const void *source,
	size_t bytes,
	size_t *offset)
{
	size_t capacity;
	size_t start;
	void *memory;
	int error;

	/* An empty array is at no place. */
	*offset = 0U;
	if (bytes == 0U)
		return 0;

	/* Room after the arrays before it, at the alignment of any structure. */
	start = (list->data_used + LIST_ALIGN - 1U) & ~(size_t)(LIST_ALIGN - 1U);
	memory = list->data;
	capacity = list->data_capacity;
	error = list_room(&memory, &capacity, start + bytes, LIST_FIRST_DATA);
	if (error != 0) {
		list->failed = 1U;
		return -1;
	}

	/* The grown array is the list's. */
	list->data = memory;
	list->data_capacity = capacity;

	/* The array, copied. */
	memcpy(list->data + start, source, bytes);
	list->data_used = start + bytes;
	*offset = start;

	/* Succeeded: the array is kept. */
	return 0;
}

/*
 * Makes an array at least as large as needed, doubling it from a first
 * size; returns -1 when it cannot be allocated (the array is as it was).
 */
static int
list_room(
	void **memory,
	size_t *capacity,
	size_t needed,
	size_t first)
{
	size_t size;
	void *grown;

	/* Large enough already. */
	if (needed <= *capacity)
		return 0;

	/* The new size: the first one, doubled until the need fits. */
	size = *capacity;
	if (size == 0U)
		size = first;
	while (size < needed)
		size *= 2U;

	/* The array grown, its contents kept. */
	grown = realloc(*memory, size);
	if (grown == NULL)
		return -1;

	/* Succeeded: the larger array. */
	*memory = grown;
	*capacity = size;
	return 0;
}

/* Gives an array of the list's data by its offset, or NULL for an empty one. */
static const void *
list_at(
	const struct kwl_list *list,
	size_t offset,
	uint32_t count)
{
	/* An empty array is passed as none, as the drawing may have passed it. */
	if (count == 0U)
		return NULL;

	/* The array in the data. */
	return list->data + offset;
}

/* Records one operation into the command buffer, with the arguments it was written with. */
static void
list_replay_op(
	const struct kwl_list *list,
	const struct kwl_list_op *op,
	VkCommandBuffer command)
{
	VkRenderPassBeginInfo begin;

	/* The Vulkan command of the operation's kind. */
	switch (op->kind) {
	case KWL_LIST_BEGIN_PASS:
		/* The pass's beginning, its clear values from the data. */
		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = op->args.begin.pass;
		begin.framebuffer = op->args.begin.framebuffer;
		begin.renderArea = op->args.begin.area;
		begin.clearValueCount = op->args.begin.clear_count;
		begin.pClearValues = list_at(list, op->args.begin.clears, op->args.begin.clear_count);
		vkCmdBeginRenderPass(command, &begin, op->args.begin.contents);
		break;
	case KWL_LIST_END_PASS:
		vkCmdEndRenderPass(command);
		break;
	case KWL_LIST_VIEWPORT:
		vkCmdSetViewport(command, op->args.range.first, op->args.range.count, list_at(list, op->args.range.items, op->args.range.count));
		break;
	case KWL_LIST_SCISSOR:
		vkCmdSetScissor(command, op->args.range.first, op->args.range.count, list_at(list, op->args.range.items, op->args.range.count));
		break;
	case KWL_LIST_PIPELINE:
		vkCmdBindPipeline(command, op->args.pipeline.point, op->args.pipeline.pipeline);
		break;
	case KWL_LIST_SETS:
		vkCmdBindDescriptorSets(
			command,
			op->args.sets.point,
			op->args.sets.layout,
			op->args.sets.first,
			op->args.sets.count,
			list_at(list, op->args.sets.sets, op->args.sets.count),
			op->args.sets.dynamic_count,
			list_at(list, op->args.sets.dynamic_offsets, op->args.sets.dynamic_count));
		break;
	case KWL_LIST_PUSH:
		vkCmdPushConstants(
			command,
			op->args.push.layout,
			op->args.push.stages,
			op->args.push.offset,
			op->args.push.size,
			list_at(list, op->args.push.values, op->args.push.size));
		break;
	case KWL_LIST_VERTEX_BUFFERS:
		vkCmdBindVertexBuffers(
			command,
			op->args.vertex_buffers.first,
			op->args.vertex_buffers.count,
			list_at(list, op->args.vertex_buffers.buffers, op->args.vertex_buffers.count),
			list_at(list, op->args.vertex_buffers.offsets, op->args.vertex_buffers.count));
		break;
	case KWL_LIST_DRAW:
		vkCmdDraw(command, op->args.draw.vertices, op->args.draw.instances, op->args.draw.first_vertex, op->args.draw.first_instance);
		break;
	case KWL_LIST_BARRIER:
		vkCmdPipelineBarrier(
			command,
			op->args.barrier.source,
			op->args.barrier.destination,
			op->args.barrier.dependency,
			op->args.barrier.memory_count,
			list_at(list, op->args.barrier.memory, op->args.barrier.memory_count),
			op->args.barrier.buffer_count,
			list_at(list, op->args.barrier.buffers, op->args.barrier.buffer_count),
			op->args.barrier.image_count,
			list_at(list, op->args.barrier.images, op->args.barrier.image_count));
		break;
	default:
		/* The copies, the blit and the clear. */
		list_replay_copy(list, op, command);
		break;
	}
}

/* Records one copy, blit or clear into the command buffer, with the arguments it was written with. */
static void
list_replay_copy(
	const struct kwl_list *list,
	const struct kwl_list_op *op,
	VkCommandBuffer command)
{
	/* The Vulkan command of the operation's kind. */
	switch (op->kind) {
	case KWL_LIST_COPY_IMAGE:
		vkCmdCopyImage(
			command,
			op->args.images.source,
			op->args.images.source_layout,
			op->args.images.destination,
			op->args.images.destination_layout,
			op->args.images.count,
			list_at(list, op->args.images.regions, op->args.images.count));
		break;
	case KWL_LIST_COPY_BUFFER_TO_IMAGE:
		vkCmdCopyBufferToImage(
			command,
			op->args.buffer_image.buffer,
			op->args.buffer_image.image,
			op->args.buffer_image.layout,
			op->args.buffer_image.count,
			list_at(list, op->args.buffer_image.regions, op->args.buffer_image.count));
		break;
	case KWL_LIST_COPY_IMAGE_TO_BUFFER:
		vkCmdCopyImageToBuffer(
			command,
			op->args.buffer_image.image,
			op->args.buffer_image.layout,
			op->args.buffer_image.buffer,
			op->args.buffer_image.count,
			list_at(list, op->args.buffer_image.regions, op->args.buffer_image.count));
		break;
	case KWL_LIST_BLIT:
		vkCmdBlitImage(
			command,
			op->args.images.source,
			op->args.images.source_layout,
			op->args.images.destination,
			op->args.images.destination_layout,
			op->args.images.count,
			list_at(list, op->args.images.regions, op->args.images.count),
			op->args.images.filter);
		break;
	case KWL_LIST_CLEAR:
		vkCmdClearColorImage(
			command,
			op->args.clear.image,
			op->args.clear.layout,
			&op->args.clear.color,
			op->args.clear.count,
			list_at(list, op->args.clear.ranges, op->args.clear.count));
		break;
	default:
		/* The kinds list_replay_op records itself never come here. */
		break;
	}
}
