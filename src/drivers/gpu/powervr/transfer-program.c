/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The words and fixed programs of a PowerVR transfer job (copy and
 * clear).  See transfer-program.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-program.h"
#include "drivers/gpu/powervr/shader-private.h"
#include "drivers/gpu/powervr/transfer-program.h"
#include "drivers/gpu/powervr/usc-encode.h"

/*
 * The copy program's registers: the shared registers of the image state
 * (0 to 3), the sampler state (4 to 7) and the coordinate floats (8 to
 * 11, x scale and offset, y scale and offset); the temporaries of the
 * pixel's position (0, 1), the source coordinates (2, 3), the sampled
 * words (4 to 7) and the coordinate floats (8 to 11; a multiply-add
 * takes no two shared registers, so they are moved first).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_usc.c (pvr_uscgen_tq with
 * layer_floats PVR_INT_COORD_SET_FLOATS_4 and iterated false: the
 * position's x and y, coords = position * (sh[0], sh[2]) + (sh[1],
 * sh[3]) from the dynamic constants, the sample from the combined image
 * sampler's image and sampler state, the raw words to the output).
 */
#define SHARED_IMAGE		0U
#define SHARED_SAMPLER		4U
#define SHARED_FLOATS		8U
#define TEMP_POSITION		0U
#define TEMP_COORDINATES	2U
#define TEMP_SAMPLE		4U
#define TEMP_FLOATS		8U
#define TEMP_PIXEL		12U
#define COPY_TEMPS		12U

/*
 * The pack of four 8-bit unorm channels with its scale (the blit).
 *
 * Values referenced:
 * the compiler back end's pack (shader-backend.c PACK_U8888 0,
 * PACK_SCALE 1), from Mesa 26.2.5 src/imagination/pco/pco_isa.py.
 */
#define PACK_U8888		0U
#define PACK_SCALE		1U

/*
 * The special registers of the pixel's position at the pixel rate, and
 * the first pixel output register.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_isa.py (special registers x_p 97,
 * y_p 100, pixout 32); src/imagination/pco/pco_trans_nir.c
 * (trans_load_input: VARYING_SLOT_POS x and y from x_p and y_p when
 * single sampled).
 */
#define SPECIAL_X		97U
#define SPECIAL_Y		100U
#define SPECIAL_PIXEL_OUTPUT	32U

/*
 * The sample's fields: unnormalised coordinates, a 2D lookup of four
 * channels.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_isa.py (F_DIM 2d 0b10); the
 * compiler back end's sample (shader-backend.c) with the normalised flag
 * cleared, as pco_emit_nir_smp leaves it for nncoords.
 */
#define SAMPLE_2D		2U
#define SAMPLE_CHANNELS		4U

/*
 * The raw formats a same-format copy moves texels as.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_3d_copy_blit: a point-sampled copy between one format moves
 * 32-bit texels as VK_FORMAT_R32_UINT); src/imagination/vulkan/
 * pvr_arch_formats.c (R32_UINT: texture format U32 24, pixel back end
 * pack mode U32 0x34); src/imagination/vulkan/pvr_formats.c (the
 * R32_UINT swizzle X001).
 */
static const struct powervr_format raw_32 = {
	VK_FORMAT_R32_UINT, 4, POWERVR_FORMAT_COLOR, 24, 0x34,
	{ POWERVR_CHANNEL_0, POWERVR_CHANNEL_ZERO, POWERVR_CHANNEL_ZERO, POWERVR_CHANNEL_ONE },
	0, 0, 0
};

/*
 * The fields of the transfer command's registers.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/cr.xml (ISP_RENDER mode_type
 * 0..1 FAST_SCALE 3, dir_type 2..3 TL2BR 0; ISP_RENDER_ORIGIN and
 * ISP_MTILE_SIZE x 16..25, y 0..9; ISP_BGOBJVALS enablebgtag 9; ISP_CTL
 * process_empty_tiles 17, pipe_enable 12..15; USC_PIXEL_OUTPUT_CTRL
 * width 0..1 2REGISTERS 0, partition_mask 3..20; FRAG_SCREEN xmax 0..14,
 * ymax 16..30; PBESTATE_STATE_WORD1 norm 13).
 */
#define RENDER_FAST_SCALE	3U
#define BGOBJ_ENABLE_TAG	(1U << 9)
#define ISP_PROCESS_EMPTY	(1U << 17)
#define ISP_PIPE_SHIFT		12U
#define PIXEL_PARTITION_SHIFT	3U
#define TILE_FIELD_MAX		0x3ffU
#define SCREEN_FIELD_MAX	0x7fffU
#define PBE_NORMALISED		(1U << 13)

