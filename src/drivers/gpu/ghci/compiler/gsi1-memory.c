/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GSI1 compiler's uniforms, buffers, shared memory and
 * synchronization.
 *
 * Everything a program binds is reached through the constant bank, whose
 * words the command processor fills from the descriptors as the interface
 * table says: the push constants from word 0 (so that they are the c0-c31
 * operands), then two words for each storage buffer ({address, bytes}) and
 * the words each storage image, texel buffer and input attachment needs,
 * then the words of the uniform blocks the program reads.  A uniform block
 * that does not fit is read from memory with lw, through two words of its
 * own like a storage buffer's.  Each sampled image takes a texture slot.
 *
 * A buffer access adds the byte offset to the buffer's address.  With
 * robustBufferAccess, and for a load the IR does not predicate in a
 * graphics stage, the offset is first clamped to the buffer's last word,
 * so that an access stays within the range the descriptor bound.  An
 * access the IR predicates narrows EXEC to the predicate's lanes for that
 * one instruction.
 */

#include "gsi1-private.h"

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <stddef.h>
#include <stdint.h>

/* The words a storage buffer's (or a uniform buffer read from memory's) descriptor takes: its address and its bytes. */
#define GSI1_BUFFER_WORDS		2U

/* The words of a storage image's, a storage texel buffer's and an input attachment's descriptor. */
#define GSI1_STORAGE_IMAGE_WORDS	5U
#define GSI1_TEXEL_BUFFER_WORDS		2U
#define GSI1_INPUT_ATTACHMENT_WORDS	3U

/* The texture slots of a GPU (CAPS2 says how many it has; GSI1 has room for 16). */
#define GSI1_TEXTURE_SLOTS		16U

