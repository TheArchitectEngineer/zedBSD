/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR Rogue shading cluster and data sequencer
 * encoders against bit strings worked out by hand.
 *
 * Every expected byte or word below was assembled from the field tables
 * of Mesa 26.2.5 (src/imagination/pco/pco_isa.py for the shading
 * cluster, src/imagination/vulkan/pds/pvr_rogue_pds_defs.h and
 * src/imagination/csbgen/rogue/pds.xml for the data sequencer); the
 * comment beside each names the fields it holds, high bits first.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/pds-encode.h"
#include "drivers/gpu/powervr/usc-encode.h"

static int failures;
static int passes;

/* Counts one check and reports a failure. */
static void
check(
	int condition,
	const char *what)
{
	if (condition) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: %s\n", what);
}

/* Compares an encoded operation with the expected bytes. */
static void
check_operation(
	int error,
	const struct powervr_usc_operation *operation,
	const uint8_t *expected,
	unsigned length,
	const char *what)
{
	unsigned i;

	if (error != 0 || operation->length != length || memcmp(operation->bytes, expected, length) != 0) {
		failures++;
		printf("FAIL: %s: error %d length %u:", what, error, operation->length);
		for (i = 0; i < operation->length; i++)
			printf(" %02x", operation->bytes[i]);
		printf("\n");
		return;
	}

	passes++;
}

/* Compares an encoded program with the expected bytes. */
static void
check_program(
	const struct powervr_usc_group *groups,
	size_t count,
	const uint8_t *expected,
	size_t expected_length,
	const char *what)
{
	uint8_t output[256];
	size_t length;
	size_t i;
	int error;

	memset(output, 0x5a, sizeof(output));
	length = 0;
	error = powervr_usc_program_encode(groups, count, output, sizeof(output), &length);
	if (error != 0 || length != expected_length || memcmp(output, expected, length) != 0) {
		failures++;
		printf("FAIL: %s: error %d length %zu:", what, error, length);
		for (i = 0; i < length; i++)
			printf(" %02x", output[i]);
		printf("\n");
		return;
	}

	passes++;
}

/* Sets one operand register. */
static void
set_register(
	struct powervr_usc_register *reg,
	unsigned bank,
	unsigned index)
{
	reg->used = 1;
	reg->bank = (uint8_t)bank;
	reg->index = (uint16_t)index;
}

/* Starts a group with the defaults: enabled, repeat once. */
static void
group_start(
	struct powervr_usc_group *group,
	unsigned unit)
{
	memset(group, 0, sizeof(*group));
	group->unit = (uint8_t)unit;
	group->repeat = 1;
}

