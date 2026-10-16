/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The state a draw or a dispatch writes into an indirect buffer
 * (amdrdna-vk-exec.h): the start of every buffer (the registers every draw
 * shares, then the caches emptied of what the CPU wrote), the colour
 * target, the viewport and scissor, the blend, the rasteriser, the
 * programs of a draw and of a dispatch, the draw itself and the barrier
 * between passes.
 *
 * Every image the executor draws into is linear, four bytes a pixel, its
 * rows its width's bytes rounded up to 256 apart: that is how GFX10 lays a
 * linear colour target out, so a target's size alone says its rows.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

/*
 * The registers only the start of a buffer sets (bytes from their space's
 * base): the pixel and vertex programs' accumulators (four each), the
 * compute program's (four), the depth unit's punch-out control and cache
 * policies, the colour unit's cache policies, the binner's second word,
 * the primitive shader's mode, the clipper's NGG control, the line
 * stipple scale, the primitive restart index, the scan converter's second
 * mode word, the second sample mask, the stereo control and the line
 * stipple's value and state.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_SHADER_USER_ACCUM_PS_0 0xb0c8, SPI_SHADER_USER_ACCUM_VS_0 0xb1c8,
 * COMPUTE_USER_ACCUM_0 0xb890, DB_DFSM_CONTROL 0x28038,
 * DB_RMI_L2_CACHE_CONTROL 0x2807c, CB_RMI_GL2_CACHE_CONTROL 0x28410,
 * PA_SC_BINNER_CNTL_1 0x28c48, PA_SC_NGG_MODE_CNTL 0x28c50,
 * PA_CL_NGG_CNTL 0x28838, PA_SU_LINE_STIPPLE_SCALE 0x28828,
 * VGT_MULTI_PRIM_IB_RESET_INDX 0x2840c, PA_SC_MODE_CNTL_1 0x28a4c,
 * PA_SC_AA_MASK_X0Y1_X1Y1 0x28c3c, GE_STEREO_CNTL 0x3097c,
 * PA_SU_LINE_STIPPLE_VALUE 0x30a00, PA_SC_LINE_STIPPLE_STATE 0x30a04).
 */
#define STATE_PIXEL_ACCUMULATORS	0xb0c8U
#define STATE_VERTEX_ACCUMULATORS	0xb1c8U
#define STATE_KERNEL_ACCUMULATORS	0xb890U
#define STATE_ACCUMULATORS		4U
#define STATE_DEPTH_PUNCH_OUT		0x28038U
#define STATE_DEPTH_CACHE		0x2807cU
#define STATE_COLOUR_CACHE		0x28410U
#define STATE_BINNER_LIMITS		0x28c48U
#define STATE_PRIMITIVE_SHADER_MODE	0x28c50U
#define STATE_CLIP_NGG			0x28838U
#define STATE_STIPPLE_SCALE		0x28828U
#define STATE_RESTART_INDEX		0x2840cU
#define STATE_SCAN_MODE_MORE		0x28a4cU
#define STATE_SAMPLE_MASK_MORE		0x28c3cU
#define STATE_STEREO			0x3097cU
#define STATE_STIPPLE_VALUE		0x30a00U
#define STATE_STIPPLE_STATE		0x30a04U

/*
 * The values the start of a buffer sets: punch-out forced off (2); the
 * depth unit streaming its writes (1) and not allocating on reads that
 * miss (1) for depth, stencil and metadata; the colour unit the same for
 * colour and its masks; the binner's 1024 / 3 - 1 batches of 1023
 * primitives (the parameter cache of Navi10 is 1024 lines); 512
 * deallocations a wave; the restart index all ones; the scan converter's
 * walk (the fence of size 3, the supertile and tile orders, the
 * multi-engine discard, the end-of-vector count and rez forced, an
 * out-of-order water mark of 7 and align8); and the line stipple scale
 * of 1.0.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (DB_DFSM_CONTROL: PUNCHOUT_MODE and the enum: FORCE_OFF 2;
 * DB_RMI_L2_CACHE_CONTROL: Z_WR_POLICY 1:0, S_WR_POLICY 3:2,
 * HTILE_WR_POLICY 5:4, ZPCPSD_WR_POLICY 7:6, Z_RD_POLICY 17:16,
 * S_RD_POLICY 19:18, HTILE_RD_POLICY 21:20; CB_RMI_GL2_CACHE_CONTROL:
 * CMASK_WR_POLICY 1:0, FMASK_WR_POLICY 3:2, DCC_WR_POLICY 5:4,
 * COLOR_WR_POLICY 7:6, CMASK_RD_POLICY 17:16, FMASK_RD_POLICY 19:18,
 * DCC_RD_POLICY 21:20, COLOR_RD_POLICY 23:22; the enums WritePolicy:
 * CACHE_STREAM 1, ReadPolicy: CACHE_NOA 1; PA_SC_BINNER_CNTL_1:
 * MAX_ALLOC_COUNT 15:0, MAX_PRIM_PER_BATCH 31:16; PA_SC_NGG_MODE_CNTL:
 * MAX_DEALLOCS_IN_WAVE; PA_SC_MODE_CNTL_1: WALK_ALIGN8_PRIM_FITS_ST 2,
 * WALK_FENCE_ENABLE 3, WALK_FENCE_SIZE 6:4, SUPERTILE_WALK_ORDER_ENABLE 7,
 * TILE_WALK_ORDER_ENABLE 8, MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE 17,
 * FORCE_EOV_CNTDWN_ENABLE 25, FORCE_EOV_REZ_ENABLE 26,
 * OUT_OF_ORDER_WATER_MARK 30:28), src/amd/common/ac_cmdbuf.c
 * (gfx10_init_graphics_preamble_state: the policies of a chip of more than
 * four render backends, the binner's count less one, 512 deallocations),
 * src/amd/common/ac_gpu_info.c (pc_lines 1024 for CHIP_NAVI10,
 * pbb_max_alloc_count = pc_lines / 3), src/amd/vulkan/radv_queue.c
 * (radv_emit_graphics: the restart index, the stipple scale) and
 * src/amd/vulkan/radv_cmd_buffer.c (radv_emit_rasterization_samples: the
 * walk of a chip of more than two tile pipes).
 */
#define STATE_PUNCH_OUT_OFF		2U
#define STATE_DEPTH_CACHE_STREAM	(1U | (1U << 2) | (1U << 4) | (1U << 6) | (1U << 16) | (1U << 18) | (1U << 20))
#define STATE_COLOUR_CACHE_STREAM	(1U | (1U << 2) | (1U << 4) | (1U << 6) | (1U << 16) | (1U << 18) | (1U << 20) | \
					 (1U << 22))
