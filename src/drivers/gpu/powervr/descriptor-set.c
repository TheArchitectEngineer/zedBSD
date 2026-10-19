/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR descriptor set's memory: the bindings' places in a set and
 * the bytes of each descriptor, laid out as the shader's shared register
 * requests read them.  Nothing here touches the core or memory; the
 * executor's set commands (vk-layout.c) call it, and the host check
 * builds it unchanged.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/powervr/descriptor-set.h"
#include "drivers/gpu/powervr/shader.h"

/*
 * The pack information's fields.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/common/pvr_iface.h
 * (PVR_PCK_INFO_FORMAT_OFFSET 0 with _LENGTH 5, PVR_PCK_INFO_SPLIT_OFFSET 5,
 * _SCALE_OFFSET 6, _ROUNDZERO_OFFSET 7, _UNORM_OFFSET 8) and
 * src/imagination/pco/pco_isa.py (F_PCK_FORMAT: u8888 0).
 */
#define PACK_FORMAT_U8888		0x00U
#define PACK_INFO_SCALE			0x40U
#define PACK_INFO_UNORM			0x100U

/* Where the sampler's words lie in a combined image and sampler descriptor. */
#define COMBINED_SAMPLER_OFFSET		POWERVR_DESCRIPTOR_IMAGE_BYTES

/*
 * The filter fields of the sampler's first word.
 *
 * Values referenced: Mesa 26.2.5 src/imagination/csbgen/rogue/
 * texstate.xml (SAMPLER_WORD0: magfilter 36-37, minfilter 38-39,
 * mipfilter 40; enum FILTER: linear 1).
 */
#define SAMPLER_FILTER_MASK		((uint64_t)0x1fU << 36)
#define SAMPLER_GATHER_FILTER		(((uint64_t)1U << 36) | ((uint64_t)1U << 38))

static uint32_t descriptor_stride(uint32_t type);
static void store32(uint8_t *bytes, uint32_t value);
static void store64(uint8_t *bytes, uint64_t value);

/*
 * Lays out a set's bindings in its memory.
 *
 * The bindings are placed in the order of their numbers, each binding's
 * descriptors one after another, with no padding.  A binding of count 0
 * takes no room.  Refuses a binding number past the most, a number named
 * twice, and a type the executor does not take (ENOTSUP, a dynamic
 * buffer among them).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_descriptor_set.c
 * (pvr_CreateDescriptorSetLayout: the bindings sorted by number, each
 * non-dynamic binding at the layout's size so far with the stride of
 * pvr_descriptor_size, the size growing by count times stride).
 */
int
powervr_set_layout_build(
	const struct powervr_set_binding_info *infos,
	uint32_t count,
	struct powervr_set_layout *layout)
{
	struct powervr_set_binding *binding;
	uint32_t index;
	uint32_t number;
	uint32_t stride;
	uint64_t bytes;
	bool named[POWERVR_SET_BINDINGS_MAX];

	/* Refuses a missing table or result. */
	if ((count != 0 && infos == NULL) || layout == NULL)
		return EINVAL;

	/* No binding yet. */
	for (number = 0; number < POWERVR_SET_BINDINGS_MAX; number++) {
		layout->bindings[number].type = 0;
		layout->bindings[number].count = 0;
		layout->bindings[number].offset = 0;
		layout->bindings[number].stride = 0;
		named[number] = false;
	}
	layout->bytes = 0;

	/* Each named binding's type and count, checked. */
	for (index = 0; index < count; index++) {
		number = infos[index].binding;
		if (number >= POWERVR_SET_BINDINGS_MAX)
			return ENOTSUP;
		if (named[number])
			return EINVAL;
		named[number] = true;
		if (infos[index].count == 0)
			continue;

		stride = descriptor_stride(infos[index].type);
		if (stride == 0)
			return ENOTSUP;

		binding = &layout->bindings[number];
		binding->type = infos[index].type;
		binding->count = infos[index].count;
		binding->stride = stride;
	}