static int place_descriptors(struct gsi1_lowering *lowering);
static int place_blocks(struct gsi1_lowering *lowering);
static int reads_texel_buffer(const struct gsi1_lowering *lowering, uint32_t uniform);
static int take_bank(struct gsi1_lowering *lowering, uint32_t words, uint32_t *first);
static int lower_uniform_word(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_storage(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_atomic(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_shared(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int shared_atomic_address(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t *address);
static int lower_sync(struct gsi1_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int atomic_sub(uint32_t operation, uint32_t *sub);
static int storage_binding(struct gsi1_lowering *lowering, uint32_t uniform, const struct gsi1_binding **binding);
static int predicate_begin(struct gsi1_lowering *lowering, uint32_t predicate, uint32_t *save);
static void predicate_end(struct gsi1_lowering *lowering, uint32_t save);
static void emit_fence(struct gsi1_lowering *lowering, uint32_t fences);

/*
 * Lays out the constant bank and the texture slots: the push constants,
 * the descriptors, then the uniform blocks.
 */
int
gsi1_memory_prepare(
	struct gsi1_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	uint32_t push_words;
	int error;

	/* Names the module being laid out. */
	ir = lowering->ir;

	/*
	 * The push constants from the first word the program may use (word 0
	 * unless the pipeline's other stage takes the words below: one bank
	 * serves the vertex and the fragment warps of a draw).
	 */
	if (lowering->options.bank_first >= lowering->options.bank_words) {
		error = gsi1_refuse(lowering, EINVAL, "the first constant-bank word is past the bank");
		return error;
	}
	lowering->push_bank = lowering->options.bank_first;
	push_words = (ir->push_bytes + 3U) / 4U;
	if (push_words > lowering->options.bank_words - lowering->push_bank) {
		error = gsi1_refuse(lowering, ENOTSUP, "more push constants than the constant bank holds");
		return error;
	}
	lowering->bank_used = lowering->push_bank + push_words;

	/* The push constants' entry. */
	if (push_words != 0U) {
		error = gsi1_add_interface(lowering, GSI1_KIND_PUSH_CONSTANT, 0U, 0U, lowering->push_bank);
		if (error != 0)
			return error;
	}

	/* The descriptors and the texture slots. */
	error = place_descriptors(lowering);
	if (error != 0)
		return error;

	/* The uniform blocks. */
	error = place_blocks(lowering);
	if (error != 0)
		return error;

	/* A compute program's shared memory. */
	if (ir->stage == DRV_GPU_STAGE_COMPUTE && ir->shared_bytes != 0U) {
		error = gsi1_add_interface(lowering, GSI1_KIND_SHARED, 0U, 0U, ir->shared_bytes);
		if (error != 0)
			return error;
		lowering->flags |= GSI1_FLAG_LDS;
	}

	/* A barrier needs the workgroup's warps together. */
	if (ir->uses_barrier != 0U)
		lowering->flags |= GSI1_FLAG_LDS;

	/* Succeeded: every uniform has its place. */
	return 0;
}

/*
 * Lowers a uniform word, a buffer access, an atomic, a shared access or a
 * synchronization.
 */
int
gsi1_memory_lower(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	int error;

	/* The operation chooses the lowering. */
	switch (instruction->op) {
	case DRV_GPU_IR_LOAD_PUSH:
	case DRV_GPU_IR_LOAD_UBO:
		error = lower_uniform_word(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORAGE_SIZE:
		error = lower_storage(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_ATOMIC:
		error = lower_atomic(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOAD_SHARED:
	case DRV_GPU_IR_STORE_SHARED:
		error = lower_shared(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		error = lower_sync(lowering, instruction);
		break;
	case DRV_GPU_IR_IMAGE_READ:
	case DRV_GPU_IR_IMAGE_WRITE:
		error = gsi1_image_lower(lowering, instruction, dst);
		break;
	default:
		error = gsi1_refuse(lowering, ENOTSUP, "the operation is not compiled for GSI1");
		break;
	}

	/* Reports why the access could not be lowered. */
	if (error != 0)
		return error;

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Reads a constant-bank word as an operand: c0-c31 directly, a later word
 * through a scratch register (lwc).
 */
int
gsi1_bank_operand(
	struct gsi1_lowering *lowering,
	uint32_t bank,
	uint32_t *operand)
{
	uint32_t reg;
	int error;

	/* The first 32 words are operands of their own. */
	if (bank < GSI1_C_COUNT) {
		*operand = GSI1_C(bank);
		return 0;
	}

	/* Takes a scratch register for a later word. */
	error = gsi1_scratch(lowering, &reg);
	if (error != 0)
		return error;

	/* lwc reads word rs1 + immediate: zero plus the word. */
	gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LWC, reg, GSI1_ZERO, (int32_t)bank, 0U));

	/* Succeeded: the word is in the scratch register. */
	*operand = reg;
	return 0;
}

/*
 * Computes the address of a buffer access into a scratch register: the
 * buffer's address (bank word `bank`) plus the byte offset (an operand),
 * the offset first clamped to the buffer's last word (bank word bank + 1
 * holds its bytes) when `clamp` is nonzero.
 */
int
gsi1_buffer_address(
	struct gsi1_lowering *lowering,
	uint32_t bank,
	uint32_t offset,
	int clamp,
	uint32_t *address)
{
	uint32_t base;
	uint32_t bytes;
	uint32_t reg;
	int error;

	/* Takes the scratch register of the address. */
	error = gsi1_scratch(lowering, &reg);
	if (error != 0)
		return error;

	/* The clamp: the offset of the buffer's last word is its bytes less four. */
	if (clamp != 0) {
		error = gsi1_bank_operand(lowering, bank + 1U, &bytes);
		if (error != 0)
			return error;

		/* minu(offset, bytes - 4). */
		gsi1_emit_alu_immediate(lowering, GSI1_F3_ADD, reg, bytes, -4);
		gsi1_emit_alu(lowering, GSI1_F3_MINU, GSI1_F7_MINMAX, reg, offset, reg);
		offset = reg;
	}

	/* The buffer's address. */
	error = gsi1_bank_operand(lowering, bank, &base);
	if (error != 0)
		return error;

	/* address = base + offset. */
	gsi1_emit_alu(lowering, GSI1_F3_ADD, GSI1_F7_BASE, reg, base, offset);

	/* Succeeded: the address is in the scratch register. */
	*address = reg;
	return 0;
}

/*
 * Places the descriptors in the constant bank and gives the sampled images
 * their texture slots, in the order of the IR's uniform list.
 */
static int
place_descriptors(
	struct gsi1_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct gsi1_binding *binding;
	uint32_t index;
	uint32_t specific;
	uint32_t words;
	uint32_t kind;
	int buffer;
	int error;

	/* Names the module being laid out. */
	ir = lowering->ir;

	/* Each uniform but the uniform blocks. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		binding = &lowering->binding[index];
		lowering->index = index;

		/* The system buffer of a compute program is read through special operands. */
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE && uniform->set == DRV_GPU_IR_SYSTEM_SET)
			continue;

		/* A sampled image takes the next texture slot. */
		if (uniform->kind == DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE) {
			if (lowering->slots_used == GSI1_TEXTURE_SLOTS) {
				error = gsi1_refuse(lowering, ENOTSUP, "more sampled images than the texture slots");
				return error;
			}
			binding->kind = GSI1_KIND_SAMPLED_IMAGE;
			binding->count_bank = GSI1_NOT_DEFINED;
			binding->slot = lowering->slots_used;
			lowering->slots_used++;
			specific = binding->slot;

			/* A uniform texel buffer also has a bank word of its element count. */
			buffer = reads_texel_buffer(lowering, index);
			if (buffer != 0) {
				error = take_bank(lowering, 1U, &binding->count_bank);
				if (error != 0)
					return error;
				specific |= GSI1_TEX_BUFFER | (binding->count_bank << GSI1_TEX_COUNT_SHIFT);
			}

			/* The image's entry. */
			error = gsi1_add_interface(lowering, GSI1_KIND_SAMPLED_IMAGE, uniform->set, uniform->binding, specific);
			if (error != 0)
				return error;
			continue;
		}

		/* The descriptor words of the other kinds. */
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
			kind = GSI1_KIND_STORAGE_BUFFER;
			words = GSI1_BUFFER_WORDS;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE_IMAGE) {
			kind = GSI1_KIND_STORAGE_IMAGE;
			words = GSI1_STORAGE_IMAGE_WORDS;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_TEXEL_BUFFER) {
			kind = GSI1_KIND_TEXEL_BUFFER;
			words = GSI1_TEXEL_BUFFER_WORDS;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_INPUT_ATTACHMENT) {
			kind = GSI1_KIND_INPUT_ATTACHMENT;
			words = GSI1_INPUT_ATTACHMENT_WORDS;
		} else {
			continue;
		}

		/* Takes the descriptor's words. */
		error = take_bank(lowering, words, &binding->bank);
		if (error != 0)
			return error;
		binding->kind = kind;
		binding->format = uniform->format;
		binding->samples = uniform->multisampled;

		/* The descriptor's entry; an input attachment is named by its index as well. */
		error = gsi1_add_interface(lowering, kind, uniform->set, uniform->binding, binding->bank);
		if (error != 0)
			return error;
	}

	/* Succeeded: the descriptors have their words and slots. */
	lowering->index = 0U;
	return 0;
}

/*
 * Places the uniform blocks: in the bank while it has room for them and
 * for the two words each later block would take if it did not fit, in
 * memory otherwise.
 */
static int
place_blocks(
	struct gsi1_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct gsi1_binding *binding;
	uint32_t remaining;
	uint32_t index;
	uint32_t words;
	uint32_t first;
	uint32_t specific;
	int error;

	/* Names the module being laid out. */
	ir = lowering->ir;

	/* The blocks the program reads, counted for the room they may take in memory. */
	remaining = 0U;
	for (index = 0U; index < ir->uniform_count; index++) {
		if (ir->uniforms[index].kind == DRV_GPU_IR_UNIFORM_BLOCK)
			remaining++;
	}

	/* Each uniform block in turn. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		binding = &lowering->binding[index];
		lowering->index = index;
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			continue;
		remaining--;

		/* The words of the bytes it reads, from its first word read. */
		first = uniform->offset / 4U;
		words = (uniform->offset + uniform->size + 3U) / 4U - first;

		/* A block the program does not read takes nothing. */
		if (uniform->size == 0U)
			continue;

		/* The block goes into the bank while room is left for the others' memory words. */
		if (words <= GSI1_UBO_WORDS_MASK &&
		    first <= GSI1_UBO_FIRST_MASK &&
		    lowering->bank_used + words + GSI1_BUFFER_WORDS * remaining <= lowering->options.bank_words &&
		    lowering->bank_used + words <= GSI1_UBO_BANK_MASK + 1U) {
			binding->kind = GSI1_KIND_UNIFORM_BLOCK;
			binding->bank = lowering->bank_used;
			binding->words = words;
			binding->first = first;
			lowering->bank_used += words;

			/* The block's entry: its bank word, its words, and the buffer's word they come from. */
			specific = binding->bank;
			specific |= words << GSI1_UBO_WORDS_SHIFT;
			specific |= first << GSI1_UBO_FIRST_SHIFT;
			error = gsi1_add_interface(lowering, GSI1_KIND_UNIFORM_BLOCK, uniform->set, uniform->binding, specific);
			if (error != 0)
				return error;
			continue;
		}

		/* Otherwise the block is read from memory, through its address and its bytes. */
		error = take_bank(lowering, GSI1_BUFFER_WORDS, &binding->bank);
		if (error != 0)
			return error;
		binding->kind = GSI1_KIND_UNIFORM_MEMORY;

		/* The block's entry. */
		error = gsi1_add_interface(lowering, GSI1_KIND_UNIFORM_MEMORY, uniform->set, uniform->binding, binding->bank);
		if (error != 0)
			return error;
	}

	/* Succeeded: the blocks have their places. */
	lowering->index = 0U;
	return 0;
}

/* Reports whether a texture instruction fetches a sampled image as a texel buffer. */
static int
reads_texel_buffer(
	const struct gsi1_lowering *lowering,
	uint32_t uniform)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t index;

	/* Names the module being scanned. */
	ir = lowering->ir;

	/* Each texture instruction of the uniform's set and binding. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_TEXTURE)
			continue;
		if (instruction->location != ir->uniforms[uniform].set)
			continue;
		if (instruction->immediate != ir->uniforms[uniform].binding)
			continue;
		if ((instruction->image_shape & DRV_GPU_IR_IMAGE_KIND_MASK) == DRV_GPU_IR_IMAGE_BUFFER)
			return 1;
	}

	/* No instruction reads it as a buffer. */
	return 0;
}

/* Takes words of the constant bank. */
static int
take_bank(
	struct gsi1_lowering *lowering,
	uint32_t words,
	uint32_t *first)
{
	int error;

	/* The bank has a bound. */
	if (lowering->bank_used + words > lowering->options.bank_words) {
		error = gsi1_refuse(lowering, ENOTSUP, "the program binds more than the constant bank holds");
		return error;
	}

	/* The words are the caller's. */
	*first = lowering->bank_used;
	lowering->bank_used += words;

	/* Succeeded: the words are taken. */
	return 0;
}

/*
 * Lowers a push-constant word or a uniform block's word that is not a c
 * operand: lwc from the bank, or lw from a block read from memory.
 */
static int
lower_uniform_word(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir *ir;
	const struct gsi1_binding *binding;
	uint32_t word;
	uint32_t base;
	int error;

	/* A word that is a c operand needs no instruction. */
	if (dst == GSI1_NO_OPERAND)
		return 0;
	ir = lowering->ir;

	/* A push-constant word: its bank word. */
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH) {
		word = lowering->push_bank + instruction->immediate / 4U;
		gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LWC, dst, GSI1_ZERO, (int32_t)word, 0U));
		return 0;
	}

	/* A uniform block's word: its block is one of the program's. */
	if (instruction->location >= ir->uniform_count) {
		error = gsi1_refuse(lowering, EINVAL, "a uniform word of a block the program does not have");
		return error;
	}
	binding = &lowering->binding[instruction->location];

	/* A block read from memory: lw from its address. */
	if (binding->kind == GSI1_KIND_UNIFORM_MEMORY) {
		error = gsi1_bank_operand(lowering, binding->bank, &base);
		if (error != 0)
			return error;
		gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LW, dst, base, (int32_t)(instruction->immediate & ~3U), 0U));
		return 0;
	}

	/* A word outside what the block copies is malformed. */
	if (binding->kind != GSI1_KIND_UNIFORM_BLOCK ||
	    instruction->immediate < ir->uniforms[instruction->location].offset) {
		error = gsi1_refuse(lowering, EINVAL, "a uniform word outside the block's range");
		return error;
	}

	/* The bank word the block's word was copied to. */
	word = binding->bank + (instruction->immediate - ir->uniforms[instruction->location].offset) / 4U;
	gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LWC, dst, GSI1_ZERO, (int32_t)word, 0U));

	/* Succeeded: the uniform word is in dst. */
	return 0;
}

