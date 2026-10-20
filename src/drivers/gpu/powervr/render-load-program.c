/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The attachment load background of a PowerVR render.  See
 * render-load-program.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/data-program.h"
#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-load-program.h"
#include "drivers/gpu/powervr/render-program.h"
#include "drivers/gpu/powervr/shader-private.h"
#include "drivers/gpu/powervr/usc-encode.h"

/*
 * The program's registers: the image state in shared registers 0 to 3,
 * the sampler state in 4 to 7; the pixel's position in temporaries 0
 * and 1, the sampled words in 4 to 7, the packed pixel in 8.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_usc.c (pvr_uscgen_loadop with
 * one loaded render target: the coordinates the position's x and y, the
 * image and sampler state from shared registers, the sample with
 * unnormalised coordinates, the result stored to the target's output
 * register in the attachment's format).
 */
#define SHARED_IMAGE		0U
#define SHARED_SAMPLER		4U
#define TEMP_POSITION		0U
#define TEMP_SAMPLE		4U
#define TEMP_PIXEL		8U
#define LOAD_TEMPS		9U

/*
 * The special registers of the pixel's position at the pixel rate, the
 * first pixel output register, the sample's 2D lookup of four channels,
 * and the pack of four 8-bit unorm channels with its scale.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/pco/pco_isa.py (special registers x_p 97,
 * y_p 100, pixout 32; F_DIM 2d 0b10; F_PCK_FMT u8888 0, the scale
 * flag); the compiler back end's pack (shader-backend.c PACK_U8888 0,
 * PACK_SCALE 1).
 */
#define SPECIAL_X		97U
#define SPECIAL_Y		100U
#define SPECIAL_PIXEL_OUTPUT	32U
#define SAMPLE_2D		2U
#define SAMPLE_CHANNELS		4U
#define PACK_U8888		0U
#define PACK_SCALE		1U

/*
 * The sampler word's detail adjust and w wrap fields.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/csbgen/rogue/texstate.xml (SAMPLER_WORD0
 * dadjust 0..12, addrmode_w 56..58).
 */
#define SAMPLER_DADJUST_MASK	0x1fffULL
#define SAMPLER_WRAP_W_MASK	(7ULL << 56)

static const struct shader_machine_register no_register = { 0xff, 0 };

static void blank_operation(struct shader_machine_operation *operation, unsigned kind);
static struct shader_machine_register machine_register(unsigned bank, unsigned index);

/*
 * Builds the load program for an attachment of four 8-bit unorm
 * channels: the position from the special registers, the attachment
 * sampled there with unnormalised coordinates, the four channels packed
 * into pixel output register 0 under the overlap check, ending the
 * program.  Other formats are not taken yet (ENOTSUP).
 *
 * The groups are the compiler back end's (shader_backend_group); the
 * sample is rebuilt with unnormalised coordinates.
 */
int
powervr_render_load_usc(
	const struct powervr_format *format,
	struct powervr_render_usc *program)
{
	struct shader_machine_operation operations[6];
	struct powervr_usc_group groups[6];
	size_t length;
	size_t index;
	int error;

	/* Refuses missing input or result, and formats other than four 8-bit unorm channels. */
	if (format == NULL || program == NULL)
		return EINVAL;
	if (format->store_code != 0 || format->texel_bytes != 4 || (format->kind & POWERVR_FORMAT_SRGB) != 0)
		return ENOTSUP;