#define STATE_BINNER_LIMITS_NAVI10	((1024U / 3U - 1U) | (1023U << 16))
#define STATE_DEALLOCATIONS		512U
#define STATE_RESTART_ALL		0xffffffffU
#define STATE_SCAN_WALK			((1U << 2) | (1U << 3) | (3U << 4) | (1U << 7) | (1U << 8) | (1U << 17) | \
					 (1U << 25) | (1U << 26) | (7U << 28))
#define STATE_FLOAT_ONE			0x3f800000U

/*
 * The compute program's start values: the coherency start delay of 0x20,
 * every CU of shader arrays 0 and 1 of shader engines 0 and 1 (Navi10 has
 * two) and none of the others.
 *
 * Values referenced: Mesa 25.0.7 src/amd/common/ac_cmdbuf.c
 * (gfx10_init_compute_preamble_state: CP_COHER_START_DELAY 0x20 below
 * GFX11, the static thread management of the engines there are) and
 * src/amd/registers/gfx9.json (COMPUTE_STATIC_THREAD_MGMT_SE0: SH0_CU_EN
 * 15:0, SH1_CU_EN 31:16).
 */
#define STATE_COHERENCY_DELAY		0x20U
#define STATE_KERNEL_UNITS_ALL		0xffffffffU

/*
 * The window, screen and generic scissors' far corner (16384 in x and y,
 * x in bits 14:0 and y in 30:16).
 *
 * Values referenced: Mesa 25.0.7 src/amd/common/ac_cmdbuf.c
 * (gfx6_init_graphics_preamble_state: the generic scissor 16384, 16384) and
 * src/amd/registers/gfx10.json (PA_SC_WINDOW_SCISSOR_BR,
 * PA_SC_SCREEN_SCISSOR_BR, PA_SC_GENERIC_SCISSOR_BR: BR_X, BR_Y).
 */
#define STATE_SCISSOR_FAR		(16384U | (16384U << 16))
#define STATE_SCISSOR_MAX		16384

/*
 * The pixel program's depth control: early then late (1 in 5:4), or late
 * (0) for a program that writes memory, which then runs whatever the
 * depth test says (9, 10).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (DB_SHADER_CONTROL: Z_ORDER, EXEC_ON_HIER_FAIL, EXEC_ON_NOOP) and
 * src/amd/vulkan/radv_shader.c (the late depth and the two runs of a
 * program that writes memory; DUAL_QUAD_DISABLE where the chip has RB+
 * that is not allowed, as on GFX10.1, per src/amd/common/ac_gpu_info.c
 * rbplus_allowed).
 */
#define STATE_DEPTH_SHADER_WRITES	((1U << 9) | (1U << 10))

/*
 * The fields of the vertex and pixel interface: the four channels each
 * colour export writes (four bits a target), the bit of the primitive
 * type's index field, and the 12.4 limits.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (CB_SHADER_MASK: OUTPUT0_ENABLE .. OUTPUT7_ENABLE, four bits each;
 * SPI_SHADER_COL_FORMAT: COL0_EXPORT_FORMAT .. COL7, four bits each).
 */
#define STATE_EXPORT_CHANNELS		0xfU
#define STATE_TARGETS			8U

/* The fixed point the viewport is worked in: 16 fractional bits, and 24 for the depths. */
#define STATE_FRACTION			16U
#define STATE_DEPTH_FRACTION		24U

/*
 * The cache actions of the start of a buffer: everything the CPU wrote
 * since the last buffer (programs, tables, descriptors) read anew; and of
 * a barrier, after the colour unit's caches are written back.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cs.c
 * (gfx10_cs_emit_cache_flush: the instruction, scalar, vector, per-array
 * and level-2 invalidations of the acquire; the colour and depth unit
 * flush with metadata as a timestamped end-of-pipe event that writes back
 * and invalidates the level-2 cache, waited for in memory, then the
 * fetcher's sync).
 */
#define STATE_START_CACHES		(AMDRDNA_PM4_ACQUIRE_INSTRUCTIONS | AMDRDNA_PM4_ACQUIRE_SCALAR | \
					 AMDRDNA_PM4_ACQUIRE_VECTOR | AMDRDNA_PM4_ACQUIRE_LEVEL1 | \
					 AMDRDNA_PM4_ACQUIRE_LEVEL2_INVALIDATE | AMDRDNA_PM4_ACQUIRE_LEVEL2_WRITE_BACK | \
					 AMDRDNA_PM4_ACQUIRE_METADATA_WRITE_BACK | AMDRDNA_PM4_ACQUIRE_METADATA_INVALIDATE)
#define STATE_BARRIER_RELEASE		(AMDRDNA_PM4_RELEASE_METADATA_WRITE_BACK | AMDRDNA_PM4_RELEASE_METADATA_INVALIDATE | \
					 AMDRDNA_PM4_RELEASE_VECTOR_INVALIDATE | AMDRDNA_PM4_RELEASE_LEVEL1_INVALIDATE | \
					 AMDRDNA_PM4_RELEASE_LEVEL2_INVALIDATE | AMDRDNA_PM4_RELEASE_LEVEL2_WRITE_BACK | \
					 AMDRDNA_PM4_RELEASE_IN_ORDER)
#define STATE_BARRIER_ACQUIRE		(AMDRDNA_PM4_ACQUIRE_SCALAR | AMDRDNA_PM4_ACQUIRE_VECTOR | \
					 AMDRDNA_PM4_ACQUIRE_LEVEL1)

static void state_start_graphics(struct amdrdna_vk_pm4 *pm4);
static void state_start_compute(struct amdrdna_vk_pm4 *pm4);
static bool state_blend_factor(uint32_t factor, uint32_t *code);
static bool state_blend_op(uint32_t op, uint32_t *code);
static uint32_t state_blend_shortcut(uint32_t op);
static uint32_t state_export_mask(uint32_t formats);
static int32_t state_floor(int64_t value);
static int32_t state_ceil(int64_t value);
static uint32_t state_clamp(int32_t value);

/*
 * Starts a buffer: the context control and the registers' defaults, the
 * registers every draw and dispatch shares, then the caches emptied of
 * what the CPU wrote.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_queue.c
 * (radv_emit_graphics: the context control loading and keeping every
 * register group, the clear to the defaults, then the preamble; the
 * compute preamble after it).
 */
