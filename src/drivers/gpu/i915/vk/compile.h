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

/* A compiled shader: GEN code words plus the layout a draw needs.  The caller
   places the code words into a GEM object before the shader runs. */
struct i915_vk_shader_binary {
	uint32_t *code;
	uint32_t code_bytes;
	uint32_t entry_offset;
	enum i915_vk_stage stage;
	uint32_t grf_used;
	uint32_t simd;
	uint32_t thread_count;
	uint32_t sampler_count;

	/* E-128: what a draw has to program around the kernel (compile.c "Register conventions"). */
	uint32_t dispatch_grf_start;	/* first payload register after the fixed ones */
	uint32_t push_regs;		/* registers of push constants, 32 bytes each */
	uint32_t input_count;		/* vertex: attributes; fragment: interpolated inputs */
	uint32_t input_locations[3];	/* ascending: the payload order */
	uint32_t varying_count;		/* vertex: VUE slots after the position; fragment: = input_count */
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
