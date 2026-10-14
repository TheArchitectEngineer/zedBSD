/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The nvrtx shader compiler: SPIR-V to Turing (SM75) code (sm75-compile.c,
 * ws037-p008).
 *
 * The shared SPIR-V frontend makes the scalar IR (drivers/gpu/compiler/);
 * this backend chooses SM75 instructions for it, assigns registers, marks
 * the dependences, and encodes 128-bit instructions.  The result says what
 * the dispatch needs: the general registers to reserve, the shared memory,
 * the workgroup's size, and which buffer goes where in the constant
 * buffers (sm75-isa.h, NVRTX_SM75_CB_*).
 *
 * Stages A and B compile compute shaders: arithmetic, conversions, the
 * special functions, storage and shared memory with their atomics,
 * workgroup barriers and fences, integer division, and loops.  Stage E
 * adds vertex and fragment programs with their shader program header:
 * attributes, interpolated inputs, colour outputs, 2D texture samples and
 * discards.  Geometry, the other texture messages, derivatives and
 * spilling are refused with a reason.
 */

#ifndef DRIVERS_GPU_NVRTX_COMPILER_SM75_COMPILE_H
#define DRIVERS_GPU_NVRTX_COMPILER_SM75_COMPILE_H

#include "drivers/gpu/compiler/ir.h"
#include "sm75-isa.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The (set, binding) of one buffer the shader uses, and for a uniform
 * buffer the bytes it reads (from 0).
 */
struct nvrtx_sm75_binding {
	uint32_t set;
	uint32_t binding;
	uint32_t size;
};

/*
 * One compiled compute program.
 *
 * Made by nvrtx_sm75_compile() and owned by the caller, who copies the code
 * into GPU memory and frees it with nvrtx_sm75_binary_free().
 *
 * storage[k] is the buffer whose address and size the dispatch writes at
 * NVRTX_SM75_CB_STORAGE + 16 * k of constant buffer 0; uniforms[j] is the
 * buffer the dispatch binds as constant buffer 1 + j.
 */
struct nvrtx_sm75_binary {
	/* The code: 128-bit instructions, four words each, padded to a whole 128-byte line. */
	uint32_t *code;
	uint32_t code_words;

	/* The general registers the program needs, the hardware's two included (the QMD's register count). */
	uint32_t gpr_count;

	/* The workgroup's size, its shared memory in bytes, and the workgroup barriers it uses (the QMD's barrier count). */
	uint32_t local_size[3];
	uint32_t shared_bytes;
	uint32_t barrier_count;

	/* The push constant bytes the program reads (at offset 0 of constant buffer 0). */
	uint32_t push_bytes;

	/* The storage buffers and the uniform buffers, as above. */
	struct nvrtx_sm75_binding storage[NVRTX_SM75_CB_STORAGE_MAX];
	uint32_t storage_count;
	struct nvrtx_sm75_binding uniforms[NVRTX_SM75_CB_UNIFORM_MAX];
	uint32_t uniform_count;

	/* The sampled images: the k-th one's texture handle is the word at NVRTX_SM75_CB_TEXTURES + 4 * k of constant buffer 0. */
	struct nvrtx_sm75_binding textures[NVRTX_SM75_CB_TEXTURES_MAX];
	uint32_t texture_count;

	/*
	 * A vertex or fragment program's shader program header (version 4, 128
	 * bytes), which the program's address points at, the code following
	 * it; nonzero graphics says it is one (a compute program has none).
	 */
	uint32_t graphics;
	uint32_t sph[NVRTX_SM75_SPH_WORDS];
};

/*
 * Why a program was refused: the IR instruction (or SPIR-V word offset for
 * a refusal of the frontend) and an immutable reason.
 */
struct nvrtx_sm75_diagnostic {
	uint32_t instruction;
	const char *reason;
};

/* Compiles a SPIR-V compute module: 0 with the program, EINVAL or ENOTSUP with a reason, or ENOMEM. */
int nvrtx_sm75_compile(const uint32_t *words, size_t word_count, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a SPIR-V module's stage (compute, vertex or fragment), as nvrtx_sm75_compile(). */
int nvrtx_sm75_compile_stage(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a parsed compute program (the IR is only read). */
int nvrtx_sm75_compile_ir(const struct drv_gpu_shader_ir *ir, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Frees a compiled program. */
void nvrtx_sm75_binary_free(struct nvrtx_sm75_binary *binary);

#endif /* DRIVERS_GPU_NVRTX_COMPILER_SM75_COMPILE_H */