/*
 * The tile and partition figures of the BXE-4-32.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/common/device_info/bxe-4-32.h
 * (tile_size_x 16, tile_size_y 16, max_partitions 10,
 * usc_min_output_registers_per_pix 2, isp_max_tiles_in_flight 4,
 * num_raster_pipes 1, usc_pixel_partition_mask,
 * simple_parameter_format_version 2);
 * src/imagination/vulkan/winsys/pvrsrvkm/pvr_srv.c
 * (pvr_srv_get_total_reserved_partition_size: 16 x 16 tiles x 10
 * partitions x 2 registers).
 */
#define TILE_SIZE		16U
#define MAX_PARTITIONS		10U
#define MIN_OUTPUT_REGISTERS	2U
#define ISP_TILES_IN_FLIGHT	4U
#define RESERVED_PARTITIONS	(TILE_SIZE * TILE_SIZE * MAX_PARTITIONS * MIN_OUTPUT_REGISTERS)

/*
 * The sampler word's detail adjust and w wrap fields.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/texstate.xml (SAMPLER_WORD0
 * dadjust 0..12, addrmode_w 56..58); src/imagination/vulkan/
 * pvr_arch_job_transfer.c (pvr_sampler_state_for_surface: dadjust
 * MIN_UINT 0, addrmode_w only for a 3D source).
 */
#define SAMPLER_DADJUST_MASK	0x1fffULL
#define SAMPLER_WRAP_W_MASK	(7ULL << 56)


static const struct shader_machine_register no_register = { 0xff, 0 };

static void blank_operation(struct shader_machine_operation *operation, unsigned kind);
static struct shader_machine_register machine_register(unsigned bank, unsigned index);
static int build_program(unsigned packed, struct powervr_render_usc *program);
static int fill_shared(const struct powervr_view_shape *view, const struct powervr_render_area *source, const struct powervr_render_area *area, bool flip_x, bool flip_y, bool linear, uint32_t words[POWERVR_TRANSFER_SHARED_WORDS]);
static uint32_t float_from_ratio(int64_t numerator, int64_t denominator);
static int build_groups(const struct shader_machine_operation *operations, size_t count, struct powervr_usc_group *groups);
static uint32_t unorm8_from_float(uint32_t bits);
static void tiles_in_flight(uint32_t *isp_ctl, uint32_t *pixel_output_ctrl);

/* Returns the raw format a same-format copy of texel_bytes texels moves them as, NULL when there is none. */
const struct powervr_format *
powervr_transfer_raw_format(
	uint32_t texel_bytes)
{
	/* Only 32-bit texels for now (the 8-bit RGBA formats of the table). */
	if (texel_bytes == 4)
		return &raw_32;

	/* No raw format of that size. */
	return NULL;
}

/*
 * Says whether a linear surface's rectangle must be written one row a
 * job: the pixel back end's line stride counts pairs of pixels, so an
 * odd row in pixels cannot be named when the rectangle has more than one
 * row (design-p010.md section 6).
 */
bool
powervr_transfer_needs_rows(
	const struct powervr_render_surface *surface,
	const struct powervr_render_area *area)
{
	uint32_t stride;

	/* Twiddled surfaces and single rows never need it. */
	if (!surface->linear || area->height <= 1U || surface->format == NULL || surface->format->texel_bytes == 0)
		return false;

	/* An odd row in pixels does. */
	stride = surface->row_pitch / surface->format->texel_bytes;
	if ((stride & 1U) != 0)
		return true;

	/* An even row does not. */
	return false;
}

/*
 * Builds the copy program: the pixel's position from the special
 * registers, scaled and offset by the shared floats into the source's
 * unnormalised coordinates, the source texel sampled with the shared
 * image and sampler state, its first raw word into pixel output register
 * 0 under the overlap check, ending the program.
 *
 * The groups are the compiler back end's (shader_backend_group); the
 * sample is rebuilt with unnormalised coordinates.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_usc.c (pvr_uscgen_tq,
 * pvr_uscgen_tq_frag_coords, pvr_uscgen_tq_frag_load with nncoords and
 * the uint32 type of a raw format, the store of one word to output 0).
 */
int
powervr_transfer_copy_usc(
	struct powervr_render_usc *program)
{
	int error;

	/* Refuses a missing result. */
	if (program == NULL)
		return EINVAL;

	/* The raw word, not packed. */
	error = build_program(0, program);
	if (error != 0)
		return error;

	/* Succeeded: the copy program. */
	return 0;
}

/*
 * Builds the blit program: as the copy program, but the sampled channels
 * of a four-channel 8-bit unorm format (filtered as the sampler says)
 * are packed into one word.  Other formats are not taken yet (ENOTSUP).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_pbe_src_format_normal: an R8G8B8A8 or B8G8R8A8 unorm destination
 * takes PVR_TRANSFER_PBE_PIXEL_SRC_F16_U8); src/imagination/vulkan/
 * pvr_usc.c (pvr_uscgen_tq_frag_pack: F16_U8 is nir_pack_unorm_4x8).
 */
