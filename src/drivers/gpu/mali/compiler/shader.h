/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Mali Valhall (v10, Mali-G310) shader compiler: from a SPIR-V
 * module's compute, vertex or fragment entry point, through the shared
 * scalar IR, to Valhall programs and the description the driver needs to
 * run them.
 *
 * The shader ABI
 * --------------
 *
 * Fast access uniforms (FAU).  A program reads its uniforms from the FAU
 * words the driver points the stage at (the FAU pointer and its count of
 * 64-bit entries).  The program's words are, in order:
 *
 *   - words [0, push_bytes / 4): the push constants, word w holding the
 *     push constant bytes [4w, 4w + 4);
 *   - a vertex program: from the first even word after them, the six
 *     32-bit floats of the viewport transform at viewport_fau_word: the
 *     scale x, y, z, then the offset x, y, z (the hardware takes the
 *     position in screen space: the program divides by w, scales and
 *     offsets x, y and z, and writes 1 / w as the fourth component);
 *   - then, from the first even word on, one 64-bit entry for each buffer
 *     of the program's buffer table (struct mali_shader_buffer): the
 *     buffer's GPU address, low word first, at buffer.fau_word.  A uniform
 *     buffer's address is that of the byte its binding's offset names; a
 *     storage buffer's likewise.
 *
 * fau_words counts them all and is even; the driver gives the stage
 * fau_words / 2 entries.
 *
 * Resource tables.  Textures, samplers and vertex attributes are read
 * through the resource table the stage's resource table pointer names.
 * The compiler numbers what it reads; the driver writes the descriptors:
 *
 *   - table MALI_SHADER_TABLE_TEXTURES, entry k: the texture descriptor of
 *     the program's texture k (struct mali_shader_texture);
 *   - table MALI_SHADER_TABLE_SAMPLERS, entry k: the sampler descriptor of
 *     the same combined image sampler;
 *   - table MALI_SHADER_TABLE_ATTRIBUTES, entry L: a vertex program's
 *     attribute descriptor for input location L (the format, the offset and
 *     the buffer it reads, which the hardware converts to 32-bit floats or
 *     integers as the location's type says);
 *   - table MALI_SHADER_TABLE_VERTEX_BUFFERS, entry B: the buffer
 *     descriptor of Vulkan vertex binding B, which the attribute
 *     descriptors name.
 *
 * Vertex programs.  A vertex shader compiles to the two programs of an
 * index-driven vertex shading draw in one code buffer: the position
 * program at offset 0, which writes the position (and the point size),
 * and the varying program at varying_offset, which writes the generic
 * varyings.  Both find their output through the hardware's vertex output
 * buffer (resource table 61, entry 0) at the vertex's internal index the
 * hardware preloads in r59.  The position is four 32-bit floats at byte 0
 * of the position record; the point size a 16-bit float at byte 0 of the
 * attribute record.  Generic varying location L is four 32-bit words at
 * byte 16 L of the vertex's varying record: varying_bytes is the record's
 * size, which the draw gives the hardware (and the fragment program reads
 * at the same offsets, so the two stages agree by location alone).
 * A vertex program reads its vertex index and instance index from the
 * preloaded r60 and r61.
 *
 * Fragment programs.  A fragment program reads varying location L at
 * byte 16 L of the varying record, interpolated at the pixel's centre (or
 * flat, from the provoking vertex; a NoPerspective varying the vertex
 * program wrote multiplied by w, and the fragment program multiplies by
 * the fragment's 1 / w).  It ends by testing its coverage
 * (ATEST, with the hardware's alpha test datum, using colour 0's alpha)
 * and blending each colour location L it writes, as four 32-bit floats,
 * into render target L with blend descriptor L (the fixed function
 * blend); the last blend ends the program.
 *
 * Compute programs.  A compute program reads its local invocation id from
 * the preloaded r55 (x in bits 15:0, y in 31:16) and r56 (z in 15:0), and
 * its workgroup id from r57, r58 and r59.  gl_NumWorkGroups is a storage
 * buffer of the buffer table with set DRV_GPU_IR_SYSTEM_SET, binding 0,
 * holding the three group counts.  Workgroup shared memory is addressed
 * from the workgroup local storage pointer the driver sets.
 *
 * preload says which of r48..r63 the program reads as the hardware
 * delivers them (bit i for r(48 + i)), and work_registers how many
 * registers it uses (counted from r0, the preloaded ones included): the
 * driver puts both in the shader program descriptor.
 *
 * The same code runs on the build host.
 */

#ifndef KERN_DRIVERS_GPU_MALI_COMPILER_SHADER_H
#define KERN_DRIVERS_GPU_MALI_COMPILER_SHADER_H

#include "drivers/gpu/compiler/ir.h"

#include <stddef.h>
#include <stdint.h>

/* The stages the compiler takes. */
enum mali_shader_stage {
	MALI_SHADER_COMPUTE = 0,
	MALI_SHADER_VERTEX,
	MALI_SHADER_FRAGMENT
};

/* The resource tables a program reads (see the shader ABI above). */
#define MALI_SHADER_TABLE_TEXTURES		0U
#define MALI_SHADER_TABLE_SAMPLERS		1U
#define MALI_SHADER_TABLE_ATTRIBUTES		2U
#define MALI_SHADER_TABLE_VERTEX_BUFFERS	3U

/* The hardware's vertex output buffer table of an index-driven draw. */
#define MALI_SHADER_TABLE_VERTEX_OUTPUT		61U

/* The kinds of buffer a program binds. */
#define MALI_SHADER_BUFFER_UNIFORM		1U
#define MALI_SHADER_BUFFER_STORAGE		2U

/* The most entries of each table. */
#define MALI_SHADER_BUFFERS			16U
#define MALI_SHADER_TEXTURES			16U
#define MALI_SHADER_LOCATIONS			16U
#define MALI_SHADER_COLORS			8U

/* The bytes of one generic varying location in the varying record. */
#define MALI_SHADER_VARYING_STRIDE		16U

/* The byte alignment of a program's code, and of the varying program. */
#define MALI_SHADER_CODE_ALIGNMENT		128U

/*
 * One buffer the program binds: its (set, binding), its kind, the FAU word
 * its 64-bit address goes in, and for a uniform buffer the bytes the
 * program reads of it (from the binding's offset).
 */
struct mali_shader_buffer {
	uint32_t set;
	uint32_t binding;
	uint32_t kind;
	uint32_t fau_word;
	uint32_t size;
};

/*
 * One combined image sampler the program samples: its (set, binding) and
 * the entry of the texture and sampler tables its descriptors go in.
 */
struct mali_shader_texture {
	uint32_t set;
	uint32_t binding;
	uint32_t index;
};

/*
 * One compiled program.
 *
 * Made by mali_shader_compile() and owned by the caller, who copies the
 * code into GPU memory (aligned to MALI_SHADER_CODE_ALIGNMENT) and frees
 * the program with mali_shader_program_free().  The code ends with the
 * padding the instruction prefetch may read.
 */
struct mali_shader_program {
	/* The stage the program runs in (enum mali_shader_stage). */
	uint32_t stage;

	/* The code, and its bytes (every variant and the padding). */
	uint8_t *code;
	size_t length;

	/*
	 * The registers the program uses, counted from r0 (more than 32 needs
	 * the descriptor's 64-register allocation), and the preloaded
	 * registers it reads (bit i for r(48 + i)).  A vertex program's
	 * varying program has its own.
	 */
	uint32_t work_registers;
	uint32_t preload;

	/*
	 * A vertex program: the varying program's offset in the code (0 when
	 * the shader writes no generic varying, the draw then runs only the
	 * position program), its registers and its preloads.
	 */
	size_t varying_offset;
	uint32_t varying_work_registers;
	uint32_t varying_preload;

	/*
	 * The FAU words the driver writes, an even number, the push constant
	 * bytes among them, and a vertex program's first viewport word.
	 */
	uint32_t fau_words;
	uint32_t push_bytes;
	uint32_t viewport_fau_word;

	/* The buffers the program binds, whose addresses the FAU words carry. */
	struct mali_shader_buffer buffers[MALI_SHADER_BUFFERS];
	uint32_t buffer_count;

	/* The combined image samplers the program samples, by texture table entry. */
	struct mali_shader_texture textures[MALI_SHADER_TEXTURES];
	uint32_t texture_count;

	/*
	 * A vertex program: the input locations it reads (bit L), those of
	 * them read as integers rather than floats, the generic varying
	 * locations it writes (bit L), the varying record's bytes, and
	 * whether it writes the point size.
	 */
	uint32_t attribute_mask;
	uint32_t integer_attribute_mask;
	uint32_t varying_mask;
	uint32_t varying_bytes;
	uint32_t writes_point_size;

	/*
	 * A vertex program: whether it reads its vertex index and its instance
	 * index.
	 */
	uint32_t reads_vertex_index;
	uint32_t reads_instance_index;

	/*
	 * A fragment program: the varying locations it reads (bit L) and
	 * those read flat, the colour locations it writes (bit L, blended to
	 * render target L), whether it discards, and whether it reads the
	 * fragment coordinate.
	 */
	uint32_t input_mask;
	uint32_t flat_mask;
	uint32_t color_mask;
	uint32_t discards;
	uint32_t reads_frag_coord;

	/*
	 * A compute program: its workgroup size, the shared memory bytes of a
	 * workgroup, and whether it waits at a workgroup barrier.
	 */
	uint32_t local_size[3];
	uint32_t shared_bytes;
	uint32_t uses_barrier;
};

/*
 * What a pipeline asks of a compile beyond the module.  Flags is zero
 * today; the record is the place for later keys.
 */
struct mali_shader_options {
	uint32_t flags;
};

/*
 * Why a program was refused: the IR instruction (or the SPIR-V word
 * offset, for a refusal of the frontend) and an immutable reason.
 */
struct mali_shader_diagnostic {
	uint32_t instruction;
	const char *reason;
};

/*
 * Compiles a SPIR-V module's stage with the options (NULL for none): 0
 * with the program, EINVAL or ENOTSUP with a reason, ENOSPC when the
 * registers run out, or ENOMEM.
 */
int mali_shader_compile(const uint32_t *words, size_t word_count, uint32_t stage, const struct mali_shader_options *options, struct mali_shader_program **program, struct mali_shader_diagnostic *diagnostic);

/* Compiles a parsed program (the IR is only read), as mali_shader_compile(). */
int mali_shader_compile_ir(const struct drv_gpu_shader_ir *ir, const struct mali_shader_options *options, struct mali_shader_program **program, struct mali_shader_diagnostic *diagnostic);

/* Frees a compiled program. */
void mali_shader_program_free(struct mali_shader_program *program);

#endif /* KERN_DRIVERS_GPU_MALI_COMPILER_SHADER_H */
