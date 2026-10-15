/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The amdrdna shader compiler: SPIR-V (through the shared scalar IR) to
 * RDNA1 (GFX10.1) programs that run as wave32.
 *
 * A program finds everything it binds through one resource table, whose
 * GPU address the driver puts in s[0:1]: the push constants from offset
 * 0, then the buffer descriptors of the storage buffers and of the
 * uniform buffers, then the image and sampler descriptors of the sampled
 * images, then (a vertex program) the vertex buffer descriptors.  The
 * compiler chooses the layout and says it in the program; the driver
 * writes the table for each draw or dispatch.
 */

#ifndef DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_COMPILE_H
#define DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_COMPILE_H

#include "drivers/gpu/compiler/ir.h"
#include "gfx10-isa.h"

#include <stddef.h>
#include <stdint.h>

/*
 * One buffer or sampled image the program binds: its (set, binding), the
 * byte offset of its descriptor in the resource table, and for a uniform
 * buffer the bytes the program reads (from 0).
 */
struct amdrdna_gfx10_binding {
	uint32_t set;
	uint32_t binding;
	uint32_t table_offset;
	uint32_t size;
};

/*
 * One compiled program.
 *
 * Made by amdrdna_gfx10_compile() and owned by the caller, who copies the
 * code into GPU memory (256-byte aligned) and frees it with
 * amdrdna_gfx10_program_free().  The code ends with the padding the
 * instruction prefetcher may read.
 */
struct amdrdna_gfx10_program {
	/* The stage the program runs in. */
	enum drv_gpu_shader_stage stage;

	/* The code, in 32-bit words, padded. */
	uint32_t *code;
	uint32_t code_words;

	/*
	 * The VGPRs and SGPRs the program uses (counted from 0), and the user
	 * SGPRs the driver loads (s0 .. s(user_sgprs - 1)).
	 */
	uint32_t vgpr_count;
	uint32_t sgpr_count;
	uint32_t user_sgprs;

	/* A compute program's workgroup size, shared memory bytes and whether it waits at a workgroup barrier. */
	uint32_t local_size[3];
	uint32_t shared_bytes;
	uint32_t uses_barrier;

	/* The push constant bytes the program reads, at offset 0 of the table. */
	uint32_t push_bytes;

	/* The bytes of the resource table the driver writes. */
	uint32_t table_bytes;

	/*
	 * A vertex program: the locations it fetches from vertex buffers (bit
	 * L), whose descriptors start at vertex_table_offset (location L's at
	 * vertex_table_offset + 16 L); the parameter locations it exports (bit
	 * L, exported as parameter L); and whether it writes the point size.
	 */
	uint32_t fetched_locations;
	uint32_t vertex_table_offset;
	uint32_t parameter_outputs;
	uint32_t writes_point_size;

	/*
	 * A pixel program: the input VGPRs it has the hardware deliver (the
	 * bits of the pixel input enable), the attribute locations it reads
	 * (bit L, read as attribute L) and those read flat, the colour
	 * locations it writes (bit L, exported to colour target L), and
	 * whether it discards.
	 */
	uint32_t pixel_inputs;
	uint32_t attribute_inputs;
	uint32_t flat_inputs;
	uint32_t color_outputs;
	uint32_t discards;

	/* The storage buffers, the uniform buffers and the sampled images, and where their descriptors go. */
	struct amdrdna_gfx10_binding storage[AMDRDNA_GFX10_STORAGE_MAX];
	uint32_t storage_used;
	struct amdrdna_gfx10_binding uniforms[AMDRDNA_GFX10_UNIFORM_MAX];
	uint32_t uniform_used;
	struct amdrdna_gfx10_binding textures[AMDRDNA_GFX10_TEXTURE_MAX];
	uint32_t texture_used;
};

/*
 * Why a program was refused: the IR instruction (or the SPIR-V word
 * offset, for a refusal of the frontend) and an immutable reason.
 */
struct amdrdna_gfx10_diagnostic {
	uint32_t instruction;
	const char *reason;
};

/* Compiles a SPIR-V module's stage: 0 with the program, EINVAL or ENOTSUP with a reason, or ENOMEM. */
int amdrdna_gfx10_compile(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, struct amdrdna_gfx10_program **program, struct amdrdna_gfx10_diagnostic *diagnostic);

/* Compiles a parsed program (the IR is only read), as amdrdna_gfx10_compile(). */
int amdrdna_gfx10_compile_ir(const struct drv_gpu_shader_ir *ir, struct amdrdna_gfx10_program **program, struct amdrdna_gfx10_diagnostic *diagnostic);

/* Frees a compiled program. */
void amdrdna_gfx10_program_free(struct amdrdna_gfx10_program *program);

#endif /* DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_COMPILE_H */
