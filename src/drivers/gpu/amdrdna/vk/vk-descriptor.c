/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The descriptors a draw's resource table holds for what it reads
 * (amdrdna-vk-exec.h): the image descriptor of a linear 2D image of four
 * bytes a pixel, the sampler descriptor of a VkSampler, and the vertex
 * buffer formats.  The buffer descriptors themselves are the compiler's
 * (gfx10-program.h).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-exec.h"

/*
 * The image formats a descriptor reads: four 8-bit unorm channels, and the
 * same read as sRGB.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json (the
 * enum GFX10_FORMAT: GFX10_FORMAT_8_8_8_8_UNORM 56,
 * GFX10_FORMAT_8_8_8_8_SRGB 130).
 */
#define DESCRIPTOR_FORMAT_UNORM		56U
#define DESCRIPTOR_FORMAT_SRGB		130U

/*
 * The image descriptor's fields: word 1 the address's bits 47:40 (7:0),
 * the format (28:20) and the width less one's low two bits (31:30); word 2
 * the rest of the width less one (11:0), the height less one (27:14) and
 * the resource level (31); word 3 the channel selects (3 bits each from
 * 0), the border colour swizzle (27:25) and the type (31:28); word 5 the
 * performance modulation (22:20).  The base and last level, the swizzle
 * mode (linear), the depth and the first layer are 0.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json
 * (SQ_IMG_RSRC_WORD1: BASE_ADDRESS_HI, FORMAT, WIDTH_LO;
 * SQ_IMG_RSRC_WORD2: WIDTH_HI, HEIGHT, RESOURCE_LEVEL; SQ_IMG_RSRC_WORD3:
 * DST_SEL_X .. W, BASE_LEVEL, LAST_LEVEL, SW_MODE, BC_SWIZZLE, TYPE; the
 * enum SQ_IMG_RSRC_WORD3__BC_SWIZZLE: BC_SWIZZLE_XYZW 0, BC_SWIZZLE_ZYXW
 * 4; SQ_IMG_RSRC_WORD5: PERF_MOD), src/amd/registers/gfx9.json (the
 * enums SQ_RSRC_IMG_TYPE: SQ_RSRC_IMG_2D 9; SQ_SEL_XYZW01: SQ_SEL_0 0,
 * SQ_SEL_1 1, SQ_SEL_X 4 .. SQ_SEL_W 7) and src/amd/common/ac_descriptors.c
 * (ac_build_gfx10_texture_descriptor: RESOURCE_LEVEL 1 below GFX11,
 * PERF_MOD 4, word 0 the address from bit 8).
 */
#define DESCRIPTOR_ADDRESS_SHIFT	8U
#define DESCRIPTOR_ADDRESS_HIGH_SHIFT	40U
#define DESCRIPTOR_FORMAT_SHIFT		20U
#define DESCRIPTOR_WIDTH_LOW_SHIFT	30U
#define DESCRIPTOR_HEIGHT_SHIFT		14U
#define DESCRIPTOR_RESOURCE_LEVEL	(1U << 31)
#define DESCRIPTOR_SELECT_BITS		3U
#define DESCRIPTOR_SELECT_ZERO		0U
#define DESCRIPTOR_SELECT_ONE		1U
#define DESCRIPTOR_SELECT_X		4U
#define DESCRIPTOR_BORDER_SWIZZLE_SHIFT	25U
#define DESCRIPTOR_BORDER_SWIZZLE_ZYXW	4U
#define DESCRIPTOR_TYPE_2D		(9U << 28)
#define DESCRIPTOR_PERFORMANCE		(4U << 20)

/*
 * The sampler descriptor's fields: word 0 the clamp of x, y and z (2:0,
 * 5:3, 8:6), the depth compare function (14:12), the unnormalized
 * coordinates (15) and the coordinate truncation (27); word 1 the minimum
 * and maximum level of detail (11:0, 23:12, unsigned 4.8); word 2 the
 * magnifying and minifying filters (21:20, 23:22) and the mipmap filter
 * (27:26); word 3 the border colour type (31:30).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json
 * (SQ_IMG_SAMP_WORD0: CLAMP_X, CLAMP_Y, CLAMP_Z, DEPTH_COMPARE_FUNC,
 * FORCE_UNNORMALIZED, TRUNC_COORD; SQ_IMG_SAMP_WORD1: MIN_LOD, MAX_LOD;
 * SQ_IMG_SAMP_WORD2: XY_MAG_FILTER, XY_MIN_FILTER, MIP_FILTER;
 * SQ_IMG_SAMP_WORD3: BORDER_COLOR_TYPE) and src/amd/common/ac_descriptors.c
 * (ac_build_sampler_descriptor: the levels of detail clamped to 0 .. 15
 * in 4.8).
 */
