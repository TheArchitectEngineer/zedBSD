/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: every desktop shader through the nvrtx SM75 compiler.
 *
 * Each SPIR-V module named on the command line (the desktop's shaders as
 * the components ship them, sm75-desktop-extract.py) is compiled for the
 * stage of its entry point.  A compiled one prints its size; a refused one
 * prints the compiler's reason and the IR operation it stopped at.  With
 * --strict a refusal fails the run.
 *
 * Each compiled program is then run for many made-up inputs (attribute
 * words, push constants, uniform buffer words) on the SM75 interpreter
 * (sm75-machine.c) and, from the parsed IR before any optimization, on the
 * reference interpreter of the IR (sm75-reference.c); the outputs (a
 * vertex program's attribute words, a fragment program's colours and its
 * discard) must agree.  A disagreement fails the run, strict or not.
 *
 *   nvrtx-sm75-desktop-test [--strict] FILE.spv ...
 *
 * Nothing here is evidence about the GPU.
 */

#include "drivers/gpu/nvrtx/compiler/sm75-compile.h"
#include "drivers/gpu/nvrtx/compiler/sm75-isa.h"
#include "drivers/gpu/compiler/spirv.h"
#include "plan/ws037/tests/sm75-machine.h"
#include "plan/ws037/tests/sm75-reference.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SPIR-V magic word, OpEntryPoint and the execution models of the stages compiled. */
#define TEST_SPIRV_MAGIC		0x07230203U
#define TEST_SPIRV_OP_ENTRY_POINT	15U
#define TEST_SPIRV_MODEL_VERTEX		0U
#define TEST_SPIRV_MODEL_FRAGMENT	4U
#define TEST_SPIRV_MODEL_COMPUTE	5U

/* The largest module read. */
#define TEST_MAX_WORDS			65536U

/* The runs of each compiled program, and the words of the uniform buffers filled. */
#define TEST_TRIALS			200U
#define TEST_UBO_WORDS			1024U

/* The first texture handle the runs write (one more for each image). */
#define TEST_HANDLE_BASE		100U

/* Programs whose two interpreters disagreed, and the state of the made-up inputs. */
static int test_mismatches;
static uint32_t test_random = 0x2545f491U;

static int survey(const char *path, int *compiled);
static int read_module(const char *path, uint32_t *words, size_t *count);
static int entry_stage(const uint32_t *words, size_t count, enum drv_gpu_shader_stage *stage);
static uint32_t refused_op(const uint32_t *words, size_t count, enum drv_gpu_shader_stage stage, uint32_t instruction);
static void compare(const char *name, const uint32_t *words, size_t count, enum drv_gpu_shader_stage stage, const struct nvrtx_sm75_binary *binary);
static void fill_inputs(struct test_run *run, const struct nvrtx_sm75_binary *binary, uint32_t trial);
static uint32_t random_word(uint32_t trial);
static uint32_t next_random(void);
static int agree(uint32_t got, uint32_t want);

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

/* The kernel allocator the compiler calls, on the host. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	/* The C library's zeroed allocation. */
	return calloc(count, size);
}

/* The kernel free the compiler calls, on the host. */
void
kern_free(
	void *pointer)
{
	/* The C library's free. */
	free(pointer);
}

/* Compiles every module named and reports what was compiled and refused. */
int
main(
	int argc,
	char **argv)
{
	int strict;
	int first;
	int index;
	int compiled;
	int compiled_count;
	int refused_count;
	int error;

	/* --strict makes a refusal a failure. */
	strict = 0;
	first = 1;
	if (argc > 1 && strcmp(argv[1], "--strict") == 0) {
		strict = 1;
		first = 2;
	}

	/* Each module in order. */
	compiled_count = 0;
	refused_count = 0;
	for (index = first; index < argc; index++) {
		error = survey(argv[index], &compiled);
		if (error != 0)
			return 2;

		/* Counts the outcome. */
		if (compiled != 0) {
			compiled_count++;
		} else {
			refused_count++;
		}
	}

	printf("nvrtx_sm75_desktop_test: %d compiled, %d refused, %d disagreed with the IR\n", compiled_count, refused_count, test_mismatches);

	/* A disagreement always fails. */
	if (test_mismatches != 0)
		return 1;

	/* A refusal fails a strict run. */
	if (strict != 0 && refused_count != 0)
		return 1;

	/* Succeeded: the survey is printed. */
	return 0;
}