void
amdrdna_vk_state_start(
	struct amdrdna_vk_pm4 *pm4)
{
	/* The context control and the defaults. */
	amdrdna_vk_pm4_context_start(pm4);

	/* What draws and dispatches share. */
	state_start_graphics(pm4);
	state_start_compute(pm4);

	/* Everything the CPU wrote read anew. */
	amdrdna_vk_pm4_cache_acquire(pm4, STATE_START_CACHES);
}

/*
 * Starts a buffer of the compute queue, which takes no graphics state: the
 * registers every dispatch shares, then the caches emptied of what the
 * CPU wrote.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_queue.c
 * (radv_emit_compute: the compute preamble alone on a compute queue).
 */
void
amdrdna_vk_state_start_kernel(
	struct amdrdna_vk_pm4 *pm4)
{
	/* What dispatches share, then everything the CPU wrote read anew. */
	state_start_compute(pm4);
	amdrdna_vk_pm4_cache_acquire(pm4, STATE_START_CACHES);
}

/*
 * Reports the colour target's layout word of a format (its format,
 * number type and component swap, clamped blending, simple floats); false
 * for a format the executor does not draw into.
 */
bool
amdrdna_vk_target_format(
	uint32_t format,
	uint32_t *layout)
{
	uint32_t word;

	/* The four formats of four 8-bit channels. */
	word = AMDRDNA_CX_TARGET_FORMAT_8888 | AMDRDNA_CX_TARGET_BLEND_CLAMP | AMDRDNA_CX_TARGET_SIMPLE_FLOAT;
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		word |= AMDRDNA_CX_TARGET_NUMBER_UNORM | AMDRDNA_CX_TARGET_SWAP_STANDARD;
		break;
	case VK_FORMAT_B8G8R8A8_UNORM:
		word |= AMDRDNA_CX_TARGET_NUMBER_UNORM | AMDRDNA_CX_TARGET_SWAP_ALTERNATE;
		break;
	case VK_FORMAT_R8G8B8A8_SRGB:
		word |= AMDRDNA_CX_TARGET_NUMBER_SRGB | AMDRDNA_CX_TARGET_SWAP_STANDARD;
		break;
	case VK_FORMAT_B8G8R8A8_SRGB:
		word |= AMDRDNA_CX_TARGET_NUMBER_SRGB | AMDRDNA_CX_TARGET_SWAP_ALTERNATE;
		break;
	default:
		return false;
	}

	/* Succeeded: the target is drawn in the format. */
	*layout = word;
	return true;
}

/* Reports the bytes between the rows of a linear image of four-byte pixels a width wide. */
uint32_t
amdrdna_vk_pitch(
	uint32_t width)
{
	/* The width's bytes, rounded up to 256. */
	return (width * AMDRDNA_VK_PIXEL_BYTES + AMDRDNA_VK_PITCH_ALIGN - 1U) & ~(AMDRDNA_VK_PITCH_ALIGN - 1U);
}

/*
 * Writes colour target 0: its address, view, layout, attributes, size and
 * surface.  Returns 0, or EINVAL for a format, a size or an address the
 * target cannot have.
 */
int
amdrdna_vk_state_target(
	struct amdrdna_vk_pm4 *pm4,
	const struct amdrdna_vk_target *target)
{
	uint32_t layout;

	/* A format drawn into, a size the target holds, an address on 256 bytes. */
	if (!amdrdna_vk_target_format(target->format, &layout))
		return EINVAL;
	if (target->width == 0U || target->height == 0U || target->width > AMDRDNA_VK_IMAGE_SIDE_MAX ||
	    target->height > AMDRDNA_VK_IMAGE_SIDE_MAX)
		return EINVAL;
	if (target->address == 0U || (target->address & (AMDRDNA_VK_RESOURCE_ALIGN - 1U)) != 0U)
		return EINVAL;

	/* The address in two parts, the first slice and level, the layout, one sample. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_ADDRESS, (uint32_t)(target->address >> 8));
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_ADDRESS_HIGH, (uint32_t)(target->address >> 40) & 0xffU);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_VIEW, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_LAYOUT, layout);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_ATTRIBUTES, 0U);

	/* The size, and a linear 2D surface. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_SIZE,
				(target->height - 1U) | ((target->width - 1U) << AMDRDNA_CX_TARGET_WIDTH_SHIFT));
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_TARGET_SURFACE, AMDRDNA_CX_TARGET_SURFACE_2D);

	/* Succeeded: the draws after it write the target. */
	return 0;
}

/*
 * Writes viewport 0 from a VkViewport's floats (as bits: x, y, width,
 * height, minimum and maximum depth): the transform, the depth range, and
 * the viewport's scissor around it.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_viewport: the scale and the translation of x, y and z, the
 * depth range as the smaller and the larger depth; radv_emit_scissor: the
 * viewport's scissor, its corners rounded out).
 */
void
amdrdna_vk_state_viewport(
	struct amdrdna_vk_pm4 *pm4,
	const uint32_t *viewport)
{
	uint32_t transform[6];
	uint32_t range[2];
	int64_t x;
	int64_t y;
	int64_t width;
	int64_t height;
	int64_t near;
	int64_t far;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;

	/* The floats in fixed point. */
	x = amdrdna_vk_fixed(viewport[0], STATE_FRACTION);
	y = amdrdna_vk_fixed(viewport[1], STATE_FRACTION);
	width = amdrdna_vk_fixed(viewport[2], STATE_FRACTION);
	height = amdrdna_vk_fixed(viewport[3], STATE_FRACTION);
	near = amdrdna_vk_fixed(viewport[4], STATE_DEPTH_FRACTION);
	far = amdrdna_vk_fixed(viewport[5], STATE_DEPTH_FRACTION);

	/* x: half the width about its middle; y the same; z from the near depth to the far. */
	transform[0] = amdrdna_vk_float_fixed(width, STATE_FRACTION + 1U);
	transform[1] = amdrdna_vk_float_fixed(2 * x + width, STATE_FRACTION + 1U);
	transform[2] = amdrdna_vk_float_fixed(height, STATE_FRACTION + 1U);
	transform[3] = amdrdna_vk_float_fixed(2 * y + height, STATE_FRACTION + 1U);
	transform[4] = amdrdna_vk_float_fixed(far - near, STATE_DEPTH_FRACTION);
	transform[5] = viewport[4];
	amdrdna_vk_pm4_set_context_run(pm4, AMDRDNA_CX_VIEWPORT_TRANSFORM, transform, 6U);

	/* The depth range, the smaller first. */
	range[0] = viewport[4];
	range[1] = viewport[5];
	if (far < near) {
		range[0] = viewport[5];
		range[1] = viewport[4];
	}
	amdrdna_vk_pm4_set_context_run(pm4, AMDRDNA_CX_VIEWPORT_DEPTH_RANGE, range, 2U);

	/* The viewport's scissor: its corners rounded out, on the screen (a negative height spans upward). */
	x0 = state_floor(width < 0 ? x + width : x);
	x1 = state_ceil(width < 0 ? x : x + width);
	y0 = state_floor(height < 0 ? y + height : y);
	y1 = state_ceil(height < 0 ? y : y + height);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VIEWPORT_SCISSOR,
				state_clamp(x0) | (state_clamp(y0) << AMDRDNA_CX_SCISSOR_Y_SHIFT) | AMDRDNA_CX_SCISSOR_NO_WINDOW_OFFSET);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VIEWPORT_SCISSOR + 4U, state_clamp(x1) | (state_clamp(y1) << AMDRDNA_CX_SCISSOR_Y_SHIFT));
}