#define SAMPLER_CLAMP_Y_SHIFT		3U
#define SAMPLER_CLAMP_Z_SHIFT		6U
#define SAMPLER_COMPARE_SHIFT		12U
#define SAMPLER_UNNORMALIZED		(1U << 15)
#define SAMPLER_TRUNCATE		(1U << 27)
#define SAMPLER_MAX_LOD			((15U << 8) << 12)
#define SAMPLER_MAG_SHIFT		20U
#define SAMPLER_MIN_SHIFT		22U
#define SAMPLER_MIP_SHIFT		26U
#define SAMPLER_BORDER_SHIFT		30U

/*
 * The clamps, the filters, the mipmap filters, the border colours and the
 * compare functions, as VkSampler's are set.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx9.json (the enums
 * SQ_TEX_CLAMP: SQ_TEX_WRAP 0, SQ_TEX_MIRROR 1, SQ_TEX_CLAMP_LAST_TEXEL 2,
 * SQ_TEX_MIRROR_ONCE_LAST_TEXEL 3, SQ_TEX_CLAMP_BORDER 6;
 * SQ_TEX_XY_FILTER: SQ_TEX_XY_FILTER_POINT 0, SQ_TEX_XY_FILTER_BILINEAR 1;
 * SQ_TEX_MIP_FILTER: SQ_TEX_MIP_FILTER_POINT 1, SQ_TEX_MIP_FILTER_LINEAR 2;
 * SQ_TEX_BORDER_COLOR: SQ_TEX_BORDER_COLOR_TRANS_BLACK 0,
 * SQ_TEX_BORDER_COLOR_OPAQUE_BLACK 1, SQ_TEX_BORDER_COLOR_OPAQUE_WHITE 2;
 * SQ_TEX_DEPTH_COMPARE: NEVER 0 .. ALWAYS 7 in VkCompareOp's order)
 * and src/amd/vulkan/radv_sampler.c (radv_tex_wrap, radv_tex_filter,
 * radv_tex_mipfilter, radv_tex_bordercolor, radv_tex_compare: which
 * Vulkan value maps to which; radv_init_sampler: truncation for a sampler
 * nearest both ways).
 */
#define SAMPLER_WRAP			0U
#define SAMPLER_MIRROR			1U
#define SAMPLER_CLAMP_EDGE		2U
#define SAMPLER_MIRROR_ONCE_EDGE	3U
#define SAMPLER_CLAMP_BORDER		6U
#define SAMPLER_POINT			0U
#define SAMPLER_BILINEAR		1U
#define SAMPLER_MIP_POINT		1U
#define SAMPLER_MIP_LINEAR		2U
#define SAMPLER_BORDER_TRANSPARENT	0U
#define SAMPLER_BORDER_BLACK		1U
#define SAMPLER_BORDER_WHITE		2U
#define SAMPLER_COMPARE_MASK		7U

static uint32_t descriptor_select(uint32_t swizzle, uint32_t channel, bool alternate);
static uint32_t sampler_clamp(uint32_t mode);
static uint32_t sampler_filter(uint32_t filter);
static uint32_t sampler_border(uint32_t colour);

/*
 * Reports the descriptor format of an image format, and whether its
 * channels lie blue first in memory (the descriptor's channel selects then
 * swap red and blue).
 */
bool
amdrdna_vk_image_format(
	uint32_t format,
	uint32_t *code,
	bool *alternate)
{
	/* The four formats of four 8-bit channels. */
	switch (format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		*code = DESCRIPTOR_FORMAT_UNORM;
		*alternate = false;
		break;
	case VK_FORMAT_B8G8R8A8_UNORM:
		*code = DESCRIPTOR_FORMAT_UNORM;
		*alternate = true;
		break;
	case VK_FORMAT_R8G8B8A8_SRGB:
		*code = DESCRIPTOR_FORMAT_SRGB;
		*alternate = false;
		break;
	case VK_FORMAT_B8G8R8A8_SRGB:
		*code = DESCRIPTOR_FORMAT_SRGB;
		*alternate = true;
		break;
	default:
		return false;
	}

	/* Succeeded: the format is read. */
	return true;
}

