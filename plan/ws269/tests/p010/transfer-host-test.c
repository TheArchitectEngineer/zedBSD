/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010f (transfer-program.c): the transfer command's
 * registers of a clear and a copy against golden values that the
 * one-time M9 comparison with Mesa 26.2.5's csbgen packing confirmed
 * (phase010f/phase.md), the copy program against the compiler back
 * end's groups with the unnormalised sample, the shared words (the
 * sampler as Mesa's transfer sampler, the coordinate floats), the
 * clear colour's rounding, the odd-row split and the refusals.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/shader-private.h"
#include "drivers/gpu/powervr/transfer-program.h"
#include "drivers/gpu/powervr/usc-encode.h"

static unsigned passed;
static unsigned failed;

static void
check(
	int ok,
	const char *name)
{
	if (ok) {
		passed++;
	} else {
		failed++;
		printf("FAIL %s\n", name);
	}
}

/* A 64 x 64 linear R8G8B8A8 job over the whole destination. */
static void
job_init(
	struct powervr_transfer_job *job,
	enum powervr_transfer_kind kind)
{
	memset(job, 0, sizeof(*job));
	job->kind = kind;
	job->destination.format = powervr_format_find(37);
	job->destination.address = 0x10000;
	job->destination.width = 64;
	job->destination.height = 64;
	job->destination.linear = 1;
	job->destination.row_pitch = 256;
	job->area.width = 64;
	job->area.height = 64;
	job->colour[0] = 0x3e800000U;
	job->colour[1] = 0x3f000000U;
	job->colour[2] = 0x3f400000U;
	job->colour[3] = 0x3f800000U;
	job->copy_kick_offset = 0x5000;
	job->shared_code_offset = 0x5100;
	job->shared_data_offset = 0x5200;
	job->shared_data_words = 3;
	job->event_data_offset = 0x5300;
}

/* The registers both kinds share for this destination. */
static int
common_regs(
	const struct powervr_transfer_regs *regs)
{
	return regs->usc_pixel_output_ctrl == 0x1ff8U && regs->isp_mtile_size == 0x40004U &&
	       regs->isp_render_origin == 0 && regs->isp_ctl == 0x23000U && regs->isp_aa == 0 &&
	       regs->event_pixel_pds_info == 0x201U && regs->event_pixel_pds_code == 0x80U &&
	       regs->event_pixel_pds_data == 0x5300U && regs->isp_render == 3U &&
	       regs->frag_screen == 0x3f003fU && regs->isp_rgn == 0 && regs->isp_mtile_base == 0;
}

static void
test_regs(void)
{
	struct powervr_transfer_job job;
	struct powervr_transfer_regs regs;
	uint32_t state[2];
	int ok;

	/* Clear: no background, the packed colour, 16-bit float source, the R8G8B8A8 pack mode. */
	job_init(&job, POWERVR_TRANSFER_CLEAR);
	ok = powervr_transfer_regs_build(&job, &regs, state) == 0;
	ok = ok && common_regs(&regs) && regs.isp_bgobjvals == 0 && regs.pds_bgnd[0] == 0 && regs.usc_clear[0] == 0xffbf8040U;
	ok = ok && state[0] == 0x4000U && state[1] == 0x2000U;
	ok = ok && regs.pbe[0] == 0x0200007c68800000ULL && regs.pbe[1] == 0x0000003f0000003fULL && regs.pbe[2] == 0;
	check(ok, "clear registers");

	/* Copy: the background object, the raw 32-bit pack mode from 8 bits a channel. */
	job_init(&job, POWERVR_TRANSFER_COPY);
	ok = powervr_transfer_regs_build(&job, &regs, state) == 0;
	ok = ok && common_regs(&regs) && regs.isp_bgobjvals == 0x200U && regs.usc_clear[0] == 0;
	ok = ok && regs.pds_bgnd[0] == 0x0000510000005000ULL && regs.pds_bgnd[1] == 0x0000520000000000ULL && regs.pds_bgnd[2] == 0x0080000000010000ULL;
	ok = ok && state[0] == 0x4000U && state[1] == 0x00081a00U && regs.pbe[0] == 0x0200007c96800000ULL;
	check(ok, "copy registers");

	/* A rectangle inside: the clip, the origin and the tiles it covers. */
	job_init(&job, POWERVR_TRANSFER_CLEAR);
	job.area.x = 20;
	job.area.y = 36;
	job.area.width = 30;
	job.area.height = 10;
	ok = powervr_transfer_regs_build(&job, &regs, state) == 0;
	ok = ok && regs.isp_render_origin == 0x10002U && regs.isp_mtile_size == 0x30001U;
	check(ok, "clear of an inner rectangle");

	/* Refusals: an odd linear row of several rows, a missing job, a copy of 8-bit texels. */
	job_init(&job, POWERVR_TRANSFER_CLEAR);
	job.destination.row_pitch = 63 * 4;
	check(powervr_transfer_needs_rows(&job.destination, &job.area), "odd rows need one row a job");
	check(powervr_transfer_regs_build(&job, &regs, state) != 0, "odd rows refused");
	job.area.height = 1;
	check(!powervr_transfer_needs_rows(&job.destination, &job.area) && powervr_transfer_regs_build(&job, &regs, state) == 0, "one odd row taken");
	check(powervr_transfer_regs_build(NULL, &regs, state) != 0, "no job refused");
	job_init(&job, POWERVR_TRANSFER_COPY);
	job.destination.format = powervr_format_find(9);
	job.destination.row_pitch = 64;
	check(powervr_transfer_regs_build(&job, &regs, state) != 0, "copy of 8-bit texels refused");
}

