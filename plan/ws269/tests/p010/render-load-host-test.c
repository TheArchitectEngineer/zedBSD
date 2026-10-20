/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010e's attachment load background
 * (render-load-program.c): the program against the compiler back end's
 * groups with the unnormalised sample, the shared words (the image state
 * as image-state.c gives it, the sampler as Mesa's transfer sampler,
 * which the p010f M9 comparison confirmed), the data sequencer program
 * and the refusals.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-load-program.h"
#include "drivers/gpu/powervr/shader-private.h"
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

/* One machine operation from its kind, destination and up to three sources. */
static struct shader_machine_operation
operation_of(
	unsigned kind,
	unsigned destination_bank,
	unsigned destination,
	unsigned bank_0,
	unsigned source_0,
	unsigned bank_2,
	unsigned source_2)
{
	struct shader_machine_operation operation;

	memset(&operation, 0, sizeof(operation));
	operation.kind = (uint8_t)kind;
	operation.destination[0].bank = (uint8_t)destination_bank;
	operation.destination[0].index = (uint16_t)destination;
	operation.destination[1].bank = 0xff;
	operation.source[0].bank = (uint8_t)bank_0;
	operation.source[0].index = (uint16_t)source_0;
	operation.source[1].bank = 0xff;
	operation.source[2].bank = (uint8_t)bank_2;
	operation.source[2].index = (uint16_t)source_2;
	operation.source[3].bank = 0xff;
	return operation;
}

int
main(void)
{
	struct shader_machine_operation operations[6];
	struct powervr_usc_group groups[6];
	struct powervr_render_usc program;
	struct powervr_data_shared_program shared;
	struct powervr_image_layout layout;
	struct powervr_view_shape view;
	uint32_t words[POWERVR_RENDER_LOAD_SHARED_WORDS];
	uint64_t image[2];
	uint8_t bytes[POWERVR_RENDER_USC_MAX];
	size_t length;
	unsigned index;
	int ok;

	/* The program, rebuilt here from the back end's groups. */
	operations[0] = operation_of(MACHINE_MOVE_SPECIAL, POWERVR_USC_BANK_TEMPORARY, 0, POWERVR_USC_BANK_SPECIAL, 97, 0xff, 0);
	operations[1] = operation_of(MACHINE_MOVE_SPECIAL, POWERVR_USC_BANK_TEMPORARY, 1, POWERVR_USC_BANK_SPECIAL, 100, 0xff, 0);
	operations[2] = operation_of(MACHINE_SAMPLE, POWERVR_USC_BANK_TEMPORARY, 4, POWERVR_USC_BANK_SHARED, 0, POWERVR_USC_BANK_SHARED, 4);
	operations[2].source[1].bank = POWERVR_USC_BANK_TEMPORARY;
	operations[2].source[1].index = 0;
	operations[3] = operation_of(MACHINE_WAIT, 0xff, 0, 0xff, 0, 0xff, 0);
	operations[4] = operation_of(MACHINE_PACK, POWERVR_USC_BANK_TEMPORARY, 8, POWERVR_USC_BANK_TEMPORARY, 4, 0xff, 0);
	operations[4].immediate = 4;
	operations[4].immediate_2 = 1;
	operations[5] = operation_of(MACHINE_MOVE, POWERVR_USC_BANK_SPECIAL, 32, POWERVR_USC_BANK_TEMPORARY, 8, 0xff, 0);
	operations[5].variant = 1;
	ok = powervr_render_load_usc(powervr_format_find(37), &program) == 0 && program.temps == 9;
	for (index = 0; index < 6 && ok; index++)
		ok = shader_backend_group(&operations[index], 0, &groups[index]) == 0;
	ok = ok && powervr_usc_sample(0, 0, 2, 4, 0, &groups[2].operation[POWERVR_USC_PHASE_BACKEND]) == 0;
	groups[5].end = 1;
	ok = ok && powervr_usc_program_encode(groups, 6, bytes, sizeof(bytes), &length) == 0;
	check(ok && length == program.length && memcmp(bytes, program.code, length) == 0, "load program");
	check(powervr_render_load_usc(powervr_format_find(43), &program) != 0, "load of sRGB refused for now");
	check(powervr_render_load_usc(NULL, &program) != 0, "load without a format refused");

	/* The words: the attachment's image state, the transfer sampler. */
	memset(&layout, 0, sizeof(layout));
	layout.padded_width = 64;
	layout.padded_height = 64;
	layout.layer_bytes = 0x4000;
	memset(&view, 0, sizeof(view));
	view.format = powervr_format_find(44);
	view.layout = &layout;
	view.address = 0x40000;
	view.width = 64;
	view.height = 64;
	view.image_levels = 1;
	view.level_count = 1;
	view.layer_count = 1;
	ok = powervr_render_load_words(&view, words) == 0 && powervr_view_words(&view, image) == 0;
	ok = ok && words[0] == (uint32_t)image[0] && words[1] == (uint32_t)(image[0] >> 32);
	ok = ok && words[2] == (uint32_t)image[1] && words[3] == (uint32_t)(image[1] >> 32);
	check(ok && words[4] == 0 && words[5] == 0x00022400U && words[6] == 0 && words[7] == 0, "load words");

	/* The program: one DMA of eight words. */
	ok = powervr_render_load_shared(0x50000, &shared) == 0;
	check(ok && shared.data_words == 3 && shared.code_words == 1 && shared.data[0] == 0x50000U, "load shared program");

	printf("render-load-program host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
