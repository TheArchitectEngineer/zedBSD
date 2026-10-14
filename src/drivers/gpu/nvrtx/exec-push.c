/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of the Vulkan executor's push buffer that touch no hardware
 * (see nvrtx-exec.h).
 *
 * The methods and their values are the ones Mesa's NVK (MIT) sends on
 * Turing for the same state: a rendering's colour target
 * (nvk_CmdBeginRendering), the viewport and scissor (nvk_flush_vp_state),
 * the blend (nvk_flush_cb_state), the vertex input (nvk_flush_vi_state and
 * its binding macro), the draw (nvk_mme_build_draw), the clear
 * (nvk_CmdClearAttachments), a barrier (nvk_cmd_flush_wait_dep and
 * nvk_cmd_invalidate_deps), the texture header of a pitch image (NIL) and
 * a sampler (nvk_sampler.c).  Sources (SHA-256, Mesa 25.3.6) are listed
 * in draw-push.c; the code is zedBSD's own.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/nvrtx/nvrtx-exec.h"
#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

/*
 * ==== TURING_A's methods (clc597.h) ====
 */
#define M_WAIT_FOR_IDLE				0x0110U
#define M_SET_INSTANCE_COUNT			0x0220U
#define M_SET_VERTEX_STREAM_SIZE_A		0x0600U
#define M_SET_VERTEX_STREAM_SIZE_STRIDE		0x8U
#define M_SET_COLOR_TARGET_A			0x0800U
#define M_SET_COLOR_TARGET_STRIDE		0x40U
#define M_SET_VIEWPORT_SCALE_X			0x0a00U
#define M_SET_VIEWPORT_CLIP_HORIZONTAL		0x0c00U
#define M_SET_CLEAR_RECT_HORIZONTAL		0x0d6cU
#define M_SET_VERTEX_ARRAY_START		0x0d74U
#define M_SET_COLOR_CLEAR_VALUE			0x0d80U
#define M_INVALIDATE_SHADER_CACHES_NO_WFI	0x0da4U
#define M_SET_SCISSOR_ENABLE			0x0e00U
#define M_SET_SCISSOR_HORIZONTAL		0x0e04U
#define M_SET_SCISSOR_VERTICAL			0x0e08U
#define M_SET_SURFACE_CLIP_HORIZONTAL		0x0ff4U
#define M_SET_CLEAR_SURFACE_CONTROL		0x10f8U
#define M_SET_VERTEX_ID_BASE			0x1118U
#define M_SET_VERTEX_ATTRIBUTE_A		0x1160U
#define M_SET_CT_SELECT				0x121cU
#define M_INVALIDATE_TEXTURE_DATA_CACHE_NO_WFI	0x1288U
#define M_SET_DEPTH_TEST			0x12ccU
#define M_SET_DEPTH_WRITE			0x12e8U
#define M_SET_BLEND				0x1360U
#define M_SET_STENCIL_TEST			0x1380U
#define M_SET_GLOBAL_BASE_VERTEX_INDEX		0x1434U
#define M_SET_GLOBAL_BASE_INSTANCE_INDEX	0x1438U
#define M_SET_ZT_SELECT				0x1538U
#define M_SET_ANTI_ALIAS			0x15d0U
#define M_END					0x1614U
#define M_BEGIN					0x1618U
#define M_SET_VERTEX_STREAM_INSTANCE_A		0x1880U
#define M_OGL_SET_CULL				0x1918U
#define M_OGL_SET_FRONT_FACE			0x191cU
#define M_SET_VIEWPORT_CLIP_CONTROL		0x193cU
#define M_SET_RENDER_ENABLE_OVERRIDE		0x1944U
#define M_SET_LOGIC_OP				0x19c4U
#define M_CLEAR_SURFACE				0x19d0U
#define M_SET_COLOR_COMPRESSION			0x19e0U
#define M_SET_CT_WRITE				0x1a00U
#define M_SET_VERTEX_STREAM_A_FORMAT		0x1c00U
#define M_SET_VERTEX_STREAM_A_STRIDE		0x10U
#define M_SET_BLEND_PER_TARGET_SEPARATE		0x1e00U
#define M_SET_RASTER_ENABLE			0x037cU
#define M_SET_FRONT_POLYGON_MODE		0x0dacU
#define M_SET_VIEWPORT_COORDINATE_SWIZZLE	0x0a18U
#define M_SET_VIEWPORT_Z_CLIP			0x0d7cU
#define M_SET_PROVOKING_VERTEX			0x1684U
#define M_SET_ANTI_ALIAS_ALPHA_CONTROL		0x153cU
#define M_SET_SAMPLE_MASK_X0_Y0			0x0fbcU
#define M_SET_SINGLE_CT_WRITE_CONTROL		0x0f90U

/* The other colour targets, which are left null. */
#define EXEC_COLOR_TARGETS			8U

/* An incrementing method header on the 3D subchannel (0). */
#define EXEC_METHOD_INCREMENT			(1U << 29)

/* The values NVK writes (the fields' meanings beside them). */
#define V_TRUE					1U
#define V_FALSE					0U
#define V_FLOAT_ONE				0x3f800000U
#define V_TARGET_LAYOUT_PITCH			(1U << 12)
#define V_TARGET_NULL_WIDTH			64U
#define V_CT_SELECT_ONE_TARGET			1U
#define V_ANTI_ALIAS_1X1			0U
#define V_VIEWPORT_SWIZZLE_IDENTITY		0x6420U
#define V_Z_CLIP_ZERO_TO_W			1U
#define V_VIEWPORT_CLIP_DRAW			0x181dU
#define V_POLYGON_FILL				0x1b02U
#define V_FRONT_FACE_CCW			0x901U
#define V_SAMPLE_MASK_ALL			0xffffU
#define V_CLEAR_USE_RECT			(1U << 4)
#define V_CLEAR_RGBA				0x3cU
#define V_RENDER_ALWAYS				1U
#define V_RENDER_USE_ENABLE			0U
#define V_STREAM_ENABLE				(1U << 12)
#define V_INVALIDATE_CONSTANTS			(1U << 12)

