/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register values and buffer descriptors of a compiled program
 * (gfx10-program.h).
 */

#include "gfx10-program.h"

#include <kern/kcrt.h>

#include <stdint.h>

/*
 * The register count and float mode word: the VGPRs in blocks of eight
 * less one (wave32) in bits 5:0, the float mode in 19:12, the clamp of
 * NaN to zero in bit 21, and a vertex program's input VGPR count less one
 * in 25:24.  The float mode keeps the denormals of 32-bit and of 16- and
 * 64-bit floats, which is IEEE behavior.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (COMPUTE_PGM_RSRC1, SPI_SHADER_PGM_RSRC1_VS: VGPRS, FLOAT_MODE,
 * DX10_CLAMP, VGPR_COMP_CNT; the enum FLOAT_MODE: FP_32_DENORMS 48,
 * FP_16_64_DENORMS 192) and src/amd/vulkan/radv_shader.c (the VGPR blocks
 * of eight for wave32, line 1983).
 */
#define GFX10_COUNTS_VGPR_BLOCK		8U
#define GFX10_COUNTS_FLOAT_MODE_SHIFT	12U
#define GFX10_COUNTS_FLOAT_DENORMS	0xf0U
#define GFX10_COUNTS_NAN_CLAMP		(1U << 21)
#define GFX10_COUNTS_VERTEX_VGPRS_SHIFT	24U

/*
 * The bit of the register count word that has the vector memory counter
 * count loads, samples and atomics with return in the order they were
 * issued, which the waits the compiler places by count rely on: bit 27 of
 * a vertex program's, 25 of a pixel program's, 30 of a compute program's.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_SHADER_PGM_RSRC1_VS, SPI_SHADER_PGM_RSRC1_PS, COMPUTE_PGM_RSRC1:
 * MEM_ORDERED) and src/amd/vulkan/radv_shader.c (radv_mem_ordered: set on
 * GFX10 and GFX11, lines 1885-1888).
 */
#define GFX10_COUNTS_ORDERED_VERTEX	(1U << 27)
#define GFX10_COUNTS_ORDERED_PIXEL	(1U << 25)
#define GFX10_COUNTS_ORDERED_COMPUTE	(1U << 30)

/*
 * The inputs word: the user SGPR count in bits 5:1; a compute program's
 * workgroup id x, y, z enables in bits 7, 8, 9, its local id VGPR count
 * less one in 12:11, and its shared memory in blocks of 512 bytes in
 * 23:15.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (COMPUTE_PGM_RSRC2, SPI_SHADER_PGM_RSRC2_VS, SPI_SHADER_PGM_RSRC2_PS:
 * USER_SGPR, TGID_X_EN, TGID_Y_EN, TGID_Z_EN, TIDIG_COMP_CNT, LDS_SIZE)
 * and src/amd/common/ac_gpu_info.c (lds_encode_granularity, 128 * 4 from
 * GFX7, line 1164).
 */
#define GFX10_INPUTS_USER_SGPR_SHIFT	1U
#define GFX10_INPUTS_GROUP_ID_XYZ	(7U << 7)
#define GFX10_INPUTS_LOCAL_ID_SHIFT	11U
#define GFX10_INPUTS_SHARED_SHIFT	15U
#define GFX10_INPUTS_SHARED_BLOCK	512U

/*
 * A vertex program's parameter export count less one (bits 5:1) and the
 * bit that says it exports none (7); the 4-component position export
 * format of position 0 (bits 3:0) and position 1 (7:4); and the output
 * control's bits for the point size: the vertex's own size (16), the
 * misc vector (21) and its side bus (24).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_VS_OUT_CONFIG: VS_EXPORT_COUNT, NO_PC_EXPORT; SPI_SHADER_POS_FORMAT;
 * the enum SPI_SHADER_FORMAT: SPI_SHADER_4COMP 4; PA_CL_VS_OUT_CNTL:
 * USE_VTX_POINT_SIZE, VS_OUT_MISC_VEC_ENA, VS_OUT_MISC_SIDE_BUS_ENA) and
 * src/amd/vulkan/radv_shader.c (radv_precompute_registers_hw_vs, lines
 * 1408-1445: at least one parameter counted).
 */
