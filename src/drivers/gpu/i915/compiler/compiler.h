/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The shader compiler: SPIR-V to scalar IR, and scalar IR to Gen12 EU code.
 *
 * The compiler touches no device.  It returns the encoded instruction words
 * together with what a draw has to program around them; placing the words in
 * GPU memory, and checking the vertex and fragment interfaces against each
 * other, stays with the caller.
 */

#ifndef DRIVERS_GPU_I915_COMPILER_COMPILER_H
#define DRIVERS_GPU_I915_COMPILER_COMPILER_H

#include "ir.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Why a SPIR-V module was refused.
 *
 * `reason` is a static string; `opcode` and `word_offset` name the refused
 * instruction.  The parser fills it only on a refusal and clears it
 * otherwise.
 */
struct i915_compile_diagnostic {
	uint32_t opcode;
	uint32_t word_offset;
	const char *reason;
};

/*
 * A compiled shader: the EU instruction words and the layout a draw needs.
 *
 * The compiler allocates it and drv_i915_shader_binary_free() releases it.
 * The caller copies `code` into a GPU object before the shader runs.
 */
struct i915_shader_binary {
	uint32_t *code;
	uint32_t code_bytes;
	uint32_t entry_offset;
	enum i915_shader_stage stage;
	uint32_t grf_used;
	uint32_t simd;
	uint32_t thread_count;
	uint32_t sampler_count;

	/*
	 * What a draw has to program around the kernel (see the register
	 * conventions in compile.c).
	 */

	/* The first payload register after the fixed ones. */
	uint32_t dispatch_grf_start;

	/* Registers of push constants, 32 bytes each. */
	uint32_t push_regs;

	/* Vertex: attributes; fragment: interpolated inputs. */
	uint32_t input_count;

	/* The input locations in ascending order: the payload order. */
	uint32_t input_locations[3];

	/* Vertex: VUE slots after the position; fragment: equal to input_count. */
	uint32_t varying_count;
};

int drv_i915_shader_parse(const uint32_t *words, size_t word_count, enum i915_shader_stage stage, struct i915_shader_ir **out, struct i915_compile_diagnostic *diagnostic);
void drv_i915_shader_ir_free(struct i915_shader_ir *ir);
int drv_i915_shader_compile(const struct i915_shader_ir *ir, struct i915_shader_binary **out);
void drv_i915_shader_binary_free(struct i915_shader_binary *binary);

#endif /* DRIVERS_GPU_I915_COMPILER_COMPILER_H */