/* Compiles one module and prints its outcome; nonzero when the file cannot be read. */
static int
survey(
	const char *path,
	int *compiled)
{
	static uint32_t words[TEST_MAX_WORDS];
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic diagnostic;
	enum drv_gpu_shader_stage stage;
	const char *name;
	size_t count;
	uint32_t op;
	int error;

	*compiled = 0;

	/* The module's words. */
	error = read_module(path, words, &count);
	if (error != 0) {
		fprintf(stderr, "cannot read %s\n", path);
		return error;
	}

	/* The file's name without its directory. */
	name = strrchr(path, '/');
	if (name == NULL) {
		name = path;
	} else {
		name++;
	}

	/* The stage of the entry point. */
	error = entry_stage(words, count, &stage);
	if (error != 0) {
		printf("REFUSED %-40s no vertex, fragment or compute entry point\n", name);
		return 0;
	}

	/* The compilation. */
	binary = NULL;
	error = nvrtx_sm75_compile_stage(words, count, stage, &binary, &diagnostic);
	if (error == 0) {
		printf("OK      %-40s %u instructions, %u registers\n", name, binary->code_words / 4U, binary->gpr_count);
		compare(name, words, count, stage, binary);
		nvrtx_sm75_binary_free(binary);
		*compiled = 1;
		return 0;
	}

	/* A refusal with the operation it stopped at (the parsed program's instruction). */
	op = refused_op(words, count, stage, diagnostic.instruction);
	if (diagnostic.reason == NULL)
		diagnostic.reason = "(no reason)";
	printf("REFUSED %-40s error %d at %u (IR op %u): %s\n", name, error, diagnostic.instruction, op, diagnostic.reason);

	/* Succeeded in reporting the refusal. */
	return 0;
}

/* Reads a SPIR-V file into words; nonzero when it cannot be read or is not SPIR-V. */
static int
read_module(
	const char *path,
	uint32_t *words,
	size_t *count)
{
	FILE *file;
	size_t length;

	/* The file. */
	file = fopen(path, "rb");
	if (file == NULL)
		return 1;

	length = fread(words, sizeof(*words), TEST_MAX_WORDS, file);
	fclose(file);

	/* A SPIR-V module starts with its magic word. */
	if (length < 5U || words[0] != TEST_SPIRV_MAGIC)
		return 1;

	/* Succeeded: the words are read. */
	*count = length;
	return 0;
}

/* Finds the stage of the module's first entry point; nonzero for none compiled. */
static int
entry_stage(
	const uint32_t *words,
	size_t count,
	enum drv_gpu_shader_stage *stage)
{
	size_t at;
	uint32_t length;
	uint32_t opcode;
	uint32_t model;

	/* The instructions after the five header words. */
	for (at = 5U; at < count; at += length) {
		length = words[at] >> 16;
		opcode = words[at] & 0xffffU;
		if (length == 0U)
			return 1;
		if (opcode != TEST_SPIRV_OP_ENTRY_POINT)
			continue;

		/* The execution model. */
		model = words[at + 1U];
		if (model == TEST_SPIRV_MODEL_VERTEX) {
			*stage = DRV_GPU_STAGE_VERTEX;
			return 0;
		}
		if (model == TEST_SPIRV_MODEL_FRAGMENT) {
			*stage = DRV_GPU_STAGE_FRAGMENT;
			return 0;
		}
		if (model == TEST_SPIRV_MODEL_COMPUTE) {
			*stage = DRV_GPU_STAGE_COMPUTE;
			return 0;
		}
		return 1;
	}

	/* Succeeded in finding no entry point. */
	return 1;
}

/* Reports the operation of the parsed program's instruction, or 0xffffffff when there is none. */
static uint32_t
refused_op(
	const uint32_t *words,
	size_t count,
	enum drv_gpu_shader_stage stage,
	uint32_t instruction)
{
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_compile_diagnostic diagnostic;
	uint32_t op;
	int error;

	/* The parsed program. */
	ir = NULL;
	error = drv_gpu_shader_parse(words, count, stage, &ir, &diagnostic);
	if (error != 0)
		return 0xffffffffU;

	/* The instruction's operation. */
	op = 0xffffffffU;
	if (instruction < ir->instruction_count)
		op = (uint32_t)ir->instructions[instruction].op;
	drv_gpu_shader_ir_free(ir);

	/* Succeeded: the operation, or none. */
	return op;
}