/*
 * Writes the eight words of the image descriptor of a linear 2D image at
 * address (256-byte aligned), its size and format, read through a view's
 * swizzle (VkComponentSwizzle of red, green, blue and alpha, identity
 * resolved; NULL for the identity).  Its rows are amdrdna_vk_pitch()
 * apart.  Returns 0, or EINVAL for a format, a size or an address the
 * descriptor cannot hold.
 */
int
amdrdna_vk_image_descriptor(
	uint64_t address,
	uint32_t width,
	uint32_t height,
	uint32_t format,
	const uint32_t *swizzle,
	uint32_t *words)
{
	uint32_t code;
	uint32_t channel;
	uint32_t source;
	bool alternate;

	/* A format, size and address the descriptor holds. */
	if (!amdrdna_vk_image_format(format, &code, &alternate))
		return EINVAL;
	if (width == 0U || height == 0U || width > AMDRDNA_VK_IMAGE_SIDE_MAX || height > AMDRDNA_VK_IMAGE_SIDE_MAX)
		return EINVAL;
	if ((address & (AMDRDNA_VK_RESOURCE_ALIGN - 1U)) != 0U)
		return EINVAL;

	/* The address, the format and the size. */
	kern_memset(words, 0, AMDRDNA_IMAGE_WORDS * sizeof(uint32_t));
	words[0] = (uint32_t)(address >> DESCRIPTOR_ADDRESS_SHIFT);
	words[1] = (uint32_t)(address >> DESCRIPTOR_ADDRESS_HIGH_SHIFT) & 0xffU;
	words[1] |= code << DESCRIPTOR_FORMAT_SHIFT;
	words[1] |= ((width - 1U) & 3U) << DESCRIPTOR_WIDTH_LOW_SHIFT;
	words[2] = ((width - 1U) >> 2) & 0xfffU;
	words[2] |= (height - 1U) << DESCRIPTOR_HEIGHT_SHIFT;
	words[2] |= DESCRIPTOR_RESOURCE_LEVEL;

	/* The channel each of red, green, blue and alpha reads. */
	for (channel = 0U; channel < 4U; channel++) {
		source = VK_COMPONENT_SWIZZLE_R + channel;
		if (swizzle != NULL)
			source = swizzle[channel];
		words[3] |= descriptor_select(source, channel, alternate) << (DESCRIPTOR_SELECT_BITS * channel);
	}

	/* A blue-first format's border colour swizzled too; the type; the modulation. */
	if (alternate)
		words[3] |= DESCRIPTOR_BORDER_SWIZZLE_ZYXW << DESCRIPTOR_BORDER_SWIZZLE_SHIFT;
	words[3] |= DESCRIPTOR_TYPE_2D;
	words[5] = DESCRIPTOR_PERFORMANCE;

	/* Succeeded: the descriptor reads the image. */
	return 0;
}

/* Writes the four words of the sampler descriptor of a VkSampler. */
void
amdrdna_vk_sampler_descriptor(
	const struct amdrdna_vk_sampler *sampler,
	uint32_t *words)
{
	/* The clamps, the compare, the coordinates in pixels. */
	kern_memset(words, 0, AMDRDNA_SAMPLER_WORDS * sizeof(uint32_t));
	words[0] = sampler_clamp(sampler->address_u);
	words[0] |= sampler_clamp(sampler->address_v) << SAMPLER_CLAMP_Y_SHIFT;
	words[0] |= sampler_clamp(sampler->address_w) << SAMPLER_CLAMP_Z_SHIFT;
	if (sampler->compare_enable != 0U)
		words[0] |= (sampler->compare_op & SAMPLER_COMPARE_MASK) << SAMPLER_COMPARE_SHIFT;
	if (sampler->unnormalized != 0U)
		words[0] |= SAMPLER_UNNORMALIZED;
	if (sampler->mag_filter == VK_FILTER_NEAREST && sampler->min_filter == VK_FILTER_NEAREST)
		words[0] |= SAMPLER_TRUNCATE;

	/* Every level of detail from 0 (the image has one level). */
	words[1] = SAMPLER_MAX_LOD;

	/* The filters. */
	words[2] = sampler_filter(sampler->mag_filter) << SAMPLER_MAG_SHIFT;
	words[2] |= sampler_filter(sampler->min_filter) << SAMPLER_MIN_SHIFT;
	if (sampler->mipmap_mode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
		words[2] |= SAMPLER_MIP_LINEAR << SAMPLER_MIP_SHIFT;
	} else {
		words[2] |= SAMPLER_MIP_POINT << SAMPLER_MIP_SHIFT;
	}

	/* The border colour. */
	words[3] = sampler_border(sampler->border_color) << SAMPLER_BORDER_SHIFT;
}

