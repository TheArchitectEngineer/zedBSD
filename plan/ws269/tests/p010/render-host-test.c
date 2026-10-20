/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010e (render-program.c): the fixed shading cluster
 * programs are compared with the same groups built by the compiler's back
 * end (shader_backend_group, whose groups the p010-compiler group test
 * checks against Mesa's tables) and encoded, the end-of-tile emission's
 * bytes with Mesa's I_BACKEND pieces; the data sequencer programs and the
 * pixel back end, background, primitive and vertex stream words with
 * golden values that the one-time M9 comparison with Mesa 26.2.5's
 * csbgen packing confirmed (phase010e/phase.md); and the refusals.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-program.h"
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

/* A machine operation with no register named. */
static struct shader_machine_operation
blank(
	unsigned kind)
{
	struct shader_machine_operation operation;
	unsigned index;

	memset(&operation, 0, sizeof(operation));
	operation.kind = (uint8_t)kind;
	for (index = 0; index < 2; index++)
		operation.destination[index].bank = 0xff;
	for (index = 0; index < 4; index++)
		operation.source[index].bank = 0xff;
	return operation;
}

static struct shader_machine_register
reg(
	unsigned bank,
	unsigned index)
{
	struct shader_machine_register value;

	value.bank = (uint8_t)bank;
	value.index = (uint16_t)index;
	return value;
}

/* Encodes groups and compares the bytes with a program's. */
static int
same_bytes(
	const struct powervr_usc_group *groups,
	size_t count,
	const struct powervr_render_usc *program)
{
	uint8_t bytes[POWERVR_RENDER_USC_MAX];
	size_t length;
	size_t index;

	if (powervr_usc_program_encode(groups, count, bytes, sizeof(bytes), &length) != 0)
		return 0;
	if (length != program->length || memcmp(bytes, program->code, length) != 0) {
		printf("  ours:");
		for (index = 0; index < program->length; index++)
			printf(" %02x", program->code[index]);
		printf("\n  back end:");
		for (index = 0; index < length; index++)
			printf(" %02x", bytes[index]);
		printf("\n");
		return 0;
	}
	return 1;
}

/* The back end's group of an immediate into a temporary. */
static int
immediate(
	struct powervr_usc_group *group,
	unsigned temp,
	uint32_t value)
{
	struct shader_machine_operation operation;

	operation = blank(MACHINE_IMMEDIATE);
	operation.destination[0] = reg(POWERVR_USC_BANK_TEMPORARY, temp);
	operation.immediate = value;
	return shader_backend_group(&operation, 0, group) == 0;
}

static void
test_programs(void)
{
	struct shader_machine_operation operation;
	struct powervr_usc_group groups[6];
	struct powervr_render_usc program;
	static const uint32_t state[2] = { 0x11223344U, 0x55667788U };
	static const uint8_t emit[] = { 0xc2 };
	unsigned index;
	int ok;

	/* End of tile: two immediates, then a main group with the back end's emit (0b110 << 5, freep 1 << 1). */
	ok = powervr_render_eot_usc(state, &program) == 0 && program.temps == 2;
	ok = ok && immediate(&groups[0], 0, state[0]) && immediate(&groups[1], 1, state[1]);
	memset(&groups[2], 0, sizeof(groups[2]));
	groups[2].repeat = 1;
	groups[2].unit = POWERVR_USC_UNIT_MAIN;
	groups[2].end = 1;
	groups[2].source[0].used = 1;
	groups[2].source[0].bank = POWERVR_USC_BANK_TEMPORARY;
	groups[2].source[0].index = 0;
	groups[2].source[2].used = 1;
	groups[2].source[2].bank = POWERVR_USC_BANK_TEMPORARY;
	groups[2].source[2].index = 1;
	groups[2].operation[POWERVR_USC_PHASE_BACKEND].length = 1;
	memcpy(groups[2].operation[POWERVR_USC_PHASE_BACKEND].bytes, emit, 1);
	check(ok && same_bytes(groups, 3, &program), "end of tile program");

	/* Clear: the move from shared register 0 to the pixel output under the overlap check, the end. */
	operation = blank(MACHINE_MOVE);
	operation.source[0] = reg(POWERVR_USC_BANK_SHARED, 0);
	operation.destination[0] = reg(POWERVR_USC_BANK_SPECIAL, 32);
	operation.variant = 1;
	ok = powervr_render_clear_usc(&program) == 0 && program.temps == 0;
	ok = ok && shader_backend_group(&operation, 0, &groups[0]) == 0;
	groups[0].end = 1;
	check(ok && same_bytes(groups, 1, &program), "clear program");

	/* A clear of two words (four halves): shared 0 and 1 to pixel outputs 0 and 1, the first checked, the second ending. */
	operation = blank(MACHINE_MOVE);
	operation.source[0] = reg(POWERVR_USC_BANK_SHARED, 0);
	operation.destination[0] = reg(POWERVR_USC_BANK_SPECIAL, 32);
	operation.variant = 1;
	ok = powervr_render_clear_usc_words(2, &program) == 0 && program.temps == 0;
	ok = ok && shader_backend_group(&operation, 0, &groups[0]) == 0;
	operation = blank(MACHINE_MOVE);
	operation.source[0] = reg(POWERVR_USC_BANK_SHARED, 1);
	operation.destination[0] = reg(POWERVR_USC_BANK_SPECIAL, 33);
	ok = ok && shader_backend_group(&operation, 0, &groups[1]) == 0;
	groups[1].end = 1;
	check(ok && same_bytes(groups, 2, &program), "clear program of two words");
	check(powervr_render_clear_usc_words(3, &program) != 0 && powervr_render_clear_usc_words(0, &program) != 0, "clear of 0 or 3 words refused");
	{
		struct powervr_data_shared_program one;
		struct powervr_data_shared_program two;
		uint32_t halves[2] = { 0x38003c00U, 0x3c000000U };

		ok = powervr_render_clear_shared_words(halves, 2, &two) == 0 && powervr_render_clear_shared(0x38003c00U, &one) == 0;
		check(ok && two.data_words >= 2 && two.data[0] == 0x38003c00U && two.data[1] == 0x3c000000U && one.data[0] == 0x38003c00U, "the two clear words written as one 64-bit write");
	}

	/* Colour: the immediate, then the move to the pixel output. */
	ok = powervr_render_colour_usc(0xff00ff00U, &program) == 0 && program.temps == 1;
	ok = ok && immediate(&groups[0], 0, 0xff00ff00U);
	operation = blank(MACHINE_MOVE);
	operation.source[0] = reg(POWERVR_USC_BANK_TEMPORARY, 0);
	operation.destination[0] = reg(POWERVR_USC_BANK_SPECIAL, 32);
	operation.variant = 1;
	ok = ok && shader_backend_group(&operation, 0, &groups[1]) == 0;
	groups[1].end = 1;
	check(ok && same_bytes(groups, 2, &program), "colour program");

	/* Vertex: three input writes, the w of 1.0, the emit and end. */
	ok = powervr_render_vertex_usc(&program) == 0 && program.temps == 1;
	for (index = 0; index < 3 && ok; index++) {
		operation = blank(MACHINE_VERTEX_WRITE);
		operation.source[0] = reg(POWERVR_USC_BANK_VERTEX_INPUT, index);
		operation.immediate = index;
		ok = shader_backend_group(&operation, 0, &groups[index]) == 0;
	}
	ok = ok && immediate(&groups[3], 0, 0x3f800000U);
	operation = blank(MACHINE_VERTEX_WRITE);
	operation.source[0] = reg(POWERVR_USC_BANK_TEMPORARY, 0);
	operation.immediate = 3;
	ok = ok && shader_backend_group(&operation, 0, &groups[4]) == 0;
	operation = blank(MACHINE_VERTEX_END);
	ok = ok && shader_backend_group(&operation, 0, &groups[5]) == 0;
	check(ok && same_bytes(groups, 6, &program), "vertex program");

	/* Refusals. */
	check(powervr_render_eot_usc(NULL, &program) != 0, "end of tile without words");
	check(powervr_render_clear_usc(NULL) != 0, "clear without result");
}

static void
test_data_sequencer(void)
{
	struct powervr_data_shared_program shared;
	struct powervr_data_vertex_program vertex;
	struct powervr_render_kick kick;
	uint32_t data[POWERVR_RENDER_EVENT_DATA_WORDS];
	uint32_t code;
	uint32_t data_word;
	uint32_t info;
	int ok;

	/* The end of tile's task word: the offset 0x1200, two temporaries (one block of four) at bit 35. */
	ok = powervr_render_event_data(0x1200, 2, data) == 0;
	check(ok && data[0] == 0x1200U && data[1] == 0x8U, "end of tile data");
	check(powervr_render_event_data(0x1202, 2, data) != 0, "end of tile data misaligned");

	/* Its fragment command words: the fixed code at 128, the data, one unit of data and one of state. */
	ok = powervr_render_event_words(0x2340, &code, &data_word, &info) == 0;
	check(ok && code == 0x80U && data_word == 0x2340U && info == 0x201U, "end of tile words");
	check(powervr_render_event_words(0x2348, &code, &data_word, &info) != 0, "end of tile words misaligned");

	/* A kick: the task word, six zeros, one task issue that ends. */
	ok = powervr_render_kick_build(0x4000, 1, &kick) == 0;
	ok = ok && kick.data[0] == 0x4000U && kick.data[1] == 0x8U && kick.data[7] == 0;
	check(ok && kick.code[0] == 0xf4000002U, "kick program");

	/* The clear's shared program: one write of the colour, 3 data words and 1 code word. */
	ok = powervr_render_clear_shared(0xffff0000U, &shared) == 0;
	check(ok && shared.data_words == 3 && shared.code_words == 1 && shared.data[0] == 0xffff0000U && shared.data[2] == 0x90000000U, "clear shared program");

	/* The vertex program: one load of three words a vertex. */
	ok = powervr_render_vertex_pds(0x123450000ULL, 0x1000, 1, &vertex) == 0;
	check(ok && vertex.data_words == 8 && vertex.code_words == 3 && vertex.temps == 2, "vertex data sequencer program");
}

static void
test_pbe(void)
{
	struct powervr_render_surface surface;
	struct powervr_render_area area;
	uint32_t state[2];
	uint64_t words[3];
	int ok;

	/* R8G8B8A8 linear 64 x 64 at 0x8000001000, the whole area (values M9 confirmed). */
	memset(&surface, 0, sizeof(surface));
	surface.format = powervr_format_find(37);
	surface.address = 0x8000001000ULL;
	surface.width = 64;
	surface.height = 64;
	surface.linear = 1;
	surface.row_pitch = 256;
	area.x = 0;
	area.y = 0;
	area.width = 64;
	area.height = 64;
	ok = powervr_render_pbe_words(&surface, &area, state, words) == 0;
	ok = ok && state[0] == 0x00000400U && state[1] == 0x00082020U;
	ok = ok && words[0] == 0x0200007c68800000ULL && words[1] == 0x0000003f0000003fULL && words[2] == 0;
	check(ok, "pbe linear words");

	/* A partial area: the clip is the area within the attachment. */
	area.x = 8;
	area.y = 4;
	area.width = 100;
	area.height = 20;
	ok = powervr_render_pbe_words(&surface, &area, state, words) == 0;
	ok = ok && ((words[0] >> 6) & 0x3fffU) == 8 && (words[1] & 0x3fffU) == 23;
	ok = ok && ((words[1] >> 32) & 0x3fffU) == 63 && ((words[1] >> 46) & 0x3fffU) == 4;
	check(ok, "pbe clip to the area");

	/* Twiddled 320 x 240 over a 512 padded edge: layout 1, sizes log2 9 and 8. */
	surface.linear = 0;
	surface.width = 320;
	surface.height = 240;
	surface.padded_width = 512;
	area.x = 0;
	area.y = 0;
	area.width = 320;
	area.height = 240;
	ok = powervr_render_pbe_words(&surface, &area, state, words) == 0;
	ok = ok && ((words[0] >> 32) & 3U) == 1 && ((words[0] >> 34) & 0xffffU) == 255;
	ok = ok && (words[1] >> 60) == 9 && ((words[1] >> 28) & 0xfU) == 8;
	check(ok, "pbe twiddled words");

	/* B8G8R8A8: source 0 to channel 2, 2 to 0. */
	surface.format = powervr_format_find(44);
	ok = powervr_render_pbe_words(&surface, &area, state, words) == 0;
	ok = ok && ((words[0] >> 20) & 7U) == 2 && ((words[0] >> 26) & 7U) == 0;
	check(ok, "pbe swizzle of b8g8r8a8");

	/* Refusals: a depth format, an area outside, a missing format. */
	surface.format = powervr_format_find(126);
	check(powervr_render_pbe_words(&surface, &area, state, words) != 0, "pbe refuses depth");
	surface.format = powervr_format_find(37);
	area.x = 400;
	check(powervr_render_pbe_words(&surface, &area, state, words) != 0, "pbe refuses an area outside");
	surface.format = NULL;
	area.x = 0;
	check(powervr_render_pbe_words(&surface, &area, state, words) != 0, "pbe refuses no format");
}

static void
test_streams(void)
{
	static const uint32_t ppp_golden[POWERVR_RENDER_PPP_WORDS] = {
		0x007f05c7U, 0x00040000U, 0x012e0000U, 0x0e000000U,
		0x00002000U, 0x00002100U, 0x00000000U, 0x00000000U,
		0x00000000U, 0x00002200U, 0x800001ffU, 0x000001ffU,
		0x29e12e13U, 0x04010000U, 0x00000000U, 0x00000000U,
		0x00000000U, 0x00000040U, 0x00000000U
	};
	static const uint32_t vdm_golden[POWERVR_RENDER_VDM_WORDS] = {
		0x00001300U, 0x81234560U, 0x4c000f80U, 0x00003000U,
		0x00003040U, 0x00000004U, 0x20000442U, 0x68000600U,
		0x00000003U, 0xc0000000U
	};
	struct powervr_render_background background;
	struct powervr_render_triangle triangle;
	uint32_t ppp[POWERVR_RENDER_PPP_WORDS];
	uint32_t vdm[POWERVR_RENDER_VDM_WORDS];
	uint64_t words[3];
	int ok;

	/* The triangle's block and stream (values M9 confirmed). */
	memset(&triangle, 0, sizeof(triangle));
	triangle.ppp_address = 0x81234560ULL;
	triangle.fragment.kick_offset = 0x2000;
	triangle.fragment.shared_code_offset = 0x2100;
	triangle.fragment.shared_data_offset = 0x2200;
	triangle.vertex_data_offset = 0x3000;
	triangle.vertex_code_offset = 0x3040;
	triangle.vertex_data_words = 8;
	triangle.vertex_temps = 2;
	ok = powervr_render_triangle_ppp(&triangle, ppp) == 0;
	check(ok && memcmp(ppp, ppp_golden, sizeof(ppp)) == 0, "triangle primitive block");
	ok = powervr_render_triangle_vdm(&triangle, vdm) == 0;
	check(ok && memcmp(vdm, vdm_golden, sizeof(vdm)) == 0, "triangle vertex stream");

	/* Refusals: a misaligned program and too many temporaries. */
	triangle.vertex_code_offset = 0x3044;
	check(powervr_render_triangle_vdm(&triangle, vdm) != 0, "vertex stream refuses a misaligned program");
	triangle.vertex_code_offset = 0x3040;
	triangle.vertex_temps = 1000;
	check(powervr_render_triangle_vdm(&triangle, vdm) != 0, "vertex stream refuses too many temporaries");
	triangle.fragment.kick_offset = 0x2004;
	check(powervr_render_triangle_ppp(&triangle, ppp) != 0, "primitive block refuses a misaligned program");

	/* The background words (values M9 confirmed). */
	background.kick_offset = 0x1230;
	background.shared_code_offset = 0x4560;
	background.shared_data_offset = 0x7890;
	background.shared_data_words = 3;
	background.shared_registers = 1;
	ok = powervr_render_background_words(&background, words) == 0;
	ok = ok && words[0] == 0x0000456000001230ULL && words[1] == 0x0000789000000000ULL;
	check(ok && words[2] == 0x0080000000010000ULL, "background words");
}

int
main(void)
{
	test_programs();
	test_data_sequencer();
	test_pbe();
	test_streams();
	printf("render-program host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
