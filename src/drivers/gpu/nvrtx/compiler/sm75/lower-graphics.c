/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The lowering of the vertex and fragment programs (see lower.h): their
 * inputs and outputs, the texture operations, the discards, and the shader
 * program header.
 */

#include "drivers/gpu/nvrtx/compiler/sm75/lower.h"

#include "drivers/gpu/compiler/spirv.h"

#include <uapi/errno.h>

#include <stddef.h>

/*
 * What one texture operation passes the sampler (nvrtx_sm75_texture_operands()):
 * the values of its coordinates and of its level of detail (or bias;
 * NVRTX_SM75_NOT_DEFINED for none), the level of detail mode, whether it
 * is a fetch (TLD) rather than a sample, and its constant texel offset as
 * the register TEX reads takes it.  Lives on the lowering's stack.
 */
struct nvrtx_sm75_texture_operands {
	uint32_t u;
	uint32_t v;
	uint32_t lod;
	uint32_t mode;
	int fetch;
	int has_offset;
	uint32_t offset;
};

static int nvrtx_sm75_texture_operands(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, struct nvrtx_sm75_texture_operands *operands);
static const struct drv_gpu_shader_ir_io *nvrtx_sm75_input(const struct drv_gpu_shader_ir *ir, uint32_t location);
static void nvrtx_sm75_sph_set(uint32_t *sph, unsigned low, unsigned bits, uint32_t value);
static void nvrtx_sm75_mark_store(struct nvrtx_sm75_lowering *lowering, uint32_t address);

/*
 * Prepares a graphics program: a fragment program's colour outputs take
 * R0 onward, four registers for each location it writes in ascending
 * order (the registers the hardware reads at EXIT), which no value may
 * take; a vertex program's store request starts empty.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
 * (ShaderFromNir::parse_intrinsic, copy_fs_outputs_nv: the order of the
 * fragment outputs in OpRegOut, four for each location written, in
 * ascending order).
 */
int
nvrtx_sm75_prepare_graphics(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t written;
	uint32_t location;
	uint32_t index;
	uint32_t next;
	int error;

	/* The program. */
	ir = lowering->ir;

	/* No colour location has registers yet, and nothing is stored. */
	for (location = 0U; location < NVRTX_SM75_COLOR_TARGETS; location++)
		lowering->color_reg[location] = NVRTX_SM75_NO_REG;
	lowering->inverse_w = NVRTX_SM75_NO_REG;
	lowering->store_first = 0xffU;
	lowering->store_last = 0U;

	/* Only a fragment program has colour outputs. */
	if (ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return 0;

	/* The locations the program writes. */
	written = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_STORE_OUTPUT)
			continue;

		/* An output is a colour word, or it is refused at its instruction. */
		lowering->index = index;
		if (instruction->location >= NVRTX_SM75_COLOR_TARGETS || instruction->component > 3U) {
			error = nvrtx_sm75_refuse(lowering, ENOTSUP, "a fragment output other than a colour (depth, second colour)");
			return error;
		}

		/* The location is written. */
		written |= 1U << instruction->location;
	}

	/* The lowering starts again at the first instruction. */
	lowering->index = 0U;

	/* Four registers for each, from R0, which stay taken. */
	next = 0U;
	for (location = 0U; location < NVRTX_SM75_COLOR_TARGETS; location++) {
		if ((written & (1U << location)) == 0U)
			continue;
		lowering->color_reg[location] = next;
		for (index = 0U; index < 4U; index++)
			lowering->taken[next + index] = 1U;
		next += 4U;
	}

	/* The program uses those registers at least. */
	if (next > lowering->highest)
		lowering->highest = next;

	/* Succeeded: the outputs have their registers. */
	return 0;
}

/*
 * Starts a fragment program that interpolates in perspective: w at the
 * pixel (the position's w, passed screen-linear) and its reciprocal, which
 * every perspective input is multiplied by.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak_nir_lower_fs_inputs.c (load_frag_w and
 * lower_fs_input_intrin: the SM70 interpolation, w passed screen-linear,
 * then 1 / w).
 */