/* Writes the scissor: the pixels from (x0, y0) up to, not including, (x1, y1). */
void
amdrdna_vk_state_scissor(
	struct amdrdna_vk_pm4 *pm4,
	uint32_t x0,
	uint32_t y0,
	uint32_t x1,
	uint32_t y1)
{
	/* The near corner without the window offset, the far corner. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_GENERIC_SCISSOR,
				x0 | (y0 << AMDRDNA_CX_SCISSOR_Y_SHIFT) | AMDRDNA_CX_SCISSOR_NO_WINDOW_OFFSET);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_GENERIC_SCISSOR + 4U, x1 | (y1 << AMDRDNA_CX_SCISSOR_Y_SHIFT));
}

/*
 * Writes the blend of colour target 0, its blend shortcut, the channels
 * written and the colour control.  Returns 0, or EINVAL for a factor or a
 * function the target does not blend with.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_color_blend: the alpha fields and the separate alpha bit
 * only when the alpha's factors or function differ from the colour's, the
 * factors of a minimum or maximum taken as one, the shortcut off without
 * blending).
 */
int
amdrdna_vk_state_blend(
	struct amdrdna_vk_pm4 *pm4,
	const struct amdrdna_vk_blend *blend)
{
	uint32_t control;
	uint32_t shortcut;
	uint32_t source;
	uint32_t destination;
	uint32_t function;
	uint32_t alpha_source;
	uint32_t alpha_destination;
	uint32_t alpha_function;
	bool known;

	/* Without blending, the target is written as the program exports it. */
	control = 0U;
	shortcut = AMDRDNA_CX_BLEND_SHORTCUT_OFF;
	if (blend->enable != 0U) {
		/* The colour's and the alpha's factors and functions. */
		known = state_blend_factor(blend->colour_source, &source);
		known = state_blend_factor(blend->colour_destination, &destination) && known;
		known = state_blend_op(blend->colour_op, &function) && known;
		known = state_blend_factor(blend->alpha_source, &alpha_source) && known;
		known = state_blend_factor(blend->alpha_destination, &alpha_destination) && known;
		known = state_blend_op(blend->alpha_op, &alpha_function) && known;
		if (!known)
			return EINVAL;

		/* A minimum or a maximum takes no factors. */
		if (function == AMDRDNA_BLEND_MINIMUM || function == AMDRDNA_BLEND_MAXIMUM) {
			source = AMDRDNA_BLEND_ONE;
			destination = AMDRDNA_BLEND_ONE;
		}
		if (alpha_function == AMDRDNA_BLEND_MINIMUM || alpha_function == AMDRDNA_BLEND_MAXIMUM) {
			alpha_source = AMDRDNA_BLEND_ONE;
			alpha_destination = AMDRDNA_BLEND_ONE;
		}

		/* The colour, and the alpha apart when it differs. */
		control = source | (function << AMDRDNA_CX_BLEND_FUNCTION_SHIFT) | (destination << AMDRDNA_CX_BLEND_DESTINATION_SHIFT);
		if (alpha_source != source || alpha_destination != destination || alpha_function != function) {
			control |= (alpha_source | (alpha_function << AMDRDNA_CX_BLEND_FUNCTION_SHIFT) |
				    (alpha_destination << AMDRDNA_CX_BLEND_DESTINATION_SHIFT))
				   << AMDRDNA_CX_BLEND_ALPHA_SHIFT;
			control |= AMDRDNA_CX_BLEND_SEPARATE_ALPHA;
		}
		control |= AMDRDNA_CX_BLEND_ENABLE;

		/* The export unit keeps every channel and combines as the blend does. */
		shortcut = AMDRDNA_CX_BLEND_SHORTCUT_KEEP_ALL;
		shortcut |= state_blend_shortcut(blend->colour_op) << AMDRDNA_CX_BLEND_SHORTCUT_COLOUR_SHIFT;
		shortcut |= state_blend_shortcut(blend->alpha_op) << AMDRDNA_CX_BLEND_SHORTCUT_ALPHA_SHIFT;
	}

	/* The blend, its shortcut, the channels written, the normal colour mode. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_BLEND, control);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_BLEND_SHORTCUT, shortcut);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_COLOUR_WRITE_MASK, blend->write_mask & STATE_EXPORT_CHANNELS);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_COLOUR_CONTROL, AMDRDNA_CX_COLOUR_CONTROL_NORMAL);

	/* Succeeded: the draws after it blend so. */
	return 0;
}

/*
 * Writes the rasteriser's culling (VkCullModeFlags) and front face
 * (VkFrontFace).
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_raster_state: CULL_FRONT and CULL_BACK from the cull mode's
 * bits, FACE from the front face as it is).
 */
void
amdrdna_vk_state_raster(
	struct amdrdna_vk_pm4 *pm4,
	uint32_t cull_mode,
	uint32_t front_face)
{
	uint32_t mode;

	/* The faces culled and the face in front. */
	mode = 0U;
	if ((cull_mode & VK_CULL_MODE_FRONT_BIT) != 0U)
		mode |= AMDRDNA_CX_RASTER_CULL_FRONT;
	if ((cull_mode & VK_CULL_MODE_BACK_BIT) != 0U)
		mode |= AMDRDNA_CX_RASTER_CULL_BACK;
	if (front_face == VK_FRONT_FACE_CLOCKWISE)
		mode |= AMDRDNA_CX_RASTER_CLOCKWISE;
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_RASTER_MODE, mode);
}

