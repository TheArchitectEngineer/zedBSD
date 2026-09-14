/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Baseline shader compiler: lowers the SPIR-V IR to Gen12 GEN code one
 * instruction at a time, no optimization, one GRF per SSA value.  Contract
 * for p006; see external-design.md section 4.6.  The binary is what pipe and
 * cmdbuf consume.
 */

#ifndef I915_VK_COMPILE_H
#define I915_VK_COMPILE_H

#include "vk-internal.h"
#include "spirv.h"

/* A varying placed in the URB. */
struct i915_vk_urb_slot {
	uint32_t location;
	uint32_t urb_offset;
};

/* A compiled shader: GEN code plus the layout a draw needs. */
struct i915_vk_shader_binary {
	struct i915_vk_memory *code;
	uint32_t code_bytes;
	uint32_t entry_offset;
	enum i915_vk_stage stage;
	uint32_t grf_used;
	uint32_t simd;
	uint32_t thread_count;
	struct i915_vk_urb_slot *urb;
	uint32_t urb_count;
	uint32_t *bindings;
	uint32_t binding_count;
};

/* Compiles one shader IR to a GEN binary. */
int
i915_vk_compile(
	struct i915_vk_device *vk,
	const struct i915_vk_shader_ir *ir,
	struct i915_vk_shader_binary **out);

/* Releases a compiled shader binary and its GEN code. */
void
i915_vk_shader_binary_free(
	struct i915_vk_shader_binary *binary);

#endif /* I915_VK_COMPILE_H */
