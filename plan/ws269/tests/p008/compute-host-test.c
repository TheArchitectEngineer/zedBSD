/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the PowerVR driver's own compute programs
 * (compute-program.c) against words and bytes worked out by hand.
 *
 * The control stream words come from Mesa 26.2.5
 * src/imagination/csbgen/rogue/cdm.xml, the data sequencer words from
 * src/imagination/vulkan/pds/pvr_rogue_pds_defs.h and the shading
 * cluster bytes from src/imagination/pco/pco_isa.py; the comment beside
 * each names the fields it holds.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/compute-program.h"

static int failures;
static int passes;
static uint8_t program[0x8000];

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

/* Compares a word with its expected value. */
static void
check_word(
	uint32_t word,
	uint32_t expected,
	const char *what)
{
	if (word == expected) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: %s: 0x%08x, expected 0x%08x\n", what, word, expected);
}

/* Compares bytes of a program with the expected ones. */
static void
check_bytes(
	const uint8_t *bytes,
	const uint8_t *expected,
	unsigned length,
	const char *what)
{
	unsigned i;

	if (memcmp(bytes, expected, length) == 0) {
		passes++;
		return;
	}

	failures++;
	printf("FAIL: %s:", what);
	for (i = 0; i < length; i++)
		printf(" %02x", bytes[i]);
	printf("\n");
}

/*
 * Walks a program by its group headers' lengths (half-words, sixteen
 * written as zero) and records where each group starts.
 */
static size_t
walk(
	const uint8_t *bytes,
	size_t length,
	size_t *starts,
	size_t room)
{
	size_t offset;
	size_t count;
	unsigned halfwords;

	offset = 0;
	count = 0;
	while (offset < length) {
		if (count < room)
			starts[count] = offset;
		halfwords = bytes[offset] & 0x0fU;
		if (halfwords == 0)
			halfwords = 16;
		offset += halfwords * 2U;
		count++;
	}

	if (offset != length)
		return 0;
	return count;
}

static void
test_kernels(void)
{
	struct powervr_compute_kernel kernel;
	uint32_t words[POWERVR_COMPUTE_KERNEL_WORDS];
	int error;

	/*
	 * The store kernel: data 0x10000, code 0x10010, 2 data words, 3 data
	 * sequencer temporaries, 7 unified ones, 32 instances, any cluster.
	 * KERNEL0: usc_unified_size ceil(28/16)=2 <<12, pds_temp_size
	 * ceil(12/16)=1 <<8, pds_data_size ceil(8/16)=1 <<2, usc_target 1 <<1.
	 */
	memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = 0x10000;
	kernel.code_offset = 0x10010;
	kernel.data_words = 2;
	kernel.pds_temporaries = 3;
	kernel.usc_temporaries = 7;
	kernel.instances = 32;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	check(error == 0, "store kernel");
	check_word(words[0], 0x00002106, "store KERNEL0");
	check_word(words[1], 0x00010000, "store KERNEL1 data_addr, sd_type none");
	check_word(words[2], 0x00010010, "store KERNEL2 code_addr");
	check_word(words[3], 0, "store KERNEL3 one workgroup");
	check_word(words[4], 0, "store KERNEL4");
	check_word(words[5], 0, "store KERNEL5");
	/* KERNEL8: max_instances 0 (32) <<27, workgroup_size_x 31 <<18. */
	check_word(words[6], 0x007c0000, "store KERNEL8");

	/*
	 * The fence kernel: data 0x10280, code 0x10290, fence, shared data
	 * from the data sequencer, common shared, 8 instances per task.
	 * KERNEL0: pds_data_size 1 <<2, usc_target 1 <<1, fence 1.
	 * KERNEL1: data_addr | sd_type pds 1 <<2 | usc_common_shared 1 <<1.
	 * KERNEL8: max_instances 8 <<27, sizes 0.
	 */
	memset(&kernel, 0, sizeof(kernel));
	kernel.data_offset = 0x10280;
	kernel.code_offset = 0x10290;
	kernel.data_words = 2;
	kernel.instances = 1;
	kernel.max_instances = 8;
	kernel.fence = 1;
	kernel.shared_from_pds = 1;
	kernel.common_shared = 1;
	kernel.any_cluster = 1;
	error = powervr_compute_kernel_words(&kernel, words);
	check(error == 0, "fence kernel");
	check_word(words[0], 0x00000007, "fence KERNEL0");
	check_word(words[1], 0x00010286, "fence KERNEL1");
	check_word(words[2], 0x00010290, "fence KERNEL2");
	check_word(words[6], 0x40000000, "fence KERNEL8");

	/* STREAM_TERMINATE: block_type 2 <<30. */
	check_word(powervr_compute_terminate_word(), 0x80000000, "terminate");

	/* Refusals: a misaligned data segment, no instances, a task too large. */
	kernel.data_offset = 0x10284;
	check(powervr_compute_kernel_words(&kernel, words) == EINVAL, "misaligned data refused");
	kernel.data_offset = 0x10280;
	kernel.instances = 0;
	check(powervr_compute_kernel_words(&kernel, words) == EINVAL, "no instances refused");
	kernel.instances = 1;
	kernel.max_instances = 32;
	check(powervr_compute_kernel_words(&kernel, words) == EINVAL, "task of 32 written as 32 refused");
}

