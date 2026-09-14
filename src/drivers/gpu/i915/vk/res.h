/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Resource model: memory, buffer, image, sampler and descriptor objects
 * mapped onto WS029 GEM/GGTT/PPGTT and Gen12 surface/sampler/binding state.
 * Contract for p003; see external-design.md section 4.3.
 */

#ifndef I915_VK_RES_H
#define I915_VK_RES_H

#include "vk-internal.h"

/* Routes memory/buffer/image/sampler/descriptor opcodes. */
int
i915_vk_res_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Device memory backed by a GEM object; host-visible memory maps a CPU view. */
int
i915_vk_memory_alloc(
	struct i915_vk_session *session,
	uint64_t size,
	uint32_t flags,
	struct i915_vk_memory **out);

void
i915_vk_memory_free(
	struct i915_vk_memory *memory);

int
i915_vk_memory_map(
	struct i915_vk_memory *memory,
	void **cpu);

/* Buffers bind a memory range and address the GPU through the session PPGTT. */
int
i915_vk_buffer_create(
	struct i915_vk_session *session,
	uint64_t size,
	uint32_t usage,
	struct i915_vk_buffer **out);

int
i915_vk_buffer_bind(
	struct i915_vk_buffer *buffer,
	struct i915_vk_memory *memory,
	uint64_t offset);

void
i915_vk_buffer_destroy(
	struct i915_vk_buffer *buffer);

/* Images carry tiling and a surface state; views select format and range. */
int
i915_vk_image_create(
	struct i915_vk_session *session,
	const struct i915_vk_image_info *info,
	struct i915_vk_image **out);

int
i915_vk_image_bind(
	struct i915_vk_image *image,
	struct i915_vk_memory *memory,
	uint64_t offset);

void
i915_vk_image_destroy(
	struct i915_vk_image *image);

int
i915_vk_image_view_create(
	struct i915_vk_session *session,
	struct i915_vk_image *image,
	uint32_t format,
	struct i915_vk_image_view **out);

void
i915_vk_image_view_destroy(
	struct i915_vk_image_view *view);

/* Samplers carry a Gen12 sampler state. */
int
i915_vk_sampler_create(
	struct i915_vk_session *session,
	const struct i915_vk_sampler_info *info,
	struct i915_vk_sampler **out);

void
i915_vk_sampler_destroy(
	struct i915_vk_sampler *sampler);

/* Descriptor set layout, pool, set and updates. */
int
i915_vk_dsl_create(
	struct i915_vk_session *session,
	const void *bindings,
	uint32_t count,
	struct i915_vk_dsl **out);

void
i915_vk_dsl_destroy(
	struct i915_vk_dsl *dsl);

int
i915_vk_dpool_create(
	struct i915_vk_session *session,
	uint32_t max_sets,
	struct i915_vk_dpool **out);

void
i915_vk_dpool_destroy(
	struct i915_vk_dpool *dpool);

int
i915_vk_dset_alloc(
	struct i915_vk_dpool *dpool,
	struct i915_vk_dsl *dsl,
	struct i915_vk_dset **out);

void
i915_vk_dset_free(
	struct i915_vk_dset *dset);

int
i915_vk_dset_update(
	struct i915_vk_dset *dset,
	const struct i915_vk_write_dset *writes,
	uint32_t count);

/*
 * State supplied to pipe and cmdbuf.  Surface and sampler state are the
 * Gen12 dword blocks; the binding table places them in the session state
 * heap and returns the heap offsets a draw references.
 */
const uint32_t *
i915_vk_image_surface_state(
	const struct i915_vk_image_view *view);

const uint32_t *
i915_vk_sampler_state(
	const struct i915_vk_sampler *sampler);

int
i915_vk_build_binding_table(
	struct i915_vk_session *session,
	struct i915_vk_dset *const *sets,
	uint32_t set_count,
	uint32_t *binding_table_offset,
	uint32_t *sampler_state_offset);

#endif /* I915_VK_RES_H */