/*
 * Writes the programs of a draw: each one's code address, register
 * counts and inputs, and user SGPRs (the resource table's address; the
 * vertex program's base vertex, first instance and the mask of the
 * locations fetched by instance), and what the two programs decide of the
 * vertex export, the pixel inputs, the colour exports and the depth unit.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_hw_vs, radv_emit_fragment_shader_state, radv_emit_ps_inputs,
 * radv_emit_vgt_shader_config_gfx6 and radv_emit_db_shader_control: the
 * registers each sets; the pixel inputs enabled and addressed together).
 */
void
amdrdna_vk_state_programs(
	struct amdrdna_vk_pm4 *pm4,
	const struct amdrdna_vk_program *vertex,
	const struct amdrdna_vk_program *fragment,
	uint64_t vertex_table,
	uint64_t fragment_table,
	uint32_t base_vertex,
	uint32_t first_instance,
	uint32_t instance_mask)
{
	const struct amdrdna_gfx10_registers *vs;
	const struct amdrdna_gfx10_registers *ps;
	uint32_t words[5];
	uint32_t depth;

	vs = &vertex->registers;
	ps = &fragment->registers;

	/* The vertex program: its code, counts and inputs, its five user SGPRs. */
	words[0] = (uint32_t)(vertex->va >> AMDRDNA_SH_CODE_LOW_SHIFT);
	words[1] = (uint32_t)(vertex->va >> AMDRDNA_SH_CODE_HIGH_SHIFT) & 0xffU;
	words[2] = vs->counts;
	words[3] = vs->inputs;
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_VERTEX_CODE_LOW, words, 4U);
	words[0] = (uint32_t)vertex_table;
	words[1] = (uint32_t)(vertex_table >> 32);
	words[2] = base_vertex;
	words[3] = first_instance;
	words[4] = instance_mask;
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_VERTEX_USER_DATA, words, 5U);

	/* The pixel program: its code, counts and inputs, its two user SGPRs. */
	words[0] = (uint32_t)(fragment->va >> AMDRDNA_SH_CODE_LOW_SHIFT);
	words[1] = (uint32_t)(fragment->va >> AMDRDNA_SH_CODE_HIGH_SHIFT) & 0xffU;
	words[2] = ps->counts;
	words[3] = ps->inputs;
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_PIXEL_CODE_LOW, words, 4U);
	words[0] = (uint32_t)fragment_table;
	words[1] = (uint32_t)(fragment_table >> 32);
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_PIXEL_USER_DATA, words, 2U);

	/* The vertex export: the parameters, the positions, the point size, the primitive number. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VERTEX_EXPORT_CONFIG, vs->vertex_export_config);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_POSITION_FORMATS, vs->position_formats);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VERTEX_OUTPUT_CONTROL, vs->vertex_output_control);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_PRIMITIVE_ID_ENABLE, vs->primitive_id_enable);

	/* The pixel inputs: enabled and addressed alike, the interpolants, each attribute, the sprite. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_PIXEL_INPUT_ENABLE, ps->pixel_input_enable);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_PIXEL_INPUT_ADDRESS, ps->pixel_input_enable);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_PIXEL_INPUT_CONTROL, ps->interpolant_control);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_BARYCENTRIC_CONTROL, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_INTERPOLATION_CONTROL, ps->interpolation_control);
	if (ps->attribute_count != 0U)
		amdrdna_vk_pm4_set_context_run(pm4, AMDRDNA_CX_ATTRIBUTE_CONTROL, ps->attribute_control, ps->attribute_count);

	/* The colour exports and the channels the colour unit takes of them; no depth export. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_COLOUR_EXPORT_FORMATS, ps->color_formats);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_COLOUR_EXPORT_MASK, state_export_mask(ps->color_formats));
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_DEPTH_EXPORT_FORMAT, ps->depth_format);

	/*
	 * The depth unit: early then late, or late and always run for a
	 * program that writes memory; the kill for one that discards; no dual
	 * quads.
	 */
	depth = AMDRDNA_CX_DEPTH_SHADER_EARLY | AMDRDNA_CX_DEPTH_SHADER_NO_DUAL_QUAD;
	if (fragment->compiled != NULL && fragment->compiled->storage_used != 0U)
		depth = (depth & ~AMDRDNA_CX_DEPTH_SHADER_EARLY) | STATE_DEPTH_SHADER_WRITES;
	if (ps->discards != 0U)
		depth |= AMDRDNA_CX_DEPTH_SHADER_KILL;
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_DEPTH_SHADER_CONTROL, depth);

	/* A legacy vertex program in wave32, no geometry program. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_STAGES, AMDRDNA_CX_STAGES_LEGACY_VERTEX);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_GEOMETRY_MODE, 0U);
}

/*
 * Draws vertex_count generated vertices of instance_count instances as a
 * VkPrimitiveTopology.  Returns 0, or EINVAL for a topology the executor
 * does not draw.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_primitive_topology: the primitive type through the indexed
 * setter; gfx10_emit_ge_cntl; the instance count and the draw) and
 * src/amd/vulkan/radv_pipeline_graphics.h (radv_translate_prim: which
 * topology is which type).
 */
int
amdrdna_vk_state_draw(
	struct amdrdna_vk_pm4 *pm4,
	uint32_t topology,
	uint32_t vertex_count,
	uint32_t instance_count)
{
	uint32_t type;

	/*
	 * The primitive type.
	 *
	 * Value referenced: Mesa 25.0.7 src/amd/registers/gfx10.json (the enum
	 * VGT_DI_PRIM_TYPE: DI_PT_TRIFAN 5).
	 */
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
		type = AMDRDNA_PRIMITIVE_POINTS;
		break;
	case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
		type = AMDRDNA_PRIMITIVE_LINES;
		break;
	case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
		type = AMDRDNA_PRIMITIVE_LINE_STRIP;
		break;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		type = AMDRDNA_PRIMITIVE_TRIANGLES;
		break;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
		type = AMDRDNA_PRIMITIVE_TRIANGLE_STRIP;
		break;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		type = 5U;
		break;
	default:
		return EINVAL;
	}

	/* The type, the groups, then the instances and the vertices. */
	amdrdna_vk_pm4_set_user_config_indexed(pm4, AMDRDNA_UC_PRIMITIVE_TYPE, AMDRDNA_UC_PRIMITIVE_TYPE_INDEX, type);
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_GEOMETRY_CONTROL, AMDRDNA_UC_GEOMETRY_GROUPS);
	amdrdna_vk_pm4_draw(pm4, vertex_count, instance_count);

	/* Succeeded: the draw is written. */
	return 0;
}

