/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GHCI shader compiler: SPIR-V (through the shared scalar IR) to GSI1
 * programs, the standard shader ISA every GHCI GPU with a shader unit
 * runs.
 *
 * The result is the program binary the PROGRAM gpu-op hands to the GPU: a
 * header, the interface table that binds the program to the pipeline's
 * descriptors, and the code.  The interface table is the only contract
 * between the program and the command processor: it says which registers
 * the vertex attributes arrive in, which varying words the program reads
 * and writes, which constant-bank words hold the push constants, the
 * uniform blocks and the descriptors of the buffers and images, and which
 * texture slot each sampled image takes.
 */

#ifndef DRIVERS_GPU_GHCI_COMPILER_GSI1_COMPILE_H
#define DRIVERS_GPU_GHCI_COMPILER_GSI1_COMPILE_H

#include "drivers/gpu/compiler/ir.h"

#include <stddef.h>
#include <stdint.h>

/* The user locations a vertex program writes and a fragment program reads. */
#define GHCI_GSI1_LOCATIONS		16U

/* The input attachments a fragment program may read (maxPerStageDescriptorInputAttachments). */
#define GHCI_GSI1_INPUT_ATTACHMENTS	8U

/* A varying word that is not given. */
#define GHCI_GSI1_NO_WORD		0xffffffffU

/*
 * What a pipeline tells a compile beyond the module.
 *
 * A zeroed record is a valid request for a GPU with 32 registers per
 * lane, one sample per pixel and the plain varying layout (location L
 * component c in varying word 4 L + c).
 */
struct ghci_gsi1_options {
	/* The registers per lane of the GPU (32 or 64; 0 reads as 32). */
	uint32_t registers;

	/* The constant-bank words of the GPU (0 reads as 256). */
	uint32_t bank_words;

	/*
	 * The first constant-bank word the program may use.  The vertex and
	 * the fragment program of a pipeline share one bank, so the fragment
	 * program is compiled from the vertex program's header word 9 (its
	 * words used) on.
	 */
	uint32_t bank_first;

	/* Nonzero when the GPU writes the sample mask in r6 (CAPS[29]). */
	uint32_t sample_mask_input;

	/* A fragment program: the pipeline's samples per pixel (1 or 4; 0 reads as 1). */
	uint32_t samples;

	/* A fragment program of a pipeline that draws points: its inputs are vertex 0's. */
	uint32_t points;

	/* Nonzero when the device was created with robustBufferAccess. */
	uint32_t robust_buffer_access;

	/*
	 * The varying words: nonzero has_varying_map gives each location's
	 * first word (GHCI_GSI1_NO_WORD for none) instead of 4 L; w_word is the
	 * varying word a vertex program also writes the position's w to and
	 * a fragment program reads it from, for noperspective inputs and
	 * FragCoord.w (GHCI_GSI1_NO_WORD for none; zero is a word when
	 * has_w_word is nonzero).
	 */
	uint32_t has_varying_map;
	uint32_t varying_word[GHCI_GSI1_LOCATIONS];
	uint32_t has_w_word;
	uint32_t w_word;

	/*
	 * A fragment program's input attachments, by InputAttachmentIndex:
	 * the VkFormat of the render pass's attachment (0 for none) and its
	 * samples.
	 */
	uint32_t input_attachment_format[GHCI_GSI1_INPUT_ATTACHMENTS];
	uint32_t input_attachment_samples[GHCI_GSI1_INPUT_ATTACHMENTS];
};

/*
 * One compiled program: the binary the PROGRAM gpu-op points at.
 *
 * Made by ghci_gsi1_compile() and owned by the caller, who copies the
 * words into GPU memory (8-byte aligned) and frees the record with
 * ghci_gsi1_program_free().
 */
struct ghci_gsi1_program {
	/* The stage the program runs in. */
	enum drv_gpu_shader_stage stage;

	/* The binary: header, interface table and code, in 32-bit words. */
	uint32_t *words;
	uint32_t word_count;

	/* The instructions of the code (two words each) and the registers it uses. */
	uint32_t instruction_count;
	uint32_t register_count;
};

/*
 * Why a program was refused: the IR instruction (or the SPIR-V word
 * offset, for a refusal of the frontend) and an immutable reason.
 */
struct ghci_gsi1_diagnostic {
	uint32_t instruction;
	const char *reason;
};

/*
 * Compiles a SPIR-V module's stage with the options (NULL for none): 0
 * with the program, EINVAL or ENOTSUP with a reason, or ENOMEM.
 */
int ghci_gsi1_compile(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, const struct ghci_gsi1_options *options, struct ghci_gsi1_program **program, struct ghci_gsi1_diagnostic *diagnostic);

/* Compiles a parsed program (the IR is only read), as ghci_gsi1_compile(). */
int ghci_gsi1_compile_ir(const struct drv_gpu_shader_ir *ir, const struct ghci_gsi1_options *options, struct ghci_gsi1_program **program, struct ghci_gsi1_diagnostic *diagnostic);

/* Frees a compiled program. */
void ghci_gsi1_program_free(struct ghci_gsi1_program *program);

#endif /* DRIVERS_GPU_GHCI_COMPILER_GSI1_COMPILE_H */