static void
test_colour(void)
{
	static const struct {
		uint32_t bits;
		uint32_t byte;
	} cases[] = {
		{ 0x00000000U, 0 }, { 0x3e800000U, 64 }, { 0x3f000000U, 128 }, { 0x3f400000U, 191 },
		{ 0x3f800000U, 255 }, { 0x40000000U, 255 }, { 0xbf800000U, 0 }, { 0x7fc00000U, 255 },
		{ 0x7f800000U, 255 }, { 0x3b808081U, 1 }, { 0x3b000000U, 0 }, { 0x00000001U, 0 }
	};
	uint32_t colour[4];
	uint32_t packed[4];
	unsigned index;
	int ok;

	/* Each case in the red channel, the others zero. */
	ok = 1;
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		colour[0] = cases[index].bits;
		colour[1] = 0;
		colour[2] = 0;
		colour[3] = 0;
		if (powervr_transfer_clear_colour(powervr_format_find(37), colour, packed) != 0 || packed[0] != cases[index].byte) {
			printf("  %08x -> %08x, expected %u\n", cases[index].bits, packed[0], cases[index].byte);
			ok = 0;
		}
	}
	check(ok, "clear colour rounding");
	check(powervr_transfer_clear_colour(powervr_format_find(43), colour, packed) != 0, "sRGB clear refused for now");
}

static void
test_shared(void)
{
	struct powervr_data_shared_program shared;
	struct powervr_image_layout layout;
	struct powervr_view_shape view;
	struct powervr_render_area area;
	uint32_t words[POWERVR_TRANSFER_SHARED_WORDS];
	int ok;

	/* A linear 64 x 64 source read from (3, 5) for a destination at (10, 1). */
	memset(&layout, 0, sizeof(layout));
	layout.row_pitch = 256;
	layout.padded_width = 64;
	layout.padded_height = 64;
	layout.layer_bytes = 0x4000;
	memset(&view, 0, sizeof(view));
	view.format = powervr_format_find(37);
	view.layout = &layout;
	view.address = 0x20000;
	view.width = 64;
	view.height = 64;
	view.image_levels = 1;
	view.level_count = 1;
	view.layer_count = 1;
	view.linear = 1;
	area.x = 10;
	area.y = 1;
	area.width = 8;
	area.height = 8;
	ok = powervr_transfer_shared_words(&view, 3, 5, &area, words) == 0;
	ok = ok && words[4] == 0 && words[5] == 0x00022400U && words[6] == 0 && words[7] == 0;
	ok = ok && words[8] == 0x3f800000U && words[9] == 0xc0e00000U && words[10] == 0x3f800000U && words[11] == 0x40800000U;
	check(ok, "shared words: the transfer sampler and the floats");

	/* The image state names the raw 32-bit texture format (24 at bits 27 to 33). */
	check(ok && (((((uint64_t)words[1] << 32) | words[0]) >> 27) & 0x7fU) == 24U, "shared words: raw image format");

	/* Refusals: 8-bit texels. */
	view.format = powervr_format_find(9);
	check(powervr_transfer_shared_words(&view, 0, 0, &area, words) != 0, "shared words refuse 8-bit texels");

	/* The program: one DMA of twelve words, three data words, one code word. */
	ok = powervr_transfer_shared_program(0x30000, &shared) == 0;
	check(ok && shared.data_words == 3 && shared.code_words == 1 && shared.data[0] == 0x30000U, "shared program");
}