/* Checks the operation encoders one form at a time. */
static void
test_operations(void)
{
	struct powervr_usc_operation op;
	int error;

	/* fadd: main_op 000, sat 1, s0neg 1, s0abs 0, s1abs 1, s0flr 0. */
	{
		static const uint8_t expected[] = { 0x1a };
		error = powervr_usc_float_pair(POWERVR_USC_FLOAT_ADD, 1, 1, 0, 1, 0, &op);
		check_operation(error, &op, expected, 1, "float add");
	}

	/* fmul.lp: main_op 011, all modifiers clear. */
	{
		static const uint8_t expected[] = { 0x60 };
		error = powervr_usc_float_pair(POWERVR_USC_FLOAT_MULTIPLY_LOW_PRECISION, 0, 0, 0, 0, 0, &op);
		check_operation(error, &op, expected, 1, "float multiply low precision");
	}

	/* fmad: 110 ext0 0 s0neg 0 s0abs 0 s2neg 1 sat 1. */
	{
		static const uint8_t expected[] = { 0xc3 };
		error = powervr_usc_float_multiply_add(1, 0, 0, 0, 0, 1, 0, 0, 0, &op);
		check_operation(error, &op, expected, 1, "multiply-add short");
	}

	/* fmad ext: 110 1 0000, then rsvd 000 lp 1 s1abs 0 s1neg 1 s2flr 0 s2abs 1. */
	{
		static const uint8_t expected[] = { 0xd0, 0x15 };
		error = powervr_usc_float_multiply_add(0, 0, 0, 1, 0, 0, 1, 0, 1, &op);
		check_operation(error, &op, expected, 2, "multiply-add long");
	}

	/* sngl rcp: 100 0 0000. */
	{
		static const uint8_t expected[] = { 0x80 };
		error = powervr_usc_single(POWERVR_USC_SINGLE_RECIPROCAL, 0, 0, &op);
		check_operation(error, &op, expected, 1, "reciprocal");
	}

	/* sngl byp ext: 100 1 0111, then rsvd 00000 0 s0neg 1 s0abs 0. */
	{
		static const uint8_t expected[] = { 0x97, 0x02 };
		error = powervr_usc_single(POWERVR_USC_SINGLE_PASS, 1, 0, &op);
		check_operation(error, &op, expected, 2, "pass negated");
	}

	/* pck: 100 1 1100, then prog 0 rtz 1 scale 0 format f32 01111. */
	{
		static const uint8_t expected[] = { 0x9c, 0x4f };
		error = powervr_usc_pack(0x0f, 0, 1, 0, &op);
		check_operation(error, &op, expected, 2, "pack f32");
	}

	/* int32_64 add6432: 111 0 s 1 s2neg 0 op 00. */
	{
		static const uint8_t expected[] = { 0xe8 };
		error = powervr_usc_integer(POWERVR_USC_INTEGER_ADD_64_FROM_32, 1, 0, 0, 0, 0, 0, 0, 0, &op);
		check_operation(error, &op, expected, 1, "integer add short");
	}

	/* int32_64 ext: 111 1 0 0 00, then 0 cin 1 s1neg 0 s0neg 1 0 s0abs 0 s1abs 0 s2abs 0. */
	{
		static const uint8_t expected[] = { 0xf0, 0x50 };
		error = powervr_usc_integer(POWERVR_USC_INTEGER_ADD_64_FROM_32, 0, 1, 1, 0, 0, 0, 0, 0, &op);
		check_operation(error, &op, expected, 2, "integer add long");
	}

	/* tst: 111 0 op g 101 pwen 1. */
	{
		static const uint8_t expected[] = { 0xeb };
		error = powervr_usc_test(5, 0, 0, 1, 0, 0, &op);
		check_operation(error, &op, expected, 1, "test short");
	}

	/* tst ext le 1001: 111 1 001 0, then type u32 101 p2end 1 elem 10 0 op3 1. */
	{
		static const uint8_t expected[] = { 0xf2, 0xb9 };
		error = powervr_usc_test(9, 5, 2, 0, 1, 1, &op);
		check_operation(error, &op, expected, 2, "test long");
	}

	/* movc: 110 0 movw1 01 movw0 10. */
	{
		static const uint8_t expected[] = { 0xc6 };
		error = powervr_usc_move(1, 2, 0, 0, 0, 0, &op);
		check_operation(error, &op, expected, 1, "move short");
	}

	/* movc ext: 110 1 00 00, then 00 maskw0 1111 aw 1 p2end 0. */
	{
		static const uint8_t expected[] = { 0xd0, 0x3e };
		error = powervr_usc_move(0, 0, 15, 1, 0, 1, &op);
		check_operation(error, &op, expected, 2, "move long");
	}

	/* ld immbl 16: 111 1 drc 1 001, then add s1 001 burst 000 bypass 01, then 0000000 burst3 0. */
	{
		static const uint8_t expected[] = { 0xf9, 0x21, 0x00 };
		error = powervr_usc_load(1, 16, 0, 1, 1, &op);
		check_operation(error, &op, expected, 3, "load burst 16");
	}

	/* ld immbl 9: burst 1001 splits as 001 in the second byte and 1 in the third. */
	{
		static const uint8_t expected[] = { 0xf9, 0x25, 0x01 };
		error = powervr_usc_load(1, 9, 0, 1, 1, &op);
		check_operation(error, &op, expected, 3, "load burst 9");
	}

	/* ld regbl: 111 0 0 001, then add s0 000 bl s2 010 cache 00. */
	{
		static const uint8_t expected[] = { 0xe1, 0x08 };
		error = powervr_usc_load(0, 0, 2, 0, 0, &op);
		check_operation(error, &op, expected, 2, "load register burst");
	}

	/* st immbl 1: 111 1 0 010, then 000 001 01, then tiled 0 data s3 011 dsize 32 10 0 burst3 0. */
	{
		static const uint8_t expected[] = { 0xf2, 0x05, 0x38 };
		error = powervr_usc_store(0, 1, 0, 3, 2, 1, 0, &op);
		check_operation(error, &op, expected, 3, "store burst 1");
	}

	/* st regbl: 111 0 1 010, then add s2 010 bl s1 001 cache 10, then 0 s0 000 dsize 8 00 0 0. */
	{
		static const uint8_t expected[] = { 0xea, 0x46, 0x00 };
		error = powervr_usc_store(2, 0, 1, 0, 0, 2, 1, &op);
		check_operation(error, &op, expected, 3, "store register burst");
	}

	/* idf: 111 0 drc 1 000, then 00000 s4 100. */
	{
		static const uint8_t expected[] = { 0xe8, 0x04 };
		error = powervr_usc_fence(4, 1, &op);
		check_operation(error, &op, expected, 2, "fence");
	}

	/* atomic xor: 111 0 0 101, then 1010 0 s1 001, then 00000 s2 010. */
	{
		static const uint8_t expected[] = { 0xe5, 0xa1, 0x02 };
		error = powervr_usc_atomic(10, 1, 2, 0, &op);
		check_operation(error, &op, expected, 3, "atomic xor");
	}

	/* uvsw endtask: 000 0 0 100. */
	{
		static const uint8_t expected[] = { 0x04 };
		error = powervr_usc_vertex_output(4, 0, 0, 0, 0, &op);
		check_operation(error, &op, expected, 1, "vertex end task");
	}

	/* uvsw write reg: 000 dsel 1 imm 0 000, then 00000 s3 011. */
	{
		static const uint8_t expected[] = { 0x10, 0x03 };
		error = powervr_usc_vertex_output(0, 1, 3, 0, 0, &op);
		check_operation(error, &op, expected, 2, "vertex write register");
	}

	/* uvsw write imm: 000 0 1 000, then address 0x2a. */
	{
		static const uint8_t expected[] = { 0x08, 0x2a };
		error = powervr_usc_vertex_output(0, 0, 0, 1, 0x2a, &op);
		check_operation(error, &op, expected, 2, "vertex write immediate");
	}

	/* phase0 src: 0 csrc 1 cnt 0 ext0 0 shft1 lsl 11 cnt_byp 1 bm 1 (count bypass is 10). */
	{
		static const uint8_t expected[] = { 0x4f };
		error = powervr_usc_bit_count_shift(1, 2, 1, 3, &op);
		check_operation(error, &op, expected, 1, "bitwise phase 0 source");
	}

	/* phase0 imm32: 0 0 0 ext0 1 00 cnt_byp 1 bm 1, then the word low byte first. */
	{
		static const uint8_t expected[] = { 0x13, 0x78, 0x56, 0x34, 0x12 };
		error = powervr_usc_bit_immediate(0x12345678, 1, 0, 2, 0, &op);
		check_operation(error, &op, expected, 5, "bitwise immediate 32");
	}

	/* phase0 imm16: 0 0 0 1 00 1 bm 0, then the half-word low byte first. */
	{
		static const uint8_t expected[] = { 0x12, 0xef, 0xbe };
		error = powervr_usc_bit_immediate(0xbeef, 0, 0, 2, 0, &op);
		check_operation(error, &op, expected, 3, "bitwise immediate 16");
	}

	/* phase1: 0 ph1 1 mskb 0 0 mska 1 and 001. */
	{
		static const uint8_t expected[] = { 0x49 };
		error = powervr_usc_bit_logic(1, 1, 0, &op);
		check_operation(error, &op, expected, 1, "bitwise logic");
	}

	/* phase2: 00 pwen 1 tsrc 1 top 1 shr 001. */
	{
		static const uint8_t expected[] = { 0x39 };
		error = powervr_usc_bit_shift(1, 1, 1, 1, &op);
		check_operation(error, &op, expected, 1, "bitwise shift");
	}

	/*
	 * branch -8: 000 link 1 anyp 10 abs 0 0; the halved offset 0x7ffffffc
	 * puts 1111100 above a zero bit, then 0xff three times.
	 */
	{
		static const uint8_t expected[] = { 0x18, 0xf8, 0xff, 0xff, 0xff };
		error = powervr_usc_branch(-8, 0, 1, 2, &op);
		check_operation(error, &op, expected, 5, "branch back");
	}

	/* branch +64: halved 0x20 sits at bits 7..1 of the second byte. */
	{
		static const uint8_t expected[] = { 0x00, 0x40, 0x00, 0x00, 0x00 };
		error = powervr_usc_branch(64, 0, 0, 0, &op);
		check_operation(error, &op, expected, 5, "branch forward");
	}

	/* cnd: 0 adjust 01 pcnd p0_true 01 st 000. */
	{
		static const uint8_t expected[] = { 0x28 };
		error = powervr_usc_conditional(0, 1, 1, &op);
		check_operation(error, &op, expected, 1, "conditional");
	}

	/* mutex: lock 11 00 id 0101. */
	{
		static const uint8_t expected[] = { 0xc5 };
		error = powervr_usc_mutex(5, 3, &op);
		check_operation(error, &op, expected, 1, "mutex");
	}

	/* sbo 0x85 shared: offset bits 6..0 0000101 tgt 1, then 0000000 offset bit 7 1. */
	{
		static const uint8_t expected[] = { 0x0b, 0x01 };
		error = powervr_usc_shared_base(0x85, 1, &op);
		check_operation(error, &op, expected, 2, "shared base");
	}

	/* The refusals: undefined codes, odd branch offsets, oversized fields. */
	check(powervr_usc_test(10, 0, 0, 0, 0, 1, &op) == EINVAL, "test refuses an undefined comparison");
	check(powervr_usc_test(9, 0, 0, 0, 0, 0, &op) == EINVAL, "short test refuses a four-bit comparison");
	check(powervr_usc_atomic(3, 0, 0, 0, &op) == EINVAL, "atomic refuses code 3");
	check(powervr_usc_branch(3, 0, 0, 0, &op) == EINVAL, "branch refuses an odd offset");
	check(powervr_usc_bit_logic(3, 0, 0, &op) == EINVAL, "logic refuses code 3");
	check(powervr_usc_bit_immediate(0x10000, 0, 0, 2, 0, &op) == EINVAL, "16-bit immediate refuses 17 bits");
	check(powervr_usc_load(0, 17, 0, 0, 0, &op) == EINVAL, "load refuses a burst of 17");
	check(powervr_usc_load(0, 4, 1, 0, 0, &op) == EINVAL, "load refuses two burst lengths");
	check(powervr_usc_store(0, 1, 0, 0, 3, 0, 0, &op) == EINVAL, "store refuses an undefined size");
	check(powervr_usc_float_pair(POWERVR_USC_FLOAT_ADD, 2, 0, 0, 0, 0, &op) == EINVAL, "float add refuses a two-valued flag");
	check(powervr_usc_vertex_output(4, 0, 1, 0, 0, &op) == EINVAL, "vertex signal refuses a source");
}