int
powervr_transfer_blit_usc(
	const struct powervr_format *format,
	struct powervr_render_usc *program)
{
	int error;

	/* Refuses missing input or result, and formats other than four 8-bit unorm channels. */
	if (format == NULL || program == NULL)
		return EINVAL;
	if (format->store_code != 0 || format->texel_bytes != 4 || (format->kind & POWERVR_FORMAT_SRGB) != 0)
		return ENOTSUP;

	/* The channels packed. */
	error = build_program(1, program);
	if (error != 0)
		return error;

	/* Succeeded: the blit program. */
	return 0;
}

/*
 * Builds the transfer program of powervr_transfer_copy_usc, with the
 * four sampled channels packed as 8-bit unorm when packed is 1.
 */
static int
build_program(
	unsigned packed,
	struct powervr_render_usc *program)
{
	struct shader_machine_operation operations[12];
	struct powervr_usc_group groups[12];
	size_t length;
	size_t count;
	unsigned axis;
	int error;

	/* The position's x and y into temporaries 0 and 1. */
	for (axis = 0; axis < 2; axis++) {
		blank_operation(&operations[axis], MACHINE_MOVE_SPECIAL);
		operations[axis].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_POSITION + axis);
		if (axis == 0) {
			operations[axis].source[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_X);
		} else {
			operations[axis].source[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_Y);
		}
	}

	/* The four floats into temporaries 8 to 11. */
	for (axis = 0; axis < 4; axis++) {
		blank_operation(&operations[2 + axis], MACHINE_MOVE);
		operations[2 + axis].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_FLOATS + axis);
		operations[2 + axis].source[0] = machine_register(POWERVR_USC_BANK_SHARED, SHARED_FLOATS + axis);
	}

	/* Each coordinate: the position times its scale plus its offset. */
	for (axis = 0; axis < 2; axis++) {
		blank_operation(&operations[6 + axis], MACHINE_FLOAT_MULTIPLY_ADD);
		operations[6 + axis].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_COORDINATES + axis);
		operations[6 + axis].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_POSITION + axis);
		operations[6 + axis].source[1] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_FLOATS + axis * 2U);
		operations[6 + axis].source[2] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_FLOATS + axis * 2U + 1U);
	}

	/* The sample, and the wait for its data. */
	blank_operation(&operations[8], MACHINE_SAMPLE);
	operations[8].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_SAMPLE);
	operations[8].source[0] = machine_register(POWERVR_USC_BANK_SHARED, SHARED_IMAGE);
	operations[8].source[1] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_COORDINATES);
	operations[8].source[2] = machine_register(POWERVR_USC_BANK_SHARED, SHARED_SAMPLER);
	blank_operation(&operations[9], MACHINE_WAIT);

	/* The four channels packed into one word, for the blit. */
	count = 10;
	if (packed) {
		blank_operation(&operations[count], MACHINE_PACK);
		operations[count].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_PIXEL);
		operations[count].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_SAMPLE);
		operations[count].variant = PACK_U8888;
		operations[count].immediate = 4;
		operations[count].immediate_2 = PACK_SCALE;
		count++;
	}

	/* The word to the pixel output under the overlap check. */
	blank_operation(&operations[count], MACHINE_MOVE);
	operations[count].destination[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_PIXEL_OUTPUT);
	if (packed) {
		operations[count].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_PIXEL);
	} else {
		operations[count].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_SAMPLE);
	}
	operations[count].variant = 1;
	count++;

	/* The groups, the sample with unnormalised coordinates, the last one ending. */
	error = build_groups(operations, count, groups);
	if (error != 0)
		return error;
	error = powervr_usc_sample(0, 0, SAMPLE_2D, SAMPLE_CHANNELS, 0, &groups[8].operation[POWERVR_USC_PHASE_BACKEND]);
	if (error != 0)
		return error;
	groups[count - 1U].end = 1;

	/* Encodes them. */
	error = powervr_usc_program_encode(groups, count, program->code, sizeof(program->code), &length);
	if (error != 0)
		return error;

	/* Succeeded: the program and its temporaries. */
	program->length = length;
	program->temps = COPY_TEMPS + packed;
	return 0;
}

/*
 * Fills the twelve words the copy program's shared registers hold: the
 * source's image state (as its raw format), a point sampler that clamps
 * and takes unnormalised coordinates, and the floats that map a
 * destination pixel (x, y) to the source texel (x - area.x + source_x,
 * y - area.y + source_y): scale 1, offset source - destination.
 *
 * Refuses a source whose texels have no raw format (ENOTSUP) and
 * offsets beyond 2^24 (E2BIG).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_image_state_set_codegen_defaults: one level, the raw format;
 * pvr_sampler_state_for_surface: point filters, clamp to edge,
 * non_normalized_coords; pvr_uv_space: FLOATS_4 when the source and
 * destination corners differ; pvr_dma_texture_floats).
 */
