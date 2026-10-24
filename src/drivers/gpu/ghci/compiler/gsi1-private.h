/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GSI1 compiler's state and the functions its files share.
 *
 * gsi1-compile.c drives a compilation: it checks the IR, finds how long
 * each value lives, hands out the registers, lowers the arithmetic and
 * the control flow, and lays out the binary.  gsi1-io.c lowers the
 * inputs and outputs of the vertex and fragment stages, gsi1-memory.c
 * the uniforms, the buffers, the shared memory and the images, and
 * gsi1-texture.c the sampled images.
 */

#ifndef DRIVERS_GPU_GHCI_COMPILER_GSI1_PRIVATE_H
#define DRIVERS_GPU_GHCI_COMPILER_GSI1_PRIVATE_H

#include "gsi1-compile.h"
#include "gsi1-encode.h"
#include "gsi1_isa.h"

#include "drivers/gpu/compiler/ir.h"

#include <stddef.h>
#include <stdint.h>

/* A value or an input without an operand, and an IR instruction or label that is not there. */
#define GSI1_NO_OPERAND			0xffU
#define GSI1_NOT_DEFINED		UINT32_MAX

/* The deepest loop nest and skip nest the compiler follows. */
#define GSI1_LOOP_DEPTH			16U
#define GSI1_SKIP_DEPTH			32U

/* The most values one IR instruction reads, and the scratch registers it may take. */
#define GSI1_MAX_SOURCES		DRV_GPU_IR_TEXTURE_MAX_PARAMS
#define GSI1_SCRATCH_MAX		16U

/* The most interface entries of a program and bindings of its uniforms. */
#define GSI1_INTERFACE_MAX		96U

/* The instructions the code list grows by when it is full, and the labels likewise. */
#define GSI1_LIST_GROWTH		256U
#define GSI1_LABEL_GROWTH		64U

/*
 * The output channels a program stores, by number.  A vertex program:
 * the position (0-3), the point size (4), and four channels for each
 * location from 8.  A fragment program: four for each colour attachment
 * (0-15), the depth (16) and the sample mask (17).
 */
#define GSI1_OUTPUT_POSITION		0U
#define GSI1_OUTPUT_POINT_SIZE		4U
#define GSI1_OUTPUT_LOCATIONS		8U
#define GSI1_OUTPUT_COLOR		0U
#define GSI1_OUTPUT_DEPTH		16U
#define GSI1_OUTPUT_SAMPLE_MASK		17U
#define GSI1_OUTPUT_CHANNELS		(GSI1_OUTPUT_LOCATIONS + 4U * GHCI_GSI1_LOCATIONS)

/* The IR instruction index that stands for "after the last instruction" (the exports at the end). */
#define GSI1_END_OF_PROGRAM(ir)		((ir)->instruction_count + 1U)

/*
 * How a value is reached.
 *
 * A register value lives in a register from its definition to its last
 * read.  An operand value is a special operand or a constant-bank word
 * that is always there.  A constant is put into a scratch register where
 * an instruction reads it, so that a constant the parser shares over the
 * whole program does not hold a register all along.
 */
enum gsi1_value_kind {
	GSI1_VALUE_NONE = 0,
	GSI1_VALUE_REGISTER,
	GSI1_VALUE_OPERAND,
	GSI1_VALUE_CONSTANT
};

/*
 * The registers the hardware or the program's start fills, which IR
 * instructions read: a fragment warp's barycentrics, depth, position and
 * sample mask, the centroid barycentrics, the screen-linear barycentrics
 * and the reciprocal of w the start computes, and a vertex warp's system
 * values and attribute components.
 */