static void
test_pds(void)
{
	uint32_t data[POWERVR_COMPUTE_PDS_DATA_WORDS];
	uint32_t code[POWERVR_COMPUTE_PDS_CODE_MAX];
	unsigned field;
	unsigned count;
	int error;

	/* The temporaries field in blocks of two and of four, rounded up. */
	check(powervr_compute_temporaries_field(7, 2, &field) == 0 && field == 4, "7 in twos");
	check(powervr_compute_temporaries_field(8, 4, &field) == 0 && field == 2, "8 in fours");
	check(powervr_compute_temporaries_field(8, 2, &field) == 0 && field == 4, "8 in twos");
	check(powervr_compute_temporaries_field(7, 4, &field) == 0 && field == 2, "7 in fours");
	check(powervr_compute_temporaries_field(8, 3, &field) == EINVAL, "unit 3 refused");
	check(powervr_compute_temporaries_field(127, 2, &field) == EINVAL, "64 blocks refused");

	/*
	 * The kick program for code at 0x10000 with the field 4: the data
	 * word is the address with temps 4 <<35 (upper word 4 <<3).
	 * BRA: opcode 0xc <<28, srcc IF1 2 <<24, neg 1 <<23, setc keep 8
	 * <<19, addr 2.  HALT: opcode sp 0xd <<28, op halt 6 <<23.
	 * DOUT: opcode 0xf <<28, end 1 <<26, src1 0, src0 0, dst DOUTU 2.
	 */
	error = powervr_compute_kick_program(0x10000, 4, data, code, &count);
	check(error == 0 && count == 4, "kick program");
	check_word(data[0], 0x00010000, "kick data low: exe offset");
	check_word(data[1], 0x00000020, "kick data high: temps 4");
	check_word(code[0], 0xc2c00002, "kick bra if1 negated to 2");
	check_word(code[1], 0xd3000000, "kick halt");
	check_word(code[2], 0xf4000002, "kick doutu end");
	check_word(code[3], 0xd3000000, "kick final halt");
	check(powervr_compute_kick_program(0x10002, 4, data, code, &count) == EINVAL, "misaligned code refused");
	check(powervr_compute_kick_program(0x10000, 64, data, code, &count) == EINVAL, "wide field refused");

	/*
	 * The fence program: zero data.  ADD64: opcode 8 <<28, src0 0, src1
	 * 0, dst ptemp64 0 = 16.  DOUT: opcode 0xf <<28, end 0, dst DOUTC 5.
	 */
	error = powervr_compute_fence_program(data, code, &count);
	check(error == 0 && count == 5, "fence program");
	check_word(data[0], 0, "fence data low");
	check_word(data[1], 0, "fence data high");
	check_word(code[0], 0xc2c00002, "fence bra");
	check_word(code[1], 0xd3000000, "fence halt");
	check_word(code[2], 0x80000010, "fence add64 into ptemp64 0");
	check_word(code[3], 0xf0000005, "fence doutc");
	check_word(code[4], 0xd3000000, "fence final halt");
}

