/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Native Vulkan executor: shared types across the vk modules.
 *
 * The executor decodes the Vulkan command stream that libvulkan submits
 * through the drv_gpu UAPI (Venus wire opcodes, reused only as a Vulkan
 * serialization) and translates it into i915 GEN work.  This header holds
 * the types every module shares.  Module-private structures are forward
 * declared here and defined inside each module.
 */

#ifndef I915_VK_INTERNAL_H
#define I915_VK_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

/* WS029 i915 driver and drv_gpu core objects the executor builds upon. */
struct i915_device;
struct i915_session;

/* One Vulkan object handle as carried on the wire. */
typedef uint64_t i915_vk_handle;

/* Programmable stages the baseline compiler targets. */
enum i915_vk_stage {
	I915_VK_STAGE_VERTEX = 0,
	I915_VK_STAGE_FRAGMENT = 1,
	I915_VK_STAGE_COUNT = 2
};

/* Kinds tracked in the per-device object table. */
enum i915_vk_object_kind {
	I915_VK_OBJ_NONE = 0,
	I915_VK_OBJ_INSTANCE,
	I915_VK_OBJ_PHYSICAL_DEVICE,
	I915_VK_OBJ_DEVICE,
	I915_VK_OBJ_QUEUE,
	I915_VK_OBJ_MEMORY,
	I915_VK_OBJ_BUFFER,
	I915_VK_OBJ_BUFFER_VIEW,
	I915_VK_OBJ_IMAGE,
	I915_VK_OBJ_IMAGE_VIEW,
	I915_VK_OBJ_SAMPLER,
	I915_VK_OBJ_DESCRIPTOR_SET_LAYOUT,
	I915_VK_OBJ_DESCRIPTOR_POOL,
	I915_VK_OBJ_DESCRIPTOR_SET,
	I915_VK_OBJ_PIPELINE_LAYOUT,
	I915_VK_OBJ_PIPELINE,
	I915_VK_OBJ_SHADER_MODULE,
	I915_VK_OBJ_RENDER_PASS,
	I915_VK_OBJ_FRAMEBUFFER,
	I915_VK_OBJ_COMMAND_POOL,
	I915_VK_OBJ_COMMAND_BUFFER,
	I915_VK_OBJ_FENCE,
	I915_VK_OBJ_SEMAPHORE,
	I915_VK_OBJ_EVENT,
	I915_VK_OBJ_QUERY_POOL,
	I915_VK_OBJ_SWAPCHAIN,
	I915_VK_OBJ_KIND_COUNT
};

/* A bounded cursor over an inbound wire command. */
struct i915_vk_reader {
	const uint8_t *base;
	size_t size;
	size_t offset;
	int error;
};

/* A bounded cursor building a reply. */
struct i915_vk_writer {
	uint8_t *base;
	size_t size;
	size_t offset;
	int error;
};

/*
 * The scratch a command decodes its records into (vkc.h).  One per session, emptied before each
 * command: a decoded pointer lives until its command returns and no longer.
 */
struct i915_vk_arena {
	uint8_t *base;
	size_t size;
	size_t used;
};

#define I915_VK_ARENA_BYTES (256U * 1024U)

/* WS029 GEM object that backs vk memory, batches and shader code. */
struct i915_gem_object;

/* The per-device object table, defined privately in cmd. */
struct i915_vk_object_table;

/* The largest capset the executor reports, in 32-bit words (>= 156 bytes). */
#define I915_VK_CAPSET_WORDS 64U

/*
 * Per-device executor state.  One exists per i915 device that the executor
 * is attached to.  Fields are added by later phases as modules need them.
 */
struct i915_vk_device {
	struct i915_device *i915;
	struct i915_vk_object_table *objects;
	uint32_t capset[I915_VK_CAPSET_WORDS];
	uint32_t capset_bytes;

	/* The active display mode, filled by the WSI display path. */
	uint32_t display_width;
	uint32_t display_height;
	uint32_t display_stride;
};

/*
 * Per-open state, wrapping one WS029 drv_gpu session (its PPGTT and engine
 * contexts).  Fields are added by later phases (command pools, descriptor
 * pools, swapchains).
 */
struct i915_vk_session {
	struct i915_vk_device *vk;
	struct i915_session *gpu;
	struct i915_vk_arena arena;
	struct gfx_session *gfx;	/* gfx-draw.c: what draws keep between them */
};

/* A Gen batch under construction: a GEM buffer and a dword cursor. */
struct i915_vk_batch {
	struct i915_gem_object *object;
	uint32_t *map;
	uint32_t cursor;
	uint32_t capacity;
	int error;
};

/*
 * Module-private structures, defined inside their owning module.  Only the
 * pointer type is shared here so modules can name each other's objects.
 */
struct i915_vk_memory;		/* res */
struct i915_vk_buffer;		/* res */
struct i915_vk_image;		/* res */
struct i915_vk_image_view;	/* res */
struct i915_vk_sampler;		/* res */
struct i915_vk_dsl;		/* res: descriptor set layout */
struct i915_vk_dpool;		/* res: descriptor pool */
struct i915_vk_dset;		/* res: descriptor set */
struct i915_vk_image_info;	/* res */
struct i915_vk_sampler_info;	/* res */
struct i915_vk_write_dset;	/* res */
struct i915_vk_shader_ir;	/* spirv */
struct i915_vk_shader_binary;	/* compile */
struct i915_vk_pipeline;	/* pipe */
struct i915_vk_pipeline_info;	/* pipe */
struct i915_vk_cmdbuf;		/* cmdbuf */
struct i915_vk_fence;		/* sync */
struct i915_vk_semaphore;	/* sync */
struct i915_vk_query_pool;	/* sync */
struct i915_vk_swapchain;	/* wsi */

/* Translates a Vulkan result code carried on the wire to an errno. */
int
i915_vk_errno(
	int vk_result);

#endif /* I915_VK_INTERNAL_H */