int
powervr_transfer_shared_words(
	const struct powervr_view_shape *source,
	uint32_t source_x,
	uint32_t source_y,
	const struct powervr_render_area *area,
	uint32_t words[POWERVR_TRANSFER_SHARED_WORDS])
{
	struct powervr_view_shape view;
	struct powervr_render_area rectangle;
	int error;

	/* Refuses missing input or result. */
	if (source == NULL || source->format == NULL || area == NULL || words == NULL)
		return EINVAL;

	/* The source as its raw format. */
	view = *source;
	view.format = powervr_transfer_raw_format(source->format->texel_bytes);
	if (view.format == NULL)
		return ENOTSUP;
	view.mapping[0] = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.mapping[1] = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.mapping[2] = VK_COMPONENT_SWIZZLE_IDENTITY;
	view.mapping[3] = VK_COMPONENT_SWIZZLE_IDENTITY;

	/* The source rectangle of the destination's size, point sampled. */
	rectangle.x = source_x;
	rectangle.y = source_y;
	rectangle.width = area->width;
	rectangle.height = area->height;
	error = fill_shared(&view, &rectangle, area, false, false, false, words);
	if (error != 0)
		return error;

	/* Succeeded: the twelve words. */
	return 0;
}

/*
 * Fills the twelve shared words of a blit: the source's image state as
 * the view gives it, a sampler that clamps and takes unnormalised
 * coordinates (linear when linear and the rectangles differ in size,
 * else point), and the floats that map the destination rectangle onto
 * the source rectangle, mirrored along an axis whose flip is set.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_transfer_set_filter: point filtering when the sizes match;
 * pvr_sampler_state_for_surface; pvr_dma_texture_floats with
 * PVR_INT_COORD_SET_FLOATS_4).
 */
int
powervr_transfer_blit_words(
	const struct powervr_view_shape *source,
	const struct powervr_render_area *source_area,
	const struct powervr_render_area *area,
	bool flip_x,
	bool flip_y,
	bool linear,
	uint32_t words[POWERVR_TRANSFER_SHARED_WORDS])
{
	int error;

	/* Refuses missing input or result. */
	if (source == NULL || source->format == NULL || source_area == NULL || area == NULL || words == NULL)
		return EINVAL;

	/* Point filtering when no scaling is applied. */
	if (source_area->width == area->width && source_area->height == area->height)
		linear = false;

	/* The words. */
	error = fill_shared(source, source_area, area, flip_x, flip_y, linear, words);
	if (error != 0)
		return error;

	/* Succeeded: the twelve words. */
	return 0;
}

/*
 * Fills the twelve shared words: the view's image state, the clamping
 * unnormalised sampler, and for each axis the scale source / destination
 * and the offset (source corner * destination - source size *
 * destination corner) / destination, the destination size negated and
 * its far edge used when the axis flips; each float the correctly
 * rounded value of its ratio.  Refuses empty rectangles and sizes past
 * 2^15 (EINVAL, E2BIG).
 */