int
nvrtx_sm75_prologue(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	const struct drv_gpu_shader_ir_io *input;
	struct nvrtx_sm75_word word;
	uint32_t index;
	int perspective;
	int error;

	/* The program. */
	ir = lowering->ir;

	/* Only a fragment program interpolates. */
	if (ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return 0;

	/* Whether any input is read in perspective. */
	perspective = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_LOAD_INPUT || instruction->location >= 64U)
			continue;
		input = nvrtx_sm75_input(ir, instruction->location);
		if (input != NULL &&
		    input->flat == 0U &&
		    input->noperspective == 0U)
			perspective = 1;
	}

	/* No input needs 1 / w. */
	if (perspective == 0)
		return 0;

	/* A register for 1 / w that stays taken. */
	error = nvrtx_sm75_take(lowering, &lowering->inverse_w);
	if (error != 0)
		return error;

	/* w, passed screen-linear. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ipa(&word, lowering->inverse_w, NVRTX_SM75_ATTR_POSITION_W, NVRTX_SM75_IPA_PASS);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	/* Its reciprocal. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mufu(&word, lowering->inverse_w, NVRTX_SM75_MUFU_RCP, lowering->inverse_w);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	/* Succeeded: 1 / w is ready. */
	nvrtx_sm75_sph_set(lowering->binary->sph, NVRTX_SM75_SPH_IMAP_AB_LOW + NVRTX_SM75_ATTR_POSITION_W / 4U, 1U, 1U);
	return 0;
}

/*
 * Lowers an input: a vertex program's attribute word (ALD), its vertex or
 * instance number; a fragment program's interpolated word (IPA, in
 * perspective multiplied by 1 / w), or the pixel's position.  The
 * header's input map records each.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak_nir_lower_fs_inputs.c (interp_fs_input: from
 * SM70 on, an input in perspective is passed, then multiplied by 1 / w).
 */
int
nvrtx_sm75_lower_input(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_io *input;
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	uint32_t *sph;
	uint32_t address;
	uint32_t slot;
	uint32_t mode;
	int error;

	/* The header's maps, and an input of at most four components. */
	sph = lowering->binary->sph;
	if (instruction->component > 3U) {
		error = nvrtx_sm75_refuse(lowering, EINVAL, "an input component past the fourth");
		return error;
	}

	/*
	 * A vertex program: a generic attribute, the vertex number or the
	 * instance number.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (VtgIoInfo::mark_attrs: a bit of the input map for each word, the
	 * generic words from 0x080 and the C part of the system values from
	 * 0x2c0).
	 */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		if (instruction->location < 32U) {
			address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (address - NVRTX_SM75_ATTR_GENERIC) / 4U, 1U, 1U);
		} else if (instruction->location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
			address = NVRTX_SM75_ATTR_VERTEX_ID;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_C_VTG_LOW + (address - 0x2c0U) / 4U, 1U, 1U);
		} else if (instruction->location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
			address = NVRTX_SM75_ATTR_INSTANCE_ID;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_C_VTG_LOW + (address - 0x2c0U) / 4U, 1U, 1U);
		} else {
			error = nvrtx_sm75_refuse(lowering, ENOTSUP, "a vertex input not compiled yet");
			return error;
		}

		/* ALD of the word. */
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_ald(&word, dst, address);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		if (error != 0)
			return error;

		/* Succeeded: the input is in dst. */
		return 0;
	}

	/* Only the fragment stage reads interpolated inputs. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT) {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "inputs of this stage are not compiled yet");
		return error;
	}

	/* The pixel's position: screen-linear, passed. */
	if (instruction->location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
		address = NVRTX_SM75_ATTR_POSITION + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_AB_LOW + address / 4U, 1U, 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_PASS);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		if (error != 0)
			return error;

		/* Succeeded: the position's component is in dst. */
		return 0;
	}

	/*
	 * A located input and its interpolation.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (FragmentIoInfo::mark_attr_read) and
	 * src/nouveau/compiler/nak/sph.rs
	 * (ShaderProgramHeader::set_imap_vector_ps: two bits of the input map
	 * for each generic word).
	 */
	input = NULL;
	if (instruction->location < 32U)
		input = nvrtx_sm75_input(lowering->ir, instruction->location);
	if (input == NULL) {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "a fragment input not compiled yet");
		return error;
	}

	/* Its word, and its interpolation in the header's input map. */
	address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
	slot = (address - NVRTX_SM75_ATTR_GENERIC) / 4U;
	mode = NVRTX_SM75_IMAP_PERSPECTIVE;
	if (input->noperspective != 0U)
		mode = NVRTX_SM75_IMAP_SCREEN_LINEAR;
	if (input->flat != 0U)
		mode = NVRTX_SM75_IMAP_CONSTANT;
	nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_GENERIC_LOW + slot * 2U, 2U, mode);

	/* The interpolation: a flat input is the provoking vertex's, the others are passed. */
	nvrtx_sm75_begin(&word);
	if (mode == NVRTX_SM75_IMAP_CONSTANT) {
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_CONSTANT);
	} else {
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_PASS);
	}

	/* Appends the IPA. */
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	/* In perspective, times 1 / w. */
	if (mode == NVRTX_SM75_IMAP_PERSPECTIVE) {
		a = nvrtx_sm75_reg(dst);
		b = nvrtx_sm75_reg(lowering->inverse_w);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, dst, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* Succeeded: the input is in dst. */
	return 0;
}