/* The colour target formats (COLOR_TARGET_FORMAT). */
#define V_TARGET_A8R8G8B8			0xcfU
#define V_TARGET_A8RL8GL8BL8			0xd0U
#define V_TARGET_A8B8G8R8			0xd5U
#define V_TARGET_A8BL8GL8RL8			0xd6U

/* A vertex attribute: inactive, its component widths and its type (float). */
#define V_ATTRIBUTE_INACTIVE			(1U << 6)
#define V_ATTRIBUTE_OFFSET_SHIFT		7U
#define V_ATTRIBUTE_WIDTHS_SHIFT		21U
#define V_ATTRIBUTE_FLOAT			(7U << 27)
#define V_WIDTHS_R32_G32_B32_A32		0x01U
#define V_WIDTHS_R32_G32_B32			0x02U
#define V_WIDTHS_R32_G32			0x04U
#define V_WIDTHS_R32				0x12U
#define V_WIDTHS_INACTIVE			0x01U

/* The draw: the topology, the instances iterated from the first. */
#define V_BEGIN_TRIANGLES			4U
#define V_BEGIN_TRIANGLE_STRIP			5U
#define V_BEGIN_ITERATE				(1U << 31)

/* The blend's operations and factors (the OGL values NVK uses). */
#define V_BLEND_ADD				0x8006U
#define V_BLEND_SUBTRACT			0x800aU
#define V_BLEND_REVERSE_SUBTRACT		0x800bU
#define V_BLEND_MIN				0x8007U
#define V_BLEND_MAX				0x8008U
#define V_FACTOR_ZERO				0x4000U
#define V_FACTOR_ONE				0x4001U
#define V_FACTOR_SRC_COLOR			0x4300U
#define V_FACTOR_ONE_MINUS_SRC_COLOR		0x4301U
#define V_FACTOR_SRC_ALPHA			0x4302U
#define V_FACTOR_ONE_MINUS_SRC_ALPHA		0x4303U
#define V_FACTOR_DST_ALPHA			0x4304U
#define V_FACTOR_ONE_MINUS_DST_ALPHA		0x4305U
#define V_FACTOR_DST_COLOR			0x4306U
#define V_FACTOR_ONE_MINUS_DST_COLOR		0x4307U

/*
 * ==== The texture header (pitch) and the sampler (clc597tex.h, cl9097tex.h) ====
 */
#define TIC_COMPONENTS_A8B8G8R8			0x08U
#define TIC_TYPE_UNORM				2U
#define TIC_SOURCE_ZERO				0U
#define TIC_SOURCE_R				2U
#define TIC_SOURCE_G				3U
#define TIC_SOURCE_B				4U
#define TIC_SOURCE_A				5U
#define TIC_SOURCE_ONE_FLOAT			7U
#define TIC_VERSION_PITCH			2U
#define TIC_TYPE_TWO_D_NO_MIPMAP		7U
#define TIC_PROMOTE_TO_2_V			1U
#define TIC_BORDER_SAMPLER_COLOR		7U
#define TIC_SPREAD_FUNC_TWO			2U
#define TIC_SPREAD_FUNC_ONE			1U
#define TSC_WRAP				0U
#define TSC_MIRROR				1U
#define TSC_CLAMP_TO_EDGE			2U
#define TSC_BORDER				3U
#define TSC_FONT_SIZE_2				1U
#define TSC_MAG_POINT				1U
#define TSC_MAG_LINEAR				2U
#define TSC_MIN_POINT				1U
#define TSC_MIN_LINEAR				2U
#define TSC_MIP_POINT				2U
#define TSC_CUBE_AUTO_SPAN_SEAM			2U
#define TSC_FORCE_UNNORMALIZED			1U

/* The words of a texture header and of a sampler. */
#define EXEC_TIC_WORDS				8U
#define EXEC_TSC_WORDS				8U

/* The fixed point the viewport is worked in: eight fractional bits. */
#define EXEC_FIXED_FRACTION			8U

/* A float's fields. */
#define FLOAT_SIGN				0x80000000U
#define FLOAT_EXPONENT_SHIFT			23U
#define FLOAT_EXPONENT_MASK			0xffU
#define FLOAT_MANTISSA_MASK			0x7fffffU
#define FLOAT_HIDDEN				0x800000U
#define FLOAT_BIAS				127
#define FLOAT_MANTISSA_BITS			23

static size_t exec_method(uint8_t *push, size_t at, uint32_t method, uint32_t count);
static size_t exec_set(uint8_t *push, size_t at, uint32_t method, uint32_t value);
static uint32_t exec_blend_op(uint32_t op);
static uint32_t exec_blend_factor(uint32_t factor);
static uint32_t exec_attribute_widths(uint32_t format);
static uint32_t exec_address_mode(uint32_t mode);
static void exec_field(uint32_t *words, unsigned low, unsigned bits, uint32_t value);
static uint32_t exec_swizzle_source(uint32_t swizzle, const uint32_t *identity);

/*
 * Reports the colour target format of a VkFormat the executor renders
 * to, or false for another.
 */