/*
 * Writes a compute program for the dispatches after it: its code address,
 * register counts and inputs, the workgroup's size, the start at 0 and
 * the resource table's address in its user SGPRs.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_compute_shader: the code address, the resource words, the
 * thread counts) and src/amd/common/ac_cmdbuf.c
 * (gfx10_init_compute_preamble_state: the start at 0 is the dispatch's).
 */
void
amdrdna_vk_state_kernel(
	struct amdrdna_vk_pm4 *pm4,
	const struct amdrdna_vk_program *kernel,
	uint64_t table)
{
	uint32_t words[6];

	/* The code, the counts and inputs, no shared VGPRs, no limits. */
	words[0] = (uint32_t)(kernel->va >> AMDRDNA_SH_CODE_LOW_SHIFT);
	words[1] = (uint32_t)(kernel->va >> AMDRDNA_SH_CODE_HIGH_SHIFT) & 0xffU;
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_KERNEL_CODE_LOW, words, 2U);
	words[0] = kernel->registers.counts;
	words[1] = kernel->registers.inputs;
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_KERNEL_RESOURCES, words, 2U);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_MORE_RESOURCES, 0U);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_LIMITS, 0U);

	/* The start at 0, 0, 0, then the workgroup's size. */
	words[0] = 0U;
	words[1] = 0U;
	words[2] = 0U;
	words[3] = kernel->compiled->local_size[0];
	words[4] = kernel->compiled->local_size[1];
	words[5] = kernel->compiled->local_size[2];
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_KERNEL_START, words, 6U);

	/* The resource table. */
	words[0] = (uint32_t)table;
	words[1] = (uint32_t)(table >> 32);
	amdrdna_vk_pm4_set_shader_run(pm4, AMDRDNA_SH_KERNEL_USER_DATA, words, 2U);
}

/*
 * Writes a barrier between passes and transfers: the pixel and compute
 * programs drained, the colour unit's metadata flushed, then an
 * end-of-pipe event that flushes the colour and depth caches, writes back
 * and invalidates the level-2 cache and writes sync_value to
 * sync_address; the engine waits for the value, the fetcher for the
 * engine, and the shader caches are invalidated.  sync_value must differ
 * from what the word held before (the session's counter).
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cs.c
 * (gfx10_cs_emit_cache_flush: the partial flushes, the metadata flush,
 * the timestamped flush and invalidation with the release's cache actions
 * waited for in memory, PFP_SYNC_ME, then the acquire of the shader
 * caches).
 */
void
amdrdna_vk_state_barrier(
	struct amdrdna_vk_pm4 *pm4,
	uint64_t sync_address,
	uint32_t sync_value)
{
	/* The programs drained, the metadata flushed. */
	amdrdna_vk_pm4_event(pm4, AMDRDNA_PM4_EVENT_PIXELS_DRAINED, AMDRDNA_PM4_EVENT_INDEX_DRAIN);
	amdrdna_vk_pm4_event(pm4, AMDRDNA_PM4_EVENT_COMPUTE_DRAINED, AMDRDNA_PM4_EVENT_INDEX_DRAIN);
	amdrdna_vk_pm4_event(pm4, AMDRDNA_PM4_EVENT_COLOUR_META_FLUSHED, 0U);

	/* The caches flushed at the end of the pipe, the value written, waited for. */
	amdrdna_vk_pm4_event_fence(pm4, AMDRDNA_PM4_EVENT_CACHES_FLUSHED, STATE_BARRIER_RELEASE, sync_address, sync_value);
	amdrdna_vk_pm4_wait_value(pm4, sync_address, sync_value);
	amdrdna_vk_pm4_fetcher_sync(pm4);

	/* The shader caches read anew. */
	amdrdna_vk_pm4_cache_acquire(pm4, STATE_BARRIER_ACQUIRE);
}

/*
 * Writes a barrier of the compute queue: the compute programs drained,
 * then an end-of-pipe event that writes back and invalidates the level-2
 * cache and writes sync_value to sync_address, waited for, and the shader
 * caches invalidated.  sync_value must differ from what the word held
 * before (the session's counter).
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_cs.c
 * (gfx10_cs_emit_cache_flush on a compute queue: the compute partial
 * flush, the end-of-pipe release waited for in memory, no fetcher's
 * sync, then the acquire of the shader caches).
 */
void
amdrdna_vk_state_barrier_kernel(
	struct amdrdna_vk_pm4 *pm4,
	uint64_t sync_address,
	uint32_t sync_value)
{
	/* The compute programs drained. */
	amdrdna_vk_pm4_event(pm4, AMDRDNA_PM4_EVENT_COMPUTE_DRAINED, AMDRDNA_PM4_EVENT_INDEX_DRAIN);

	/* The caches written back at the end of the pipe, the value written, waited for. */
	amdrdna_vk_pm4_event_fence(pm4, AMDRDNA_PM4_EVENT_CACHES_FLUSHED, STATE_BARRIER_RELEASE, sync_address, sync_value);
	amdrdna_vk_pm4_wait_value(pm4, sync_address, sync_value);

	/* The shader caches read anew. */
	amdrdna_vk_pm4_cache_acquire(pm4, STATE_BARRIER_ACQUIRE);
}

/*
 * Writes the graphics registers every draw shares: the program units and
 * accumulators, the caches' policies, the binner, the scissors, the
 * rasteriser's fixed state, one sample, no depth, and the geometry
 * engine's index limits.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/common/ac_cmdbuf.c
 * (gfx10_init_graphics_preamble_state) and src/amd/vulkan/radv_queue.c
 * (radv_emit_graphics: the point size and limits, the vertex rounding, the
 * viewport enables, the interpolation control, the NGG control, the
 * restart index and the stipple scale).
 */
static void
state_start_graphics(
	struct amdrdna_vk_pm4 *pm4)
{
	static const uint32_t zeros[STATE_ACCUMULATORS] = { 0U, 0U, 0U, 0U };
	static const uint32_t guard_band[4] = { STATE_FLOAT_ONE, STATE_FLOAT_ONE, STATE_FLOAT_ONE, STATE_FLOAT_ONE };

