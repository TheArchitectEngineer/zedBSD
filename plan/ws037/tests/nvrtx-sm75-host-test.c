/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: the host test of the nvrtx SM75 shader compiler.
 *
 *   1. Known instructions: words the CUDA compiler writes in every SM70+
 *      kernel (the prologue's MOV R1, c[0x0][0x28], S2R, IMAD, EXIT, the
 *      closing BRA and NOP) are built with the encoder and compared bit for
 *      bit.  They check the field positions and the ALU forms.
 *   2. Fields: an instruction of each kind the compiler emits has its
 *      opcode and modifiers where NAK (Mesa, MIT) puts them.
 *   3. Execution: compute modules (SPIR-V made by glslc) are compiled and
 *      run on a small interpreter of the instructions stage A emits, one
 *      invocation after another, against buffers the test fills; the
 *      results are compared with what C computes from the same inputs.
 *      The interpreter decodes the same field positions the encoder uses,
 *      so this part checks the lowering (instruction choice, registers,
 *      addresses, predicates, the serial dependences), not the encoding.
 *   4. Refusals: a stage A does not cover is refused with a reason.
 *   5. The first compute launch's parts (ws037-p008 stage C): the QMD 2.2
 *      fields at the positions of NVIDIA's clc3c0qmd.h, the launch's push
 *      buffer, the IR of the first program compiled and run, and the
 *      system-memory page entries of the launch's tables.
 *
 *   nvrtx-sm75-host-test DIR    DIR holds const.spv, add.spv, ids.spv, float.spv, integer.spv and loop.spv
 *
 * Nothing here is evidence about the GPU.
 */

#include "drivers/gpu/nvrtx/compiler/sm75-compile.h"
#include "drivers/gpu/nvrtx/compiler/sm75-encode.h"
#include "drivers/gpu/nvrtx/compiler/sm75-isa.h"
#include "drivers/gpu/nvrtx/nvrtx-compute.h"
#include "drivers/gpu/nvrtx/nvrtx-gsp.h"

#include <uapi/errno.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most buffers a run binds, and where the interpreter places them in the GPU's address space. */
#define TEST_BUFFERS		4U
#define TEST_BUFFER_BASE	0x100000000ULL
#define TEST_BUFFER_STRIDE	0x01000000ULL

/* The most instructions one invocation may run. */
#define TEST_STEPS		100000U

/*
 * One storage buffer of a run, at (set, binding): its words and size.
 */
struct test_buffer {
	uint32_t set;
	uint32_t binding;
	uint32_t *words;
	uint32_t bytes;
};

/*
 * One dispatch on the interpreter: the program, the constant buffers, the
 * storage buffers, and the grid.
 */
struct test_run {
	const struct nvrtx_sm75_binary *binary;
	uint8_t cb[8][0x10000];
	struct test_buffer buffers[TEST_BUFFERS];
	uint32_t buffer_count;
	uint32_t groups[3];
	int failed;
};

/* Checks counted and failed. */
static unsigned test_checks;
static unsigned test_failures;

static void check(int passed, const char *what);
static void check_known_words(void);
static void check_fields(void);
static void check_const(const char *directory);
static void check_add(const char *directory);
static void check_ids(const char *directory);
static void check_float(const char *directory);
static void check_integer(const char *directory);
static void check_refusals(const char *directory);
static void check_qmd(void);
static void check_launch_push(void);
static void check_first_program(void);
static void check_system_pages(void);
static uint64_t field(const uint32_t *words, unsigned low, unsigned bits);
static int words_are(const struct nvrtx_sm75_word *word, uint64_t low, uint64_t high);
static struct nvrtx_sm75_binary *compile_file(const char *directory, const char *name);
static void bind(struct test_run *run, uint32_t set, uint32_t binding, uint32_t *words, uint32_t bytes);
static void dispatch(struct test_run *run);
static void invoke(struct test_run *run, const uint32_t local[3], const uint32_t group[3]);
static uint32_t *memory(struct test_run *run, uint64_t address);
static uint32_t cb_word(const struct test_run *run, uint32_t index, uint32_t offset);
static float as_float(uint32_t bits);
static uint32_t as_bits(float value);
static int near(float a, float b);

/* The kernel's allocator, on the host. */
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

/*
 * Runs every part and reports the tally.
 */