/*
 * Lowers a storage buffer's load, store and size: lw and sw at the
 * buffer's address plus the offset, predicated and clamped as the
 * header comment says; the size is the descriptor's second word.
 */
static int
lower_storage(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct gsi1_binding *binding;
	uint32_t offset;
	uint32_t data;
	uint32_t address;
	uint32_t bytes;
	uint32_t predicate;
	uint32_t save;
	int predicated;
	int clamp;
	int error;

	/* A group count read through a special operand needs no instruction. */
	if (instruction->op == DRV_GPU_IR_LOAD_STORAGE && dst == GSI1_NO_OPERAND)
		return 0;

	/* The buffer. */
	binding = NULL;
	error = storage_binding(lowering, instruction->location, &binding);
	if (error != 0)
		return error;

	/* The size: the descriptor's bytes. */
	if (instruction->op == DRV_GPU_IR_STORAGE_SIZE) {
		error = gsi1_bank_operand(lowering, binding->bank + 1U, &bytes);
		if (error != 0)
			return error;
		gsi1_emit_move(lowering, dst, bytes);
		return 0;
	}

	/* The offset. */
	error = gsi1_source(lowering, instruction->src[0], &offset);
	if (error != 0)
		return error;

	/*
	 * The clamp: always with robustBufferAccess, and for a load a graphics
	 * stage does not predicate (its lanes outside the block may hold any
	 * offset).
	 */
	predicated = 0;
	if (instruction->component != 0U)
		predicated = 1;
	clamp = 0;
	if (lowering->options.robust_buffer_access != 0U)
		clamp = 1;
	if (instruction->op == DRV_GPU_IR_LOAD_STORAGE && predicated == 0 && lowering->ir->stage != DRV_GPU_STAGE_COMPUTE)
		clamp = 1;

	/* The address. */
	error = gsi1_buffer_address(lowering, binding->bank, offset, clamp, &address);
	if (error != 0)
		return error;

	/* The value of a store. */
	data = 0U;
	if (instruction->op == DRV_GPU_IR_STORE_STORAGE) {
		error = gsi1_source(lowering, instruction->src[1], &data);
		if (error != 0)
			return error;
	}

	/* The predicate narrows EXEC for the access. */
	save = GSI1_NO_OPERAND;
	if (predicated != 0) {
		predicate = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
			predicate = instruction->src[2];
		error = predicate_begin(lowering, predicate, &save);
		if (error != 0)
			return error;
	}

	/* The access. */
	if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
		gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_LOAD, GSI1_F3_LW, dst, address, 0, 0U));
	} else {
		gsi1_emit(lowering, gsi1_encode_s(GSI1_OP_STORE, GSI1_F3_SW, address, data, 0));
	}

	/* EXEC comes back. */
	if (save != GSI1_NO_OPERAND)
		predicate_end(lowering, save);

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Lowers an atomic on a storage buffer's word, a storage image's 32-bit
 * texel or a word of shared memory: atom (atoms for shared memory, sub[4])
 * at the address, its old value into dst (a texel outside the image is not
 * changed).
 */