/*
 * Lowers an output: a vertex program's attribute word (AST: the position,
 * the point size or a generic attribute), recorded in the header's output
 * map and store request; a fragment program's colour word, moved into the
 * register the hardware reads it from at EXIT and recorded in the header's
 * targets.
 */
int
nvrtx_sm75_lower_output(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src value;
	uint32_t *sph;
	uint32_t address;
	int error;

	/* The header's maps, the value, and an output of at most four components. */
	sph = lowering->binary->sph;
	value = nvrtx_sm75_value(lowering, instruction->src[0]);
	if (instruction->component > 3U) {
		error = nvrtx_sm75_refuse(lowering, EINVAL, "an output component past the fourth");
		return error;
	}

	/*
	 * A fragment program: the colour's register.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
	 * (ShaderFromNir::parse_intrinsic, copy_fs_outputs_nv: writes_color, a
	 * bit for each component of each location) and
	 * src/nouveau/compiler/nak/sph.rs
	 * (ShaderProgramHeader::set_omap_targets).
	 */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, lowering->color_reg[instruction->location] + instruction->component, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;

		/* Succeeded: the colour word is in its register, and the header's targets say so. */
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_TARGETS_LOW + instruction->location * 4U + instruction->component, 1U, 1U);
		return 0;
	}

	/* Only the vertex stage stores attributes. */
	if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX) {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "outputs of this stage are not compiled yet");
		return error;
	}

	/*
	 * The attribute's address and its bit of the output map.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (VtgIoInfo::mark_attrs: a bit of the output map for each word, the
	 * system values below 0x080 and the generic words from 0x080).
	 */
	if (instruction->location == DRV_GPU_IR_LOCATION_POSITION) {
		address = NVRTX_SM75_ATTR_POSITION + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_AB_LOW + address / 4U, 1U, 1U);
	} else if (instruction->location == DRV_GPU_IR_LOCATION_POINT_SIZE) {
		address = NVRTX_SM75_ATTR_POINT_SIZE;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_AB_LOW + address / 4U, 1U, 1U);
	} else if (instruction->location < 32U) {
		address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_GENERIC_LOW + (address - NVRTX_SM75_ATTR_GENERIC) / 4U, 1U, 1U);
	} else {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "a vertex output not compiled yet");
		return error;
	}

	/* The store, which reads its data register later. */
	nvrtx_sm75_mark_store(lowering, address);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ast(&word, address, value.value);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
	if (error != 0)
		return error;

	/* Succeeded: the attribute is written. */
	return 0;
}

/*
 * Lowers a 2D texture operation: the coordinates into a pair, the level of
 * detail and the constant texel offset (when there are any) into the
 * register or the pair after, TEX (a sample) or TLD (a fetch) with the
 * image's handle from constant buffer 0, and the four channels into two
 * pairs that become the four results' registers.  The level of detail of a
 * sample is implicit in a fragment program, zero in a vertex one, or the
 * instruction's bias or level.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_tex.c
 * (lower_tex: the coordinates in the first source, the level of detail and
 * then the offset in the second) and src/nouveau/compiler/nak/sm70_encode.rs
 * (SM70Op for OpTex, encode: two destination registers and two source
 * registers).
 */