bool
nvrtx_exec_target_format(
	uint32_t format,
	uint32_t *code)
{
	/* B8G8R8A8 and R8G8B8A8, linear and sRGB. */
	switch (format) {
	case VK_FORMAT_B8G8R8A8_UNORM:
		*code = V_TARGET_A8R8G8B8;
		return true;
	case VK_FORMAT_B8G8R8A8_SRGB:
		*code = V_TARGET_A8RL8GL8BL8;
		return true;
	case VK_FORMAT_R8G8B8A8_UNORM:
		*code = V_TARGET_A8B8G8R8;
		return true;
	case VK_FORMAT_R8G8B8A8_SRGB:
		*code = V_TARGET_A8BL8GL8RL8;
		return true;
	default:
		break;
	}

	/* Any other format is not rendered to. */
	*code = 0U;
	return false;
}

/*
 * Writes colour target 0 (pitch linear) and the rendering's fixed state:
 * the surface clip, the seven other targets null, no depth target, one
 * sample, filled triangles without culling, every sample, no depth,
 * stencil or logic operation, target 0 the one written.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_target(
	uint8_t *push,
	size_t at,
	const struct nvrtx_exec_target *target)
{
	uint32_t format;
	uint32_t index;

	/* The format (one the caller checked). */
	(void)nvrtx_exec_target_format(target->format, &format);

	/* The surface clip: the whole target. */
	at = exec_method(push, at, M_SET_SURFACE_CLIP_HORIZONTAL, 2U);
	nvrtx_put32(push, at, target->width << 16);
	nvrtx_put32(push, at + 4U, target->height << 16);
	at += 8U;

	/* Colour target 0: the address, the pitch in place of the width, the height, the format, pitch layout, one layer. */
	at = exec_method(push, at, M_SET_COLOR_TARGET_A, 9U);
	nvrtx_put32(push, at, (uint32_t)(target->address >> 32));
	nvrtx_put32(push, at + 4U, (uint32_t)(target->address & 0xffffffffU));
	nvrtx_put32(push, at + 8U, target->pitch);
	nvrtx_put32(push, at + 12U, target->height);
	nvrtx_put32(push, at + 16U, format);
	nvrtx_put32(push, at + 20U, V_TARGET_LAYOUT_PITCH);
	nvrtx_put32(push, at + 24U, 1U);
	nvrtx_put32(push, at + 28U, 0U);
	nvrtx_put32(push, at + 32U, 0U);
	at += 36U;
	at = exec_set(push, at, M_SET_COLOR_COMPRESSION, V_FALSE);

	/* The other targets null: no address, a 64-wide pitch, no format, one layer. */
	for (index = 1U; index < EXEC_COLOR_TARGETS; index++) {
		at = exec_method(push, at, M_SET_COLOR_TARGET_A + index * M_SET_COLOR_TARGET_STRIDE, 9U);
		nvrtx_put32(push, at, 0U);
		nvrtx_put32(push, at + 4U, 0U);
		nvrtx_put32(push, at + 8U, V_TARGET_NULL_WIDTH);
		nvrtx_put32(push, at + 12U, 0U);
		nvrtx_put32(push, at + 16U, 0U);
		nvrtx_put32(push, at + 20U, 0U);
		nvrtx_put32(push, at + 24U, 1U);
		nvrtx_put32(push, at + 28U, 0U);
		nvrtx_put32(push, at + 32U, 0U);
		at += 36U;
	}

	/* No depth target, one sample a pixel. */
	at = exec_set(push, at, M_SET_ZT_SELECT, 0U);
	at = exec_set(push, at, M_SET_ANTI_ALIAS, V_ANTI_ALIAS_1X1);

	/* The viewport's swizzle and depth clip, rasterization on, filled, not culled, the first vertex provoking. */
	at = exec_set(push, at, M_SET_VIEWPORT_COORDINATE_SWIZZLE, V_VIEWPORT_SWIZZLE_IDENTITY);
	at = exec_set(push, at, M_SET_VIEWPORT_Z_CLIP, V_Z_CLIP_ZERO_TO_W);
	at = exec_set(push, at, M_SET_RASTER_ENABLE, V_TRUE);
	at = exec_set(push, at, M_SET_VIEWPORT_CLIP_CONTROL, V_VIEWPORT_CLIP_DRAW);
	at = exec_method(push, at, M_SET_FRONT_POLYGON_MODE, 2U);
	nvrtx_put32(push, at, V_POLYGON_FILL);
	nvrtx_put32(push, at + 4U, V_POLYGON_FILL);
	at += 8U;
	at = exec_set(push, at, M_OGL_SET_CULL, V_FALSE);
	at = exec_set(push, at, M_OGL_SET_FRONT_FACE, V_FRONT_FACE_CCW);
	at = exec_set(push, at, M_SET_PROVOKING_VERTEX, 0U);

	/* Every sample, no alpha to coverage. */
	at = exec_set(push, at, M_SET_ANTI_ALIAS_ALPHA_CONTROL, 0U);
	at = exec_method(push, at, M_SET_SAMPLE_MASK_X0_Y0, 4U);
	for (index = 0U; index < 4U; index++)
		nvrtx_put32(push, at + index * 4U, V_SAMPLE_MASK_ALL);
	at += 16U;

	/* No depth, stencil or logic operation; target 0 the one written. */
	at = exec_set(push, at, M_SET_DEPTH_TEST, V_FALSE);
	at = exec_set(push, at, M_SET_DEPTH_WRITE, V_FALSE);
	at = exec_set(push, at, M_SET_STENCIL_TEST, V_FALSE);
	at = exec_set(push, at, M_SET_LOGIC_OP, V_FALSE);
	at = exec_set(push, at, M_SET_SINGLE_CT_WRITE_CONTROL, V_TRUE);
	at = exec_set(push, at, M_SET_CT_SELECT, V_CT_SELECT_ONE_TARGET);

	/* Succeeded: the rendering writes the target. */
	return at;
}