static int
lower_atomic(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct gsi1_binding *binding;
	struct gsi1_extra extra;
	uint32_t offset;
	uint32_t data;
	uint32_t compare;
	uint32_t address;
	uint32_t predicate;
	uint32_t save;
	uint32_t sub;
	uint32_t sources;
	uint32_t inside;
	int clamp;
	int error;

	/* The operation's sub-operation. */
	error = atomic_sub(instruction->immediate, &sub);
	if (error != 0) {
		error = gsi1_refuse(lowering, EINVAL, "an atomic operation that is not one of the IR's");
		return error;
	}

	/*
	 * The word's address: a byte offset in shared memory, a storage image's
	 * texel (with the lanes whose texel is inside), or a storage buffer's
	 * word.
	 */
	inside = GSI1_NO_OPERAND;
	binding = NULL;
	if (instruction->location == DRV_GPU_IR_LOCATION_SHARED) {
		/* Shared memory is a compute program's; atoms wraps at its size, as lds and sts do. */
		error = shared_atomic_address(lowering, instruction, &address);
		if (error != 0)
			return error;
		sub |= GSI1_ATOM_SHARED;
	} else if (instruction->location < lowering->ir->uniform_count &&
	    (lowering->binding[instruction->location].kind == GSI1_KIND_STORAGE_IMAGE ||
	     lowering->binding[instruction->location].kind == GSI1_KIND_TEXEL_BUFFER)) {
		error = gsi1_image_texel(lowering, instruction, &lowering->binding[instruction->location], &address, &inside);
		if (error != 0)
			return error;
	} else {
		error = storage_binding(lowering, instruction->location, &binding);
		if (error != 0)
			return error;

		/* The offset. */
		error = gsi1_source(lowering, instruction->src[0], &offset);
		if (error != 0)
			return error;

		/* The address, clamped with robustBufferAccess. */
		clamp = 0;
		if (lowering->options.robust_buffer_access != 0U)
			clamp = 1;
		error = gsi1_buffer_address(lowering, binding->bank, offset, clamp, &address);
		if (error != 0)
			return error;
	}

	/* The value. */
	error = gsi1_source(lowering, instruction->src[1], &data);
	if (error != 0)
		return error;

	/* The compare of a compare-and-exchange. */
	sources = 2U;
	compare = 0U;
	if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
		error = gsi1_source(lowering, instruction->src[2], &compare);
		if (error != 0)
			return error;
		sources = 3U;
	}

	/* The predicate narrows EXEC for the atomic. */
	save = GSI1_NO_OPERAND;
	if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
		predicate = instruction->src[sources];
		error = predicate_begin(lowering, predicate, &save);
		if (error != 0)
			return error;
	}

	/* So does a texel outside its image. */
	if (inside != GSI1_NO_OPERAND) {
		if (save == GSI1_NO_OPERAND) {
			error = gsi1_scratch(lowering, &save);
			if (error != 0)
				return error;
			gsi1_emit_exec(lowering, GSI1_EXEC_SAVE, save);
		}
		gsi1_emit_exec(lowering, GSI1_EXEC_AND, inside);
	}

	/* atom.op dst, (address), data[, compare]. */
	kern_memset(&extra, 0, sizeof(extra));
	extra.sub = sub;
	gsi1_emit(lowering, gsi1_encode_r4(GSI1_OP_CUSTOM0, GSI1_F3_ATOM, dst, address, data, compare, &extra));
	lowering->flags |= GSI1_FLAG_ATOMIC;

	/* EXEC comes back. */
	if (save != GSI1_NO_OPERAND)
		predicate_end(lowering, save);

	/* Succeeded: the atomic is lowered. */
	return 0;
}