enum gsi1_input {
	GSI1_INPUT_B0 = 0,
	GSI1_INPUT_B1,
	GSI1_INPUT_B2,
	GSI1_INPUT_Z,
	GSI1_INPUT_X,
	GSI1_INPUT_Y,
	GSI1_INPUT_SAMPLE_MASK,
	GSI1_INPUT_CENTROID_B0,
	GSI1_INPUT_CENTROID_B1,
	GSI1_INPUT_CENTROID_B2,
	GSI1_INPUT_LINEAR_B0,
	GSI1_INPUT_LINEAR_B1,
	GSI1_INPUT_LINEAR_B2,
	GSI1_INPUT_LINEAR_CENTROID_B0,
	GSI1_INPUT_LINEAR_CENTROID_B1,
	GSI1_INPUT_LINEAR_CENTROID_B2,
	GSI1_INPUT_RECIPROCAL_W,
	GSI1_INPUT_VERTEX_INDEX,
	GSI1_INPUT_INSTANCE_INDEX,
	GSI1_INPUT_ATTRIBUTE,
	GSI1_INPUT_COUNT = GSI1_INPUT_ATTRIBUTE + 4 * GHCI_GSI1_LOCATIONS
};

/*
 * How the program reaches one uniform of the IR's list: its interface
 * kind, the constant-bank word of its block or its descriptor, a sampled
 * image's texture slot, a uniform block's words and the buffer's first
 * word they come from, and an image's VkFormat and samples.
 */
struct gsi1_binding {
	uint32_t kind;
	uint32_t bank;
	uint32_t slot;
	uint32_t words;
	uint32_t first;
	uint32_t format;
	uint32_t samples;
	uint32_t count_bank;
};

/*
 * The state of one compilation.
 *
 * Lives on the stack of ghci_gsi1_compile_ir(); its arrays are allocated
 * there and freed before it returns.
 */
struct gsi1_lowering {
	/* The program, the options it is compiled with, and where a refusal is described. */
	const struct drv_gpu_shader_ir *ir;
	struct ghci_gsi1_options options;
	struct ghci_gsi1_diagnostic *diagnostic;

	/* The IR instruction being lowered. */
	uint32_t index;

	/*
	 * Each value: how it is reached, its operand (a register, a special
	 * operand or a bank word), its bits when it is a constant, the IR
	 * instruction that last reads it plus one (0: none; the end of the
	 * program for a value the exports read), and whether a MOVE writes
	 * it (it then always has a register of its own).
	 */
	uint8_t *kind;
	uint8_t *operand;
	uint32_t *constant;
	uint32_t *last_use;
	uint8_t *moved;

	/* Each IR instruction whose results nothing reads and that has no other effect: it is not lowered. */
	uint8_t *dead;

	/* The registers a lane has (at most 63: r63 is the systolic queue), which are taken, and one past the highest ever taken. */
	uint32_t register_limit;
	uint8_t taken[GSI1_REGISTERS_MAX];
	uint32_t register_highest;

	/*
	 * Each input: its register (GSI1_NO_OPERAND when the program has none)
	 * and the IR instruction after which nothing reads it, plus one (0:
	 * only the start).  An input's register goes back at that point.
	 */
	uint8_t input_reg[GSI1_INPUT_COUNT];
	uint32_t input_until[GSI1_INPUT_COUNT];

	/* The scratch registers taken for the IR instruction being lowered, given back after it. */
	uint32_t scratch[GSI1_SCRATCH_MAX];
	uint32_t scratch_count;

	/* The code: instructions, room, and each instruction's branch label plus one (0: not a branch). */
	uint64_t *code;
	uint32_t *branch_label;
	uint32_t count;
	uint32_t capacity;

	/* The labels: each one's instruction index (GSI1_NOT_DEFINED until placed), and room. */
	uint32_t *label_at;
	uint32_t label_count;
	uint32_t label_capacity;

	/* Nonzero when a list could not grow. */
	int out_of_memory;

	/* The loops being lowered: each one's label at its start and the register its EXEC is saved in, innermost last. */
	uint32_t loop_label[GSI1_LOOP_DEPTH];
	uint32_t loop_save[GSI1_LOOP_DEPTH];
	uint32_t loop_depth;

	/* The depth of the skippable blocks being lowered. */
	uint32_t skip_depth;

	/* Each uniform of the IR's list: how the program reaches it. */
	struct gsi1_binding *binding;

	/* The constant-bank words the program uses, the first push-constant word, and the texture slots taken. */
	uint32_t bank_used;
	uint32_t push_bank;
	uint32_t slots_used;