int
main(
	int argc,
	char **argv)
{
	/* The directory of the modules. */
	if (argc != 2) {
		fprintf(stderr, "usage: %s DIR\n", argv[0]);
		return 2;
	}

	/* Each part. */
	check_known_words();
	check_fields();
	check_const(argv[1]);
	check_add(argv[1]);
	check_ids(argv[1]);
	check_float(argv[1]);
	check_integer(argv[1]);
	check_refusals(argv[1]);
	check_qmd();
	check_launch_push();
	check_first_program();
	check_system_pages();

	/* The tally. */
	printf("nvrtx_sm75_host_test: %u checks, %u failures\n", test_checks, test_failures);
	if (test_failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts a check, and names a failed one. */
static void
check(
	int passed,
	const char *what)
{
	test_checks++;

	/* A failure is named. */
	if (!passed) {
		test_failures++;
		printf("FAIL: %s\n", what);
	}
}

/*
 * Builds the words the CUDA compiler writes in every SM70+ kernel and
 * compares them bit for bit.
 */
static void
check_known_words(void)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src c;

	/* MOV R1, c[0x0][0x28] (the stack pointer of the prologue), delay 2. */
	nvrtx_sm75_begin(&word);
	b = nvrtx_sm75_cb(0U, 0x28U);
	nvrtx_sm75_mov(&word, 1U, &b);
	nvrtx_sm75_control(&word, 2U, 0, NVRTX_SM75_BAR_NONE, NVRTX_SM75_BAR_NONE, 0U);
	check(words_are(&word, 0x00000a0000017a02ULL, 0x000fc40000000f00ULL), "MOV R1, c[0x0][0x28]");

	/* S2R R2, SR_TID.X, writing scoreboard 0, delay 1, yield. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_s2r(&word, 2U, NVRTX_SM75_SR_TID_X);
	nvrtx_sm75_control(&word, 1U, 1, 0U, NVRTX_SM75_BAR_NONE, 0U);
	check(words_are(&word, 0x0000000000027919ULL, 0x000e220000002100ULL), "S2R R2, SR_TID.X");

	/* IMAD R0, R3, c[0x0][0x0], R0, waiting for scoreboard 0, delay 5. */
	nvrtx_sm75_begin(&word);
	a = nvrtx_sm75_reg(3U);
	b = nvrtx_sm75_cb(0U, 0U);
	c = nvrtx_sm75_reg(0U);
	nvrtx_sm75_imad(&word, 0U, &a, &b, &c, 1);
	nvrtx_sm75_control(&word, 5U, 0, NVRTX_SM75_BAR_NONE, NVRTX_SM75_BAR_NONE, 1U);
	check(words_are(&word, 0x0000000003007a24ULL, 0x001fca00078e0200ULL), "IMAD R0, R3, c[0x0][0x0], R0");

	/* EXIT, delay 5, yield. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_exit(&word);
	nvrtx_sm75_control(&word, 5U, 1, NVRTX_SM75_BAR_NONE, NVRTX_SM75_BAR_NONE, 0U);
	check(words_are(&word, 0x000000000000794dULL, 0x000fea0003800000ULL), "EXIT");

	/* BRA to itself. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bra(&word, -1);
	check(words_are(&word, 0xfffffff000007947ULL, 0x000fc0000383ffffULL), "BRA to itself");

	/* NOP. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_nop(&word);
	check(words_are(&word, 0x0000000000007918ULL, 0x000fc00000000000ULL), "NOP");
}

/* Checks the opcode and the modifiers of each kind of instruction the compiler emits. */
static void
check_fields(void)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* FADD R4, R5, -R6: form 1, b at 32 with neg at 63. */
	nvrtx_sm75_begin(&word);
	a = nvrtx_sm75_reg(5U);
	b = nvrtx_sm75_reg(6U);
	b.neg = 1;
	nvrtx_sm75_fadd(&word, 4U, &a, &b);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x021U && nvrtx_sm75_get(&word, 9U, 3U) == 1U, "FADD opcode and form");
	check(nvrtx_sm75_get(&word, 16U, 8U) == 4U && nvrtx_sm75_get(&word, 24U, 8U) == 5U && nvrtx_sm75_get(&word, 32U, 8U) == 6U, "FADD registers");
	check(nvrtx_sm75_get(&word, 63U, 1U) == 1U && nvrtx_sm75_get(&word, 72U, 1U) == 0U, "FADD negation of b");

	/* FADD R4, R5, 1.0: the immediate is the third operand (form 2), RZ the second. */
	nvrtx_sm75_begin(&word);
	b = nvrtx_sm75_imm(0x3f800000U);
	nvrtx_sm75_fadd(&word, 4U, &a, &b);
	check(nvrtx_sm75_get(&word, 9U, 3U) == 2U && nvrtx_sm75_get(&word, 32U, 32U) == 0x3f800000U && nvrtx_sm75_get(&word, 64U, 8U) == 255U, "FADD immediate in form 2");

	/* ISETP.GE.AND P0, PT, R1, R2, PT, signed. */
	nvrtx_sm75_begin(&word);
	a = nvrtx_sm75_reg(1U);
	b = nvrtx_sm75_reg(2U);
	nvrtx_sm75_isetp(&word, 0U, NVRTX_SM75_ICMP_GE, 1, &a, &b);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x00cU && nvrtx_sm75_get(&word, 76U, 3U) == 6U && nvrtx_sm75_get(&word, 73U, 1U) == 1U, "ISETP compare and sign");
	check(nvrtx_sm75_get(&word, 81U, 3U) == 0U && nvrtx_sm75_get(&word, 84U, 3U) == 7U && nvrtx_sm75_get(&word, 87U, 3U) == 7U && nvrtx_sm75_get(&word, 68U, 3U) == 7U, "ISETP predicates");

	/* LOP3.LUT R1, R2, 0x80000000, RZ, 0x3c. */
	nvrtx_sm75_begin(&word);
	a = nvrtx_sm75_reg(2U);
	b = nvrtx_sm75_imm(0x80000000U);
	nvrtx_sm75_lop3(&word, 1U, &a, &b, &zero, 0x3cU);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x012U && nvrtx_sm75_get(&word, 9U, 3U) == 4U && nvrtx_sm75_get(&word, 72U, 8U) == 0x3cU, "LOP3 opcode, form and table");
	check(nvrtx_sm75_get(&word, 64U, 8U) == 255U && nvrtx_sm75_get(&word, 87U, 4U) == 0xfU, "LOP3 third operand and predicate input");

	/* LDC.64 R2, c[0x0][0x210]. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ldc(&word, 2U, 0U, 0x210U, 1);
	check(nvrtx_sm75_get(&word, 0U, 12U) == 0xb82U && nvrtx_sm75_get(&word, 38U, 16U) == 0x210U && nvrtx_sm75_get(&word, 73U, 3U) == 5U, "LDC.64 opcode, offset and size");
	check(nvrtx_sm75_get(&word, 24U, 8U) == 255U && nvrtx_sm75_get(&word, 54U, 5U) == 0U, "LDC index and buffer");

	/* STG.E.STRONG.GPU [R4.64], R6. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_stg(&word, 4U, 6U);
	check(nvrtx_sm75_get(&word, 0U, 12U) == 0x386U && nvrtx_sm75_get(&word, 24U, 8U) == 4U && nvrtx_sm75_get(&word, 32U, 8U) == 6U, "STG opcode and registers");
	check(nvrtx_sm75_get(&word, 72U, 1U) == 1U && nvrtx_sm75_get(&word, 73U, 3U) == 4U && nvrtx_sm75_get(&word, 77U, 4U) == 0xaU && nvrtx_sm75_get(&word, 84U, 3U) == 1U, "STG access bits");

	/* IMAD.WIDE.U32 R4, R1, 0x1, R2. */
	nvrtx_sm75_begin(&word);
	a = nvrtx_sm75_reg(1U);
	b = nvrtx_sm75_imm(1U);
	zero = nvrtx_sm75_reg(2U);
	nvrtx_sm75_imad_wide(&word, 4U, &a, &b, &zero);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x025U && nvrtx_sm75_get(&word, 9U, 3U) == 4U && nvrtx_sm75_get(&word, 64U, 8U) == 2U && nvrtx_sm75_get(&word, 73U, 1U) == 0U, "IMAD.WIDE.U32 form and pair");

	/* SHF.R.U32.HI R1, RZ, R2, R3. */
	nvrtx_sm75_begin(&word);
	b = nvrtx_sm75_reg(2U);
	nvrtx_sm75_shift(&word, 1U, 3U, &b, 1, 0);
	check(nvrtx_sm75_get(&word, 24U, 8U) == 255U && nvrtx_sm75_get(&word, 64U, 8U) == 3U && nvrtx_sm75_get(&word, 73U, 2U) == 3U, "SHF right operands and type");
	check(nvrtx_sm75_get(&word, 75U, 1U) == 1U && nvrtx_sm75_get(&word, 76U, 1U) == 1U && nvrtx_sm75_get(&word, 80U, 1U) == 1U, "SHF wrap, right, high");

	/* MUFU.RSQ R1, R2; F2I.TRUNC.NTZ R1, R2; I2F.U32 R1, R2. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mufu(&word, 1U, NVRTX_SM75_MUFU_RSQ, 2U);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x108U && nvrtx_sm75_get(&word, 32U, 8U) == 2U && nvrtx_sm75_get(&word, 74U, 6U) == 5U, "MUFU.RSQ");
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_f2i(&word, 1U, 2U, 1);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x105U && nvrtx_sm75_get(&word, 72U, 1U) == 1U && nvrtx_sm75_get(&word, 78U, 2U) == 3U && nvrtx_sm75_get(&word, 75U, 2U) == 2U, "F2I signed truncating");
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_i2f(&word, 1U, 2U, 0);
	check(nvrtx_sm75_get(&word, 0U, 9U) == 0x106U && nvrtx_sm75_get(&word, 74U, 1U) == 0U && nvrtx_sm75_get(&word, 84U, 2U) == 2U, "I2F unsigned");
}

/* The first target: a constant written at every invocation's index. */
static void
check_const(
	const char *directory)
{
	static struct test_run run;
	uint32_t out[96];
	uint32_t index;
	int same;

	memset(&run, 0, sizeof(run));
	run.binary = compile_file(directory, "const");
	check(run.binary != NULL, "const compiles");
	if (run.binary == NULL)
		return;

	/* Three groups of 32 over a buffer of 96 words. */
	memset(out, 0, sizeof(out));
	bind(&run, 0U, 0U, out, sizeof(out));
	run.groups[0] = 3U;
	run.groups[1] = 1U;
	run.groups[2] = 1U;
	dispatch(&run);

	/* Every word is 42. */
	same = 1;
	for (index = 0U; index < 96U; index++) {
		if (out[index] != 42U)
			same = 0;
	}
	check(!run.failed && same, "const writes 42 everywhere");
	check(run.binary->gpr_count >= NVRTX_SM75_GPR_MINIMUM && (run.binary->code_words % 32U) == 0U, "const register count and padding");
	check(run.binary->storage_count == 1U && run.binary->storage[0].binding == 0U, "const storage table");

	nvrtx_sm75_binary_free((struct nvrtx_sm75_binary *)run.binary);
}