/*
 * Gives the byte address in shared memory of an atomic's word.
 */
static int
shared_atomic_address(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t *address)
{
	int error;

	/* Shared memory is a compute program's. */
	if (lowering->ir->stage != DRV_GPU_STAGE_COMPUTE) {
		error = gsi1_refuse(lowering, EINVAL, "shared memory outside a compute shader");
		return error;
	}

	/* The byte offset is the address; atoms has no immediate. */
	error = gsi1_source(lowering, instruction->src[0], address);
	if (error != 0)
		return error;

	/* The program uses shared memory. */
	lowering->flags |= GSI1_FLAG_LDS;

	/* Succeeded: the address is in a register or an operand. */
	return 0;
}

/*
 * Lowers a shared-memory load or store: lds and sts at the byte offset
 * (a constant offset in the immediate), predicated when the IR says.
 */
static int
lower_shared(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	uint32_t constant;
	uint32_t offset;
	uint32_t data;
	uint32_t predicate;
	uint32_t save;
	int32_t immediate;
	int known;
	int fits;
	int error;

	/* Shared memory is a compute program's. */
	if (lowering->ir->stage != DRV_GPU_STAGE_COMPUTE) {
		error = gsi1_refuse(lowering, EINVAL, "shared memory outside a compute shader");
		return error;
	}
	lowering->flags |= GSI1_FLAG_LDS;

	/* A constant offset goes into the immediate, from zero. */
	immediate = 0;
	offset = GSI1_ZERO;
	known = gsi1_value_constant(lowering, instruction->src[0], &constant);
	fits = 0;
	if (known != 0)
		fits = gsi1_immediate_fits((int64_t)(int32_t)constant);
	if (fits != 0) {
		immediate = (int32_t)constant;
	} else {
		error = gsi1_source(lowering, instruction->src[0], &offset);
		if (error != 0)
			return error;
	}

	/* The value of a store. */
	data = 0U;
	if (instruction->op == DRV_GPU_IR_STORE_SHARED) {
		error = gsi1_source(lowering, instruction->src[1], &data);
		if (error != 0)
			return error;
	}

	/* The predicate narrows EXEC for the access. */
	save = GSI1_NO_OPERAND;
	if (instruction->component != 0U) {
		predicate = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_SHARED)
			predicate = instruction->src[2];
		error = predicate_begin(lowering, predicate, &save);
		if (error != 0)
			return error;
	}

	/* The access. */
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_CUSTOM0, GSI1_F3_LDS, dst, offset, immediate, 0U));
	} else {
		gsi1_emit(lowering, gsi1_encode_s(GSI1_OP_CUSTOM0, GSI1_F3_STS, offset, data, immediate));
	}

	/* EXEC comes back. */
	if (save != GSI1_NO_OPERAND)
		predicate_end(lowering, save);

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Lowers a barrier and a fence.  A barrier's fences come before bar; a
 * barrier that orders global memory also invalidates the data cache after
 * it, so that the warp reads what the others wrote.
 */