#define GFX10_VERTEX_EXPORT_COUNT_SHIFT	1U
#define GFX10_VERTEX_NO_PARAMETERS	(1U << 7)
#define GFX10_POSITION_FOUR		4U
#define GFX10_POSITION1_SHIFT		4U
#define GFX10_OUTPUT_POINT_SIZE		(1U << 16)
#define GFX10_OUTPUT_MISC_VECTOR	(1U << 21)
#define GFX10_OUTPUT_MISC_SIDE_BUS	(1U << 24)

/*
 * A pixel program's colour export format (four 16-bit floats, 4 bits per
 * target: the format of an 8-bit colour target, the only kind the
 * executor draws into; four 32-bit channels blend wrongly into one), the
 * interpolant count (bits 5:0) and the wave32 bit (15) of the interpolant
 * control, and an attribute's control word: the parameter it reads (bits
 * 5:0) and flat shading (bit 10).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_SHADER_COL_FORMAT; the enum SPI_SHADER_EX_FORMAT: SPI_SHADER_FP16_ABGR
 * 4; SPI_PS_IN_CONTROL: NUM_INTERP, PS_W32_EN; SPI_PS_INPUT_CNTL_0:
 * OFFSET, FLAT_SHADE) and src/amd/common/ac_shader_util.c
 * (ac_choose_spi_color_formats: FP16_ABGR for an 8_8_8_8 UNORM or SRGB
 * target, blended or not).
 */
#define GFX10_COLOR_FOUR_HALVES		4U
#define GFX10_INTERPOLANT_WAVE32	(1U << 15)
#define GFX10_ATTRIBUTE_FLAT		(1U << 10)

/*
 * The attribute the hardware makes the point sprite coordinate in: the
 * sprite texture bit (17) and the parameter offset that names no
 * parameter (0x20); the interpolation control that enables flat shading
 * (bit 0) and the sprite (bit 1) and gives its channels s, t, 0, 1 (3 bits
 * each from bit 2) with t from 0 at the top (bit 14 clear); and the bit
 * that has the hardware deliver a vertex program its primitive number.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_PS_INPUT_CNTL_0: PT_SPRITE_TEX, OFFSET; SPI_INTERP_CONTROL_0:
 * FLAT_SHADE_ENA, PNT_SPRITE_ENA, PNT_SPRITE_OVRD_X .. W, PNT_SPRITE_TOP_1;
 * the enum SPI_PNT_SPRITE_OVERRIDE: SEL_0 0, SEL_1 1, SEL_S 2, SEL_T 3;
 * VGT_PRIMITIVEID_EN: PRIMITIVEID_EN), src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_ps_inputs: the sprite attribute PT_SPRITE_TEX with OFFSET
 * 0x20, line 2551) and src/amd/vulkan/radv_queue.c (the interpolation
 * control of the preamble, lines 838-843).
 */
#define GFX10_ATTRIBUTE_SPRITE		((1U << 17) | 0x20U)
#define GFX10_INTERPOLATION_SPRITE	(1U | (1U << 1) | (2U << 2) | (3U << 5) | (0U << 8) | (1U << 11))
#define GFX10_PRIMITIVE_ID_ENABLE	1U

/*
 * The words of a buffer descriptor: the base's high 16 bits and the
 * stride (bits 29:16) in word 1; in word 3 the channel selects (x, y, z,
 * w in 3 bits each from bit 0: 0 for zero, 1 for one, 4 .. 7 for the
 * channels), the format (18:12), the resource level bit (24) and the
 * bounds check (29:28): by record index (1) or by byte offset (3).  A raw
 * buffer reads one 32-bit float channel.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json
 * (SQ_BUF_RSRC_WORD1, SQ_BUF_RSRC_WORD3, lines 321-340; the enum
 * SQ_BUF_RSRC_WORD3__OOB_SELECT: OOB_SELECT_STRUCTURED 1, OOB_SELECT_RAW
 * 3; the enum GFX10_FORMAT: 32_FLOAT 22), src/amd/registers/gfx9.json
 * (the enum SQ_SEL_XYZW01, line 709, which the GFX10 descriptors use) and
 * src/amd/common/ac_descriptors.c (ac_set_buf_desc_word3: RESOURCE_LEVEL 1
 * below GFX11; ac_build_raw_buffer_descriptor: R32_FLOAT, XYZW,
 * OOB_SELECT_RAW).
 */