static void
test_store_program(void)
{
	size_t starts[3200];
	size_t length;
	size_t count;
	int error;

	/*
	 * Group 0, r5 = movs1(sr51), main, the compiler's MACHINE_MOVE_SPECIAL
	 * form (a special register only in s1, s2 or s4): header da 5 | 6
	 * half-words = 0x56; ext | oporg p2 1 <<4 | w0p = 0x92; main, rpt 1 =
	 * 0x00.  Phase 2: the extended move 0xd3, 0x3f (as the p010-compiler
	 * group check).  The lower sources with s1 special 51 and the mux s1
	 * 0x80, 0xd3, 0x81, 0x10 as the encoder lays them out (checked
	 * against the field tables in the p008 encoder test, not by hand
	 * here); the upper sources unused 0x00; is4 fte 3 <<4 = 0x30; the
	 * destination bank temp 1 <<6 | 5 = 0x45.
	 */
	static const uint8_t pass_r5[] = { 0x56, 0x92, 0x00, 0xd3, 0x3f, 0x80, 0xd3, 0x81, 0x10, 0x00, 0x30, 0x45 };

	/*
	 * The store, back end: ext | oporg be 2 <<4 = 0xa0, no write; then
	 * the back end operation: backend_op dma 0b111 <<5 | immbl 1 <<4 |
	 * drc 0 | dma_op st 2 = 0xf2; srcseladd s3 3 <<5 | burstlen 1 <<2 |
	 * cachemode write_through 0 = 0x64; tiled 0 | srcseldata s0 0 |
	 * dsize 32bit 2 <<2 | burstlen_3 0 = 0x08.
	 */
	static const uint8_t store_head[] = { 0xa0, 0x00, 0xf2, 0x64, 0x08 };

	/*
	 * The wait, control: da 0 | 2 half-words = 0x02; ext 0x80; miscctl
	 * drc 0 | alutype control 3 <<5 | ctrlop wdf 5 <<1 = 0x6a; padding.
	 */
	static const uint8_t wait[] = { 0x02, 0x80, 0x6a, 0xff };

	/*
	 * The multiply-add, main: ext | oporg p0 | w0p = 0x82, 0x00; the
	 * short int32_64: main_op 0b111 <<5 | int32_64_op madd32 2 = 0xe2.
	 */
	static const uint8_t multiply_add_head[] = { 0x82, 0x00, 0xe2 };

	/*
	 * The 64-bit add of a 32-bit word that makes the address, main: ext |
	 * oporg p0 | w1p | w0p = 0x86, 0x00; the short int32_64: main_op
	 * 0b111 <<5 | int32_64_op add6432 0 = 0xe0.
	 */
	static const uint8_t add64_head[] = { 0x86, 0x00, 0xe0 };

	/*
	 * The end, control, padded so the program ends on 8 bytes: 4 + 4
	 * bytes = 4 half-words = 0x04; ext 0x80; miscctl end 1 <<7 |
	 * alutype control 3 <<5 | ctrlop nop 7 <<1 = 0xee; the nop byte 0x00;
	 * the alignment marker 0xf0 | 2 half-words, then filler.
	 */
	static const uint8_t end[] = { 0x04, 0x80, 0xee, 0x00, 0xf2, 0xff, 0xff, 0xff };

	error = powervr_compute_store_program(0x8000100000ULL, 512, program, sizeof(program), &length);
	check(error == 0, "store program built");
	if (error != 0)
		return;

	check((length % 8U) == 0, "store program ends on 8 bytes");
	count = walk(program, length, starts, sizeof(starts) / sizeof(starts[0]));
	check(count == 6U + 512U * 6U + 1U, "store program: 6 + 6 x 512 + 1 groups");
	if (count != 6U + 512U * 6U + 1U)
		return;

	check_bytes(program + starts[0], pass_r5, sizeof(pass_r5), "store group 0, r5 = movs1 sr51");
	check_bytes(program + starts[3] + 1, multiply_add_head, sizeof(multiply_add_head), "store group 3, madd32 (the offset)");
	check_bytes(program + starts[7] + 1, add64_head, sizeof(add64_head), "store group 7, add64_32 (base and the instance's offset)");
	check_bytes(program + starts[8] + 1, add64_head, sizeof(add64_head), "store group 8, add64_32 (the step's offset)");
	check_bytes(program + starts[10] + 1, store_head, sizeof(store_head), "store group 10, st");
	check_bytes(program + starts[11], wait, sizeof(wait), "store group 11, wdf");
	check_bytes(program + starts[16] + 1, store_head, sizeof(store_head), "store step 2, st");
	check_bytes(program + starts[10 + 6 * 511] + 1, store_head, sizeof(store_head), "store last step st");
	check((starts[count - 1] % 8U) == 0, "store last group starts on 8 bytes");
	check_bytes(program + starts[count - 1], end, sizeof(end), "store end");

	/* Refusals: a run whose low half wraps, a misaligned one, no steps. */
	check(powervr_compute_store_program(0x80fffff000ULL, 512, program, sizeof(program), &length) == EINVAL, "wrapping run refused");
	check(powervr_compute_store_program(0x8000100002ULL, 512, program, sizeof(program), &length) == EINVAL, "misaligned run refused");
	check(powervr_compute_store_program(0x8000100000ULL, 0, program, sizeof(program), &length) == EINVAL, "no steps refused");
	check(powervr_compute_store_program(0x8000100000ULL, 512, program, 64, &length) == ENOSPC, "small room refused");
}