	/* The places, in the order of the numbers. */
	bytes = 0;
	for (number = 0; number < POWERVR_SET_BINDINGS_MAX; number++) {
		binding = &layout->bindings[number];
		if (binding->count == 0)
			continue;

		binding->offset = (uint32_t)bytes;
		bytes += (uint64_t)binding->count * binding->stride;
		if (bytes > UINT32_MAX)
			return E2BIG;
	}

	/* Succeeded: the set's size. */
	layout->bytes = (uint32_t)bytes;
	return 0;
}

/*
 * Reports where a part of one descriptor lies in a set's memory: a
 * buffer's address, an image's state words or a sampler's state words
 * (enum powervr_shader_descriptor_part).
 *
 * A combined image and sampler holds the image first and the sampler
 * after it.  Refuses a binding the layout does not name, an element past
 * its count, and a part its type does not have.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_descriptor_set.c
 * (write_buffer, write_sampler, write_image_sampler, write_sampled_image:
 * the descriptor at the binding's offset plus the element times the
 * stride) and src/imagination/vulkan/pvr_common.h
 * (struct pvr_combined_image_sampler_descriptor: image, then sampler).
 */
int
powervr_set_descriptor_offset(
	const struct powervr_set_layout *layout,
	uint32_t binding,
	uint32_t element,
	uint32_t part,
	uint32_t *offset)
{
	const struct powervr_set_binding *entry;
	uint32_t within;

	/* Refuses a missing layout or result, and a binding or element it does not have. */
	if (layout == NULL || offset == NULL || binding >= POWERVR_SET_BINDINGS_MAX)
		return EINVAL;
	entry = &layout->bindings[binding];
	if (element >= entry->count)
		return EINVAL;

	/* The part's place in the descriptor, by the binding's type. */
	switch (entry->type) {
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		if (part != POWERVR_SHADER_PART_BUFFER_ADDRESS)
			return EINVAL;
		within = 0;
		break;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		if (part != POWERVR_SHADER_PART_IMAGE_STATE)
			return EINVAL;
		within = 0;
		break;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		if (part != POWERVR_SHADER_PART_SAMPLER_STATE)
			return EINVAL;
		within = 0;
		break;
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		if (part == POWERVR_SHADER_PART_IMAGE_STATE) {
			within = 0;
		} else if (part == POWERVR_SHADER_PART_SAMPLER_STATE) {
			within = COMBINED_SAMPLER_OFFSET;
		} else {
			return EINVAL;
		}
		break;
	default:
		return EINVAL;
	}

	/* Succeeded: the binding's place, the element's, then the part's. */
	*offset = entry->offset + element * entry->stride + within;
	return 0;
}

/*
 * Reports the pack information an image descriptor carries for the
 * shader's pack instruction: the pack format, and whether the channels
 * are split, scaled, rounded to zero and unsigned normalized.
 *
 * Of the executor's formats only the 8-bit unsigned normalized ones of
 * one to four channels in red-first order have a pack format; the rest
 * carry the invalid format, with the unsigned normalized bit kept for
 * the sRGB, blue-first and 16-bit depth ones.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_tex_state.c
 * (setup_pck_info), src/imagination/pco/pco.c (pco_pipe_to_pck_format:
 * R8, R8G8, R8G8B8A8 UNORM to U8888 with scale) and
 * src/util/format/u_format_parse.py (is_unorm: a name with _UNORM or
 * _SRGB).
 */
uint32_t
powervr_pack_info(
	uint32_t vk_format)
{
	/* The pack format and the bits, by format. */
	switch (vk_format) {
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8A8_UNORM:
		return PACK_FORMAT_U8888 | PACK_INFO_SCALE | PACK_INFO_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_D16_UNORM:
		return POWERVR_PACK_FORMAT_INVALID | PACK_INFO_UNORM;
	default:
		break;
	}

	/* No pack format, and not unsigned normalized. */
	return POWERVR_PACK_FORMAT_INVALID;
}

/*
 * Writes a buffer descriptor: the address of the range's first byte, the
 * range's size and a zero offset.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_descriptor_set.c
 * (write_buffer: addr, size, the offset left zero) and
 * src/imagination/vulkan/pvr_common.h (struct pvr_buffer_descriptor:
 * addr 64 bits, size 32, offset 32).
 */