#define GFX10_BUFFER_STRIDE_SHIFT	16U
#define GFX10_BUFFER_SELECT_ZERO	0U
#define GFX10_BUFFER_SELECT_ONE		1U
#define GFX10_BUFFER_SELECT_X		4U
#define GFX10_BUFFER_FORMAT_SHIFT	12U
#define GFX10_BUFFER_RESOURCE_LEVEL	(1U << 24)
#define GFX10_BUFFER_CHECK_INDEX	(1U << 28)
#define GFX10_BUFFER_CHECK_OFFSET	(3U << 28)
#define GFX10_BUFFER_FORMAT_FLOAT1	22U

static uint32_t highest_bit_count(uint32_t mask);

/*
 * Computes the register values of a compiled program.
 *
 * The values cover what the program decides; the driver adds what the
 * draw or the dispatch decides (the addresses, the wave32 dispatch bits of
 * the other units, the depth unit's kill enable from `discards`).
 */
void
amdrdna_gfx10_program_registers(
	const struct amdrdna_gfx10_program *program,
	struct amdrdna_gfx10_registers *registers)
{
	uint32_t vgprs;
	uint32_t location;
	uint32_t parameters;
	uint32_t shared_blocks;

	kern_memset(registers, 0, sizeof(*registers));

	/* The VGPR blocks, at least one, the float mode and the NaN clamp. */
	vgprs = program->vgpr_count;
	if (vgprs == 0U)
		vgprs = 1U;
	registers->counts = (vgprs - 1U) / GFX10_COUNTS_VGPR_BLOCK;
	registers->counts |= GFX10_COUNTS_FLOAT_DENORMS << GFX10_COUNTS_FLOAT_MODE_SHIFT;
	registers->counts |= GFX10_COUNTS_NAN_CLAMP;

	/* The vector memory counter in order, in the stage's own bit. */
	if (program->stage == DRV_GPU_STAGE_VERTEX) {
		registers->counts |= GFX10_COUNTS_ORDERED_VERTEX;
	} else if (program->stage == DRV_GPU_STAGE_FRAGMENT) {
		registers->counts |= GFX10_COUNTS_ORDERED_PIXEL;
	} else {
		registers->counts |= GFX10_COUNTS_ORDERED_COMPUTE;
	}

	/* The user SGPRs every stage loads. */
	registers->inputs = program->user_sgprs << GFX10_INPUTS_USER_SGPR_SHIFT;

	/* A compute program: the workgroup id, the local ids x, y, z, and the shared memory. */
	if (program->stage == DRV_GPU_STAGE_COMPUTE) {
		shared_blocks = (program->shared_bytes + GFX10_INPUTS_SHARED_BLOCK - 1U) / GFX10_INPUTS_SHARED_BLOCK;
		registers->inputs |= GFX10_INPUTS_GROUP_ID_XYZ;
		registers->inputs |= 2U << GFX10_INPUTS_LOCAL_ID_SHIFT;
		registers->inputs |= shared_blocks << GFX10_INPUTS_SHARED_SHIFT;
	}

	/* A vertex program: v0 .. v3 delivered, the parameters, the positions and the point size. */
	if (program->stage == DRV_GPU_STAGE_VERTEX) {
		registers->counts |= 3U << GFX10_COUNTS_VERTEX_VGPRS_SHIFT;

		/* At least one parameter is counted; none exported is said apart. */
		parameters = highest_bit_count(program->parameter_outputs);
		if (parameters == 0U) {
			registers->vertex_export_config = GFX10_VERTEX_NO_PARAMETERS;
		} else {
			registers->vertex_export_config = (parameters - 1U) << GFX10_VERTEX_EXPORT_COUNT_SHIFT;
		}

		/* The primitive number delivered in v2 for its export. */
		if (program->exports_primitive_id != 0U)
			registers->primitive_id_enable = GFX10_PRIMITIVE_ID_ENABLE;

		/* Position 0 always, position 1 for the point size. */
		registers->position_formats = GFX10_POSITION_FOUR;
		if (program->writes_point_size != 0U) {
			registers->position_formats |= GFX10_POSITION_FOUR << GFX10_POSITION1_SHIFT;
			registers->vertex_output_control = GFX10_OUTPUT_POINT_SIZE;
			registers->vertex_output_control |= GFX10_OUTPUT_MISC_VECTOR;
			registers->vertex_output_control |= GFX10_OUTPUT_MISC_SIDE_BUS;
		}
	}

