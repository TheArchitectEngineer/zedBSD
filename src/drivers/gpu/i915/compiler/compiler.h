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

/* The most sampled images a kernel reads: binding table entries 1 .. 16. */
#define I915_SHADER_MAX_SAMPLERS	16U

/* The most uniform blocks a kernel reads. */
#define I915_SHADER_MAX_BLOCKS		8U

/* The most vertex attributes, and varyings, a kernel reads or writes. */
#define I915_SHADER_MAX_INPUTS		16U

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
 * One uniform block a kernel reads, delivered with its push constants.
 *
 * The draw copies bytes [offset, offset + bytes) of the buffer bound at
 * (set, binding) to byte `push_offset` of the stage's push data.
 */
struct i915_shader_block {
	uint32_t set;
	uint32_t binding;
	uint32_t offset;
	uint32_t bytes;
	uint32_t push_offset;
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

	/* The sampled images: the n-th is binding table entry 1 + n and sampler n. */
	uint32_t sampler_count;
	uint32_t sampler_set[I915_SHADER_MAX_SAMPLERS];
	uint32_t sampler_binding[I915_SHADER_MAX_SAMPLERS];

	/*
	 * What a draw has to program around the kernel (see the register
	 * conventions in compile.c).
	 */

	/* The first payload register after the fixed ones. */
	uint32_t dispatch_grf_start;

	/*
	 * Registers of push data, 32 bytes each: the push constants first
	 * (push_constant_bytes of them, a whole number of registers), then the
	 * uniform blocks.
	 */
	uint32_t push_regs;
	uint32_t push_constant_bytes;

	/* The uniform blocks delivered after the push constants. */
	uint32_t block_count;
	struct i915_shader_block blocks[I915_SHADER_MAX_BLOCKS];

	/* Vertex: attributes; fragment: interpolated inputs. */
	uint32_t input_count;

	/* The input locations in ascending order: the payload order. */
	uint32_t input_locations[I915_SHADER_MAX_INPUTS];

	/* Vertex: VUE slots after the position; fragment: equal to input_count. */
	uint32_t varying_count;

	/* Vertex: the location each VUE slot after the position holds, ascending. */
	uint32_t varying_locations[I915_SHADER_MAX_INPUTS];

	/*
	 * Fragment: nonzero when the kernel discards pixels, which the draw
	 * declares in 3DSTATE_PS_EXTRA (Pixel Shader Kills Pixel).
	 */
	uint32_t uses_kill;

	/*
	 * The scratch memory each thread of the kernel needs for the values it
	 * spills: a power of two from 1 KiB to 2 MiB, or 0 for a kernel that
	 * spills nothing.  The draw programs it, and a buffer of it for every
	 * thread the stage may run at once, in 3DSTATE_VS / PS.
	 */
	uint32_t scratch_bytes;
};

int drv_i915_shader_parse(const uint32_t *words, size_t word_count, enum i915_shader_stage stage, struct i915_shader_ir **out, struct i915_compile_diagnostic *diagnostic);
void drv_i915_shader_ir_free(struct i915_shader_ir *ir);
int drv_i915_shader_compile(const struct i915_shader_ir *ir, struct i915_shader_binary **out);
void drv_i915_shader_binary_free(struct i915_shader_binary *binary);

#endif /* DRIVERS_GPU_I915_COMPILER_COMPILER_H */