static void
test_temps_program(void)
{
	size_t starts[16];
	size_t length;
	size_t count;
	int error;

	/* Group 0, r7 = movs1(sr51): as the store's group 0 with destination 0x47. */
	static const uint8_t pass_r7[] = { 0x56, 0x92, 0x00, 0xd3, 0x3f, 0x80, 0xd3, 0x81, 0x10, 0x00, 0x30, 0x47 };

	/* Group 1, r3 = 0xdead0003: immediate low byte first, destination 0x43. */
	static const uint8_t fill_r3[] = { 0x86, 0x92, 0x40, 0x13, 0x03, 0x00, 0xad, 0xde, 0x00, 0x00, 0x43, 0xff };

	/* Group 11, r2 = byp(r7): lower source bank temp 1 <<6 | 7 = 0x47, destination 0x42. */
	static const uint8_t pass_r2[] = { 0x34, 0x82, 0x00, 0x87, 0x47, 0x00, 0x00, 0x42 };

	error = powervr_compute_temps_program(0x80001f0000ULL, program, sizeof(program), &length);
	check(error == 0, "temps program built");
	if (error != 0)
		return;

	check((length % 8U) == 0, "temps program ends on 8 bytes");
	count = walk(program, length, starts, sizeof(starts) / sizeof(starts[0]));
	check(count == 15, "temps program: 15 groups");
	if (count != 15)
		return;

	check_bytes(program + starts[0], pass_r7, sizeof(pass_r7), "temps group 0, r7 = movs1 sr51");
	check_bytes(program + starts[1], fill_r3, sizeof(fill_r3), "temps group 1, r3 fill");
	check(program[starts[10] + 1] == 0x86 && program[starts[10] + 3] == 0xe0, "temps group 10, add64_32");
	check_bytes(program + starts[11], pass_r2, sizeof(pass_r2), "temps group 11, r2 = r7");
	check(program[starts[12] + 1] == 0xa0 && program[starts[12] + 3] == 0xf2, "temps group 12, st");
	check(program[starts[13] + 2] == 0x6a, "temps group 13, wdf");
	check(program[starts[14] + 2] == 0xee, "temps group 14, nop end");
}

static void
test_command(void)
{
	uint32_t words[POWERVR_COMPUTE_COMMAND_WORDS];
	unsigned i;
	int zero;

	/*
	 * rogue_fwif_cmd_compute: the control stream base at byte 40 (word
	 * 10) and the context state base at 48 (word 12), 64 bits each.
	 */
	powervr_compute_command(0x8000401000ULL, 0x8000400000ULL, words);
	check_word(words[10], 0x00401000, "command stream low");
	check_word(words[11], 0x00000080, "command stream high");
	check_word(words[12], 0x00400000, "command resume low");
	check_word(words[13], 0x00000080, "command resume high");
	zero = 1;
	for (i = 0; i < POWERVR_COMPUTE_COMMAND_WORDS; i++) {
		if (i >= 10 && i <= 13)
			continue;
		if (words[i] != 0)
			zero = 0;
	}
	check(zero, "command other words zero");
}

int
main(void)
{
	test_kernels();
	test_pds();
	test_store_program();
	test_temps_program();
	test_command();

	printf("powervr compute host test: %d passed, %d failed\n", passes, failures);
	return failures != 0;
}