/*
 * Runs a compiled graphics program on the SM75 interpreter and its parsed IR
 * on the reference for TEST_TRIALS made-up inputs, and counts the program
 * once when any output disagrees.
 */
static void
compare(
	const char *name,
	const uint32_t *words,
	size_t count,
	enum drv_gpu_shader_stage stage,
	const struct nvrtx_sm75_binary *binary)
{
	static struct test_run run;
	static struct test_thread thread;
	static struct sm75_reference_result result;
	const uint32_t group[3] = { 0U, 0U, 0U };
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_compile_diagnostic diagnostic;
	uint32_t base[SM75_REFERENCE_COLORS];
	uint32_t trial;
	uint32_t index;
	uint32_t location;
	uint32_t component;
	uint32_t next;
	uint32_t got;
	uint32_t want;
	uint32_t runs;
	int bad;
	int error;

	/* Only vertex and fragment programs are compared. */
	if (stage == DRV_GPU_STAGE_COMPUTE)
		return;

	/* The parsed program, before the optimizer. */
	ir = NULL;
	error = drv_gpu_shader_parse(words, count, stage, &ir, &diagnostic);
	if (error != 0) {
		printf("  %s: the reference cannot parse it\n", name);
		test_mismatches++;
		return;
	}

	/* A fragment program's colours lie from R0, four registers for each location written, in ascending order. */
	for (location = 0U; location < SM75_REFERENCE_COLORS; location++)
		base[location] = 0xffffffffU;
	for (index = 0U; index < ir->instruction_count; index++) {
		if (ir->instructions[index].op == DRV_GPU_IR_STORE_OUTPUT && ir->instructions[index].location < SM75_REFERENCE_COLORS)
			base[ir->instructions[index].location] = 0U;
	}
	next = 0U;
	for (location = 0U; location < SM75_REFERENCE_COLORS; location++) {
		if (base[location] != 0U)
			continue;
		base[location] = next;
		next += 4U;
	}

	/* The trials. */
	bad = 0;
	runs = 0U;
	for (trial = 0U; trial < TEST_TRIALS && bad == 0; trial++) {
		memset(&run, 0, sizeof(run));
		run.binary = binary;
		fill_inputs(&run, binary, trial);

		/* The compiled program. */
		memset(&thread, 0, sizeof(thread));
		thread.p[NVRTX_SM75_PT] = 1;
		sm75_machine_invoke(&run, &thread, group);
		if (run.failed || (thread.state != TEST_DONE && run.killed == 0)) {
			printf("  %s: trial %u: the SM75 interpreter failed\n", name, trial);
			bad = 1;
			break;
		}

		/* The IR. */
		sm75_reference_run(ir, binary, &run, &result);
		if (result.failed) {
			printf("  %s: trial %u: the reference cannot run it: %s\n", name, trial, result.why);
			bad = 1;
			break;
		}

		/* An input the IR leaves undefined proves nothing. */
		if (result.undefined)
			continue;
		runs++;

		/* The discard. */
		if (result.killed != run.killed) {
			printf("  %s: trial %u: discarded %d, the IR %d\n", name, trial, run.killed, result.killed);
			bad = 1;
			break;
		}
		if (result.killed)
			continue;

		/* A vertex program's attribute words. */
		for (index = 0U; index < 256U && stage == DRV_GPU_STAGE_VERTEX; index++) {
			if (result.attr_written[index] == 0U)
				continue;
			got = run.attr_out[index];
			want = result.attr_out[index];
			if (agree(got, want) == 0) {
				printf("  %s: trial %u: attribute 0x%03x is 0x%08x (%g), the IR 0x%08x (%g)\n", name, trial, index * 4U, got, (double)sm75_machine_float(got), want, (double)sm75_machine_float(want));
				bad = 1;
			}
		}

		/* A fragment program's colours. */
		for (location = 0U; location < SM75_REFERENCE_COLORS && stage == DRV_GPU_STAGE_FRAGMENT; location++) {
			for (component = 0U; component < 4U; component++) {
				if (result.color_written[location][component] == 0U)
					continue;
				got = run.exit_regs[base[location] + component];
				want = result.color[location][component];
				if (agree(got, want) == 0) {
					printf("  %s: trial %u: colour %u.%u is 0x%08x (%g), the IR 0x%08x (%g)\n", name, trial, location, component, got, (double)sm75_machine_float(got), want, (double)sm75_machine_float(want));
					bad = 1;
				}
			}
		}
	}

	/* The outcome. */
	if (bad != 0) {
		test_mismatches++;
	} else {
		printf("        %-40s agrees with the IR on %u inputs\n", name, runs);
	}
	drv_gpu_shader_ir_free(ir);
}