	/* The interface table, four words to an entry. */
	uint32_t interface[GSI1_INTERFACE_MAX * GSI1_IFACE_WORDS];
	uint32_t interface_count;

	/* The header's flags. */
	uint32_t flags;

	/*
	 * Each output channel: the value its export reads where it lives
	 * (GSI1_NOT_DEFINED for none), when the channel is stored once
	 * outside any loop; otherwise the register the stores copy into
	 * (GSI1_NO_OPERAND until the first store takes one, kept to the
	 * end).  And whether the program stores it at all.
	 */
	uint32_t output_value[GSI1_OUTPUT_CHANNELS];
	uint8_t output_reg[GSI1_OUTPUT_CHANNELS];
	uint8_t output_written[GSI1_OUTPUT_CHANNELS];

	/* A vertex or fragment program: each location's first varying word (GHCI_GSI1_NO_WORD for none). */
	uint32_t varying_word[GHCI_GSI1_LOCATIONS];
};

/* Describes a refusal at the instruction being lowered and returns its error. */
int gsi1_refuse(struct gsi1_lowering *lowering, int error, const char *reason);

/* Lists the values an IR instruction reads; -1 for an operation the compiler does not lower. */
int gsi1_ir_sources(const struct drv_gpu_shader_ir_inst *instruction, uint32_t *sources);

/* Reports how many values an IR instruction defines from its destination: 0, 1 or 4. */
int gsi1_ir_defines(const struct drv_gpu_shader_ir_inst *instruction);

/* Appends one instruction to the code. */
void gsi1_emit(struct gsi1_lowering *lowering, uint64_t instruction);

/* Appends a branch to a label. */
void gsi1_emit_branch(struct gsi1_lowering *lowering, uint32_t funct3, uint32_t rs1, uint32_t label);

/* Makes a new label, not yet placed; GSI1_NOT_DEFINED when the table could not grow. */
uint32_t gsi1_new_label(struct gsi1_lowering *lowering);

/* Places a label at the next instruction. */
void gsi1_place_label(struct gsi1_lowering *lowering, uint32_t label);

/* Takes the lowest free register for the whole of a value's life. */
int gsi1_take_register(struct gsi1_lowering *lowering, uint32_t *reg);

/* Takes the lowest run of free registers. */
int gsi1_take_block(struct gsi1_lowering *lowering, uint32_t count, uint32_t *reg);

/* Gives a register back. */
void gsi1_give_register(struct gsi1_lowering *lowering, uint32_t reg);

/* Takes a scratch register for the instruction being lowered. */
int gsi1_scratch(struct gsi1_lowering *lowering, uint32_t *reg);

/* Takes a run of scratch registers for the instruction being lowered. */
int gsi1_scratch_block(struct gsi1_lowering *lowering, uint32_t count, uint32_t *reg);

/* Reports the scratch registers taken so far, for gsi1_scratch_release(). */
uint32_t gsi1_scratch_mark(const struct gsi1_lowering *lowering);

/* Gives back the scratch registers taken after a mark. */
void gsi1_scratch_release(struct gsi1_lowering *lowering, uint32_t mark);

/* Finds the operand an instruction reads a value through, putting a constant into a scratch register. */
int gsi1_source(struct gsi1_lowering *lowering, uint32_t value, uint32_t *operand);

/* Reports the register a value defined by the instruction being lowered lives in. */
uint32_t gsi1_value_register(const struct gsi1_lowering *lowering, uint32_t value);

/* Reports whether a value is a constant, with its bits. */
int gsi1_value_constant(const struct gsi1_lowering *lowering, uint32_t value, uint32_t *bits);

/* Emits a copy of an operand into a register. */
void gsi1_emit_move(struct gsi1_lowering *lowering, uint32_t rd, uint32_t operand);

/* Emits a register set to a constant. */
void gsi1_emit_constant(struct gsi1_lowering *lowering, uint32_t rd, uint32_t bits);

/* Emits an integer instruction of the OP opcode. */
void gsi1_emit_alu(struct gsi1_lowering *lowering, uint32_t funct3, uint32_t funct7, uint32_t rd, uint32_t rs1, uint32_t rs2);