int
nvrtx_sm75_lower_sample(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_texture_operands operands;
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src value;
	struct nvrtx_sm75_binary *binary;
	uint32_t coordinates;
	uint32_t extra;
	uint32_t extra_count;
	uint32_t first;
	uint32_t second;
	uint32_t texture;
	uint32_t index;
	int error;

	/* The result being built. */
	binary = lowering->binary;

	/* The image's texture handle. */
	texture = NVRTX_SM75_NO_SLOT;
	for (index = 0U; index < binary->texture_count; index++) {
		if (binary->textures[index].set == instruction->location && binary->textures[index].binding == instruction->immediate)
			texture = index;
	}

	/* An image the program does not use is refused. */
	if (texture == NVRTX_SM75_NO_SLOT) {
		error = nvrtx_sm75_refuse(lowering, EINVAL, "a sample names no sampled image");
		return error;
	}

	/* The results must be new values. */
	for (index = 0U; index < 4U; index++) {
		if (lowering->reg[instruction->dst + index] != NVRTX_SM75_NO_REG) {
			error = nvrtx_sm75_refuse(lowering, EINVAL, "a sample's result defined before");
			return error;
		}
	}

	/* What the operation passes. */
	error = nvrtx_sm75_texture_operands(lowering, instruction, &operands);
	if (error != 0)
		return error;

	/* The registers after the coordinates: the level of detail, the offset, both as a pair, or none. */
	extra_count = 0U;
	if (operands.lod != NVRTX_SM75_NOT_DEFINED)
		extra_count++;
	if (operands.has_offset != 0)
		extra_count++;

	/* The coordinates' pair. */
	error = nvrtx_sm75_take_pair(lowering, &coordinates);
	if (error != 0)
		return error;

	/* The level of detail's and the offset's register or pair. */
	extra = NVRTX_SM75_RZ;
	if (extra_count == 1U)
		error = nvrtx_sm75_take(lowering, &extra);
	if (extra_count == 2U)
		error = nvrtx_sm75_take_pair(lowering, &extra);
	if (error != 0) {
		nvrtx_sm75_give(lowering, coordinates);
		nvrtx_sm75_give(lowering, coordinates + 1U);
		return error;
	}

	/* The results' two pairs. */
	error = nvrtx_sm75_take_pair(lowering, &first);
	if (error == 0) {
		error = nvrtx_sm75_take_pair(lowering, &second);
		if (error != 0) {
			nvrtx_sm75_give(lowering, first);
			nvrtx_sm75_give(lowering, first + 1U);
		}
	}

	/* Without them the operands' registers go back. */
	if (error != 0) {
		nvrtx_sm75_give(lowering, coordinates);
		nvrtx_sm75_give(lowering, coordinates + 1U);
		nvrtx_sm75_give(lowering, extra);
		if (extra_count == 2U)
			nvrtx_sm75_give(lowering, extra + 1U);
		return error;
	}

	/* The coordinates. */
	value = nvrtx_sm75_value(lowering, operands.u);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, coordinates, &value);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);

	/* The second coordinate. */
	if (error == 0) {
		value = nvrtx_sm75_value(lowering, operands.v);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, coordinates + 1U, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The level of detail, first after the coordinates. */
	index = 0U;
	if (error == 0 && operands.lod != NVRTX_SM75_NOT_DEFINED) {
		value = nvrtx_sm75_value(lowering, operands.lod);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, extra, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		index = 1U;
	}

	/* The offset, after the level of detail. */
	if (error == 0 && operands.has_offset != 0) {
		value = nvrtx_sm75_imm(operands.offset);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, extra + index, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The sample or the fetch. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_tex(&word, first, second, coordinates, extra, operands.mode, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_TEXTURES + texture * 4U);
		if (operands.fetch != 0)
			nvrtx_sm75_tex_fetch(&word);
		if (operands.has_offset != 0)
			nvrtx_sm75_tex_offset(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The operands go back; the results' registers are the four values'. */
	nvrtx_sm75_give(lowering, coordinates);
	nvrtx_sm75_give(lowering, coordinates + 1U);
	nvrtx_sm75_give(lowering, extra);
	if (extra_count == 2U)
		nvrtx_sm75_give(lowering, extra + 1U);
	lowering->reg[instruction->dst] = (uint16_t)first;
	lowering->reg[instruction->dst + 1U] = (uint16_t)(first + 1U);
	lowering->reg[instruction->dst + 2U] = (uint16_t)second;
	lowering->reg[instruction->dst + 3U] = (uint16_t)(second + 1U);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the channels are in the results' registers. */
	return 0;
}

/*
 * Lowers a discard of the fragments where the Boolean holds: @P0 KILL.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
 * (ShaderFromNir::parse_intrinsic: nir_intrinsic_demote and
 * nir_intrinsic_demote_if) and src/nouveau/compiler/nak/sph.rs
 * (encode_header: set_kills_pixels).
 */
int
nvrtx_sm75_lower_kill(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	int error;

	/* Only a fragment program discards. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT) {
		error = nvrtx_sm75_refuse(lowering, EINVAL, "a discard outside a fragment program");
		return error;
	}

	/* P0, then the KILL under it. */
	error = nvrtx_sm75_predicate(lowering, instruction->src[0]);
	if (error != 0)
		return error;

	/* @P0 KILL. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_kill(&word);
	nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* Succeeded: the header says the program discards. */
	nvrtx_sm75_sph_set(lowering->binary->sph, NVRTX_SM75_SPH_KILLS_PIXELS, 1U, 1U);
	return 0;
}

/*
 * Completes a graphics program's header (version 4):
 * the type and version, the SASS version 1, a fragment program's
 * multiple-target bit and its input map's position w, the global memory
 * bits, a vertex program's store request.  The other maps were set as the
 * program was lowered.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sph.rs
 * (encode_header).
 *
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sph.rs
 * (encode_header: the SASS version and the fields set).
 */
void
nvrtx_sm75_header(
	struct nvrtx_sm75_lowering *lowering)
{
	struct nvrtx_sm75_binary *binary;
	const struct drv_gpu_shader_ir *ir;
	uint32_t index;
	int global;
	int stores;

	/* The result and the program. */
	binary = lowering->binary;
	ir = lowering->ir;

	/* A compute program has none. */
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return;
	binary->graphics = 1U;

	/* The type, the version and the stage. */
	if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_TYPE_LOW, 5U, NVRTX_SM75_SPH_TYPE_PS);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SHADER_TYPE_LOW, 4U, NVRTX_SM75_SPH_PIXEL);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_MRT_ENABLE, 1U, 1U);

		/*
		 * The position's w is mapped in even when the program does not
		 * read it, as a trap (a shader program header error) follows
		 * without it.
		 *
		 * Value referenced: Mesa 25.3.6
		 * src/nouveau/compiler/nak/from_nir.rs (init_info_from_nir: bit
		 * 31 of a fragment program's sysvals_in.ab, the position's w in
		 * its input map).
		 */
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_IMAP_AB_LOW + NVRTX_SM75_ATTR_POSITION_W / 4U, 1U, 1U);
	} else {
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_TYPE_LOW, 5U, NVRTX_SM75_SPH_TYPE_VTG);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SHADER_TYPE_LOW, 4U, NVRTX_SM75_SPH_VERTEX);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_STORE_REQ_START_LOW, 8U, lowering->store_first);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_STORE_REQ_END_LOW, 8U, lowering->store_last);
	}

	/* The header's version and the SASS version. */
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_VERSION_LOW, 5U, NVRTX_SM75_SPH_VERSION);
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SASS_VERSION_LOW, 4U, 1U);

	/* Whether it loads or stores global memory. */
	global = 0;
	stores = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		if (ir->instructions[index].op == DRV_GPU_IR_LOAD_STORAGE)
			global = 1;
		if (ir->instructions[index].op == DRV_GPU_IR_STORE_STORAGE || ir->instructions[index].op == DRV_GPU_IR_ATOMIC) {
			global = 1;
			stores = 1;
		}
	}

	/* The header's two global memory bits. */
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_LOAD_OR_STORE, 1U, (uint32_t)global);
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_GLOBAL_STORE, 1U, (uint32_t)stores);
}