/*
 * Reports the buffer descriptor format of a vertex attribute format, its
 * channels and its bytes; false for a format the executor does not fetch.
 */
bool
amdrdna_vk_vertex_format(
	uint32_t format,
	uint32_t *code,
	uint32_t *components,
	uint32_t *bytes)
{
	/* Floats, unsigned integers and four unorm bytes. */
	switch (format) {
	case VK_FORMAT_R32_SFLOAT:
		*code = AMDRDNA_GFX10_VERTEX_FLOAT1;
		*components = 1U;
		break;
	case VK_FORMAT_R32G32_SFLOAT:
		*code = AMDRDNA_GFX10_VERTEX_FLOAT2;
		*components = 2U;
		break;
	case VK_FORMAT_R32G32B32_SFLOAT:
		*code = AMDRDNA_GFX10_VERTEX_FLOAT3;
		*components = 3U;
		break;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		*code = AMDRDNA_GFX10_VERTEX_FLOAT4;
		*components = 4U;
		break;
	case VK_FORMAT_R32_UINT:
		*code = AMDRDNA_GFX10_VERTEX_UINT1;
		*components = 1U;
		break;
	case VK_FORMAT_R32G32_UINT:
		*code = AMDRDNA_GFX10_VERTEX_UINT2;
		*components = 2U;
		break;
	case VK_FORMAT_R32G32B32_UINT:
		*code = AMDRDNA_GFX10_VERTEX_UINT3;
		*components = 3U;
		break;
	case VK_FORMAT_R32G32B32A32_UINT:
		*code = AMDRDNA_GFX10_VERTEX_UINT4;
		*components = 4U;
		break;
	case VK_FORMAT_R8G8B8A8_UNORM:
		*code = AMDRDNA_GFX10_VERTEX_UNORM8X4;
		*components = 4U;
		*bytes = 4U;
		return true;
	default:
		return false;
	}

	/* Succeeded: four bytes a channel. */
	*bytes = *components * 4U;
	return true;
}

/*
 * Reports the channel select of a descriptor for one view channel: the
 * swizzle's source, with red and blue swapped for a blue-first format
 * (the descriptor's X is the first byte in memory).
 */
static uint32_t
descriptor_select(
	uint32_t swizzle,
	uint32_t channel,
	bool alternate)
{
	uint32_t source;

	/* Zero and one are constants; identity is the channel itself. */
	switch (swizzle) {
	case VK_COMPONENT_SWIZZLE_ZERO:
		return DESCRIPTOR_SELECT_ZERO;
	case VK_COMPONENT_SWIZZLE_ONE:
		return DESCRIPTOR_SELECT_ONE;
	case VK_COMPONENT_SWIZZLE_R:
	case VK_COMPONENT_SWIZZLE_G:
	case VK_COMPONENT_SWIZZLE_B:
	case VK_COMPONENT_SWIZZLE_A:
		source = swizzle - VK_COMPONENT_SWIZZLE_R;
		break;
	default:
		source = channel;
		break;
	}

	/* Blue first in memory: red is the third byte, blue the first. */
	if (alternate && source != 1U && source != 3U)
		source = 2U - source;

	/* Succeeded: the select of the memory channel. */
	return DESCRIPTOR_SELECT_X + source;
}

/* Reports the clamp of a VkSamplerAddressMode. */
static uint32_t
sampler_clamp(
	uint32_t mode)
{
	/* Each mode's clamp; repeat for any other. */
	switch (mode) {
	case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
		return SAMPLER_MIRROR;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
		return SAMPLER_CLAMP_EDGE;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
		return SAMPLER_CLAMP_BORDER;
	case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
		return SAMPLER_MIRROR_ONCE_EDGE;
	default:
		return SAMPLER_WRAP;
	}
}

/* Reports the filter of a VkFilter. */
static uint32_t
sampler_filter(
	uint32_t filter)
{
	/* Linear is bilinear; anything else the nearest texel. */
	if (filter == VK_FILTER_LINEAR)
		return SAMPLER_BILINEAR;
	return SAMPLER_POINT;
}

/* Reports the border colour type of a VkBorderColor. */
static uint32_t
sampler_border(
	uint32_t colour)
{
	/* Opaque black, opaque white, or transparent black. */
	switch (colour) {
	case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
	case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
		return SAMPLER_BORDER_BLACK;
	case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
	case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
		return SAMPLER_BORDER_WHITE;
	default:
		return SAMPLER_BORDER_TRANSPARENT;
	}
}