static int
fill_shared(
	const struct powervr_view_shape *view,
	const struct powervr_render_area *source,
	const struct powervr_render_area *area,
	bool flip_x,
	bool flip_y,
	bool linear,
	uint32_t words[POWERVR_TRANSFER_SHARED_WORDS])
{
	struct powervr_sampler_shape sampler;
	uint64_t image_words[2];
	uint64_t sampler_words[2];
	int64_t destination_size;
	int64_t constant;
	int error;

	/* Refuses empty and oversized rectangles. */
	if (source->width == 0 || source->height == 0 || area->width == 0 || area->height == 0)
		return EINVAL;
	if (source->x > 0x8000U || source->y > 0x8000U || source->width > 0x8000U || source->height > 0x8000U ||
	    area->x > 0x8000U || area->y > 0x8000U || area->width > 0x8000U || area->height > 0x8000U)
		return E2BIG;

	/* The source's image state. */
	error = powervr_view_words(view, image_words);
	if (error != 0)
		return error;

	/* The sampler with unnormalised coordinates. */
	sampler.mag_filter = VK_FILTER_NEAREST;
	sampler.min_filter = VK_FILTER_NEAREST;
	if (linear) {
		sampler.mag_filter = VK_FILTER_LINEAR;
		sampler.min_filter = VK_FILTER_LINEAR;
	}
	sampler.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.lod_bias_bits = 0;
	sampler.min_lod_bits = 0;
	sampler.max_lod_bits = 0;
	sampler.unnormalized = true;
	error = powervr_sampler_words(&sampler, sampler_words);
	if (error != 0)
		return error;

	/* As the transfer's sampler: the detail adjust at its minimum and no w wrap (a 2D source). */
	sampler_words[0] &= ~(SAMPLER_DADJUST_MASK | SAMPLER_WRAP_W_MASK);

	/* The image and sampler state, low words first. */
	words[SHARED_IMAGE + 0] = (uint32_t)image_words[0];
	words[SHARED_IMAGE + 1] = (uint32_t)(image_words[0] >> 32);
	words[SHARED_IMAGE + 2] = (uint32_t)image_words[1];
	words[SHARED_IMAGE + 3] = (uint32_t)(image_words[1] >> 32);
	words[SHARED_SAMPLER + 0] = (uint32_t)sampler_words[0];
	words[SHARED_SAMPLER + 1] = (uint32_t)(sampler_words[0] >> 32);
	words[SHARED_SAMPLER + 2] = (uint32_t)sampler_words[1];
	words[SHARED_SAMPLER + 3] = (uint32_t)(sampler_words[1] >> 32);

	/* x: the scale and the offset. */
	destination_size = (int64_t)area->width;
	constant = (int64_t)source->x * destination_size - (int64_t)source->width * (int64_t)area->x;
	if (flip_x) {
		destination_size = -destination_size;
		constant = (int64_t)source->x * destination_size - (int64_t)source->width * ((int64_t)area->x + (int64_t)area->width);
	}
	words[SHARED_FLOATS + 0] = float_from_ratio((int64_t)source->width, destination_size);
	words[SHARED_FLOATS + 1] = float_from_ratio(constant, destination_size);

	/* y: the scale and the offset. */
	destination_size = (int64_t)area->height;
	constant = (int64_t)source->y * destination_size - (int64_t)source->height * (int64_t)area->y;
	if (flip_y) {
		destination_size = -destination_size;
		constant = (int64_t)source->y * destination_size - (int64_t)source->height * ((int64_t)area->y + (int64_t)area->height);
	}
	words[SHARED_FLOATS + 2] = float_from_ratio((int64_t)source->height, destination_size);
	words[SHARED_FLOATS + 3] = float_from_ratio(constant, destination_size);

	/* Succeeded: the twelve words. */
	return 0;
}

/*
 * Builds the data sequencer program that copies the twelve shared words
 * from words_address into shared registers 0 to 11.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_3d_copy_blit_core: one texture DMA kick of the driver's shared
 * words to common store 0, pvr_pds_encode_dma_burst).
 */
int
powervr_transfer_shared_program(
	uint64_t words_address,
	struct powervr_data_shared_program *program)
{
	struct powervr_data_load load;
	int error;

	/* Refuses a missing result. */
	if (program == NULL)
		return EINVAL;