/*
 * Reports nonzero for the texture operations, which define four results.
 */
int
nvrtx_sm75_is_sample(
	enum drv_gpu_shader_ir_op op)
{
	/* The implicit, the explicit and the biased level of detail. */
	if (op == DRV_GPU_IR_SAMPLE)
		return 1;
	if (op == DRV_GPU_IR_SAMPLE_LOD)
		return 1;
	if (op == DRV_GPU_IR_SAMPLE_BIAS)
		return 1;

	/* The general sampler message. */
	if (op == DRV_GPU_IR_TEXTURE)
		return 1;

	/* Succeeded: another operation. */
	return 0;
}

/*
 * Finds what a texture operation passes: its coordinates, its level of
 * detail value and mode, whether it fetches, and its texel offset packed as
 * TEX takes it (u, v and r in four bits each).  A 2D sample of its own
 * operation keeps its offset as u in bits 11..8 and v in 7..4 of the
 * component; the general message keeps the message in bits 4..0 and the
 * offset shifted by 8.  The general message passes no image dimension, so
 * only the messages whose parameter count says 2D are taken: a sample
 * (u v), a biased or levelled one (bias or lod, u v), and a fetch (u v lod).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_tex.c
 * (lower_tex: how the coordinates, level of detail and offset are packed into
 * registers, the offset four bits for each component with u lowest).
 */