/*
 * Writes viewport 0 from a VkViewport's six floats (x, y, width, height,
 * min depth, max depth, as their bits), as NVK does: the scale half the
 * size, the offset its corner plus that, the depth's scale its range, and
 * the clip its rectangle and depth range.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_viewport(
	uint8_t *push,
	size_t at,
	const uint32_t *viewport)
{
	int64_t x;
	int64_t y;
	int64_t width;
	int64_t height;
	int64_t near;
	int64_t far;
	int64_t ymin;
	int64_t ymax;
	int64_t clip_x;
	int64_t clip_y;
	int64_t clip_width;
	int64_t clip_height;
	uint32_t z_low;
	uint32_t z_high;

	/* The values in fixed point; the depths' range in fixed point too. */
	x = nvrtx_exec_fixed(viewport[0], EXEC_FIXED_FRACTION);
	y = nvrtx_exec_fixed(viewport[1], EXEC_FIXED_FRACTION);
	width = nvrtx_exec_fixed(viewport[2], EXEC_FIXED_FRACTION);
	height = nvrtx_exec_fixed(viewport[3], EXEC_FIXED_FRACTION);
	near = nvrtx_exec_fixed(viewport[4], 16U);
	far = nvrtx_exec_fixed(viewport[5], 16U);

	/* Scale x, y, z, then offset x, y, z. */
	at = exec_method(push, at, M_SET_VIEWPORT_SCALE_X, 6U);
	nvrtx_put32(push, at, nvrtx_exec_float_fixed(width, EXEC_FIXED_FRACTION + 1U));
	nvrtx_put32(push, at + 4U, nvrtx_exec_float_fixed(height, EXEC_FIXED_FRACTION + 1U));
	nvrtx_put32(push, at + 8U, nvrtx_exec_float_fixed(far - near, 16U));
	nvrtx_put32(push, at + 12U, nvrtx_exec_float_fixed(2 * x + width, EXEC_FIXED_FRACTION + 1U));
	nvrtx_put32(push, at + 16U, nvrtx_exec_float_fixed(2 * y + height, EXEC_FIXED_FRACTION + 1U));
	nvrtx_put32(push, at + 20U, viewport[4]);
	at += 24U;

	/* The clip: the rectangle in whole pixels (a negative height reaches up), the depths in order. */
	ymin = y;
	ymax = y + height;
	if (height < 0) {
		ymin = y + height;
		ymax = y;
	}
	clip_x = x >> EXEC_FIXED_FRACTION;
	clip_y = ymin >> EXEC_FIXED_FRACTION;
	clip_width = ((x + width) >> EXEC_FIXED_FRACTION) - clip_x;
	clip_height = (ymax >> EXEC_FIXED_FRACTION) - clip_y;
	if (clip_x < 0)
		clip_x = 0;
	if (clip_y < 0)
		clip_y = 0;
	if (clip_width < 0)
		clip_width = 0;
	if (clip_height < 0)
		clip_height = 0;
	z_low = viewport[4];
	z_high = viewport[5];
	if (far < near) {
		z_low = viewport[5];
		z_high = viewport[4];
	}
	at = exec_method(push, at, M_SET_VIEWPORT_CLIP_HORIZONTAL, 4U);
	nvrtx_put32(push, at, (uint32_t)(clip_x & 0xffffU) | ((uint32_t)(clip_width & 0xffffU) << 16));
	nvrtx_put32(push, at + 4U, (uint32_t)(clip_y & 0xffffU) | ((uint32_t)(clip_height & 0xffffU) << 16));
	nvrtx_put32(push, at + 8U, z_low);
	nvrtx_put32(push, at + 12U, z_high);
	at += 16U;

	/* Succeeded: the viewport is set. */
	return at;
}

/*
 * Writes scissor 0: enabled, from (x0, y0) to (x1, y1), the far edges
 * excluded.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_scissor(
	uint8_t *push,
	size_t at,
	uint32_t x0,
	uint32_t y0,
	uint32_t x1,
	uint32_t y1)
{
	/* Enabled, then the two ranges. */
	at = exec_method(push, at, M_SET_SCISSOR_ENABLE, 3U);
	nvrtx_put32(push, at, V_TRUE);
	nvrtx_put32(push, at + 4U, (x0 & 0xffffU) | ((x1 & 0xffffU) << 16));
	nvrtx_put32(push, at + 8U, (y0 & 0xffffU) | ((y1 & 0xffffU) << 16));
	at += 12U;

	/* Succeeded: the scissor is set. */
	return at;
}