void
powervr_descriptor_buffer(
	uint8_t bytes[POWERVR_DESCRIPTOR_BUFFER_BYTES],
	uint64_t address,
	uint32_t range)
{
	/* The address, the size, then the offset. */
	store64(&bytes[0], address);
	store32(&bytes[8], range);
	store32(&bytes[12], 0);
}

/*
 * Writes an image descriptor: the view's two state words, then the four
 * words the shader reads (the layer size, the buffer elements, the depth
 * slice and the pack information).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_tex_state.c
 * (pvr_arch_pack_tex_state: meta[LAYER_SIZE], [BUFFER_ELEMS], [Z_SLICE],
 * [PCK_INFO]), src/imagination/vulkan/pvr_arch_image.c (a 2D view: the
 * plane's layer size, no buffer elements, slice 0) and
 * src/imagination/pco/pco_data.h (enum pco_image_meta).
 */
void
powervr_descriptor_image(
	uint8_t bytes[POWERVR_DESCRIPTOR_IMAGE_BYTES],
	const uint64_t words[2],
	uint32_t layer_bytes,
	uint32_t pack_info)
{
	/* The state words. */
	store64(&bytes[0], words[0]);
	store64(&bytes[8], words[1]);

	/* The layer size, no buffer elements, slice 0, the pack information. */
	store32(&bytes[16], layer_bytes);
	store32(&bytes[20], 0);
	store32(&bytes[24], 0);
	store32(&bytes[28], pack_info);
}

/*
 * Writes a sampler descriptor: its two state words, the four words the
 * shader reads (the comparison, then three reserved), and the two words
 * of its gather form, which filters linearly with no mip filter.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_sampler.c
 * (pvr_CreateSampler: meta[COMPARE_OP] VK_COMPARE_OP_NEVER without
 * compare; the gather sampler: word 0 with mipfilter false, minfilter
 * and magfilter LINEAR, word 1 copied) and
 * src/imagination/vulkan/pvr_common.h (struct pvr_sampler_descriptor).
 */
void
powervr_descriptor_sampler(
	uint8_t bytes[POWERVR_DESCRIPTOR_SAMPLER_BYTES],
	const uint64_t words[2],
	uint32_t compare_op)
{
	uint64_t gather;

	/* The state words. */
	store64(&bytes[0], words[0]);
	store64(&bytes[8], words[1]);

	/* The comparison and the three reserved words. */
	store32(&bytes[16], compare_op);
	store32(&bytes[20], 0);
	store32(&bytes[24], 0);
	store32(&bytes[28], 0);

	/* The gather form: linear filters, no mip filter, the second word as it is. */
	gather = (words[0] & ~SAMPLER_FILTER_MASK) | SAMPLER_GATHER_FILTER;
	store64(&bytes[32], gather);
	store64(&bytes[40], words[1]);
}

/* Reports the bytes between descriptors of a type, 0 for a type the executor does not take. */
static uint32_t
descriptor_stride(
	uint32_t type)
{
	/* The size of each kind. */
	switch (type) {
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		return POWERVR_DESCRIPTOR_BUFFER_BYTES;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		return POWERVR_DESCRIPTOR_IMAGE_BYTES;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
		return POWERVR_DESCRIPTOR_SAMPLER_BYTES;
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		return POWERVR_DESCRIPTOR_COMBINED_BYTES;
	default:
		break;
	}

	/* Not a type the executor takes. */
	return 0;
}

/* Stores a 32-bit word in little-endian order. */
static void
store32(
	uint8_t *bytes,
	uint32_t value)
{
	/* Each byte, lowest first. */
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

/* Stores a 64-bit word in little-endian order. */
static void
store64(
	uint8_t *bytes,
	uint64_t value)
{
	/* The low word, then the high one. */
	store32(&bytes[0], (uint32_t)value);
	store32(&bytes[4], (uint32_t)(value >> 32));
}