static int
nvrtx_sm75_texture_operands(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	struct nvrtx_sm75_texture_operands *operands)
{
	uint32_t message;
	uint32_t count;
	uint32_t offset;
	uint32_t first;
	int error;

	/* No level of detail, no fetch, no offset. */
	operands->lod = NVRTX_SM75_NOT_DEFINED;
	operands->mode = NVRTX_SM75_TEX_LOD_AUTO;
	operands->fetch = 0;
	operands->has_offset = 0;
	operands->offset = 0U;

	/* The 2D samples of their own operations: the coordinates, then the bias or the level. */
	if (instruction->op != DRV_GPU_IR_TEXTURE) {
		operands->u = instruction->src[0];
		operands->v = instruction->src[1];

		/* A sample at a level of detail takes the level third. */
		if (instruction->op == DRV_GPU_IR_SAMPLE_LOD) {
			operands->lod = instruction->src[2];
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
		}

		/* A biased sample takes the bias third. */
		if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS) {
			operands->lod = instruction->src[2];
			operands->mode = NVRTX_SM75_TEX_LOD_BIAS;
		}

		/* u in bits 11..8 and v in 7..4 become u in 3..0 and v in 7..4. */
		offset = instruction->component;
		if (offset != 0U) {
			operands->has_offset = 1;
			operands->offset = ((offset >> 8) & 0xfU) | (offset & 0xf0U);
		}
	} else {
		/* The general message, its parameters from src[0]. */
		message = instruction->component & 0x1fU;
		count = instruction->src[1];
		first = instruction->src[0];
		if (message == DRV_GPU_IR_TEXTURE_SAMPLE && count == 2U) {
			operands->u = first;
			operands->v = first + 1U;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS && count == 3U) {
			operands->lod = first;
			operands->mode = NVRTX_SM75_TEX_LOD_BIAS;
			operands->u = first + 1U;
			operands->v = first + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_LOD && count == 3U) {
			operands->lod = first;
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
			operands->u = first + 1U;
			operands->v = first + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_LD && count == 3U) {
			operands->u = first;
			operands->v = first + 1U;
			operands->lod = first + 2U;
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
			operands->fetch = 1;
		} else {
			error = nvrtx_sm75_refuse(lowering, ENOTSUP, "a texture message other than a 2D sample or fetch (array, cube, 3D, compare, gradients, size) is not compiled yet");
			return error;
		}

		/* u in bits 11..8, v in 7..4 and r in 3..0 of the offset become u in 3..0, v in 7..4 and r in 11..8. */
		offset = (instruction->component >> 8) & 0xfffU;
		if (offset != 0U) {
			operands->has_offset = 1;
			operands->offset = ((offset >> 8) & 0xfU) | (offset & 0xf0U) | ((offset & 0xfU) << 8);
		}
	}

	/* Outside a fragment program there are no derivatives: an implicit level is level zero. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT && operands->mode == NVRTX_SM75_TEX_LOD_AUTO)
		operands->mode = NVRTX_SM75_TEX_LOD_ZERO;

	/* Succeeded: the operands are found. */
	return 0;
}

/* Finds a fragment program's located input, or NULL. */
static const struct drv_gpu_shader_ir_io *
nvrtx_sm75_input(
	const struct drv_gpu_shader_ir *ir,
	uint32_t location)
{
	uint32_t index;

	/* The input at the location. */
	for (index = 0U; index < ir->input_count; index++) {
		if (ir->inputs[index].location == location)
			return &ir->inputs[index];
	}

	/* Succeeded in finding none. */
	return NULL;
}

/* Sets a field of the header. */
static void
nvrtx_sm75_sph_set(
	uint32_t *sph,
	unsigned low,
	unsigned bits,
	uint32_t value)
{
	unsigned bit;
	uint32_t mask;

	/* Each bit of the field into its word. */
	for (bit = 0U; bit < bits; bit++) {
		mask = 1U << ((low + bit) % 32U);
		if (((value >> bit) & 1U) != 0U) {
			sph[(low + bit) / 32U] |= mask;
		} else {
			sph[(low + bit) / 32U] &= ~mask;
		}
	}
}

/*
 * Widens a vertex program's store request to an attribute word's address.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
 * (VtgIoInfo::mark_store_req: the first and the last word, by address / 4).
 */
static void
nvrtx_sm75_mark_store(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t address)
{
	/* The first and the last word stored. */
	if (address / 4U < lowering->store_first)
		lowering->store_first = address / 4U;
	if (address / 4U > lowering->store_last)
		lowering->store_last = address / 4U;
}