/* Emits an integer instruction of the OP-IMM opcode with a 22-bit immediate. */
void gsi1_emit_alu_immediate(struct gsi1_lowering *lowering, uint32_t funct3, uint32_t rd, uint32_t rs1, int32_t immediate);

/* Emits a float instruction of the OP-FP opcode with its modifiers. */
void gsi1_emit_float(struct gsi1_lowering *lowering, uint32_t funct7, uint32_t funct3, uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t modifiers);

/* Emits a fused multiply-add family instruction. */
void gsi1_emit_fused(struct gsi1_lowering *lowering, uint32_t opcode, uint32_t rd, uint32_t rs1, uint32_t rs2, uint32_t rs3, uint32_t modifiers);

/* Emits one of the transcendental unit's operations. */
void gsi1_emit_transcendental(struct gsi1_lowering *lowering, uint32_t sub, uint32_t rd, uint32_t rs1);

/* Emits an exec.* operation: exec.save writes rd, the others read rs1. */
void gsi1_emit_exec(struct gsi1_lowering *lowering, uint32_t sub, uint32_t operand);

/* Computes a buffer access's address, the offset clamped to the buffer when asked; the buffer's {address, bytes} are bank words. */
int gsi1_buffer_address(struct gsi1_lowering *lowering, uint32_t bank, uint32_t offset, int clamp, uint32_t *address);

/* Reads a constant-bank word as an operand: c0-c31 directly, a later word through a scratch register. */
int gsi1_bank_operand(struct gsi1_lowering *lowering, uint32_t bank, uint32_t *operand);

/* Adds an entry to the interface table. */
int gsi1_add_interface(struct gsi1_lowering *lowering, uint32_t kind, uint32_t set, uint32_t binding, uint32_t specific);

/* Finds the interface record of an input location; NULL when the program has none. */
const struct drv_gpu_shader_ir_io *gsi1_find_input(const struct drv_gpu_shader_ir *ir, uint32_t location);

/* Finds the interface record of an output location; NULL when the program has none. */
const struct drv_gpu_shader_ir_io *gsi1_find_output(const struct drv_gpu_shader_ir *ir, uint32_t location);

/* gsi1-io.c: lists the inputs an IR instruction reads, as GSI1_INPUT_* numbers. */
uint32_t gsi1_io_inputs(const struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t *inputs);

/* gsi1-io.c: checks a graphics program's inputs and outputs and plans their registers and varying words. */
int gsi1_io_prepare(struct gsi1_lowering *lowering);

/* gsi1-io.c: plans which output channels are exported straight from the value stored. */
void gsi1_io_plan_outputs(struct gsi1_lowering *lowering);

/* gsi1-io.c: takes the inputs' registers and computes those the start makes. */
int gsi1_io_prologue(struct gsi1_lowering *lowering);

/* gsi1-io.c: lowers the read of an input. */
int gsi1_io_lower_input(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);

/* gsi1-io.c: lowers the write of an output. */
int gsi1_io_lower_output(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);

/* gsi1-io.c: emits the exports at the end of the program. */
int gsi1_io_finish(struct gsi1_lowering *lowering);

/* gsi1-memory.c: lays out the constant bank and the texture slots of the uniforms. */
int gsi1_memory_prepare(struct gsi1_lowering *lowering);

/* gsi1-memory.c: lowers a uniform word, a buffer access, an atomic, a shared access or a synchronization. */
int gsi1_memory_lower(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);

/* gsi1-image.c: lowers a read or a write of a storage image, a texel buffer or an input attachment. */
int gsi1_image_lower(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);

/* gsi1-image.c: computes an image atomic's texel address and whether it is inside the image. */
int gsi1_image_texel(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, const struct gsi1_binding *binding, uint32_t *address, uint32_t *inside);

/* gsi1-texture.c: lowers a sample, a fetch, a gather or a query of a sampled image. */
int gsi1_texture_lower(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);

#endif /* DRIVERS_GPU_GHCI_COMPILER_GSI1_PRIVATE_H */