/*
 * Writes the blend of colour target 0: on or off, the separate colour
 * and alpha operations and factors, and the channels written.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_blend(
	uint8_t *push,
	size_t at,
	const struct nvrtx_exec_blend *blend)
{
	uint32_t mask;

	/* On or off. */
	at = exec_set(push, at, M_SET_BLEND, blend->enable);

	/* The separate alpha, the colour's operation and factors, the alpha's. */
	at = exec_method(push, at, M_SET_BLEND_PER_TARGET_SEPARATE, 7U);
	nvrtx_put32(push, at, V_TRUE);
	nvrtx_put32(push, at + 4U, exec_blend_op(blend->colour_op));
	nvrtx_put32(push, at + 8U, exec_blend_factor(blend->colour_source));
	nvrtx_put32(push, at + 12U, exec_blend_factor(blend->colour_destination));
	nvrtx_put32(push, at + 16U, exec_blend_op(blend->alpha_op));
	nvrtx_put32(push, at + 20U, exec_blend_factor(blend->alpha_source));
	nvrtx_put32(push, at + 24U, exec_blend_factor(blend->alpha_destination));
	at += 28U;

	/* The channels written: red, green, blue and alpha a nibble each. */
	mask = 0U;
	if ((blend->write_mask & VK_COLOR_COMPONENT_R_BIT) != 0U)
		mask |= 1U;
	if ((blend->write_mask & VK_COLOR_COMPONENT_G_BIT) != 0U)
		mask |= 1U << 4;
	if ((blend->write_mask & VK_COLOR_COMPONENT_B_BIT) != 0U)
		mask |= 1U << 8;
	if ((blend->write_mask & VK_COLOR_COMPONENT_A_BIT) != 0U)
		mask |= 1U << 12;
	at = exec_set(push, at, M_SET_CT_WRITE, mask);

	/* Succeeded: the blend is set. */
	return at;
}

/*
 * Writes the vertex input: each attribute from its stream at its offset
 * (the others inactive), and each stream enabled with its address,
 * stride, size and rate, or disabled.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_vertex(
	uint8_t *push,
	size_t at,
	const struct nvrtx_exec_vertex *vertex)
{
	const struct nvrtx_exec_attribute *attribute;
	const struct nvrtx_exec_stream *stream;
	uint32_t words[NVRTX_EXEC_ATTRIBUTES];
	uint32_t index;

	/* Every attribute inactive, then each of the vertex input's at its location. */
	for (index = 0U; index < NVRTX_EXEC_ATTRIBUTES; index++)
		words[index] = V_ATTRIBUTE_INACTIVE | (V_WIDTHS_INACTIVE << V_ATTRIBUTE_WIDTHS_SHIFT) | V_ATTRIBUTE_FLOAT;
	for (index = 0U; index < vertex->attribute_count; index++) {
		attribute = &vertex->attributes[index];
		if (attribute->location >= NVRTX_EXEC_ATTRIBUTES)
			continue;
		words[attribute->location] = attribute->stream |
		    (attribute->offset << V_ATTRIBUTE_OFFSET_SHIFT) |
		    (exec_attribute_widths(attribute->format) << V_ATTRIBUTE_WIDTHS_SHIFT) |
		    V_ATTRIBUTE_FLOAT;
	}
	at = exec_method(push, at, M_SET_VERTEX_ATTRIBUTE_A, NVRTX_EXEC_ATTRIBUTES);
	for (index = 0U; index < NVRTX_EXEC_ATTRIBUTES; index++)
		nvrtx_put32(push, at + index * 4U, words[index]);
	at += NVRTX_EXEC_ATTRIBUTES * 4U;

	/* Each stream: its rate, its format word, address and frequency, then its size. */
	for (index = 0U; index < NVRTX_EXEC_STREAMS; index++) {
		stream = &vertex->streams[index];
		if (stream->enabled == 0U) {
			at = exec_set(push, at, M_SET_VERTEX_STREAM_A_FORMAT + index * M_SET_VERTEX_STREAM_A_STRIDE, 0U);
			continue;
		}

		at = exec_set(push, at, M_SET_VERTEX_STREAM_INSTANCE_A + index * 4U, stream->instance);
		at = exec_method(push, at, M_SET_VERTEX_STREAM_A_FORMAT + index * M_SET_VERTEX_STREAM_A_STRIDE, 4U);
		nvrtx_put32(push, at, (stream->stride & 0xfffU) | V_STREAM_ENABLE);
		nvrtx_put32(push, at + 4U, (uint32_t)(stream->address >> 32));
		nvrtx_put32(push, at + 8U, (uint32_t)(stream->address & 0xffffffffU));
		nvrtx_put32(push, at + 12U, stream->instance);
		at += 16U;
		at = exec_method(push, at, M_SET_VERTEX_STREAM_SIZE_A + index * M_SET_VERTEX_STREAM_SIZE_STRIDE, 2U);
		nvrtx_put32(push, at, (uint32_t)(stream->bytes >> 32));
		nvrtx_put32(push, at + 4U, (uint32_t)(stream->bytes & 0xffffffffU));
		at += 8U;
	}

	/* Succeeded: the vertex input is set. */
	return at;
}

/*
 * Writes a draw: the bases, the instances (iterated from the first), then
 * BEGIN with the topology, the vertex range and END.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_draw(
	uint8_t *push,
	size_t at,
	uint32_t topology,
	uint32_t first,
	uint32_t count,
	uint32_t instances,
	uint32_t first_instance)
{
	uint32_t begin;

	/* The topology: a list or a strip of triangles. */
	begin = V_BEGIN_TRIANGLES | V_BEGIN_ITERATE;
	if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
		begin = V_BEGIN_TRIANGLE_STRIP | V_BEGIN_ITERATE;

	/* The vertex numbers from the first, the instances from the first instance. */
	at = exec_set(push, at, M_SET_GLOBAL_BASE_VERTEX_INDEX, 0U);
	at = exec_set(push, at, M_SET_VERTEX_ID_BASE, 0U);
	at = exec_set(push, at, M_SET_GLOBAL_BASE_INSTANCE_INDEX, first_instance);
	at = exec_set(push, at, M_SET_INSTANCE_COUNT, instances);

	/* The primitives. */
	at = exec_set(push, at, M_BEGIN, begin);
	at = exec_method(push, at, M_SET_VERTEX_ARRAY_START, 2U);
	nvrtx_put32(push, at, first);
	nvrtx_put32(push, at + 4U, count);
	at += 8U;
	at = exec_set(push, at, M_END, 0U);

	/* Succeeded: the draw is written. */
	return at;
}