	/* One DMA of the twelve words to shared register 0. */
	load.kind = POWERVR_DATA_LOAD_DMA;
	load.shared = 0;
	load.words = POWERVR_TRANSFER_SHARED_WORDS;
	load.source = words_address;
	error = powervr_data_shared_build(&load, 1, program);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Converts a float, as its bits, to a half float without float
 * arithmetic (the kernel is soft float): clamped to the largest half,
 * denormalised below the smallest normal, rounded half up.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c pvr_float_to_f16
 * (round_to_even false), used by pvr_pack_clear_color.
 */
static uint32_t
half_from_float(
	uint32_t bits)
{
	uint32_t sign;
	uint32_t mantissa;
	int32_t exponent;
	uint32_t result;

	/* The sign apart; zero (either sign) is zero. */
	sign = (bits >> 16) & 0x8000U;
	if ((bits & 0x7FFFFFFFU) == 0)
		return 0;

	/* Clamps to 131008, the largest half (and infinities and NaNs). */
	if ((bits & 0x7FFFFFFFU) > 0x47FFE000U)
		bits = (bits & 0x80000000U) | 0x47FFE000U;

	/* The half's exponent and the mantissa with its hidden bit. */
	exponent = (int32_t)((bits >> 23) & 0xFFU) - 127 + 15;
	mantissa = (bits & 0x7FFFFFU) | 0x800000U;

	/* Below the smallest normal: shifts the mantissa down, exponent 0. */
	if (exponent <= 0) {
		if (1 - exponent >= 24)
			mantissa = 0;
		else
			mantissa >>= (uint32_t)(1 - exponent);
		exponent = 0;
	} else {
		mantissa &= 0x7FFFFFU;
	}

	/* Rounds up on the first dropped bit. */
	result = ((uint32_t)exponent << 10) + (mantissa >> 13);
	if ((mantissa & 0x1000U) != 0)
		result++;

	/* Succeeded: the half with its sign. */
	return sign | result;
}

/*
 * Packs a clear colour (four float bit patterns) for the clear registers:
 * a format of four 8-bit unorm channels as one word, red in the low
 * byte, each channel rounded to the nearest of 255 steps and clamped.
 * Other formats are not taken yet (ENOTSUP).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_pack_clear_color: PACKMODE_U8U8U8U8 with normalised channels,
 * pvr_float_to_ufixed: NaN, infinity and 1.0 or more to 255, below 0 to
 * 0, else floor(value * 255 + 0.5)).
 */
int
powervr_transfer_clear_colour(
	const struct powervr_format *format,
	const uint32_t colour[4],
	uint32_t packed[4])
{
	unsigned channel;

	/* Refuses missing input or result. */
	if (format == NULL || colour == NULL || packed == NULL)
		return EINVAL;

	/* 32-bit float channels: the raw bits, one word a channel. */
	switch (format->vk_format) {
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		for (channel = 0; channel < 4; channel++)
			packed[channel] = channel < format->texel_bytes / 4U ? colour[channel] : 0;
		return 0;

	/*
	 * The one- and two-channel 8-bit unorm formats, the sRGB ones and
	 * the 16-bit float one: half floats, two a word.
	 */
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		packed[0] = half_from_float(colour[0]) | (half_from_float(colour[1]) << 16);
		packed[1] = half_from_float(colour[2]) | (half_from_float(colour[3]) << 16);
		if (format->vk_format == VK_FORMAT_R8_UNORM)
			packed[0] &= 0xFFFFU;
		if (format->texel_bytes <= 2U)
			packed[1] = 0;
		packed[2] = 0;
		packed[3] = 0;
		return 0;
	default:
		break;
	}

	/* Otherwise takes the four-channel 8-bit unorm formats only. */
	if (format->store_code != 0 || format->texel_bytes != 4 || (format->kind & POWERVR_FORMAT_SRGB) != 0)
		return ENOTSUP;

	/* The four bytes in channel order, the other words zero. */
	packed[0] = 0;
	for (channel = 0; channel < 4; channel++)
		packed[0] |= unorm8_from_float(colour[channel]) << (channel * 8U);
	packed[1] = 0;
	packed[2] = 0;
	packed[3] = 0;

	/* Succeeded: the packed colour. */
	return 0;
}

/*
 * Fills the registers of one transfer job and the pixel back end's two
 * state words its end-of-tile program emits.
 *
 * The copy runs a background object over the rectangle's tiles; the
 * clear runs none and lets the tiles' pixels start from the clear
 * registers.  The pixel back end clips to the rectangle within the
 * destination, and an odd linear row with more than one row is refused
 * (EINVAL: write it one row a job, powervr_transfer_needs_rows).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_job_transfer.c
 * (pvr_3d_copy_blit_core: the fill without a shader or background, the
 * clear registers from the packed colour; the copy with the background
 * tag enabled; pvr_setup_hwbg_object: CR_PDS_BGRND0/1/3; ISP_RENDER
 * FAST_SCALE with the scan direction; FRAG_SCREEN the destination's
 * edges less one; pvr_pbe_setup_codegen_defaults: the clip from the
 * scissor, the tiles from the clip; pvr_pbe_setup_swizzle: 8 bits a
 * channel for a fill of R8G8B8A8 or B8G8R8A8 unorm and for the F16_U8
 * pixel of a blit, as the render's words already have; pvr_isp_tiles: ISP_MTILE_SIZE, ISP_RENDER_ORIGIN,
 * ISP_CTL process_empty_tiles with the tiles in flight;
 * pvr_pbe_setup_emit: the end-of-tile program's sizes and addresses);
 * src/imagination/vulkan/pvr_arch_job_common.c
 * (pvr_arch_setup_tiles_in_flight).
 */
int
powervr_transfer_regs_build(
	const struct powervr_transfer_job *job,
	struct powervr_transfer_regs *regs,
	uint32_t state[2])
{
	struct powervr_render_background background;
	struct powervr_render_surface destination;
	uint64_t pbe[3];
	uint32_t min_x;
	uint32_t min_y;
	uint32_t max_x;
	uint32_t max_y;
	uint32_t origin_x;
	uint32_t origin_y;
	unsigned index;
	int error;

	/* Refuses missing input or results, an unknown kind, and an odd linear row of several rows. */
	if (job == NULL || regs == NULL || state == NULL)
		return EINVAL;
	if (job->kind != POWERVR_TRANSFER_CLEAR && job->kind != POWERVR_TRANSFER_COPY && job->kind != POWERVR_TRANSFER_BLIT)
		return EINVAL;
	if (powervr_transfer_needs_rows(&job->destination, &job->area))
		return EINVAL;

	/* Starts from zero: no background, no clear words, no region list. */
	for (index = 0; index < 3; index++)
		regs->pds_bgnd[index] = 0;
	regs->isp_mtile_base = 0;
	for (index = 0; index < POWERVR_TRANSFER_PBE_WORDS; index++)
		regs->pbe[index] = 0;
	regs->isp_bgobjvals = 0;
	for (index = 0; index < 4; index++)
		regs->usc_clear[index] = 0;
	regs->isp_aa = 0;
	regs->isp_rgn = 0;

	/* A copy stores the destination as the raw format of its texels. */
	destination = job->destination;
	if (job->kind == POWERVR_TRANSFER_COPY) {
		if (destination.format == NULL)
			return EINVAL;
		destination.format = powervr_transfer_raw_format(destination.format->texel_bytes);
		if (destination.format == NULL)
			return ENOTSUP;
	}

	/* The pixel back end's words of the destination clipped to the rectangle. */
	error = powervr_render_pbe_words(&destination, &job->area, state, pbe);
	if (error != 0)
		return error;

	/* The raw format's channels are integers, not normalised. */
	if (destination.format == &raw_32)
		state[1] &= ~PBE_NORMALISED;
	regs->pbe[0] = pbe[0];
	regs->pbe[1] = pbe[1];
	regs->pbe[2] = pbe[2];

	/* The clear's colour, or the copy's and the blit's background object. */
	if (job->kind == POWERVR_TRANSFER_CLEAR) {
		error = powervr_transfer_clear_colour(job->destination.format, job->colour, regs->usc_clear);
		if (error != 0)
			return error;
	} else {
		background.kick_offset = job->copy_kick_offset;
		background.shared_code_offset = job->shared_code_offset;
		background.shared_data_offset = job->shared_data_offset;
		background.shared_data_words = job->shared_data_words;
		background.shared_registers = POWERVR_TRANSFER_SHARED_WORDS;
		error = powervr_render_background_words(&background, regs->pds_bgnd);
		if (error != 0)
			return error;
		regs->isp_bgobjvals = BGOBJ_ENABLE_TAG;
	}

	/* The clip again, for the tiles it covers. */
	min_x = (uint32_t)((pbe[0] >> 6) & 0x3fffU);
	max_y = (uint32_t)(pbe[1] & 0x3fffU);
	max_x = (uint32_t)((pbe[1] >> 32) & 0x3fffU);
	min_y = (uint32_t)((pbe[1] >> 46) & 0x3fffU);
	origin_x = min_x / TILE_SIZE;
	origin_y = min_y / TILE_SIZE;
	if ((max_x + TILE_SIZE) / TILE_SIZE - origin_x > TILE_FIELD_MAX)
		return E2BIG;
	regs->isp_mtile_size = (((max_x + TILE_SIZE) / TILE_SIZE - origin_x) << 16) | ((max_y + TILE_SIZE) / TILE_SIZE - origin_y);
	regs->isp_render_origin = (origin_x << 16) | origin_y;

	/* The tiles in flight and the pixel output's partitions. */
	tiles_in_flight(&regs->isp_ctl, &regs->usc_pixel_output_ctrl);
	regs->isp_ctl |= ISP_PROCESS_EMPTY;

	/* The end-of-tile program's words. */
	error = powervr_render_event_words(job->event_data_offset, &regs->event_pixel_pds_code, &regs->event_pixel_pds_data, &regs->event_pixel_pds_info);
	if (error != 0)
		return error;

	/* A fast scaled render scanning from the top left, over the destination's screen. */
	regs->isp_render = RENDER_FAST_SCALE;
	if (job->destination.width - 1U > SCREEN_FIELD_MAX || job->destination.height - 1U > SCREEN_FIELD_MAX)
		return E2BIG;
	regs->frag_screen = (job->destination.width - 1U) | ((job->destination.height - 1U) << 16);

	/* Succeeded: the registers and the state words. */
	return 0;
}

/* Starts a machine operation that names no register. */
static void
blank_operation(
	struct shader_machine_operation *operation,
	unsigned kind)
{
	unsigned index;

	/* Everything zero but the kind, and no register in any slot. */
	*operation = (struct shader_machine_operation){ 0 };
	operation->kind = (uint8_t)kind;
	for (index = 0; index < 2; index++)
		operation->destination[index] = no_register;
	for (index = 0; index < 4; index++)
		operation->source[index] = no_register;
}

/* Names a register of a bank. */
static struct shader_machine_register
machine_register(
	unsigned bank,
	unsigned index)
{
	struct shader_machine_register value;

	/* The bank and the number. */
	value.bank = (uint8_t)bank;
	value.index = (uint16_t)index;
	return value;
}

/* Builds the compiler back end's group of each operation. */
static int
build_groups(
	const struct shader_machine_operation *operations,
	size_t count,
	struct powervr_usc_group *groups)
{
	size_t index;
	int error;