/* Checks whole groups and programs, their forms, lengths and padding. */
static void
test_groups(void)
{
	struct powervr_usc_group groups[3];
	size_t size;
	int error;

	/*
	 * fadd r0 = r1 + r2 in the main unit's first phase.
	 *
	 * Header: da 3 (header only) length 8 after padding, then ext 1
	 * oporg p0 000 olchk 0 w1p 0 w0p 1 cc 0, then end 0 alutype 00 rsvd 0
	 * atom 0 rpt 00 ccext 0.  Operation 0x00.  Lower sources take the
	 * two-source short form: ext0 1 sbA 1 sA 000001, sel 1 ext1 0 sbB 1
	 * sB 00010.  Upper sources unused take one zero byte; the internal
	 * selectors one zero byte; the one destination ext0 0 db 1 d 000000.
	 * Nine bytes, a word padding byte, then 0xf3 and five 0xff.
	 */
	{
		static const uint8_t expected[] = {
			0x38, 0x82, 0x00, 0x00, 0xc1, 0xa2, 0x00, 0x00,
			0x40, 0xff, 0xf3, 0xff, 0xff, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		groups[0].write_0 = 1;
		powervr_usc_float_pair(POWERVR_USC_FLOAT_ADD, 0, 0, 0, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 1);
		set_register(&groups[0].source[1], POWERVR_USC_BANK_TEMPORARY, 2);
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_TEMPORARY, 0);
		check_program(groups, 1, expected, sizeof(expected), "float add group");

		error = powervr_usc_group_size(&groups[0], &size);
		check(error == 0 && size == 10, "float add group size");
	}

	/*
	 * nop with the end flag in the control header and no operands.
	 *
	 * da 0 (no operands) length 2, then ext 1 opcnt 000 olchk 0 w 00
	 * cc 0, then miscctl 1 alutype 11 ctrlop nop 0111 ccext 0; the
	 * one-byte operation 0x00.  Alone, it is padded to eight bytes:
	 * length 4, then 0xf2 (two half-words of padding) and 0xff.
	 */
	{
		static const uint8_t expected[] = {
			0x04, 0x80, 0xee, 0x00, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_CONTROL);
		groups[0].control = POWERVR_USC_CONTROL_NOTHING;
		groups[0].control_misc = 1;
		powervr_usc_nothing(&groups[0].operation[0]);
		check_program(groups, 1, expected, sizeof(expected), "end nop group");
	}

	/*
	 * wdf: control header ctrlop 0101, no operation bytes, so three
	 * bytes and one word padding byte 0xff, then the alignment padding.
	 */
	{
		static const uint8_t expected[] = {
			0x04, 0x80, 0x6a, 0xff, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_CONTROL);
		groups[0].control = POWERVR_USC_CONTROL_WAIT_DATA;
		powervr_usc_empty(&groups[0].operation[0]);
		check_program(groups, 1, expected, sizeof(expected), "wait data group");
	}

	/*
	 * Three groups: the 10-byte fadd, a 4-byte wdf and a 4-byte nop.  The
	 * nop is padded to end on eight bytes (0xf2 and three 0xff), and the
	 * wdf by two bytes (0xf1 and 0xff) so the nop starts on eight bytes,
	 * each with its own length.
	 */
	{
		static const uint8_t expected[] = {
			0x35, 0x82, 0x00, 0x00, 0xc1, 0xa2, 0x00, 0x00,
			0x40, 0xff, 0x03, 0x80, 0x6a, 0xff, 0xf1, 0xff,
			0x04, 0x80, 0xee, 0x00, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		groups[0].write_0 = 1;
		powervr_usc_float_pair(POWERVR_USC_FLOAT_ADD, 0, 0, 0, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 1);
		set_register(&groups[0].source[1], POWERVR_USC_BANK_TEMPORARY, 2);
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_TEMPORARY, 0);

		group_start(&groups[1], POWERVR_USC_UNIT_CONTROL);
		groups[1].control = POWERVR_USC_CONTROL_WAIT_DATA;
		powervr_usc_empty(&groups[1].operation[0]);

		group_start(&groups[2], POWERVR_USC_UNIT_CONTROL);
		groups[2].control = POWERVR_USC_CONTROL_NOTHING;
		groups[2].control_misc = 1;
		powervr_usc_nothing(&groups[2].operation[0]);

		check_program(groups, 3, expected, sizeof(expected), "three-group program alignment");
	}

	/*
	 * byp (pass) of shared 300 into temporary 200, ending the program.
	 *
	 * The end flag needs the long header: da 3 length 8 (after padding),
	 * ext 1 oporg 000 olchk 0 w1p 0 w0p 1 cc 0, end 1 alutype 00 rsvd 0
	 * atom 0 rpt 00 ccext 0.  The pass 0x87.  The source with the
	 * selector set to s0 needs the 11-bit form: ext0 1 sbA bit 0 0 sA
	 * 101100, sel 0 ext1 0 mux 00 sbA bits 2..1 10 sA bits 7..6 00,
	 * rsvd 00000 sA bits 10..8 001.  Upper 0x00.  Internal is4 fte 11.
	 * The destination needs the 11-bit single form: ext0 1 db 1 d
	 * 001000, rsvd 0 d bits 10..8 000 db bits 2..1 00 d bits 7..6 11.
	 * Eleven bytes, a word padding 0xff, then four bytes of alignment.
	 */
	{
		static const uint8_t expected[] = {
			0x38, 0x82, 0x80, 0x87, 0xac, 0x08, 0x01, 0x00,
			0x30, 0xc8, 0x03, 0xff, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		groups[0].end = 1;
		groups[0].write_0 = 1;
		groups[0].internal[4] = 3;
		groups[0].mux_used = 1;
		groups[0].mux = 0;
		powervr_usc_single(POWERVR_USC_SINGLE_PASS, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_SHARED, 300);
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_TEMPORARY, 200);
		check_program(groups, 1, expected, sizeof(expected), "pass group with end");
	}

	/*
	 * A bitwise group loading 0xdeadbeef into temporary 3.
	 *
	 * Header: da 8 (header and operation) length 8 after padding, ext 1 opcnt p0 001
	 * olchk 0 w1p 0 w0p 1 cc 0, end 0 alutype 10 rsvd 0 atom 0 rpt 00
	 * ccext 0.  The immediate phase, the two unused source bytes, no
	 * internal selectors, the destination 0x43 and a word padding byte.
	 */
	{
		static const uint8_t expected[] = {
			0x88, 0x92, 0x40, 0x13, 0xef, 0xbe, 0xad, 0xde,
			0x00, 0x00, 0x43, 0xff, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_BITWISE);
		groups[0].write_0 = 1;
		powervr_usc_bit_immediate(0xdeadbeef, 1, 0, 2, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_TEMPORARY, 3);
		check_program(groups, 1, expected, sizeof(expected), "bitwise immediate group");
	}

	/*
	 * A main group using the first phase, the second phase and the back
	 * end, three lower sources with the selector on s1, one upper source
	 * and two destinations.
	 *
	 * Header: da 6 (header, second phase, back end) length 9, ext 1
	 * oporg p0_p2_be 110 olchk 1 w1p 1 w0p 1 cc 0, then 0x00.  Operations from the
	 * back end: idf on s0 e0 00, movc 0xc3, fadd 0x00.  The lower sources
	 * need the 8-bit three-source form for the 3-bit selector: ext0 1 sbA
	 * 1 sA 000001; sel 0 ext1 1 sbB 1 sB 00010; ext2 1 mux 00 sbA 0 sbB 0
	 * sA 0 sB 00; sbC 01 sC 000011; sbC 0 sC 00 mux 1 sbA 0 ext4 0 sA 0
	 * sB 0.  The upper source, coefficient 70, needs the 11-bit form:
	 * ext0 1 sbA 1 sA 000110; 0 0 00 sbA 01 sA 01; 00000 000.  Internal
	 * is5 ft1 01 is4 ft2 10 is3 ft1 01 is2 fte 1 is1 fte 1.  Two
	 * destinations in the short dual form: db0 1 d0 0000000; ext1 0 db1 1
	 * d1 000101.  Eighteen bytes, needing no word padding.
	 */
	{
		static const uint8_t expected[] = {
			0x69, 0xee, 0x00, 0xe0, 0x00, 0xc3, 0x00, 0xc1,
			0x62, 0x80, 0x43, 0x10, 0xc6, 0x05, 0x00, 0x67,
			0x80, 0x45
		};
		uint8_t output[64];
		size_t length;

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		groups[0].overlap_check = 1;
		groups[0].write_0 = 1;
		groups[0].write_1 = 1;
		powervr_usc_float_pair(POWERVR_USC_FLOAT_ADD, 0, 0, 0, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		powervr_usc_move(0, 3, 0, 0, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_2]);
		powervr_usc_fence(0, 0, &groups[0].operation[POWERVR_USC_PHASE_BACKEND]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 1);
		set_register(&groups[0].source[1], POWERVR_USC_BANK_TEMPORARY, 2);
		set_register(&groups[0].source[2], POWERVR_USC_BANK_TEMPORARY, 3);
		set_register(&groups[0].source[3], POWERVR_USC_BANK_COEFFICIENT, 70);
		groups[0].mux_used = 1;
		groups[0].mux = 4;
		groups[0].internal[1] = 1;
		groups[0].internal[2] = 1;
		groups[0].internal[3] = 1;
		groups[0].internal[4] = 2;
		groups[0].internal[5] = 1;
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_TEMPORARY, 0);
		set_register(&groups[0].destination[1], POWERVR_USC_BANK_TEMPORARY, 5);

		error = powervr_usc_group_size(&groups[0], &size);
		check(error == 0 && size == sizeof(expected), "three-phase group size");

		/* Two copies: the first is padded so the second starts aligned. */
		groups[1] = groups[0];
		error = powervr_usc_program_encode(groups, 2, output, sizeof(output), &length);
		check(error == 0 && length == 48, "two three-phase groups length");
		check(output[0] == 0x6c && memcmp(output + 1, expected + 1, sizeof(expected) - 1) == 0, "first three-phase group with alignment");
		check(output[18] == 0xf3 && output[19] == 0xff && output[23] == 0xff, "alignment marker of the first group");
		check(output[24] == 0x6c && memcmp(output + 25, expected + 1, sizeof(expected) - 1) == 0, "second three-phase group with end padding");
		check(output[42] == 0xf3 && output[47] == 0xff, "alignment marker of the second group");
	}

	/* A control branch with an if-predicate condition: cc bit 0 in the second byte. */
	{
		static const uint8_t expected[] = {
			0x04, 0x81, 0x60, 0x00, 0x10, 0x00, 0x00, 0x00
		};

		group_start(&groups[0], POWERVR_USC_UNIT_CONTROL);
		groups[0].control = POWERVR_USC_CONTROL_BRANCH;
		groups[0].condition = POWERVR_USC_RUN_IF_PREDICATE;
		powervr_usc_branch(16, 0, 0, 0, &groups[0].operation[0]);
		check_program(groups, 1, expected, sizeof(expected), "conditional branch group");
	}

	/*
	 * The context store and resume burst (design-p010.md 5.4): index
	 * register 0 set from a temporary, then a load and a store of a
	 * register-given burst whose shared registers are named through it.
	 *
	 * Values referenced:
	 * Mesa 26.2.5 src/imagination/pco/pco_trans_nir.c
	 * (dma_ld_shregs_pco, dma_st_shregs_pco: the offset moved into index
	 * register 0, shared register 0 named through it), pco_map.py
	 * (O_LD_REGBL: address s0, burst s1, the shared registers as s3;
	 * O_ST32_REGBL: data s0, burst s2, address s3; F_IDXBANK shared 0b011,
	 * idx 0b101) and pco_map.h.py (pco_map_reg_index: the index bank
	 * and the offset shifted by 3, in the 11-bit form).
	 *
	 * The index register is bank idx0 110, index idx 101 offset 0; the
	 * shared registers through it bank 110, index shared 011 offset 0.
	 * In the 11-bit destination form: ext0 1 db 0 d 000101, then rsvd 0
	 * d 000 db 11 d 00.  In the 11-bit upper source form: ext0 1 sbA 0
	 * sA 000011, then 00 00 sbA 11 sA 00, then 00000 000.
	 */
	{
		static const uint8_t expected[] = {
			0x38, 0x82, 0x00, 0x87, 0xc2, 0x00, 0x00, 0x00,
			0x30, 0x85, 0x0c, 0xff, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		groups[0].write_0 = 1;
		groups[0].internal[4] = 3;
		groups[0].mux_used = 1;
		groups[0].mux = 0;
		powervr_usc_single(POWERVR_USC_SINGLE_PASS, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_0]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 2);
		set_register(&groups[0].destination[0], POWERVR_USC_BANK_INDEXED_0, 5);
		check_program(groups, 1, expected, sizeof(expected), "index register 0 from a temporary");
	}

	/*
	 * ld regbl: header oporg be 010, no writes; the load 111 0 0 001,
	 * then add s0 000 bl s1 001 cache 00; temporaries 4 and 6 in the
	 * short two-source form; the shared registers through index 0 as s3.
	 */
	{
		static const uint8_t expected[] = {
			0x58, 0xa0, 0x00, 0xe1, 0x04, 0xc4, 0xa6, 0x83,
			0x0c, 0x00, 0x00, 0xff, 0xf2, 0xff, 0xff, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		powervr_usc_load(0, 0, 1, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_BACKEND]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 4);
		set_register(&groups[0].source[1], POWERVR_USC_BANK_TEMPORARY, 6);
		set_register(&groups[0].source[3], POWERVR_USC_BANK_INDEXED_0, 3);
		check_program(groups, 1, expected, sizeof(expected), "shared register burst load");
	}

	/*
	 * st regbl: the store 111 0 0 010, then add s3 011 bl s2 010 cache 00,
	 * then tiled 0 data s0 000 dsize 32 10 0 burst3 0; the shared
	 * registers through index 0 as s0, the burst in temporary 6 as s2,
	 * the address pair in temporary 4 as s3.
	 */
	{
		static const uint8_t expected[] = {
			0x68, 0xa0, 0x00, 0xe2, 0x68, 0x08, 0x83, 0x40,
			0x90, 0x46, 0x0c, 0x00, 0x44, 0x00, 0xf1, 0xff
		};

		group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
		powervr_usc_store(3, 0, 2, 0, 2, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_BACKEND]);
		set_register(&groups[0].source[0], POWERVR_USC_BANK_INDEXED_0, 3);
		set_register(&groups[0].source[2], POWERVR_USC_BANK_TEMPORARY, 6);
		set_register(&groups[0].source[3], POWERVR_USC_BANK_TEMPORARY, 4);
		check_program(groups, 1, expected, sizeof(expected), "shared register burst store");
	}

	/* The refusals of whole groups. */
	group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
	powervr_usc_bit_logic(0, 0, 0, &groups[0].operation[POWERVR_USC_PHASE_1]);
	check(powervr_usc_group_size(&groups[0], &size) == EINVAL, "main group refuses phase 1 alone");

	group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
	groups[0].repeat = 0;
	powervr_usc_nothing(&groups[0].operation[POWERVR_USC_PHASE_0]);
	check(powervr_usc_group_size(&groups[0], &size) == EINVAL, "group refuses a zero repeat");

	group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
	powervr_usc_nothing(&groups[0].operation[POWERVR_USC_PHASE_0]);
	set_register(&groups[0].source[0], POWERVR_USC_BANK_TEMPORARY, 2048);
	check(powervr_usc_group_size(&groups[0], &size) == EINVAL, "group refuses a 12-bit index");

	group_start(&groups[0], POWERVR_USC_UNIT_MAIN);
	powervr_usc_nothing(&groups[0].operation[POWERVR_USC_PHASE_0]);
	groups[0].internal[3] = 2;
	check(powervr_usc_group_size(&groups[0], &size) == EINVAL, "group refuses an undefined internal selector");

	group_start(&groups[0], POWERVR_USC_UNIT_BITWISE);
	powervr_usc_fence(0, 0, &groups[0].operation[POWERVR_USC_PHASE_BACKEND]);
	check(powervr_usc_group_size(&groups[0], &size) == EINVAL, "bitwise group refuses a back end");

	group_start(&groups[0], POWERVR_USC_UNIT_CONTROL);
	groups[0].control = POWERVR_USC_CONTROL_NOTHING;
	powervr_usc_nothing(&groups[0].operation[0]);
	check(powervr_usc_program_encode(groups, 1, (uint8_t *)&size, 4, &size) == ENOSPC, "program refuses a small output");
}