static int
lower_sync(
	struct gsi1_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct gsi1_extra extra;
	uint32_t fences;

	/* The fences the instruction names. */
	fences = instruction->immediate & (DRV_GPU_IR_FENCE_GLOBAL | DRV_GPU_IR_FENCE_SHARED | DRV_GPU_IR_FENCE_ACQUIRE);

	/* A fence alone. */
	if (instruction->op == DRV_GPU_IR_FENCE) {
		if (fences != 0U)
			emit_fence(lowering, fences);
		return 0;
	}

	/* A barrier's fences before the warps meet. */
	if ((fences & (DRV_GPU_IR_FENCE_GLOBAL | DRV_GPU_IR_FENCE_SHARED)) != 0U)
		emit_fence(lowering, fences & (DRV_GPU_IR_FENCE_GLOBAL | DRV_GPU_IR_FENCE_SHARED));

	/* bar. */
	kern_memset(&extra, 0, sizeof(extra));
	gsi1_emit(lowering, gsi1_encode_r(GSI1_OP_CUSTOM0, GSI1_F3_BAR, 0U, 0U, 0U, 0U, &extra));
	lowering->flags |= GSI1_FLAG_LDS;

	/* What the others wrote to global memory is read again after the barrier. */
	if ((fences & DRV_GPU_IR_FENCE_GLOBAL) != 0U)
		emit_fence(lowering, GSI1_FENCE_ACQUIRE);

	/* Succeeded: the synchronization is lowered. */
	return 0;
}