/* c[i] = a[i] + b[i] for i < n, the rest untouched (ws101's add.comp). */
static void
check_add(
	const char *directory)
{
	static struct test_run run;
	uint32_t a[128];
	uint32_t b[128];
	uint32_t c[128];
	uint32_t n;
	uint32_t index;
	int same;

	memset(&run, 0, sizeof(run));
	run.binary = compile_file(directory, "add");
	check(run.binary != NULL, "add compiles");
	if (run.binary == NULL)
		return;

	/* The inputs, the output's sentinel, and n = 100 in the push constants. */
	for (index = 0U; index < 128U; index++) {
		a[index] = index * 7U + 3U;
		b[index] = 0xfffffff0U + index;
		c[index] = 0xdeadbeefU;
	}
	n = 100U;
	memcpy(&run.cb[0][NVRTX_SM75_CB_PUSH], &n, sizeof(n));
	bind(&run, 0U, 0U, a, sizeof(a));
	bind(&run, 0U, 1U, b, sizeof(b));
	bind(&run, 0U, 2U, c, sizeof(c));
	run.groups[0] = 2U;
	run.groups[1] = 1U;
	run.groups[2] = 1U;
	dispatch(&run);

	/* The sums below n, the sentinel above. */
	same = 1;
	for (index = 0U; index < 128U; index++) {
		if (index < n && c[index] != a[index] + b[index])
			same = 0;
		if (index >= n && c[index] != 0xdeadbeefU)
			same = 0;
	}
	check(!run.failed && same, "add: c = a + b below n, untouched above");
	check(run.binary->push_bytes == 4U, "add reads four push bytes");

	nvrtx_sm75_binary_free((struct nvrtx_sm75_binary *)run.binary);
}

/* Every invocation's built-ins (ws101's ids.comp), with the group counts in the system buffer. */
static void
check_ids(
	const char *directory)
{
	static struct test_run run;
	static uint32_t out[13U * 4U * 2U * 3U * 2U * 2U * 1U];
	uint32_t counts[3];
	uint32_t expect[13];
	uint32_t g[3];
	uint32_t l[3];
	uint32_t size[3];
	uint32_t linear;
	uint32_t word;
	int same;

	memset(&run, 0, sizeof(run));
	run.binary = compile_file(directory, "ids");
	check(run.binary != NULL, "ids compiles");
	if (run.binary == NULL)
		return;

	/* A grid of 2 x 2 x 1 groups of 4 x 2 x 3. */
	counts[0] = 2U;
	counts[1] = 2U;
	counts[2] = 1U;
	memset(out, 0xff, sizeof(out));
	bind(&run, 0U, 0U, out, sizeof(out));
	bind(&run, DRV_GPU_IR_SYSTEM_SET, 0U, counts, sizeof(counts));
	run.groups[0] = 2U;
	run.groups[1] = 2U;
	run.groups[2] = 1U;
	dispatch(&run);

	/* Each invocation's thirteen words. */
	same = 1;
	size[0] = 8U;
	size[1] = 4U;
	size[2] = 3U;
	for (g[2] = 0U; g[2] < size[2]; g[2]++) {
		for (g[1] = 0U; g[1] < size[1]; g[1]++) {
			for (g[0] = 0U; g[0] < size[0]; g[0]++) {
				l[0] = g[0] % 4U;
				l[1] = g[1] % 2U;
				l[2] = g[2] % 3U;
				linear = g[0] + size[0] * (g[1] + size[1] * g[2]);
				expect[0] = l[0];
				expect[1] = l[1];
				expect[2] = l[2];
				expect[3] = l[0] + 4U * (l[1] + 2U * l[2]);
				expect[4] = g[0] / 4U;
				expect[5] = g[1] / 2U;
				expect[6] = g[2] / 3U;
				expect[7] = counts[0];
				expect[8] = counts[1];
				expect[9] = counts[2];
				expect[10] = g[0];
				expect[11] = g[1];
				expect[12] = g[2];
				for (word = 0U; word < 13U; word++) {
					if (out[linear * 13U + word] != expect[word])
						same = 0;
				}
			}
		}
	}
	check(!run.failed && same, "ids: every built-in of every invocation");

	nvrtx_sm75_binary_free((struct nvrtx_sm75_binary *)run.binary);
}