/*
 * Writes a clear of colour target 0 over a rectangle (the far edges
 * excluded) to a colour (four floats as their bits), all four channels,
 * whatever the render enable says.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_clear(
	uint8_t *push,
	size_t at,
	const uint32_t *colour,
	uint32_t x0,
	uint32_t y0,
	uint32_t x1,
	uint32_t y1)
{
	uint32_t index;

	/* Rendered always, by the clear rectangle. */
	at = exec_set(push, at, M_SET_RENDER_ENABLE_OVERRIDE, V_RENDER_ALWAYS);
	at = exec_set(push, at, M_SET_CLEAR_SURFACE_CONTROL, V_CLEAR_USE_RECT);

	/* The colour. */
	at = exec_method(push, at, M_SET_COLOR_CLEAR_VALUE, 4U);
	for (index = 0U; index < 4U; index++)
		nvrtx_put32(push, at + index * 4U, colour[index]);
	at += 16U;

	/* The rectangle, then the clear. */
	at = exec_method(push, at, M_SET_CLEAR_RECT_HORIZONTAL, 2U);
	nvrtx_put32(push, at, (x0 & 0xffffU) | ((x1 & 0xffffU) << 16));
	nvrtx_put32(push, at + 4U, (y0 & 0xffffU) | ((y1 & 0xffffU) << 16));
	at += 8U;
	at = exec_set(push, at, M_CLEAR_SURFACE, V_CLEAR_RGBA);

	/* Back to the render enable. */
	at = exec_set(push, at, M_SET_RENDER_ENABLE_OVERRIDE, V_RENDER_USE_ENABLE);

	/* Succeeded: the rectangle is cleared. */
	return at;
}

/*
 * Writes the barrier between work that wrote an image and work that reads
 * it: the 3D class waits for idle, then the texture data and the constant
 * caches are invalidated.
 *
 * Returns where the push buffer goes on.
 */
size_t
nvrtx_exec_barrier(
	uint8_t *push,
	size_t at)
{
	/* Everything before has finished. */
	at = exec_set(push, at, M_WAIT_FOR_IDLE, 0U);

	/* Nothing after reads a stale texel or constant. */
	at = exec_set(push, at, M_INVALIDATE_TEXTURE_DATA_CACHE_NO_WFI, 0U);
	at = exec_set(push, at, M_INVALIDATE_SHADER_CACHES_NO_WFI, V_INVALIDATE_CONSTANTS);

	/* Succeeded: the barrier is written. */
	return at;
}

/*
 * Writes the texture header of a pitch-linear 2D image of four 8-bit
 * channels (B8G8R8A8 or R8G8B8A8): the header reads the bytes as
 * A8B8G8R8 (red in the lowest byte) and the sources move the image's
 * channels and a view's swizzle into place.  sRGB is read as UNORM.
 */
void
nvrtx_exec_texture_header(
	uint32_t *header,
	uint64_t address,
	uint32_t width,
	uint32_t height,
	uint32_t pitch,
	uint32_t format,
	const uint32_t *swizzle)
{
	static const uint32_t rgba[4] = { TIC_SOURCE_R, TIC_SOURCE_G, TIC_SOURCE_B, TIC_SOURCE_A };
	static const uint32_t bgra[4] = { TIC_SOURCE_B, TIC_SOURCE_G, TIC_SOURCE_R, TIC_SOURCE_A };
	const uint32_t *identity;

	/* Where the image's red, green, blue and alpha are among the header's bytes. */
	identity = rgba;
	if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB)
		identity = bgra;

	/* Starts empty. */
	kern_memset(header, 0, EXEC_TIC_WORDS * sizeof(uint32_t));

	/* The format: four 8-bit unsigned normalized channels, each source after the view's swizzle. */
	exec_field(header, 0U, 7U, TIC_COMPONENTS_A8B8G8R8);
	exec_field(header, 7U, 3U, TIC_TYPE_UNORM);
	exec_field(header, 10U, 3U, TIC_TYPE_UNORM);
	exec_field(header, 13U, 3U, TIC_TYPE_UNORM);
	exec_field(header, 16U, 3U, TIC_TYPE_UNORM);
	exec_field(header, 19U, 3U, exec_swizzle_source(swizzle[0], identity));
	exec_field(header, 22U, 3U, exec_swizzle_source(swizzle[1], identity));
	exec_field(header, 25U, 3U, exec_swizzle_source(swizzle[2], identity));
	exec_field(header, 28U, 3U, exec_swizzle_source(swizzle[3], identity));

	/* The address (bits 31:5 and 48:32), the pitch header, the pitch (bits 20:5). */
	exec_field(header, 37U, 27U, (uint32_t)((address & 0xffffffffU) >> 5));
	exec_field(header, 64U, 17U, (uint32_t)(address >> 32));
	exec_field(header, 85U, 3U, TIC_VERSION_PITCH);
	exec_field(header, 96U, 16U, pitch >> 5);

	/* The level of detail quality, high. */
	exec_field(header, 112U, 1U, 1U);
	exec_field(header, 113U, 1U, 1U);
	exec_field(header, 114U, 1U, 1U);

	/* The size and the type: 2D without mipmaps, one level, depth one. */
	exec_field(header, 128U, 17U, width - 1U);
	exec_field(header, 151U, 4U, TIC_TYPE_TWO_D_NO_MIPMAP);
	exec_field(header, 160U, 16U, height - 1U);

	/* NIL's sector promotion, border from the sampler, normalized coordinates and spread functions. */
	exec_field(header, 155U, 2U, TIC_PROMOTE_TO_2_V);
	exec_field(header, 157U, 3U, TIC_BORDER_SAMPLER_COLOR);
	exec_field(header, 191U, 1U, 1U);
	exec_field(header, 215U, 2U, TIC_SPREAD_FUNC_TWO);
	exec_field(header, 217U, 2U, TIC_SPREAD_FUNC_ONE);
}

