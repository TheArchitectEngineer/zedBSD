/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The graphics path of the native Vulkan executor (WS031 E-127): the objects a draw is made of,
 * the recorded form of a command buffer, and the entry that turns one recorded draw into Gen12
 * commands.
 *
 * MINIMAL CONNECTION, HAPPY PATH ONLY.  This module exists so that the standard application
 * (userland/base/vkdemo) reaches the GPU through libvulkan and the ordinary ioctls.  What it does
 * not do is said where it does not do it (an `XXX:` comment and a kernel message); nothing is
 * accepted and then dropped.
 *
 *  - gfx-obj.c   objects: memory, buffer, image, view, sampler, descriptors, layouts, render pass,
 *                framebuffer, shader module, pipeline, semaphore
 *  - gfx-rec.c   command pools / buffers, recording, vkQueueSubmit
 *  - gfx-draw.c  one recorded draw -> state heaps + batch -> the GPU
 *
 * A command buffer is recorded as a list of operations, not as GPU commands: clears and copies are
 * done by the CPU between GPU batches (XXX: see gfx-rec.c), so the order of the list is the order
 * of execution and a submission is complete when vkQueueSubmit replies.
 */

#ifndef I915_VK_GFX_H
#define I915_VK_GFX_H

#include "vk-internal.h"
#include "cmd.h"

#include <vulkan/vulkan_core.h>

struct i915_gem_object;

/* ---- objects ---- */

/* VkDeviceMemory.  Its storage is the blob libvulkan exports for it right after the allocation. */
struct gfx_memory {
	struct gfx_memory *next;		/* every live allocation of the device (blob attach / detach) */
	struct i915_vk_device *vk;
	uint64_t identity;			/* the wire id: the blob names it as its blob_id */
	uint64_t size;
	struct i915_gem_object *object;		/* NULL until the blob arrives */
};

struct gfx_buffer {
	uint64_t size;
	uint32_t usage;
	struct gfx_memory *memory;
	uint64_t offset;
};

struct gfx_image {
	uint32_t format;			/* VkFormat */
	uint32_t width;
	uint32_t height;
	uint32_t usage;
	uint32_t pitch;				/* bytes to a row; every image is linear */
	uint64_t bytes;
	struct gfx_memory *memory;
	uint64_t offset;
};

struct gfx_view {
	struct gfx_image *image;
	uint32_t format;
};

struct gfx_sampler {
	uint32_t mag_filter;
	uint32_t min_filter;
	uint32_t address_u;
	uint32_t address_v;
};

#define GFX_MAX_BINDINGS 8U

struct gfx_dsl {
	uint32_t count;
	struct {
		uint32_t binding;
		uint32_t type;
		uint32_t stages;
	} bindings[GFX_MAX_BINDINGS];
};

struct gfx_dset {
	struct gfx_dsl *layout;
	struct {
		struct gfx_view *view;
		struct gfx_sampler *sampler;
	} slots[GFX_MAX_BINDINGS];		/* indexed by binding number */
};

#define GFX_MAX_ATTACHMENTS 4U

struct gfx_pass {
	uint32_t attachment_count;
	struct {
		uint32_t format;
		uint32_t load_op;
	} attachments[GFX_MAX_ATTACHMENTS];
	uint32_t color_attachment;		/* index, or VK_ATTACHMENT_UNUSED */
	uint32_t depth_attachment;
};

struct gfx_framebuffer {
	uint32_t width;
	uint32_t height;
	uint32_t view_count;
	struct gfx_view *views[GFX_MAX_ATTACHMENTS];
};

struct gfx_shader {
	uint32_t *words;
	uint32_t word_count;
};

#define GFX_MAX_VERTEX_BINDINGS 4U
#define GFX_MAX_VERTEX_ATTRIBUTES 8U

struct i915_vk_shader_binary;