/* Float arithmetic, conversions, roundings, special functions and a selection. */
static void
check_float(
	const char *directory)
{
	static struct test_run run;
	float x[48];
	float o[48 * 3];
	float scale;
	float v;
	float r;
	uint32_t count;
	uint32_t index;
	int same;

	memset(&run, 0, sizeof(run));
	run.binary = compile_file(directory, "float");
	check(run.binary != NULL, "float compiles");
	if (run.binary == NULL)
		return;

	/* The inputs, the scale and the count. */
	for (index = 0U; index < 48U; index++)
		x[index] = (float)index * 0.37f - 4.0f;
	for (index = 0U; index < 48U * 3U; index++)
		o[index] = -12345.0f;
	scale = 0.75f;
	count = 40U;
	memcpy(&run.cb[0][NVRTX_SM75_CB_PUSH], &scale, sizeof(scale));
	memcpy(&run.cb[0][NVRTX_SM75_CB_PUSH + 4U], &count, sizeof(count));
	bind(&run, 0U, 0U, (uint32_t *)x, sizeof(x));
	bind(&run, 0U, 1U, (uint32_t *)o, sizeof(o));
	run.groups[0] = 3U;
	run.groups[1] = 1U;
	run.groups[2] = 1U;
	dispatch(&run);

	/* The same arithmetic in C, within a small tolerance. */
	same = 1;
	for (index = 0U; index < 48U; index++) {
		if (index >= count) {
			if (o[index * 3U] != -12345.0f)
				same = 0;
			continue;
		}
		v = x[index] * scale;
		r = (v - floorf(v)) + floorf(v) * 2.0f - fabsf(v - 1.5f);
		r = r + sqrtf(fabsf(v)) - exp2f(v * 0.25f) + fminf(v, 2.0f) * fmaxf(v, -1.0f);
		if (!(v < 0.5f))
			r = -r;
		if (!near(o[index * 3U], r))
			same = 0;
		if (!near(o[index * 3U + 1U], (float)(int)(v * 3.0f) + (float)index))
			same = 0;
		if (!near(o[index * 3U + 2U], 1.0f / (fabsf(v) + 1.0f)))
			same = 0;
	}
	check(!run.failed && same, "float: arithmetic, roundings, special functions, selection");

	nvrtx_sm75_binary_free((struct nvrtx_sm75_binary *)run.binary);
}

/* Integer arithmetic, shifts, bitwise operations and comparisons. */
static void
check_integer(
	const char *directory)
{
	static struct test_run run;
	uint32_t o[64 * 4];
	uint32_t k;
	int32_t s;
	uint32_t i;
	uint32_t a;
	int32_t b;
	uint32_t c;
	uint32_t d;
	int same;

	memset(&run, 0, sizeof(run));
	run.binary = compile_file(directory, "integer");
	check(run.binary != NULL, "integer compiles");
	if (run.binary == NULL)
		return;

	/* The push constants. */
	k = 0x12345678U;
	s = -1000;
	memcpy(&run.cb[0][NVRTX_SM75_CB_PUSH], &k, sizeof(k));
	memcpy(&run.cb[0][NVRTX_SM75_CB_PUSH + 4U], &s, sizeof(s));
	memset(o, 0, sizeof(o));
	bind(&run, 0U, 0U, o, sizeof(o));
	run.groups[0] = 4U;
	run.groups[1] = 1U;
	run.groups[2] = 1U;
	dispatch(&run);

	/* The same in C. */
	same = 1;
	for (i = 0U; i < 64U; i++) {
		a = i * 2654435761U + k;
		b = (int32_t)a >> 3;
		c = ((a << 5) ^ (a >> 7)) | (a & 0xff00U);
		d = ~c;
		if (b < s)
			d = c;
		if (o[i * 4U] != a - c)
			same = 0;
		if (o[i * 4U + 1U] != (uint32_t)b + (uint32_t)(-s))
			same = 0;
		if (o[i * 4U + 2U] != d)
			same = 0;
		if (o[i * 4U + 3U] != (uint32_t)((a >= c) ? 1U : 0U) + (uint32_t)(((int32_t)a < b) ? 2U : 0U) + (uint32_t)((a == i) ? 4U : 0U))
			same = 0;
	}
	check(!run.failed && same, "integer: arithmetic, shifts, bitwise, comparisons");

	nvrtx_sm75_binary_free((struct nvrtx_sm75_binary *)run.binary);
}

/* A loop is refused with a reason (stage A). */
static void
check_refusals(
	const char *directory)
{
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic diagnostic;
	char path[512];
	uint32_t *words;
	long bytes;
	FILE *file;
	int error;

	/* The loop module's words. */
	snprintf(path, sizeof(path), "%s/loop.spv", directory);
	file = fopen(path, "rb");
	check(file != NULL, "loop.spv opens");
	if (file == NULL)
		return;
	fseek(file, 0, SEEK_END);
	bytes = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)bytes);
	if (words == NULL || fread(words, 1U, (size_t)bytes, file) != (size_t)bytes) {
		fclose(file);
		free(words);
		check(0, "loop.spv reads");
		return;
	}
	fclose(file);

	/* Refused, with a reason, and no program. */
	binary = (struct nvrtx_sm75_binary *)1;
	error = nvrtx_sm75_compile(words, (size_t)bytes / 4U, &binary, &diagnostic);
	check(error == ENOTSUP && binary == NULL && diagnostic.reason != NULL, "a loop is refused with a reason");
	if (diagnostic.reason != NULL)
		printf("  loop refused: %s (instruction %u)\n", diagnostic.reason, diagnostic.instruction);
	free(words);
}