/*
 * Writes a sampler as NVK fills one: the address modes, the font filter
 * and sRGB border fields, the filters (nearest or linear, one level), the
 * seamless cube setting and the coordinates' normalization.
 */
void
nvrtx_exec_sampler_words(
	uint32_t *words,
	const struct nvrtx_exec_sampler *sampler)
{
	uint32_t mag;
	uint32_t min;

	/* Starts empty. */
	kern_memset(words, 0, EXEC_TSC_WORDS * sizeof(uint32_t));

	/* Word 0: the address modes, the border conversion and the font filter. */
	exec_field(words, 0U, 3U, exec_address_mode(sampler->address_u));
	exec_field(words, 3U, 3U, exec_address_mode(sampler->address_v));
	exec_field(words, 6U, 3U, exec_address_mode(sampler->address_w));
	exec_field(words, 13U, 1U, 1U);
	exec_field(words, 14U, 3U, TSC_FONT_SIZE_2);
	exec_field(words, 17U, 3U, TSC_FONT_SIZE_2);

	/* Word 1: the filters, one level, the cube seams, the normalization. */
	mag = TSC_MAG_POINT;
	if (sampler->mag_filter == VK_FILTER_LINEAR)
		mag = TSC_MAG_LINEAR;
	min = TSC_MIN_POINT;
	if (sampler->min_filter == VK_FILTER_LINEAR)
		min = TSC_MIN_LINEAR;
	exec_field(words, 32U + 0U, 3U, mag);
	exec_field(words, 32U + 4U, 2U, min);
	exec_field(words, 32U + 6U, 2U, TSC_MIP_POINT);
	exec_field(words, 32U + 8U, 2U, TSC_CUBE_AUTO_SPAN_SEAM);
	if (sampler->unnormalized != 0U)
		exec_field(words, 32U + 25U, 1U, TSC_FORCE_UNNORMALIZED);
}

/* Reports the float bits of a whole number (exact up to 2^24). */
uint32_t
nvrtx_exec_float(
	int32_t value)
{
	uint32_t bits;

	/* A whole number is a fixed point value without fraction. */
	bits = nvrtx_exec_float_fixed((int64_t)value, 0U);

	/* Succeeded: the float's bits. */
	return bits;
}

/*
 * Reports a float, given by its bits, in fixed point with fraction
 * fractional bits, truncated toward zero; a value too large for 40
 * integer bits saturates, a denormal is zero.
 */
int64_t
nvrtx_exec_fixed(
	uint32_t bits,
	uint32_t fraction)
{
	int64_t value;
	int32_t exponent;
	int32_t shift;
	uint64_t mantissa;

	/* The exponent and the mantissa with its hidden bit. */
	exponent = (int32_t)((bits >> FLOAT_EXPONENT_SHIFT) & FLOAT_EXPONENT_MASK);
	if (exponent == 0)
		return 0;
	mantissa = (uint64_t)((bits & FLOAT_MANTISSA_MASK) | FLOAT_HIDDEN);

	/* The mantissa moved to the fixed point's scale. */
	shift = exponent - FLOAT_BIAS - FLOAT_MANTISSA_BITS + (int32_t)fraction;
	if (shift > 39) {
		value = (int64_t)1 << 62;
	} else if (shift >= 0) {
		value = (int64_t)(mantissa << shift);
	} else if (shift > -64) {
		value = (int64_t)(mantissa >> (uint32_t)(-shift));
	} else {
		value = 0;
	}

	/* The sign. */
	if ((bits & FLOAT_SIGN) != 0U)
		value = -value;

	/* Succeeded: the value in fixed point. */
	return value;
}

/*
 * Reports the float bits of a fixed point value with fraction fractional
 * bits: exact when its significant bits fit the mantissa, truncated
 * otherwise.
 */
uint32_t
nvrtx_exec_float_fixed(
	int64_t value,
	uint32_t fraction)
{
	uint64_t magnitude;
	uint32_t sign;
	uint32_t mantissa;
	int32_t top;
	int32_t exponent;

	/* Zero has no bits set. */
	if (value == 0)
		return 0U;

	/* The sign and the magnitude. */
	sign = 0U;
	magnitude = (uint64_t)value;
	if (value < 0) {
		sign = FLOAT_SIGN;
		magnitude = (uint64_t)(-value);
	}

	/* The highest bit set is the hidden bit. */
	top = 63;
	while ((magnitude & ((uint64_t)1 << top)) == 0U)
		top--;

	/* The exponent; a value too small for a normal float is zero. */
	exponent = top - (int32_t)fraction + FLOAT_BIAS;
	if (exponent <= 0)
		return 0U;

	/* The mantissa's 23 bits below the hidden bit. */
	if (top > FLOAT_MANTISSA_BITS) {
		mantissa = (uint32_t)(magnitude >> (uint32_t)(top - FLOAT_MANTISSA_BITS));
	} else {
		mantissa = (uint32_t)(magnitude << (uint32_t)(FLOAT_MANTISSA_BITS - top));
	}

	/* Succeeded: the float's bits. */
	return sign | ((uint32_t)exponent << FLOAT_EXPONENT_SHIFT) | (mantissa & FLOAT_MANTISSA_MASK);
}