struct gfx_pipeline {
	struct gfx_shader *vertex;
	struct gfx_shader *fragment;
	uint32_t binding_count;
	struct {
		uint32_t binding;
		uint32_t stride;
	} bindings[GFX_MAX_VERTEX_BINDINGS];
	uint32_t attribute_count;
	struct {
		uint32_t location;
		uint32_t binding;
		uint32_t format;
		uint32_t offset;
	} attributes[GFX_MAX_VERTEX_ATTRIBUTES];
	uint32_t topology;
	uint32_t viewport[6];			/* x, y, width, height, minDepth, maxDepth as float bits */
	VkRect2D scissor;
	uint32_t cull_mode;
	uint32_t front_face;
	uint32_t depth_test;
	uint32_t depth_write;
	uint32_t depth_compare;
	int kernels_ready;
	struct i915_vk_shader_binary *vs_binary;	/* the executor's compiler (compile.h); NULL in a reference-kernel build */
	struct i915_vk_shader_binary *fs_binary;
};

/* ---- the recorded form of a command buffer ---- */

enum gfx_op_kind {
	GFX_OP_COPY_BUFFER_TO_IMAGE = 1,
	GFX_OP_COPY_IMAGE_TO_BUFFER,
	GFX_OP_BEGIN_PASS,
	GFX_OP_END_PASS,
	GFX_OP_BIND_PIPELINE,
	GFX_OP_BIND_VERTEX_BUFFER,
	GFX_OP_BIND_DESCRIPTOR_SET,
	GFX_OP_PUSH_CONSTANTS,
	GFX_OP_DRAW
};

#define GFX_PUSH_BYTES 128U

struct gfx_op {
	enum gfx_op_kind kind;
	union {
		struct {
			struct gfx_buffer *buffer;
			struct gfx_image *image;
			VkBufferImageCopy region;
		} copy;
		struct {
			struct gfx_pass *pass;
			struct gfx_framebuffer *framebuffer;
			uint32_t clear_count;
			uint32_t clear_is_depth[GFX_MAX_ATTACHMENTS];
			uint32_t clear_words[GFX_MAX_ATTACHMENTS][4];	/* colour: RGBA float bits; depth: [0] */
		} begin;
		struct gfx_pipeline *pipeline;
		struct {
			uint32_t binding;
			struct gfx_buffer *buffer;
			uint64_t offset;
		} vertex;
		struct {
			uint32_t set;
			struct gfx_dset *dset;
		} descriptor;
		struct {
			uint32_t offset;
			uint32_t size;
			uint8_t bytes[GFX_PUSH_BYTES];
		} push;
		struct {
			uint32_t vertex_count;
			uint32_t instance_count;
			uint32_t first_vertex;
			uint32_t first_instance;
		} draw;
	} u;
};

/* What is bound when a draw is reached. */
struct gfx_draw_state {
	struct gfx_pass *pass;
	struct gfx_framebuffer *framebuffer;
	struct gfx_pipeline *pipeline;
	struct {
		struct gfx_buffer *buffer;
		uint64_t offset;
	} vertex[GFX_MAX_VERTEX_BINDINGS];
	struct gfx_dset *dset[4];
	uint8_t push[GFX_PUSH_BYTES];
};

/* ---- entries ---- */

/* gfx-obj.c / gfx-rec.c: `handled` is cleared for an opcode the module does not own. */
int i915_vk_gfx_obj_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled);
int i915_vk_gfx_rec_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled);

/* The CPU view of `bytes` bytes of a memory range, or NULL when the memory has no storage yet. */
uint8_t *i915_vk_gfx_memory_cpu(struct gfx_memory *memory, uint64_t offset, uint64_t bytes);
/* The GPU address of a memory range in the session's address space (0: no storage). */
uint64_t i915_vk_gfx_memory_va(struct gfx_memory *memory, uint64_t offset);

/* gfx-draw.c: releases what the session's draws kept (the state and batch objects). */
void i915_vk_gfx_session_close(struct i915_vk_session *session);

/* gfx-draw.c: prepares a pipeline's kernels; runs one draw to its end on the GPU. */
int i915_vk_gfx_pipeline_prepare(struct i915_vk_session *session, struct gfx_pipeline *pipeline);
void i915_vk_gfx_pipeline_release(struct gfx_pipeline *pipeline);
int i915_vk_gfx_draw(struct i915_vk_session *session, const struct gfx_draw_state *state,
	uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

#endif /* I915_VK_GFX_H */