	/* The programs run on every CU; the pixel program's requests; no accumulators. */
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_PIXEL_UNITS, AMDRDNA_SH_UNITS_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_PIXEL_UNITS_HIGH, AMDRDNA_SH_UNITS_HIGH_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_PIXEL_REQUESTS, AMDRDNA_SH_PIXEL_REQUEST_CONTROL);
	amdrdna_vk_pm4_set_shader_run(pm4, STATE_PIXEL_ACCUMULATORS, zeros, STATE_ACCUMULATORS);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_VERTEX_UNITS, AMDRDNA_SH_UNITS_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_VERTEX_UNITS_HIGH, AMDRDNA_SH_UNITS_HIGH_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_VERTEX_REQUESTS, 0U);
	amdrdna_vk_pm4_set_shader_run(pm4, STATE_VERTEX_ACCUMULATORS, zeros, STATE_ACCUMULATORS);

	/* The depth unit's punch-out off, both units' cache policies. */
	amdrdna_vk_pm4_set_context(pm4, STATE_DEPTH_PUNCH_OUT, STATE_PUNCH_OUT_OFF);
	amdrdna_vk_pm4_set_context(pm4, STATE_DEPTH_CACHE, STATE_DEPTH_CACHE_STREAM);
	amdrdna_vk_pm4_set_context(pm4, STATE_COLOUR_CACHE, STATE_COLOUR_CACHE_STREAM);

	/* The binner off, its limits, the primitive shader's deallocations, the reuse block. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_BINNER_CONTROL, AMDRDNA_CX_BINNER_OFF);
	amdrdna_vk_pm4_set_context(pm4, STATE_BINNER_LIMITS, STATE_BINNER_LIMITS_NAVI10);
	amdrdna_vk_pm4_set_context(pm4, STATE_PRIMITIVE_SHADER_MODE, STATE_DEALLOCATIONS);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VERTEX_REUSE, 14U);
	amdrdna_vk_pm4_set_context(pm4, STATE_CLIP_NGG, 0U);
	amdrdna_vk_pm4_set_context(pm4, STATE_RESTART_INDEX, STATE_RESTART_ALL);

	/* The window and screen scissors over everything, the generic one until a draw sets it. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_WINDOW_OFFSET, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_WINDOW_SCISSOR, AMDRDNA_CX_SCISSOR_NO_WINDOW_OFFSET);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_WINDOW_SCISSOR + 4U, STATE_SCISSOR_FAR);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_CLIP_RECTANGLE_RULE, AMDRDNA_CX_CLIP_RECTANGLE_ALL);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SCREEN_SCISSOR, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SCREEN_SCISSOR + 4U, STATE_SCISSOR_FAR);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_GENERIC_SCISSOR, AMDRDNA_CX_SCISSOR_NO_WINDOW_OFFSET);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_GENERIC_SCISSOR + 4U, STATE_SCISSOR_FAR);

	/* The rasteriser: the edge rule, the viewport's scissor, the walk, the clipping, the viewport enables. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_EDGE_RULE, AMDRDNA_CX_EDGE_RULE_PLAIN);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SCAN_MODE, AMDRDNA_CX_SCAN_VIEWPORT_SCISSOR);
	amdrdna_vk_pm4_set_context(pm4, STATE_SCAN_MODE_MORE, STATE_SCAN_WALK);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_CLIP_CONTROL, AMDRDNA_CX_CLIP_D3D);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VIEWPORT_ENABLE, AMDRDNA_CX_VIEWPORT_ALL);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_RASTER_MODE, 0U);

	/* The vertex rounding, the guard band of 1.0, one sample with every bit of the mask. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SAMPLE_CONFIG, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_VERTEX_ROUNDING, AMDRDNA_CX_VERTEX_ROUNDING_PLAIN);
	amdrdna_vk_pm4_set_context_run(pm4, AMDRDNA_CX_GUARD_BAND, guard_band, 4U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SAMPLE_MASK, AMDRDNA_CX_SAMPLE_MASK_ALL);
	amdrdna_vk_pm4_set_context(pm4, STATE_SAMPLE_MASK_MORE, AMDRDNA_CX_SAMPLE_MASK_ALL);

	/* Points and lines of 1.0, the small primitive filter, the stipple scale. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_POINT_SIZE, AMDRDNA_CX_POINT_SIZE_ONE);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_POINT_LIMITS, AMDRDNA_CX_POINT_LIMITS_ALL);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_LINE_WIDTH, AMDRDNA_CX_LINE_WIDTH_ONE);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_SMALL_PRIMITIVE_FILTER, AMDRDNA_CX_SMALL_PRIMITIVE_FILTER_ON);
	amdrdna_vk_pm4_set_context(pm4, STATE_STIPPLE_SCALE, STATE_FLOAT_ONE);

	/* No depth or stencil; the normal colour mode; no reuse off, the export unconverted. */
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_DEPTH_RENDER, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_DEPTH_LAYOUT, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_STENCIL_LAYOUT, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_DEPTH_CONTROL, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_COLOUR_CONTROL, AMDRDNA_CX_COLOUR_CONTROL_NORMAL);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_REUSE_OFF, 0U);
	amdrdna_vk_pm4_set_context(pm4, AMDRDNA_CX_EXPORT_CONVERSION, 0U);

	/* The geometry engine: every index, no offset, instance 0, no user VGPRs, no stereo, no stipple. */
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_INDEX_MINIMUM, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_INDEX_OFFSET, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_INDEX_MAXIMUM, 0xffffffffU);
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_INSTANCE_BASE, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, STATE_STEREO, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_USER_VGPRS, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, STATE_STIPPLE_VALUE, 0U);
	amdrdna_vk_pm4_set_user_config(pm4, STATE_STIPPLE_STATE, 0U);
}

/*
 * Writes the compute registers every dispatch shares: the coherency
 * delay, the CUs of each engine, no accumulators, no tunnel.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/common/ac_cmdbuf.c
 * (gfx10_init_compute_preamble_state).
 */
static void
state_start_compute(
	struct amdrdna_vk_pm4 *pm4)
{
	static const uint32_t zeros[STATE_ACCUMULATORS] = { 0U, 0U, 0U, 0U };

	/* The delay, the CUs of engines 0 and 1. */
	amdrdna_vk_pm4_set_user_config(pm4, AMDRDNA_UC_COHERENCY_DELAY, STATE_COHERENCY_DELAY);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_UNITS_ENGINE0, STATE_KERNEL_UNITS_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_UNITS_ENGINE1, STATE_KERNEL_UNITS_ALL);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_UNITS_ENGINE2, 0U);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_UNITS_ENGINE3, 0U);

	/* No accumulators, no tunnel. */
	amdrdna_vk_pm4_set_shader_run(pm4, STATE_KERNEL_ACCUMULATORS, zeros, STATE_ACCUMULATORS);
	amdrdna_vk_pm4_set_shader(pm4, AMDRDNA_SH_KERNEL_TUNNEL, 0U);
}