/*
 * Translates the IR's atomic operation to GSI1's atom sub-operation;
 * returns EINVAL for one the IR does not have.
 */
static int
atomic_sub(
	uint32_t operation,
	uint32_t *sub)
{
	/* Each operation of the IR. */
	switch (operation) {
	case DRV_GPU_IR_ATOMIC_AND:
		*sub = GSI1_ATOM_AND;
		break;
	case DRV_GPU_IR_ATOMIC_OR:
		*sub = GSI1_ATOM_OR;
		break;
	case DRV_GPU_IR_ATOMIC_XOR:
		*sub = GSI1_ATOM_XOR;
		break;
	case DRV_GPU_IR_ATOMIC_XCHG:
		*sub = GSI1_ATOM_XCHG;
		break;
	case DRV_GPU_IR_ATOMIC_ADD:
		*sub = GSI1_ATOM_ADD;
		break;
	case DRV_GPU_IR_ATOMIC_SUB:
		*sub = GSI1_ATOM_SUB;
		break;
	case DRV_GPU_IR_ATOMIC_SMAX:
		*sub = GSI1_ATOM_SMAX;
		break;
	case DRV_GPU_IR_ATOMIC_SMIN:
		*sub = GSI1_ATOM_SMIN;
		break;
	case DRV_GPU_IR_ATOMIC_UMAX:
		*sub = GSI1_ATOM_UMAX;
		break;
	case DRV_GPU_IR_ATOMIC_UMIN:
		*sub = GSI1_ATOM_UMIN;
		break;
	case DRV_GPU_IR_ATOMIC_CMPXCHG:
		*sub = GSI1_ATOM_CMPXCHG;
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the sub-operation. */
	return 0;
}

/* Finds the place of a storage buffer the program accesses. */
static int
storage_binding(
	struct gsi1_lowering *lowering,
	uint32_t uniform,
	const struct gsi1_binding **binding)
{
	int error;

	/* The uniform is one of the program's storage buffers. */
	if (uniform >= lowering->ir->uniform_count ||
	    lowering->binding[uniform].kind != GSI1_KIND_STORAGE_BUFFER) {
		error = gsi1_refuse(lowering, EINVAL, "an access to something that is not a storage buffer of the program");
		return error;
	}

	/* Succeeded: the buffer's place. */
	*binding = &lowering->binding[uniform];
	return 0;
}

/*
 * Narrows EXEC to a predicate's lanes: exec.save into a scratch register,
 * then exec.and.
 */
static int
predicate_begin(
	struct gsi1_lowering *lowering,
	uint32_t predicate,
	uint32_t *save)
{
	uint32_t condition;
	int error;

	/* The predicate. */
	error = gsi1_source(lowering, predicate, &condition);
	if (error != 0)
		return error;

	/* Takes the scratch register EXEC is kept in. */
	error = gsi1_scratch(lowering, save);
	if (error != 0)
		return error;

	/* Saves EXEC and keeps the predicate's lanes. */
	gsi1_emit_exec(lowering, GSI1_EXEC_SAVE, *save);
	gsi1_emit_exec(lowering, GSI1_EXEC_AND, condition);

	/* Succeeded: EXEC holds the predicate's lanes. */
	return 0;
}

/* Restores the EXEC predicate_begin() saved. */
static void
predicate_end(
	struct gsi1_lowering *lowering,
	uint32_t save)
{
	/* exec.set from the saved Boolean. */
	gsi1_emit_exec(lowering, GSI1_EXEC_SET, save);
}

/* Emits fence with the IR's fence bits, which are GSI1's. */
static void
emit_fence(
	struct gsi1_lowering *lowering,
	uint32_t fences)
{
	/* The I form with the bits in the immediate. */
	gsi1_emit(lowering, gsi1_encode_i(GSI1_OP_CUSTOM0, GSI1_F3_FENCE, 0U, 0U, (int32_t)fences, 0U));
}