/* Checks the data sequencer's instruction and data words. */
static void
test_data_sequencer(void)
{
	uint32_t word;
	uint64_t wide;

	/* add32: 1001 cc 1 alum 0 0 sna 1, src0 130 at 14, src1 5 at 6, dst 3. */
	check(powervr_pds_add32(1, 0, 1, 130, 5, 3, &word) == 0 && word == 0x99208143U, "add32");

	/* add64: 1000, src0 64 at 12, src1 1 at 5, dst 2. */
	check(powervr_pds_add64(0, 0, 0, 64, 1, 2, &word) == 0 && word == 0x80040022U, "add64");

	/* sftlp32: 010 im 1 cc 0 lop or 011, src0 4 at 19, src1 200 at 11, shift -2 as 111110 at 5, dst 7. */
	check(powervr_pds_shift_logic32(0, 3, 4, 200, 1, 62, 7, &word) == 0 && word == 0x532647c7U, "shift logic");

	/* cmpi: 1011 cc 0 cop lt 10 setcp 1 im 1, src0 17 at 18, imm 0x1234 at 2. */
	check(powervr_pds_compare_immediate(0, POWERVR_PDS_LESS, 17, 0x1234, &word) == 0 && word == 0xb5c448d0U, "compare immediate");

	/* cmp: 1011 cc 1 cop ne 11 setcp 1 im 0, src0 2 at 18, src1 96 at 2. */
	check(powervr_pds_compare(1, POWERVR_PDS_NOT_EQUAL, 2, 96, &word) == 0 && word == 0xbf080180U, "compare");

	/* bra: 1100 srcc p0 0000 neg 1 setc keep 1000, address 0x123. */
	check(powervr_pds_branch(0, 1, 8, 0x123, &word) == 0 && word == 0xc0c00123U, "branch");

	/* ld: 1101 cc 0 op 0000 at 23, src 100. */
	check(powervr_pds_load(0, 100, &word) == 0 && word == 0xd0000064U, "load");

	/* st: 1101 cc 1 op 0001, src 5. */
	check(powervr_pds_store(1, 5, &word) == 0 && word == 0xd8800005U, "store");

	/* wdf, halt, nop: op 0010, 0110, 1111. */
	check(powervr_pds_wait_data(0, &word) == 0 && word == 0xd1000000U, "wait data");
	check(powervr_pds_halt(0, &word) == 0 && word == 0xd3000000U, "halt");
	check(powervr_pds_nothing(0, &word) == 0 && word == 0xd7800000U, "nothing");

	/* limm: op 0011, dst 9 at 18, imm 0xabcd at 2, gr 1 at 1. */
	check(powervr_pds_load_immediate(0, 9, 0xabcd, 1, &word) == 0 && word == 0xd1a6af36U, "load immediate");

	/* ddmad: 1110 cc 0 end 1, src0 3 at 18, src1 4 at 13, src2 70 at 6, src3 10. */
	check(powervr_pds_dma_add(0, 1, 3, 4, 70, 10, &word) == 0 && word == 0xe40c918aU, "dma multiply-add");

	/* dout: 1111 cc 0 end 1, src1 1 at 16, src0 64 at 8, dst doutu 010. */
	check(powervr_pds_output(0, 1, POWERVR_PDS_OUTPUT_SHADER_TASK, 64, 1, &word) == 0 && word == 0xf4014002U, "output task");

	/* DOUTU word: address 0x10000, 6 temporaries as 2 blocks at 35. */
	check(powervr_pds_task_word(0x10000, 6, 0, 0, &wide) == 0 && wide == 0x0000001000010000ULL, "task word");

	/* DOUTU word: full rate 10 at 33 and dual phase at 41. */
	check(powervr_pds_task_word(0x10000, 6, 2, 1, &wide) == 0 && wide == 0x0000021400010000ULL, "task word dual phase");

	/* DOUTD source 0: offset 0x1abc at 40 over the 40-bit address. */
	check(powervr_pds_dma_address_word(0x1234567890ULL, 0x1abc, &wide) == 0 && wide == 0x001abc1234567890ULL, "dma address word");

	/* DOUTD source 1: last 1 wordsize four 11 dest common 1 cmode bypass 01 a0 0x100 repeat 0 bsize 0x20. */
	check(powervr_pds_dma_control_word(1, 4, 1, 1, 0x100, 0, 0x20, &word) == 0 && word == 0xf4200020U, "dma control word");

	/* DDMAD source 3: msize 0x100 at 33, test at 32, the control word below. */
	check(powervr_pds_dma_add_control_word(0xf4200020U, 1, 0x100, &wide) == 0 && wide == 0x00000201f4200020ULL, "dma multiply-add control");

	/* The refusals. */
	check(powervr_pds_add32(0, 0, 0, 170, 0, 0, &word) == EINVAL, "add32 refuses a register in the gap");
	check(powervr_pds_add32(0, 0, 0, 0, 0, 32, &word) == EINVAL, "add32 refuses a persistent destination beyond 31");
	check(powervr_pds_add64(0, 0, 0, 80, 0, 0, &word) == EINVAL, "add64 refuses a register in the gap");
	check(powervr_pds_branch(0, 0, 0, 0x80000, &word) == EINVAL, "branch refuses a 20-bit address");
	check(powervr_pds_task_word(0x10002, 0, 0, 0, &wide) == EINVAL, "task word refuses a misaligned address");
	check(powervr_pds_task_word(0x10000, 253, 0, 0, &wide) == EINVAL, "task word refuses 64 blocks");
	check(powervr_pds_dma_control_word(0, 5, 0, 0, 0, 0, 0, &word) == EINVAL, "dma control refuses five-word elements");
	check(powervr_pds_dma_add(0, 0, 0, 0, 0, 64, &word) == EINVAL, "dma multiply-add refuses a temporary control");
}

int
main(void)
{
	test_operations();
	test_groups();
	test_data_sequencer();

	printf("powervr encode host test: %d passed, %d failed\n", passes, failures);
	if (failures != 0)
		return 1;

	return 0;
}