/* Checks the QMD 2.2 fields against the positions of clc3c0qmd.h, written out here on their own. */
static void
check_qmd(void)
{
	struct nvrtx_qmd_info info;
	uint32_t qmd[NVRTX_QMD_WORDS];
	int error;

	memset(&info, 0, sizeof(info));
	info.program = 0x28006000ULL;
	info.groups[0] = 3U;
	info.groups[1] = 2U;
	info.groups[2] = 1U;
	info.local_size[0] = 32U;
	info.local_size[1] = 1U;
	info.local_size[2] = 1U;
	info.gpr_count = 8U;
	info.cbuf_address[0] = 0x128008000ULL;
	info.cbuf_size[0] = 0x300U;
	error = nvrtx_qmd_build(qmd, &info);
	check(error == 0, "QMD builds");

	/* Version 2.2, global caching, no call limit. */
	check(field(qmd, 576U, 4U) == 2U && field(qmd, 580U, 4U) == 2U, "QMD version 2.2");
	check(field(qmd, 134U, 1U) == 1U && field(qmd, 378U, 1U) == 1U && field(qmd, 382U, 1U) == 0U, "QMD caching, call limit, sampler index");

	/* The grid, the workgroup, the registers. */
	check(field(qmd, 384U, 32U) == 3U && field(qmd, 416U, 16U) == 2U && field(qmd, 448U, 16U) == 1U, "QMD grid");
	check(field(qmd, 592U, 16U) == 32U && field(qmd, 608U, 16U) == 1U && field(qmd, 624U, 16U) == 1U, "QMD workgroup");
	check(field(qmd, 648U, 9U) == 8U && field(qmd, 955U, 5U) == 0U, "QMD registers and barriers");

	/* No shared memory: 32 KiB carve-outs for one and for many, 64 KiB at most (codes 9, 9, 17). */
	check(field(qmd, 544U, 18U) == 0U && field(qmd, 562U, 7U) == 9U && field(qmd, 657U, 7U) == 9U && field(qmd, 569U, 7U) == 17U, "QMD shared memory carve-outs");

	/* Constant buffer 0 and the program. */
	check(field(qmd, 640U, 1U) == 1U && field(qmd, 641U, 7U) == 0U, "QMD constant buffer 0 valid alone");
	check(field(qmd, 1024U, 32U) == 0x28008000U && field(qmd, 1056U, 17U) == 1U && field(qmd, 1075U, 13U) == 0x30U, "QMD constant buffer 0 address and size");
	check(field(qmd, 1536U, 32U) == 0x28006000U && field(qmd, 1568U, 17U) == 0U, "QMD program address");

	/* A misaligned program is refused. */
	info.program = 0x28006010ULL;
	error = nvrtx_qmd_build(qmd, &info);
	check(error == EINVAL, "QMD refuses a misaligned program");
}

/* Checks the launch's push buffer word by word (incrementing headers on subchannel 1). */
static void
check_launch_push(void)
{
	static const uint32_t expect[] = {
		0x20012000U, 0x0000c5c0U,
		0x200220a8U, 0x00000000U, 0xfe000000U,
		0x200221ecU, 0x00000000U, 0xff000000U,
		0x20012087U, 0x00001011U,
		0x200120adU, 0x00280070U,
		0x200120afU, 0x00000003U
	};
	uint8_t push[256];
	uint32_t word;
	size_t length;
	size_t index;
	int same;

	length = nvrtx_push_compute_launch(push, 0x28007000ULL);
	check(length == sizeof(expect), "launch push length");

	same = 1;
	for (index = 0U; index < sizeof(expect) / 4U && index * 4U < length; index++) {
		memcpy(&word, push + index * 4U, 4U);
		if (word != expect[index]) {
			printf("  push word %u is 0x%08x, want 0x%08x\n", (unsigned)index, word, expect[index]);
			same = 0;
		}
	}
	check(same, "launch push words");
}

/* Compiles the first program from its IR and runs it: 32 words of 42. */
static void
check_first_program(void)
{
	static struct test_run run;
	static struct nvrtx_compute_first first;
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic diagnostic;
	uint32_t out[40];
	uint32_t index;
	int same;
	int error;

	nvrtx_compute_first_ir(&first);
	binary = NULL;
	error = nvrtx_sm75_compile_ir(&first.ir, &binary, &diagnostic);
	check(error == 0 && binary != NULL, "the first program compiles from its IR");
	if (binary == NULL)
		return;
	printf("  first: %u instructions, %u registers\n", binary->code_words / 4U, binary->gpr_count);

	/* One group of 32 over 40 words. */
	memset(&run, 0, sizeof(run));
	run.binary = binary;
	for (index = 0U; index < 40U; index++)
		out[index] = 0xdeadbeefU;
	bind(&run, 0U, 0U, out, 32U * 4U);
	run.groups[0] = 1U;
	run.groups[1] = 1U;
	run.groups[2] = 1U;
	dispatch(&run);

	same = 1;
	for (index = 0U; index < 32U; index++) {
		if (out[index] != NVRTX_COMPUTE_FIRST_VALUE)
			same = 0;
	}
	if (out[32] != 0xdeadbeefU)
		same = 0;
	check(!run.failed && same, "the first program writes 42 at each of 32 words");

	nvrtx_sm75_binary_free(binary);
}

/* Checks the system-memory page entries of a graphics range's tables. */
static void
check_system_pages(void)
{
	static uint8_t image[0x4000U + 66U * 0x1000U];
	uint64_t pages[2];
	uint64_t entry;
	size_t length;
	int error;

	length = nvrtx_mmu_gr_tables_bytes(0x8200000ULL);
	check(length == 0x4000U + 65U * 0x1000U, "tables of 130 MiB take 65 page tables");
	error = nvrtx_mmu_gr_tables(image, 0x1f0000000ULL, 0x20000000ULL, 0x8200000ULL);
	check(error == 0, "tables of the compute range build");

	/* Two pages at 0x28000000: page tables 64, entries 0 and 1. */
	pages[0] = 0x123456000ULL;
	pages[1] = 0x00789a000ULL;
	error = nvrtx_mmu_gr_map_system(image, 0x20000000ULL, 0x8200000ULL, 0x28000000ULL, pages, 2U);
	check(error == 0, "system pages map");
	memcpy(&entry, image + 0x4000U + 64U * 0x1000U, 8U);
	check(entry == ((0x123456000ULL >> 4) | 0x1U | 0x4U | 0x8U), "system page entry: valid, coherent system, volatile, not privileged");
	memcpy(&entry, image + 0x4000U + 64U * 0x1000U + 8U, 8U);
	check(entry == ((0x00789a000ULL >> 4) | 0xdU), "second system page entry");

	/* Pages past the range are refused. */
	error = nvrtx_mmu_gr_map_system(image, 0x20000000ULL, 0x8200000ULL, 0x281ff000ULL, pages, 2U);
	check(error == EINVAL, "system pages past the range are refused");
}