/*
 * Reports the blend factor of a VkBlendFactor; false for one the colour
 * unit does not have.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_pipeline_graphics.h
 * (radv_translate_blend_factor: which VkBlendFactor is which factor).
 */
static bool
state_blend_factor(
	uint32_t factor,
	uint32_t *code)
{
	/* Each factor's own. */
	switch (factor) {
	case VK_BLEND_FACTOR_ZERO:
		*code = AMDRDNA_BLEND_ZERO;
		break;
	case VK_BLEND_FACTOR_ONE:
		*code = AMDRDNA_BLEND_ONE;
		break;
	case VK_BLEND_FACTOR_SRC_COLOR:
		*code = AMDRDNA_BLEND_SOURCE_COLOUR;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
		*code = AMDRDNA_BLEND_ONE_MINUS_SOURCE_COLOUR;
		break;
	case VK_BLEND_FACTOR_DST_COLOR:
		*code = AMDRDNA_BLEND_TARGET_COLOUR;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
		*code = AMDRDNA_BLEND_ONE_MINUS_TARGET_COLOUR;
		break;
	case VK_BLEND_FACTOR_SRC_ALPHA:
		*code = AMDRDNA_BLEND_SOURCE_ALPHA;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
		*code = AMDRDNA_BLEND_ONE_MINUS_SOURCE_ALPHA;
		break;
	case VK_BLEND_FACTOR_DST_ALPHA:
		*code = AMDRDNA_BLEND_TARGET_ALPHA;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
		*code = AMDRDNA_BLEND_ONE_MINUS_TARGET_ALPHA;
		break;
	case VK_BLEND_FACTOR_CONSTANT_COLOR:
		*code = AMDRDNA_BLEND_CONSTANT_COLOUR;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
		*code = AMDRDNA_BLEND_ONE_MINUS_CONSTANT_COLOUR;
		break;
	case VK_BLEND_FACTOR_CONSTANT_ALPHA:
		*code = AMDRDNA_BLEND_CONSTANT_ALPHA;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
		*code = AMDRDNA_BLEND_ONE_MINUS_CONSTANT_ALPHA;
		break;
	case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
		*code = AMDRDNA_BLEND_SOURCE_ALPHA_SATURATED;
		break;
	case VK_BLEND_FACTOR_SRC1_COLOR:
		*code = AMDRDNA_BLEND_SECOND_COLOUR;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
		*code = AMDRDNA_BLEND_ONE_MINUS_SECOND_COLOUR;
		break;
	case VK_BLEND_FACTOR_SRC1_ALPHA:
		*code = AMDRDNA_BLEND_SECOND_ALPHA;
		break;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
		*code = AMDRDNA_BLEND_ONE_MINUS_SECOND_ALPHA;
		break;
	default:
		return false;
	}

	/* Succeeded: the factor is the unit's. */
	return true;
}

/*
 * Reports the blend function of a VkBlendOp; false for one the colour
 * unit does not have.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/vulkan/radv_pipeline_graphics.h
 * (radv_translate_blend_function: subtract is the source less the target,
 * reverse subtract the target less the source).
 */
static bool
state_blend_op(
	uint32_t op,
	uint32_t *code)
{
	/* Each function's own. */
	switch (op) {
	case VK_BLEND_OP_ADD:
		*code = AMDRDNA_BLEND_ADD;
		break;
	case VK_BLEND_OP_SUBTRACT:
		*code = AMDRDNA_BLEND_SUBTRACT;
		break;
	case VK_BLEND_OP_REVERSE_SUBTRACT:
		*code = AMDRDNA_BLEND_REVERSE_SUBTRACT;
		break;
	case VK_BLEND_OP_MIN:
		*code = AMDRDNA_BLEND_MINIMUM;
		break;
	case VK_BLEND_OP_MAX:
		*code = AMDRDNA_BLEND_MAXIMUM;
		break;
	default:
		return false;
	}

	/* Succeeded: the function is the unit's. */
	return true;
}

/* Gives the export unit's combination of a blend function (one the blend function check took). */
static uint32_t
state_blend_shortcut(
	uint32_t op)
{
	uint32_t code;

	/* Each function's own; add for any other (the blend refused it before). */
	switch (op) {
	case VK_BLEND_OP_SUBTRACT:
		code = AMDRDNA_SHORTCUT_SUBTRACT;
		break;
	case VK_BLEND_OP_REVERSE_SUBTRACT:
		code = AMDRDNA_SHORTCUT_REVERSE_SUBTRACT;
		break;
	case VK_BLEND_OP_MIN:
		code = AMDRDNA_SHORTCUT_MINIMUM;
		break;
	case VK_BLEND_OP_MAX:
		code = AMDRDNA_SHORTCUT_MAXIMUM;
		break;
	default:
		code = AMDRDNA_SHORTCUT_ADD;
		break;
	}

	/* Succeeded: the export unit's combination. */
	return code;
}

/* Reports the colour unit's mask of the channels the colour exports carry: four bits for each export with a format. */
static uint32_t
state_export_mask(
	uint32_t formats)
{
	uint32_t mask;
	uint32_t target;

	/* Each target the program exports to. */
	mask = 0U;
	for (target = 0U; target < STATE_TARGETS; target++) {
		if (((formats >> (4U * target)) & 0xfU) != 0U)
			mask |= STATE_EXPORT_CHANNELS << (4U * target);
	}

	/* Succeeded: the channels taken. */
	return mask;
}

/* Reports the largest whole number at or below a fixed point value of STATE_FRACTION fractional bits. */
static int32_t
state_floor(
	int64_t value)
{
	/* Toward minus infinity on either side of zero. */
	if (value >= 0)
		return (int32_t)(value >> STATE_FRACTION);
	return -(int32_t)((-value + ((int64_t)1 << STATE_FRACTION) - 1) >> STATE_FRACTION);
}

/* Reports the smallest whole number at or above a fixed point value of STATE_FRACTION fractional bits. */
static int32_t
state_ceil(
	int64_t value)
{
	/* Toward plus infinity on either side of zero. */
	if (value >= 0)
		return (int32_t)((value + ((int64_t)1 << STATE_FRACTION) - 1) >> STATE_FRACTION);
	return -(int32_t)((-value) >> STATE_FRACTION);
}

/* Reports a coordinate on the screen: from 0 to 16384. */
static uint32_t
state_clamp(
	int32_t value)
{
	/* Within the screen. */
	if (value < 0)
		return 0U;
	if (value > STATE_SCISSOR_MAX)
		return (uint32_t)STATE_SCISSOR_MAX;
	return (uint32_t)value;
}