	/* A pixel program: the inputs, the colour formats, the interpolants and the attributes. */
	if (program->stage == DRV_GPU_STAGE_FRAGMENT) {
		registers->pixel_input_enable = program->pixel_inputs;

		/* Each colour location written exports four 16-bit floats. */
		for (location = 0U; location < 8U; location++) {
			if ((program->color_outputs & (1U << location)) != 0U)
				registers->color_formats |= GFX10_COLOR_FOUR_HALVES << (4U * location);
		}

		/*
		 * Attribute L reads parameter L, flat where the program reads it
		 * flat; the point sprite coordinate's is made by the hardware, the
		 * primitive number's reads its parameter flat.
		 */
		registers->attribute_count = highest_bit_count(program->attribute_inputs);
		for (location = 0U; location < registers->attribute_count; location++) {
			if (location == program->point_coord_attribute) {
				registers->attribute_control[location] = GFX10_ATTRIBUTE_SPRITE;
				continue;
			}
			if (location == program->primitive_id_attribute) {
				registers->attribute_control[location] = AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER | GFX10_ATTRIBUTE_FLAT;
				continue;
			}
			registers->attribute_control[location] = location;
			if ((program->flat_inputs & (1U << location)) != 0U)
				registers->attribute_control[location] |= GFX10_ATTRIBUTE_FLAT;
		}
		registers->interpolation_control = GFX10_INTERPOLATION_SPRITE;
		registers->interpolant_control = registers->attribute_count | GFX10_INTERPOLANT_WAVE32;
		registers->discards = program->discards;
	}
}

/* Writes the four words of a raw buffer descriptor. */
void
amdrdna_gfx10_raw_buffer_descriptor(
	uint64_t address,
	uint32_t size,
	uint32_t *words)
{
	/* The base, no stride, the size in bytes. */
	words[0] = (uint32_t)address;
	words[1] = (uint32_t)(address >> 32) & 0xffffU;
	words[2] = size;

	/* The channels in order, one 32-bit float, the resource level, the byte offset bounds. */
	words[3] = GFX10_BUFFER_SELECT_X;
	words[3] |= (GFX10_BUFFER_SELECT_X + 1U) << 3;
	words[3] |= (GFX10_BUFFER_SELECT_X + 2U) << 6;
	words[3] |= (GFX10_BUFFER_SELECT_X + 3U) << 9;
	words[3] |= GFX10_BUFFER_FORMAT_FLOAT1 << GFX10_BUFFER_FORMAT_SHIFT;
	words[3] |= GFX10_BUFFER_RESOURCE_LEVEL;
	words[3] |= GFX10_BUFFER_CHECK_OFFSET;
}

/* Writes the four words of a vertex buffer descriptor. */
void
amdrdna_gfx10_vertex_descriptor(
	uint64_t address,
	uint32_t stride,
	uint32_t count,
	uint32_t format,
	uint32_t components,
	uint32_t *words)
{
	uint32_t channel;
	uint32_t select;

	/* The base, the stride, the record count. */
	words[0] = (uint32_t)address;
	words[1] = (uint32_t)(address >> 32) & 0xffffU;
	words[1] |= (stride & 0x3fffU) << GFX10_BUFFER_STRIDE_SHIFT;
	words[2] = count;

	/* The channels the format has, then 0, 0, 0, 1 for the missing ones. */
	words[3] = 0U;
	for (channel = 0U; channel < 4U; channel++) {
		if (channel < components) {
			select = GFX10_BUFFER_SELECT_X + channel;
		} else if (channel == 3U) {
			select = GFX10_BUFFER_SELECT_ONE;
		} else {
			select = GFX10_BUFFER_SELECT_ZERO;
		}
		words[3] |= select << (3U * channel);
	}

	/* The format, the resource level, the record index bounds. */
	words[3] |= (format & 0x7fU) << GFX10_BUFFER_FORMAT_SHIFT;
	words[3] |= GFX10_BUFFER_RESOURCE_LEVEL;
	words[3] |= GFX10_BUFFER_CHECK_INDEX;
}

/* Reports one past the highest bit set of a mask (0 for none). */
static uint32_t
highest_bit_count(
	uint32_t mask)
{
	uint32_t count;

	/* Shifts the mask out bit by bit. */
	count = 0U;
	while (mask != 0U) {
		mask >>= 1;
		count++;
	}

	/* The count of bits up to the highest set. */
	return count;
}