/* Reads a field of up to 64 bits from words at a bit position. */
static uint64_t
field(
	const uint32_t *words,
	unsigned low,
	unsigned bits)
{
	uint64_t value;
	unsigned bit;

	value = 0U;
	for (bit = 0U; bit < bits; bit++) {
		if (((words[(low + bit) / 32U] >> ((low + bit) % 32U)) & 1U) != 0U)
			value |= (uint64_t)1U << bit;
	}
	return value;
}

/* Reports nonzero when an instruction is the two 64-bit halves given. */
static int
words_are(
	const struct nvrtx_sm75_word *word,
	uint64_t low,
	uint64_t high)
{
	uint64_t got_low;
	uint64_t got_high;

	got_low = ((uint64_t)word->w[1] << 32) | word->w[0];
	got_high = ((uint64_t)word->w[3] << 32) | word->w[2];

	/* A mismatch is shown. */
	if (got_low != low || got_high != high) {
		printf("  got 0x%016llx 0x%016llx, want 0x%016llx 0x%016llx\n",
		    (unsigned long long)got_low,
		    (unsigned long long)got_high,
		    (unsigned long long)low,
		    (unsigned long long)high);
		return 0;
	}

	/* Succeeded: the same bits. */
	return 1;
}

/* Reads DIR/NAME.spv and compiles it; NULL (with the reason printed) when refused. */
static struct nvrtx_sm75_binary *
compile_file(
	const char *directory,
	const char *name)
{
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic diagnostic;
	char path[512];
	uint32_t *words;
	long bytes;
	FILE *file;
	int error;

	/* The module. */
	snprintf(path, sizeof(path), "%s/%s.spv", directory, name);
	file = fopen(path, "rb");
	if (file == NULL) {
		printf("  %s: cannot open\n", path);
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	bytes = ftell(file);
	fseek(file, 0, SEEK_SET);
	words = malloc((size_t)bytes);
	if (words == NULL || fread(words, 1U, (size_t)bytes, file) != (size_t)bytes) {
		fclose(file);
		free(words);
		return NULL;
	}
	fclose(file);

	/* The compile. */
	binary = NULL;
	error = nvrtx_sm75_compile(words, (size_t)bytes / 4U, &binary, &diagnostic);
	free(words);
	if (error != 0) {
		printf("  %s: refused %d: %s (instruction %u)\n", name, error, (diagnostic.reason != NULL) ? diagnostic.reason : "?", diagnostic.instruction);
		return NULL;
	}

	/* Succeeded: the program. */
	printf("  %s: %u instructions, %u registers\n", name, binary->code_words / 4U, binary->gpr_count);
	return binary;
}

/* Binds a storage buffer at (set, binding) and writes its entry into the root constant buffer. */
static void
bind(
	struct test_run *run,
	uint32_t set,
	uint32_t binding,
	uint32_t *words,
	uint32_t bytes)
{
	struct test_buffer *buffer;
	uint64_t address;
	uint32_t slot;
	uint32_t entry[4];

	buffer = &run->buffers[run->buffer_count];
	buffer->set = set;
	buffer->binding = binding;
	buffer->words = words;
	buffer->bytes = bytes;
	address = TEST_BUFFER_BASE + (uint64_t)run->buffer_count * TEST_BUFFER_STRIDE;
	run->buffer_count++;

	/* The program's table slot of the buffer, if it uses it. */
	for (slot = 0U; slot < run->binary->storage_count; slot++) {
		if (run->binary->storage[slot].set != set || run->binary->storage[slot].binding != binding)
			continue;
		entry[0] = (uint32_t)address;
		entry[1] = (uint32_t)(address >> 32);
		entry[2] = bytes;
		entry[3] = 0U;
		memcpy(&run->cb[0][NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY], entry, sizeof(entry));
	}
}

/* Runs every invocation of every group, one after another. */
static void
dispatch(
	struct test_run *run)
{
	uint32_t group[3];
	uint32_t local[3];
	const uint32_t *size;

	size = run->binary->local_size;
	for (group[2] = 0U; group[2] < run->groups[2]; group[2]++) {
		for (group[1] = 0U; group[1] < run->groups[1]; group[1]++) {
			for (group[0] = 0U; group[0] < run->groups[0]; group[0]++) {
				for (local[2] = 0U; local[2] < size[2]; local[2]++) {
					for (local[1] = 0U; local[1] < size[1]; local[1]++) {
						for (local[0] = 0U; local[0] < size[0]; local[0]++)
							invoke(run, local, group);
					}
				}
			}
		}
	}
}

/*
 * Runs one invocation: decodes each instruction by the field positions of
 * sm75-isa.h and does what it says, until EXIT.  Also checks the serial
 * dependences: an instruction after one of variable latency waits for the
 * scoreboards that one set.
 */
static void
invoke(
	struct test_run *run,
	const uint32_t local[3],
	const uint32_t group[3])
{
	const uint32_t *code;
	struct nvrtx_sm75_word word;
	uint32_t r[256];
	int p[8];
	uint32_t pc;
	uint32_t steps;
	uint32_t opcode;
	uint32_t alu;
	uint32_t form;
	uint32_t dst;
	uint32_t s0;
	uint32_t s1;
	uint32_t s2;
	uint32_t pending;
	uint32_t wait;
	uint32_t lut;
	uint32_t bit;
	uint32_t value;
	uint64_t wide;
	uint32_t *cell;
	int guard;
	int take;
	float f;

	memset(r, 0, sizeof(r));
	memset(p, 0, sizeof(p));
	p[7] = 1;
	code = run->binary->code;
	pending = 0U;

	for (pc = 0U, steps = 0U; steps < TEST_STEPS; steps++, pc++) {
		if (pc * 4U >= run->binary->code_words) {
			printf("  ran past the code\n");
			run->failed = 1;
			return;
		}
		memcpy(word.w, &code[pc * 4U], sizeof(word.w));
		r[NVRTX_SM75_RZ] = 0U;

		/* The serial dependences. */
		wait = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_WAIT_LOW, NVRTX_SM75_WAIT_BITS);
		if ((wait & pending) != pending) {
			printf("  instruction %u does not wait for scoreboards 0x%x\n", pc, pending);
			run->failed = 1;
		}
		pending = 0U;
		if (nvrtx_sm75_get(&word, NVRTX_SM75_WRITE_BAR_LOW, 3U) != NVRTX_SM75_BAR_NONE)
			pending |= 1U << nvrtx_sm75_get(&word, NVRTX_SM75_WRITE_BAR_LOW, 3U);
		if (nvrtx_sm75_get(&word, NVRTX_SM75_READ_BAR_LOW, 3U) != NVRTX_SM75_BAR_NONE)
			pending |= 1U << nvrtx_sm75_get(&word, NVRTX_SM75_READ_BAR_LOW, 3U);

		/* The guard. */
		guard = p[nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_LOW, 3U)];
		if (nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_NOT, 1U) != 0U)
			guard = !guard;
		opcode = (uint32_t)nvrtx_sm75_get(&word, 0U, 12U);
		dst = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_DST_LOW, 8U);

		/* The instructions that are not of the ALU form. */
		if (opcode == NVRTX_SM75_OP_EXIT)
			return;
		if (opcode == NVRTX_SM75_OP_NOP || opcode == NVRTX_SM75_OP_BRA) {
			printf("  instruction %u: %s before EXIT\n", pc, (opcode == NVRTX_SM75_OP_NOP) ? "NOP" : "BRA");
			run->failed = 1;
			return;
		}
		if (!guard)
			continue;
		if (opcode == NVRTX_SM75_OP_S2R) {
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_S2R_INDEX_LOW, 8U);
			if (value >= NVRTX_SM75_SR_TID_X && value <= NVRTX_SM75_SR_TID_Z)
				r[dst] = local[value - NVRTX_SM75_SR_TID_X];
			else if (value >= NVRTX_SM75_SR_CTAID_X && value <= NVRTX_SM75_SR_CTAID_Z)
				r[dst] = group[value - NVRTX_SM75_SR_CTAID_X];
			else
				run->failed = 1;
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDC) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_INDEX_LOW, 5U);
			s1 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_OFFSET_LOW, 16U);
			r[dst] = cb_word(run, s0, s1);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_MEM_TYPE_LOW, 3U) == NVRTX_SM75_MEM_B64)
				r[dst + 1U] = cb_word(run, s0, s1 + 4U);
			continue;
		}
		if (opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG) {
			s0 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U);
			wide = ((uint64_t)r[s0 + 1U] << 32) | r[s0];
			wide += (uint64_t)nvrtx_sm75_get(&word, NVRTX_SM75_MEM_OFFSET_LOW, 24U);
			cell = memory(run, wide);
			if (cell == NULL)
				return;
			if (opcode == NVRTX_SM75_OP_LDG)
				r[dst] = *cell;
			else
				*cell = r[nvrtx_sm75_get(&word, NVRTX_SM75_STORE_DATA_LOW, 8U)];
			continue;
		}

		/* The ALU form: the three sources by the form. */
		alu = (uint32_t)nvrtx_sm75_get(&word, 0U, 9U);
		form = (uint32_t)nvrtx_sm75_get(&word, 9U, 3U);
		s0 = r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_LOW, 8U)];
		s1 = 0U;
		s2 = r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U)];
		if (form == NVRTX_SM75_FORM_REG) {
			s1 = r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_LOW, 8U)];
		} else if (form == NVRTX_SM75_FORM_IMM) {
			s1 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IMM_LOW, 32U);
		} else if (form == NVRTX_SM75_FORM_CB) {
			s1 = cb_word(run, (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_INDEX_LOW, 5U), (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CB_OFFSET_LOW, 16U));
		} else if (form == NVRTX_SM75_FORM_IMM_SRC2) {
			s1 = s2;
			s2 = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_IMM_LOW, 32U);
		} else {
			printf("  instruction %u: form %u\n", pc, form);
			run->failed = 1;
			return;
		}

		/* The predicate input A, and the operation. */
		take = p[nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_LOW, 3U)];
		if (nvrtx_sm75_get(&word, NVRTX_SM75_PSRC_A_NOT, 1U) != 0U)
			take = !take;
		switch (alu) {
		case NVRTX_SM75_OP_MOV:
			r[dst] = s1;
			break;
		case NVRTX_SM75_OP_SEL:
			r[dst] = take ? s0 : s1;
			break;
		case NVRTX_SM75_OP_FMNMX:
			r[dst] = as_bits(take ? fminf(as_float(s0), as_float(s1)) : fmaxf(as_float(s0), as_float(s1)));
			break;
		case NVRTX_SM75_OP_FSETP:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CMP_LOW, 4U);
			if (value == NVRTX_SM75_FCMP_LT)
				guard = as_float(s0) < as_float(s1);
			else if (value == NVRTX_SM75_FCMP_GE)
				guard = as_float(s0) >= as_float(s1);
			else if (value == NVRTX_SM75_FCMP_EQ)
				guard = as_float(s0) == as_float(s1);
			else
				guard = !(as_float(s0) == as_float(s1));
			p[nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U)] = guard && take;
			p[7] = 1;
			break;
		case NVRTX_SM75_OP_ISETP:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_CMP_LOW, 3U);
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SIGNED, 1U) != 0U) {
				guard = (value == NVRTX_SM75_ICMP_LT) ? ((int32_t)s0 < (int32_t)s1) : (value == NVRTX_SM75_ICMP_GE) ? ((int32_t)s0 >= (int32_t)s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			} else {
				guard = (value == NVRTX_SM75_ICMP_LT) ? (s0 < s1) : (value == NVRTX_SM75_ICMP_GE) ? (s0 >= s1) : (value == NVRTX_SM75_ICMP_EQ) ? (s0 == s1) : (s0 != s1);
			}
			p[nvrtx_sm75_get(&word, NVRTX_SM75_PDST0_LOW, 3U)] = guard && take;
			p[7] = 1;
			break;
		case NVRTX_SM75_OP_IADD3:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_NEG, 1U) != 0U)
				s0 = 0U - s0;
			if (form == NVRTX_SM75_FORM_REG && nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_NEG, 1U) != 0U)
				s1 = 0U - s1;
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_NEG, 1U) != 0U)
				s2 = 0U - s2;
			r[dst] = s0 + s1 + s2;
			break;
		case NVRTX_SM75_OP_LOP3:
			lut = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_LUT_LOW, 8U);
			value = 0U;
			for (bit = 0U; bit < 32U; bit++) {
				uint32_t index;

				index = (((s0 >> bit) & 1U) << 2) | (((s1 >> bit) & 1U) << 1) | ((s2 >> bit) & 1U);
				value |= ((lut >> index) & 1U) << bit;
			}
			r[dst] = value;
			break;
		case NVRTX_SM75_OP_SHF:
			wide = ((uint64_t)s2 << 32) | s0;
			s1 &= 31U;
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_RIGHT, 1U) != 0U) {
				if (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_TYPE_LOW, 2U) == NVRTX_SM75_SHF_TYPE_I32)
					wide = (uint64_t)((int64_t)wide >> s1);
				else
					wide >>= s1;
			} else {
				wide <<= s1;
			}
			r[dst] = (nvrtx_sm75_get(&word, NVRTX_SM75_SHF_HIGH, 1U) != 0U) ? (uint32_t)(wide >> 32) : (uint32_t)wide;
			break;
		case NVRTX_SM75_OP_FADD:
		case NVRTX_SM75_OP_FMUL:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_SRC0_NEG, 1U) != 0U)
				s0 ^= 0x80000000U;
			if (form == NVRTX_SM75_FORM_REG && nvrtx_sm75_get(&word, NVRTX_SM75_SRC1_NEG, 1U) != 0U)
				s1 ^= 0x80000000U;
			if (alu == NVRTX_SM75_OP_FMUL) {
				f = as_float(s0) * as_float(s1);
			} else if (form == NVRTX_SM75_FORM_IMM_SRC2) {
				f = as_float(s0) + as_float(s2);
			} else {
				f = as_float(s0) + as_float(s1);
			}
			r[dst] = as_bits(f);
			break;
		case NVRTX_SM75_OP_IMAD:
			r[dst] = s0 * s1 + s2;
			break;
		case NVRTX_SM75_OP_IMAD_WIDE:
			wide = (uint64_t)s0 * s1;
			wide += ((uint64_t)r[nvrtx_sm75_get(&word, NVRTX_SM75_SRC2_LOW, 8U) + 1U] << 32) | s2;
			r[dst] = (uint32_t)wide;
			r[dst + 1U] = (uint32_t)(wide >> 32);
			break;
		case NVRTX_SM75_OP_F2I:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_F2I_SIGNED, 1U) != 0U)
				r[dst] = (uint32_t)(int32_t)truncf(as_float(s1));
			else
				r[dst] = (uint32_t)truncf(as_float(s1));
			break;
		case NVRTX_SM75_OP_I2F:
			if (nvrtx_sm75_get(&word, NVRTX_SM75_I2F_SIGNED, 1U) != 0U)
				r[dst] = as_bits((float)(int32_t)s1);
			else
				r[dst] = as_bits((float)s1);
			break;
		case NVRTX_SM75_OP_FRND:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_ROUND_LOW, 2U);
			f = as_float(s1);
			r[dst] = as_bits((value == NVRTX_SM75_ROUND_DOWN) ? floorf(f) : (value == NVRTX_SM75_ROUND_UP) ? ceilf(f) : (value == NVRTX_SM75_ROUND_ZERO) ? truncf(f) : nearbyintf(f));
			break;
		case NVRTX_SM75_OP_MUFU:
			value = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_MUFU_LOW, 6U);
			f = as_float(s1);
			if (value == NVRTX_SM75_MUFU_RCP)
				f = 1.0f / f;
			else if (value == NVRTX_SM75_MUFU_RSQ)
				f = 1.0f / sqrtf(f);
			else if (value == NVRTX_SM75_MUFU_SQRT)
				f = sqrtf(f);
			else if (value == NVRTX_SM75_MUFU_EX2)
				f = exp2f(f);
			else if (value == NVRTX_SM75_MUFU_LG2)
				f = log2f(f);
			else if (value == NVRTX_SM75_MUFU_SIN)
				f = sinf(f * 6.28318530718f);
			else
				f = cosf(f * 6.28318530718f);
			r[dst] = as_bits(f);
			break;
		default:
			printf("  instruction %u: ALU opcode 0x%03x not interpreted\n", pc, alu);
			run->failed = 1;
			return;
		}
	}

	/* An invocation that never reached EXIT. */
	printf("  an invocation ran %u instructions without EXIT\n", TEST_STEPS);
	run->failed = 1;
}