/* Writes a method header on the 3D subchannel: incrementing, count words from method. */
static size_t
exec_method(
	uint8_t *push,
	size_t at,
	uint32_t method,
	uint32_t count)
{
	/* The header: incrementing, the count, the subchannel (0), the method in words. */
	nvrtx_put32(push, at, EXEC_METHOD_INCREMENT | (count << 16) | (method >> 2));

	/* Succeeded: the words follow. */
	return at + 4U;
}

/* Writes one method with one word. */
static size_t
exec_set(
	uint8_t *push,
	size_t at,
	uint32_t method,
	uint32_t value)
{
	/* The header, then the word. */
	at = exec_method(push, at, method, 1U);
	nvrtx_put32(push, at, value);

	/* Succeeded: past the word. */
	return at + 4U;
}

/* Reports the class's value of a VkBlendOp (addition for any other). */
static uint32_t
exec_blend_op(
	uint32_t op)
{
	/* The five operations. */
	switch (op) {
	case VK_BLEND_OP_SUBTRACT:
		return V_BLEND_SUBTRACT;
	case VK_BLEND_OP_REVERSE_SUBTRACT:
		return V_BLEND_REVERSE_SUBTRACT;
	case VK_BLEND_OP_MIN:
		return V_BLEND_MIN;
	case VK_BLEND_OP_MAX:
		return V_BLEND_MAX;
	default:
		break;
	}

	/* Succeeded: addition. */
	return V_BLEND_ADD;
}

/* Reports the class's value of a VkBlendFactor (one for one the executor does not blend with). */
static uint32_t
exec_blend_factor(
	uint32_t factor)
{
	/* The factors of colour and alpha. */
	switch (factor) {
	case VK_BLEND_FACTOR_ZERO:
		return V_FACTOR_ZERO;
	case VK_BLEND_FACTOR_SRC_COLOR:
		return V_FACTOR_SRC_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
		return V_FACTOR_ONE_MINUS_SRC_COLOR;
	case VK_BLEND_FACTOR_DST_COLOR:
		return V_FACTOR_DST_COLOR;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
		return V_FACTOR_ONE_MINUS_DST_COLOR;
	case VK_BLEND_FACTOR_SRC_ALPHA:
		return V_FACTOR_SRC_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
		return V_FACTOR_ONE_MINUS_SRC_ALPHA;
	case VK_BLEND_FACTOR_DST_ALPHA:
		return V_FACTOR_DST_ALPHA;
	case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
		return V_FACTOR_ONE_MINUS_DST_ALPHA;
	default:
		break;
	}

	/* Succeeded: one. */
	return V_FACTOR_ONE;
}

/* Reports the component widths of a float vertex format. */
static uint32_t
exec_attribute_widths(
	uint32_t format)
{
	/* One to four 32-bit floats. */
	switch (format) {
	case VK_FORMAT_R32_SFLOAT:
		return V_WIDTHS_R32;
	case VK_FORMAT_R32G32_SFLOAT:
		return V_WIDTHS_R32_G32;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return V_WIDTHS_R32_G32_B32;
	default:
		break;
	}

	/* Succeeded: four floats. */
	return V_WIDTHS_R32_G32_B32_A32;
}

/* Reports the sampler's value of a VkSamplerAddressMode (clamp to edge for any other). */
static uint32_t
exec_address_mode(
	uint32_t mode)
{
	/* Repeat, mirror and border. */
	switch (mode) {
	case VK_SAMPLER_ADDRESS_MODE_REPEAT:
		return TSC_WRAP;
	case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
		return TSC_MIRROR;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
		return TSC_BORDER;
	default:
		break;
	}

	/* Succeeded: clamp to edge. */
	return TSC_CLAMP_TO_EDGE;
}

/* Sets a field of bits words, from bit low (counted over the words) for bits bits. */
static void
exec_field(
	uint32_t *words,
	unsigned low,
	unsigned bits,
	uint32_t value)
{
	unsigned bit;
	unsigned position;

	/* Bit by bit, so a field may cross a word. */
	for (bit = 0U; bit < bits; bit++) {
		position = low + bit;
		if (((value >> bit) & 1U) != 0U)
			words[position / 32U] |= 1U << (position % 32U);
	}
}

/*
 * Reports a header source for a channel of a view's swizzle: one of the
 * image's channels (through where the format keeps it), zero or one.
 */
static uint32_t
exec_swizzle_source(
	uint32_t swizzle,
	const uint32_t *identity)
{
	/* The constants, then the image's channels. */
	switch (swizzle) {
	case VK_COMPONENT_SWIZZLE_ZERO:
		return TIC_SOURCE_ZERO;
	case VK_COMPONENT_SWIZZLE_ONE:
		return TIC_SOURCE_ONE_FLOAT;
	case VK_COMPONENT_SWIZZLE_R:
		return identity[0];
	case VK_COMPONENT_SWIZZLE_G:
		return identity[1];
	case VK_COMPONENT_SWIZZLE_B:
		return identity[2];
	default:
		break;
	}

	/* Succeeded: alpha. */
	return identity[3];
}