static void
test_copy_usc(void)
{
	struct shader_machine_operation operation;
	struct powervr_usc_group groups[11];
	struct powervr_render_usc program;
	uint8_t bytes[POWERVR_RENDER_USC_MAX];
	size_t length;
	unsigned index;
	int ok;

	/* The program, rebuilt here from the back end's groups. */
	ok = powervr_transfer_copy_usc(&program) == 0 && program.temps == 12;
	for (index = 0; index < 11 && ok; index++) {
		memset(&operation, 0, sizeof(operation));
		operation.destination[0].bank = 0xff;
		operation.destination[1].bank = 0xff;
		operation.source[0].bank = 0xff;
		operation.source[1].bank = 0xff;
		operation.source[2].bank = 0xff;
		operation.source[3].bank = 0xff;
		if (index < 2) {
			operation.kind = MACHINE_MOVE_SPECIAL;
			operation.destination[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.destination[0].index = (uint16_t)index;
			operation.source[0].bank = POWERVR_USC_BANK_SPECIAL;
			operation.source[0].index = index == 0 ? 97 : 100;
		} else if (index < 6) {
			operation.kind = MACHINE_MOVE;
			operation.destination[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.destination[0].index = (uint16_t)(8 + index - 2);
			operation.source[0].bank = POWERVR_USC_BANK_SHARED;
			operation.source[0].index = (uint16_t)(8 + index - 2);
		} else if (index < 8) {
			operation.kind = MACHINE_FLOAT_MULTIPLY_ADD;
			operation.destination[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.destination[0].index = (uint16_t)(2 + index - 6);
			operation.source[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.source[0].index = (uint16_t)(index - 6);
			operation.source[1].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.source[1].index = (uint16_t)(8 + (index - 6) * 2);
			operation.source[2].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.source[2].index = (uint16_t)(9 + (index - 6) * 2);
		} else if (index == 8) {
			operation.kind = MACHINE_SAMPLE;
			operation.destination[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.destination[0].index = 4;
			operation.source[0].bank = POWERVR_USC_BANK_SHARED;
			operation.source[0].index = 0;
			operation.source[1].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.source[1].index = 2;
			operation.source[2].bank = POWERVR_USC_BANK_SHARED;
			operation.source[2].index = 4;
		} else if (index == 9) {
			operation.kind = MACHINE_WAIT;
		} else {
			operation.kind = MACHINE_MOVE;
			operation.destination[0].bank = POWERVR_USC_BANK_SPECIAL;
			operation.destination[0].index = 32;
			operation.source[0].bank = POWERVR_USC_BANK_TEMPORARY;
			operation.source[0].index = 4;
			operation.variant = 1;
		}
		ok = shader_backend_group(&operation, 0, &groups[index]) == 0;
	}
	ok = ok && powervr_usc_sample(0, 0, 2, 4, 0, &groups[8].operation[POWERVR_USC_PHASE_BACKEND]) == 0;
	groups[10].end = 1;
	ok = ok && powervr_usc_program_encode(groups, 11, bytes, sizeof(bytes), &length) == 0;
	check(ok && length == program.length && memcmp(bytes, program.code, length) == 0, "copy program");
}

int
main(void)
{
	test_regs();
	test_colour();
	test_shared();
	test_copy_usc();
	printf("transfer-program host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