/* Finds the word at a GPU address in the bound buffers; NULL (a failure) outside them. */
static uint32_t *
memory(
	struct test_run *run,
	uint64_t address)
{
	uint64_t offset;
	uint32_t index;

	/* The buffer the address falls in, inside its bytes and aligned. */
	for (index = 0U; index < run->buffer_count; index++) {
		offset = address - (TEST_BUFFER_BASE + (uint64_t)index * TEST_BUFFER_STRIDE);
		if (address < TEST_BUFFER_BASE + (uint64_t)index * TEST_BUFFER_STRIDE || offset >= TEST_BUFFER_STRIDE)
			continue;
		if (offset + 4U > run->buffers[index].bytes || (offset & 3U) != 0U)
			break;
		return &run->buffers[index].words[offset / 4U];
	}

	/* Outside every buffer. */
	printf("  an access at 0x%llx outside the buffers\n", (unsigned long long)address);
	run->failed = 1;
	return NULL;
}

/* Reads a word of a constant buffer. */
static uint32_t
cb_word(
	const struct test_run *run,
	uint32_t index,
	uint32_t offset)
{
	uint32_t value;

	memcpy(&value, &run->cb[index & 7U][offset & 0xfffcU], sizeof(value));
	return value;
}

/* The float of 32 bits. */
static float
as_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* The 32 bits of a float. */
static uint32_t
as_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* Reports nonzero when two floats agree to a part in 10^5 (or 10^-5 absolute). */
static int
near(
	float a,
	float b)
{
	float difference;
	float size;

	difference = fabsf(a - b);
	size = fabsf(b);
	if (size < 1.0f)
		size = 1.0f;
	return difference <= size * 1e-5f;
}