	/* One group an operation; none branches. */
	for (index = 0; index < count; index++) {
		error = shader_backend_group(&operations[index], 0, &groups[index]);
		if (error != 0)
			return error;
	}

	/* Succeeded: the groups. */
	return 0;
}

/*
 * The bits of the float nearest numerator / denominator (round to
 * nearest, ties to even), for a nonzero denominator and a quotient in
 * the normal range; both below 2^40 in magnitude.
 */
static uint32_t
float_from_ratio(
	int64_t numerator,
	int64_t denominator)
{
	uint64_t dividend;
	uint64_t divisor;
	uint64_t quotient;
	uint64_t remainder;
	uint64_t mantissa;
	uint32_t sign;
	int32_t exponent;
	unsigned sticky;

	/* Zero, and the sign of the quotient. */
	if (numerator == 0 || denominator == 0)
		return 0;
	sign = 0;
	if ((numerator < 0) != (denominator < 0))
		sign = 0x80000000U;
	dividend = (uint64_t)(numerator < 0 ? -numerator : numerator);
	divisor = (uint64_t)(denominator < 0 ? -denominator : denominator);

	/* The quotient to 26 significant bits (24, a round bit and a guard bit) and whether anything is left. */
	quotient = dividend / divisor;
	remainder = dividend % divisor;
	exponent = 25;
	sticky = 0;
	while (quotient >= (1ULL << 26)) {
		sticky |= (unsigned)(quotient & 1U);
		quotient >>= 1;
		exponent++;
	}
	while (quotient < (1ULL << 25)) {
		remainder <<= 1;
		quotient <<= 1;
		if (remainder >= divisor) {
			quotient |= 1U;
			remainder -= divisor;
		}
		exponent--;
	}
	if (remainder != 0)
		sticky = 1;

	/* Rounds the 24-bit mantissa to nearest, ties to even. */
	mantissa = quotient >> 2;
	if ((quotient & 2U) != 0 && ((quotient & 1U) != 0 || sticky != 0 || (mantissa & 1U) != 0))
		mantissa++;
	if (mantissa == (1ULL << 24)) {
		mantissa >>= 1;
		exponent++;
	}

	/* The bits: the exponent of the leading one, the 23 bits below it. */
	return sign | ((uint32_t)(exponent + 127) << 23) | ((uint32_t)mantissa & 0x7fffffU);
}

/*
 * Converts a float's bits to an 8-bit unorm channel: NaN, infinity and
 * 1.0 or more to 255, below zero to 0, else floor(value * 255 + 0.5)
 * computed exactly.
 */
static uint32_t
unorm8_from_float(
	uint32_t bits)
{
	uint64_t mantissa;
	uint32_t exponent;
	uint32_t shift;

	/* NaN, infinity and 1.0 or more (a positive exponent of 127 or above). */
	exponent = (bits >> 23) & 0xffU;
	if (exponent == 0xffU)
		return 255;
	if ((bits & 0x80000000U) != 0)
		return 0;
	if (exponent >= 127U)
		return 255;

	/* Denormals and values below 2^-40 round to zero. */
	shift = 150U - exponent;
	if (exponent == 0 || shift > 60U)
		return 0;

	/* floor((mantissa * 255) / 2^shift + 0.5). */
	mantissa = (uint64_t)((bits & 0x7fffffU) | 0x800000U) * 255U;
	return (uint32_t)((mantissa + (1ULL << (shift - 1U))) >> shift);
}

/*
 * Works out the BXE-4-32's tiles in flight and pixel output control for
 * a pixel of one output register (rounded up to the core's two): ten
 * partitions of 512 words, four tiles in flight.
 */
static void
tiles_in_flight(
	uint32_t *isp_ctl,
	uint32_t *pixel_output_ctrl)
{
	uint32_t partition_size;
	uint32_t partitions;
	uint32_t tiles;

	/* Partitions of a tile of two registers a pixel within the reserved space. */
	partition_size = MIN_OUTPUT_REGISTERS * TILE_SIZE * TILE_SIZE;
	partitions = RESERVED_PARTITIONS / partition_size;
	if (partitions > MAX_PARTITIONS)
		partitions = MAX_PARTITIONS;

	/* The ISP's tiles in flight, no more than the partitions. */
	tiles = ISP_TILES_IN_FLIGHT;
	if (tiles > partitions)
		tiles = partitions;

	/* The pipes enabled, and the two-register width with every partition in the mask. */
	*isp_ctl = (tiles - 1U) << ISP_PIPE_SHIFT;
	*pixel_output_ctrl = ((1U << MAX_PARTITIONS) - 1U) << PIXEL_PARTITION_SHIFT;
}