	/* The position's x and y. */
	blank_operation(&operations[0], MACHINE_MOVE_SPECIAL);
	operations[0].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_POSITION);
	operations[0].source[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_X);
	blank_operation(&operations[1], MACHINE_MOVE_SPECIAL);
	operations[1].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_POSITION + 1U);
	operations[1].source[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_Y);

	/* The sample there, and the wait for its data. */
	blank_operation(&operations[2], MACHINE_SAMPLE);
	operations[2].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_SAMPLE);
	operations[2].source[0] = machine_register(POWERVR_USC_BANK_SHARED, SHARED_IMAGE);
	operations[2].source[1] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_POSITION);
	operations[2].source[2] = machine_register(POWERVR_USC_BANK_SHARED, SHARED_SAMPLER);
	blank_operation(&operations[3], MACHINE_WAIT);

	/* The four channels packed. */
	blank_operation(&operations[4], MACHINE_PACK);
	operations[4].destination[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_PIXEL);
	operations[4].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_SAMPLE);
	operations[4].variant = PACK_U8888;
	operations[4].immediate = 4;
	operations[4].immediate_2 = PACK_SCALE;

	/* The pixel to the output under the overlap check. */
	blank_operation(&operations[5], MACHINE_MOVE);
	operations[5].destination[0] = machine_register(POWERVR_USC_BANK_SPECIAL, SPECIAL_PIXEL_OUTPUT);
	operations[5].source[0] = machine_register(POWERVR_USC_BANK_TEMPORARY, TEMP_PIXEL);
	operations[5].variant = 1;

	/* The back end's group of each. */
	for (index = 0; index < 6; index++) {
		error = shader_backend_group(&operations[index], 0, &groups[index]);
		if (error != 0)
			return error;
	}

	/* The sample with unnormalised coordinates, the last group ending. */
	error = powervr_usc_sample(0, 0, SAMPLE_2D, SAMPLE_CHANNELS, 0, &groups[2].operation[POWERVR_USC_PHASE_BACKEND]);
	if (error != 0)
		return error;
	groups[5].end = 1;

	/* Encodes them. */
	error = powervr_usc_program_encode(groups, 6, program->code, sizeof(program->code), &length);
	if (error != 0)
		return error;

	/* Succeeded: the program and its temporaries. */
	program->length = length;
	program->temps = LOAD_TEMPS;
	return 0;
}

/*
 * Fills the eight shared words: the attachment's image state and a
 * point sampler that clamps and takes unnormalised coordinates.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_load_op_constants_create_and_upload: the attachment's image
 * descriptor and a nearest sampler); src/imagination/vulkan/
 * pvr_arch_job_transfer.c (pvr_sampler_state_for_surface: the point,
 * clamped, unnormalised sampler with dadjust MIN_UINT and no w wrap).
 */
int
powervr_render_load_words(
	const struct powervr_view_shape *attachment,
	uint32_t words[POWERVR_RENDER_LOAD_SHARED_WORDS])
{
	struct powervr_sampler_shape sampler;
	uint64_t image_words[2];
	uint64_t sampler_words[2];
	int error;

	/* Refuses missing input or result. */
	if (attachment == NULL || words == NULL)
		return EINVAL;

	/* The attachment's image state. */
	error = powervr_view_words(attachment, image_words);
	if (error != 0)
		return error;

	/* The point sampler with unnormalised coordinates. */
	sampler.mag_filter = VK_FILTER_NEAREST;
	sampler.min_filter = VK_FILTER_NEAREST;
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
	sampler_words[0] &= ~(SAMPLER_DADJUST_MASK | SAMPLER_WRAP_W_MASK);

	/* The words, low halves first. */
	words[SHARED_IMAGE + 0] = (uint32_t)image_words[0];
	words[SHARED_IMAGE + 1] = (uint32_t)(image_words[0] >> 32);
	words[SHARED_IMAGE + 2] = (uint32_t)image_words[1];
	words[SHARED_IMAGE + 3] = (uint32_t)(image_words[1] >> 32);
	words[SHARED_SAMPLER + 0] = (uint32_t)sampler_words[0];
	words[SHARED_SAMPLER + 1] = (uint32_t)(sampler_words[0] >> 32);
	words[SHARED_SAMPLER + 2] = (uint32_t)sampler_words[1];
	words[SHARED_SAMPLER + 3] = (uint32_t)(sampler_words[1] >> 32);

	/* Succeeded: the eight words. */
	return 0;
}

/*
 * Builds the data sequencer program that copies the eight shared words
 * from words_address into shared registers 0 to 7.
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_load_op_pds_data_create_and_upload: one texture DMA of the
 * constants to common store 0).
 */
int
powervr_render_load_shared(
	uint64_t words_address,
	struct powervr_data_shared_program *program)
{
	struct powervr_data_load load;
	int error;

	/* Refuses a missing result. */
	if (program == NULL)
		return EINVAL;

	/* One DMA of the eight words to shared register 0. */
	load.kind = POWERVR_DATA_LOAD_DMA;
	load.shared = 0;
	load.words = POWERVR_RENDER_LOAD_SHARED_WORDS;
	load.source = words_address;
	error = powervr_data_shared_build(&load, 1, program);
	if (error != 0)
		return error;

	/* Succeeded: the program. */
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