/*
 * Fills a run's inputs for one trial: the attribute words, the fragment's w
 * (2, which a perspective input is multiplied by), the push constants, the
 * texture handles and the uniform buffers.
 */
static void
fill_inputs(
	struct test_run *run,
	const struct nvrtx_sm75_binary *binary,
	uint32_t trial)
{
	uint32_t index;
	uint32_t buffer;
	uint32_t word;

	/* The attribute words. */
	for (index = 0U; index < 256U; index++)
		run->attr_in[index] = random_word(trial);
	run->attr_in[NVRTX_SM75_ATTR_POSITION_W / 4U] = sm75_machine_bits(2.0f);
	run->attr_in[NVRTX_SM75_ATTR_VERTEX_ID / 4U] = next_random() % 6U;
	run->attr_in[NVRTX_SM75_ATTR_INSTANCE_ID / 4U] = next_random() % 4U;

	/* The push constants. */
	for (index = 0U; index < NVRTX_SM75_CB_PUSH_BYTES; index += 4U) {
		word = random_word(trial);
		memcpy(&run->cb[NVRTX_SM75_CB_ROOT][NVRTX_SM75_CB_PUSH + index], &word, sizeof(word));
	}

	/* The texture handles. */
	for (index = 0U; index < binary->texture_count; index++) {
		word = TEST_HANDLE_BASE + index;
		memcpy(&run->cb[NVRTX_SM75_CB_ROOT][NVRTX_SM75_CB_TEXTURES + index * 4U], &word, sizeof(word));
	}

	/* The uniform buffers. */
	for (buffer = NVRTX_SM75_CB_UNIFORM_FIRST; buffer < NVRTX_SM75_CB_UNIFORM_FIRST + NVRTX_SM75_CB_UNIFORM_MAX; buffer++) {
		for (index = 0U; index < TEST_UBO_WORDS; index++) {
			word = random_word(trial);
			memcpy(&run->cb[buffer][index * 4U], &word, sizeof(word));
		}
	}
}

/*
 * Makes one input word: a float in [0, 1], one in [-2, 2], or a small
 * integer, chosen at random (a quarter of the trials use small integers
 * only, so flags and counts take every path).
 */
static uint32_t
random_word(
	uint32_t trial)
{
	uint32_t kind;

	/* The kind of word. */
	kind = next_random() % 3U;
	if ((trial % 4U) == 3U)
		kind = 2U;

	/* A float in [0, 1]. */
	if (kind == 0U)
		return sm75_machine_bits((float)(next_random() % 1000001U) / 1000000.0f);

	/* A float in [-2, 2]. */
	if (kind == 1U)
		return sm75_machine_bits((float)(next_random() % 4000001U) / 1000000.0f - 2.0f);

	/* Succeeded: a small integer. */
	return next_random() % 8U;
}

/* The next number of the inputs' xorshift sequence. */
static uint32_t
next_random(void)
{
	/* One xorshift step. */
	test_random ^= test_random << 13;
	test_random ^= test_random >> 17;
	test_random ^= test_random << 5;

	/* Succeeded: the new state. */
	return test_random;
}

/*
 * Reports nonzero when a word of the compiled program agrees with the IR's:
 * the same bits, both NaN, or floats within a part in 10^4 (or 10^-4
 * absolute), which the special function units' rounding allows.
 */
static int
agree(
	uint32_t got,
	uint32_t want)
{
	float a;
	float b;
	float size;

	/* The same bits. */
	if (got == want)
		return 1;

	a = sm75_machine_float(got);
	b = sm75_machine_float(want);

	/* Both NaN. */
	if (a != a && b != b)
		return 1;

	/* Close floats. */
	size = fabsf(b);
	if (size < 1.0f)
		size = 1.0f;
	if (fabsf(a - b) <= size * 1e-4f)
		return 1;

	/* Succeeded in finding a disagreement. */
	return 0;
}
