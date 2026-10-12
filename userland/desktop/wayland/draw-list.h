/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display list of a frame (ws213-p003): the drawing writes the
 * commands it would record as operations of a list, and the list is
 * replayed into a Vulkan command buffer afterwards.
 */

#ifndef KWL_DRAW_LIST_H
#define KWL_DRAW_LIST_H

#include <stddef.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

/* A host-written image whose staging a frame copies first (compose.h). */
struct kwl_import;

/* The kinds of operation a list holds, one for each Vulkan command the drawing records. */
enum kwl_list_kind {
	KWL_LIST_BEGIN_PASS,
	KWL_LIST_END_PASS,
	KWL_LIST_VIEWPORT,
	KWL_LIST_SCISSOR,
	KWL_LIST_PIPELINE,
	KWL_LIST_SETS,
	KWL_LIST_PUSH,
	KWL_LIST_VERTEX_BUFFERS,
	KWL_LIST_DRAW,
	KWL_LIST_BARRIER,
	KWL_LIST_COPY_IMAGE,
	KWL_LIST_COPY_BUFFER_TO_IMAGE,
	KWL_LIST_COPY_IMAGE_TO_BUFFER,
	KWL_LIST_BLIT,
	KWL_LIST_CLEAR
};

/*
 * One recorded command: its kind and the arguments it was given.
 *
 * Arrays the command took (clear values, viewports, sets, push constants,
 * barriers, regions) are copied into the list's data and named by their
 * byte offset there, so that the list may grow while the drawing writes it.
 * Handles are kept as given: the objects they name live until the list's
 * frame completes.
 */
struct kwl_list_op {
	enum kwl_list_kind kind;
	union {
		struct {
			VkRenderPass pass;
			VkFramebuffer framebuffer;
			VkRect2D area;
			VkSubpassContents contents;
			uint32_t clear_count;
			size_t clears;
		} begin;
		struct {
			uint32_t first;
			uint32_t count;
			size_t items;
		} range;
		struct {
			VkPipelineBindPoint point;
			VkPipeline pipeline;
		} pipeline;
		struct {
			VkPipelineBindPoint point;
			VkPipelineLayout layout;
			uint32_t first;
			uint32_t count;
			size_t sets;
			uint32_t dynamic_count;
			size_t dynamic_offsets;
		} sets;
		struct {
			VkPipelineLayout layout;
			VkShaderStageFlags stages;
			uint32_t offset;
			uint32_t size;
			size_t values;
		} push;
		struct {
			uint32_t first;
			uint32_t count;
			size_t buffers;
			size_t offsets;
		} vertex_buffers;
		struct {
			uint32_t vertices;
			uint32_t instances;
			uint32_t first_vertex;
			uint32_t first_instance;
		} draw;
		struct {
			VkPipelineStageFlags source;
			VkPipelineStageFlags destination;
			VkDependencyFlags dependency;
			uint32_t memory_count;
			uint32_t buffer_count;
			uint32_t image_count;
			size_t memory;
			size_t buffers;
			size_t images;
		} barrier;
		struct {
			VkImage source;
			VkImageLayout source_layout;
			VkImage destination;
			VkImageLayout destination_layout;
			uint32_t count;
			size_t regions;
			VkFilter filter;
		} images;
		struct {
			VkBuffer buffer;
			VkImage image;
			VkImageLayout layout;
			uint32_t count;
			size_t regions;
		} buffer_image;
		struct {
			VkImage image;
			VkImageLayout layout;
			VkClearColorValue color;
			uint32_t count;
			size_t ranges;
		} clear;
	} args;
};

/*
 * A frame's display list (ws213-p003, p001 4.4).
 *
 * One list lives in the compositor's Vulkan state and is written by one
 * drawing at a time: reset when the drawing begins, written by it, and
 * replayed into the frame's command buffer when it ends.  Beside the
 * operations it holds their arrays (data), the vertices of the solid quads
 * drawn together (batch.c writes them here, and the replay copies them to
 * the vertex buffer the draws read), and the host images whose staging the
 * frame copies first (taken from their written mark when the drawing
 * ends).  failed says a growth could not be allocated: the list is not
 * replayed.  draws counts the draw commands, joined the quads drawn
 * together and joined_draws the draws that drew them, for the walk's
 * timing by the number of things drawn.  scissor_serial only increases:
 * each reset, pass begun and scissor set moves it, so a writer that
 * remembers it can tell whether the scissor it set last is still the
 * list's (compose-bind.c, ws211-p009).
 */
struct kwl_list {
	struct kwl_list_op *ops;
	size_t op_count;
	size_t op_capacity;
	unsigned char *data;
	size_t data_used;
	size_t data_capacity;
	unsigned char *vertices;
	uint32_t vertex_used;
	uint32_t vertex_capacity;
	struct kwl_import **copies;
	unsigned copy_count;
	unsigned copy_capacity;
	unsigned draws;
	unsigned joined;
	unsigned joined_draws;
	unsigned failed;
	uint64_t scissor_serial;
};

void kwl_list_reset(struct kwl_list *list);
void kwl_list_free(struct kwl_list *list);
void kwl_list_replay(const struct kwl_list *list, VkCommandBuffer command);
unsigned kwl_list_things(const struct kwl_list *list);
void kwl_list_add_vertices(struct kwl_list *list, const void *vertices, uint32_t bytes);
void kwl_list_add_copy(struct kwl_list *list, struct kwl_import *import);
void kwl_list_begin_render_pass(struct kwl_list *list, const VkRenderPassBeginInfo *begin, VkSubpassContents contents);
void kwl_list_end_render_pass(struct kwl_list *list);
void kwl_list_set_viewport(struct kwl_list *list, uint32_t first, uint32_t count, const VkViewport *viewports);
void kwl_list_set_scissor(struct kwl_list *list, uint32_t first, uint32_t count, const VkRect2D *scissors);
void kwl_list_bind_pipeline(struct kwl_list *list, VkPipelineBindPoint point, VkPipeline pipeline);
void kwl_list_bind_descriptor_sets(struct kwl_list *list, VkPipelineBindPoint point, VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *dynamic_offsets);
void kwl_list_push_constants(struct kwl_list *list, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void *values);
void kwl_list_bind_vertex_buffers(struct kwl_list *list, uint32_t first, uint32_t count, const VkBuffer *buffers, const VkDeviceSize *offsets);
void kwl_list_draw(struct kwl_list *list, uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance);
void kwl_list_pipeline_barrier(struct kwl_list *list, VkPipelineStageFlags source, VkPipelineStageFlags destination, VkDependencyFlags dependency, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count, const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images);
void kwl_list_copy_image(struct kwl_list *list, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageCopy *regions);
void kwl_list_copy_buffer_to_image(struct kwl_list *list, VkBuffer buffer, VkImage image, VkImageLayout layout, uint32_t count, const VkBufferImageCopy *regions);
void kwl_list_copy_image_to_buffer(struct kwl_list *list, VkImage image, VkImageLayout layout, VkBuffer buffer, uint32_t count, const VkBufferImageCopy *regions);
void kwl_list_blit_image(struct kwl_list *list, VkImage source, VkImageLayout source_layout, VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageBlit *regions, VkFilter filter);
void kwl_list_clear_color_image(struct kwl_list *list, VkImage image, VkImageLayout layout, const VkClearColorValue *color, uint32_t count, const VkImageSubresourceRange *ranges);

#endif
