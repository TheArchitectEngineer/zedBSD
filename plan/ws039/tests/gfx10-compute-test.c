/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: compute programs compiled by the amdrdna compiler, run on the
 * GFX10 interpreter and on the IR reference with the same random inputs,
 * their storage buffers compared.
 *
 * usage: gfx10-compute-test OUTDIR SPEC...
 *   SPEC = module.spv[:GX,GY,GZ[:float]]  (the workgroups dispatched; the
 *   storage buffers filled with small floats instead of random words)
 *
 * Each program's code is also written to OUTDIR/<name>.hex for
 * llvm-mc -disassemble.
 */

#include "drivers/gpu/amdrdna/compiler/gfx10-compile.h"
#include "drivers/gpu/compiler/spirv.h"
#include "plan/ws039/tests/gfx10-machine.h"
#include "plan/ws039/tests/gfx10-reference.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The trials of each program, the bytes of each storage and uniform buffer, and of the table. */
#define TEST_TRIALS		16U
#define TEST_BUFFER_BYTES	4096U
#define TEST_TABLE_BYTES	4096U

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);

static int test_module(const char *outdir, const char *spec);
static int run_trial(const struct drv_gpu_shader_ir *ir, const struct amdrdna_gfx10_program *program, const uint32_t *groups, int floats, unsigned seed, int tolerant, uint64_t *steps);
static uint32_t random_word(uint32_t *state);
static int words_match(uint32_t a, uint32_t b, int tolerant);
static uint32_t *read_file(const char *path, size_t *words);

/* The host's allocator stands in for the kernel's. */
void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

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

/* Tests every module given; 1 when any failed. */
int
main(
	int argc,
	char **argv)
{
	int failed;
	int index;

	if (argc < 3) {
		fprintf(stderr, "usage: %s OUTDIR SPEC...\n", argv[0]);
		return 2;
	}
	failed = 0;
	for (index = 2; index < argc; index++)
		failed |= test_module(argv[1], argv[index]);
	return failed;
}

/* Compiles one module and runs its trials. */
static int
test_module(
	const char *outdir,
	const char *spec)
{
	char path[4096];
	char hex_path[8200];
	char name[4096];
	char *colon;
	const char *base;
	uint32_t *words;
	size_t word_count;
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic diagnostic;
	uint32_t groups[3];
	uint32_t index;
	uint64_t steps;
	unsigned trial;
	int floats;
	int tolerant;
	int error;
	FILE *hex;

	/* The spec: the path, the workgroups and the fill. */
	snprintf(path, sizeof(path), "%s", spec);
	groups[0] = 2U;
	groups[1] = 1U;
	groups[2] = 1U;
	floats = 0;
	colon = strchr(path, ':');
	if (colon != NULL) {
		*colon = '\0';
		sscanf(colon + 1, "%u,%u,%u", &groups[0], &groups[1], &groups[2]);
		if (strstr(colon + 1, ":float") != NULL)
			floats = 1;
	}
	base = strrchr(path, '/');
	base = base != NULL ? base + 1 : path;
	snprintf(name, sizeof(name), "%s", base);
	colon = strrchr(name, '.');
	if (colon != NULL)
		*colon = '\0';

	words = read_file(path, &word_count);
	if (words == NULL) {
		printf("FAIL %s: cannot read the module\n", name);
		return 1;
	}

	/* The reference runs the parsed program, before any optimization. */
	ir = NULL;
	memset(&parse_diagnostic, 0, sizeof(parse_diagnostic));
	error = drv_gpu_shader_parse(words, word_count, DRV_GPU_STAGE_COMPUTE, &ir, &parse_diagnostic);
	if (error != 0) {
		printf("FAIL %s: the frontend refused it (%s)\n", name, parse_diagnostic.reason);
		free(words);
		return 1;
	}

	/* The compiler. */
	program = NULL;
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile(words, word_count, DRV_GPU_STAGE_COMPUTE, &program, &diagnostic);
	free(words);
	if (error != 0) {
		printf("FAIL %s: the compiler refused it at %u (%s)\n", name, diagnostic.instruction, diagnostic.reason);
		drv_gpu_shader_ir_free(ir);
		return 1;
	}

	/* The code for the disassembler. */
	snprintf(hex_path, sizeof(hex_path), "%s/%s.hex", outdir, name);
	hex = fopen(hex_path, "w");
	if (hex != NULL) {
		for (index = 0U; index < program->code_words; index++) {
			fprintf(hex, "0x%02x 0x%02x 0x%02x 0x%02x\n",
				program->code[index] & 0xffU,
				(program->code[index] >> 8) & 0xffU,
				(program->code[index] >> 16) & 0xffU,
				program->code[index] >> 24);
		}
		fclose(hex);
	}

	/* The sine and cosine are approximations on the hardware: their programs compare with a tolerance. */
	tolerant = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		if (ir->instructions[index].op == DRV_GPU_IR_SIN || ir->instructions[index].op == DRV_GPU_IR_COS)
			tolerant = 1;
	}

	/* The trials. */
	steps = 0U;
	error = 0;
	for (trial = 0U; trial < TEST_TRIALS && error == 0; trial++)
		error = run_trial(ir, program, groups, floats, trial * 7919U + 17U, tolerant, &steps);

	if (error == 0) {
		printf("PASS %s: %u trials, %u words, %u VGPRs, %u SGPRs, %llu instructions run\n",
		       name, TEST_TRIALS, program->code_words, program->vgpr_count, program->sgpr_count,
		       (unsigned long long)steps);
	} else {
		printf("FAIL %s (trial %u)\n", name, trial - 1U);
	}
	amdrdna_gfx10_program_free(program);
	drv_gpu_shader_ir_free(ir);
	return error;
}

/* Runs one dispatch on both interpreters with one seed's inputs; 1 when they differ or one fails. */
static int
run_trial(
	const struct drv_gpu_shader_ir *ir,
	const struct amdrdna_gfx10_program *program,
	const uint32_t *groups,
	int floats,
	unsigned seed,
	int tolerant,
	uint64_t *steps)
{
	static uint8_t gpu_bytes[REFERENCE_UNIFORMS][TEST_BUFFER_BYTES];
	static uint8_t ref_bytes[REFERENCE_UNIFORMS][TEST_BUFFER_BYTES];
	static uint8_t table[TEST_TABLE_BYTES];
	static uint8_t push[256];
	struct machine_memory memory;
	struct machine_dispatch dispatch;
	struct machine_result result;
	struct reference_memory reference;
	const struct drv_gpu_shader_ir_uniform *uniform;
	const struct amdrdna_gfx10_binding *binding;
	uint64_t addresses[REFERENCE_UNIFORMS];
	uint32_t sizes[REFERENCE_UNIFORMS];
	uint32_t group[3];
	uint32_t state;
	uint32_t word;
	uint32_t index;
	uint32_t slot;
	uint32_t gpu_word;
	uint32_t ref_word;
	const char *why;
	int undefined;
	int any_undefined;
	int match;

	if (ir->uniform_count > REFERENCE_UNIFORMS) {
		printf("  too many uniforms\n");
		return 1;
	}

	/* The inputs: every buffer and the push constants random; the system buffer holds the group counts. */
	state = seed * 2654435761U + 1U;
	memset(&memory, 0, sizeof(memory));
	memset(&reference, 0, sizeof(reference));
	memset(table, 0, sizeof(table));
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		sizes[index] = TEST_BUFFER_BYTES;
		for (word = 0U; word < TEST_BUFFER_BYTES / 4U; word++) {
			uint32_t bits = random_word(&state);

			/* Small floats (multiples of 1/16 in -8 .. 8), or uniform words as small integers. */
			if (floats != 0 && uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
				float number = (float)((int32_t)(bits % 257U) - 128) / 16.0f;

				memcpy(&bits, &number, 4U);
			} else if (uniform->kind == DRV_GPU_IR_UNIFORM_BLOCK) {
				bits %= 64U;
			}
			memcpy(&gpu_bytes[index][word * 4U], &bits, 4U);
		}
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE && uniform->set == DRV_GPU_IR_SYSTEM_SET) {
			sizes[index] = 12U;
			memcpy(&gpu_bytes[index][0], groups, 12U);
		}
		memcpy(ref_bytes[index], gpu_bytes[index], TEST_BUFFER_BYTES);
		reference.bytes[index] = ref_bytes[index];
		reference.size[index] = sizes[index];
	}
	for (index = 0U; index < sizeof(push); index++)
		push[index] = (uint8_t)(random_word(&state) % 16U);
	if (floats != 0) {
		for (index = 0U; index + 4U <= sizeof(push); index += 4U) {
			float number = (float)((int32_t)(random_word(&state) % 33U) - 8) / 4.0f;

			memcpy(&push[index], &number, 4U);
		}
		/* A count read as an integer stays small and positive. */
		for (index = 4U; index + 4U <= sizeof(push); index += 8U) {
			word = 1U + random_word(&state) % 40U;
			memcpy(&push[index], &word, 4U);
		}
	}
	reference.push = push;
	reference.push_size = ir->push_bytes;

	/* The GPU memory: the table first, then a region per buffer. */
	machine_add_region(&memory, table, TEST_TABLE_BYTES);
	for (index = 0U; index < ir->uniform_count; index++)
		addresses[index] = machine_add_region(&memory, gpu_bytes[index], TEST_BUFFER_BYTES);
	memcpy(table, push, ir->push_bytes);

	/* Each buffer's descriptor where the program expects it. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		binding = NULL;
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
			for (slot = 0U; slot < program->storage_count; slot++) {
				if (program->storage[slot].set == uniform->set && program->storage[slot].binding == uniform->binding)
					binding = &program->storage[slot];
			}
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_BLOCK) {
			for (slot = 0U; slot < program->uniform_count; slot++) {
				if (program->uniforms[slot].set == uniform->set && program->uniforms[slot].binding == uniform->binding)
					binding = &program->uniforms[slot];
			}
		}
		if (binding != NULL)
			machine_raw_descriptor(table, binding->table_offset, addresses[index], sizes[index]);
	}

	/* Every workgroup on both. */
	any_undefined = 0;
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.code = program->code;
	dispatch.code_words = program->code_words;
	dispatch.table = memory.regions[0].address;
	dispatch.local_size[0] = program->local_size[0];
	dispatch.local_size[1] = program->local_size[1];
	dispatch.local_size[2] = program->local_size[2];
	dispatch.shared_bytes = program->shared_bytes;
	dispatch.vgpr_count = program->vgpr_count;
	for (group[2] = 0U; group[2] < groups[2]; group[2]++) {
		for (group[1] = 0U; group[1] < groups[1]; group[1]++) {
			for (group[0] = 0U; group[0] < groups[0]; group[0]++) {
				dispatch.group_id[0] = group[0];
				dispatch.group_id[1] = group[1];
				dispatch.group_id[2] = group[2];
				machine_run_group(&dispatch, &memory, &result);
				*steps += result.steps;
				if (result.error != NULL) {
					printf("  GFX10: %s at word %u (0x%08x), group %u %u %u\n",
					       result.error, result.error_pc, result.error_word, group[0], group[1], group[2]);
					return 1;
				}
				why = reference_run_group(ir, group, &reference, &undefined);
				if (why != NULL) {
					printf("  reference: %s\n", why);
					return 1;
				}
				any_undefined |= undefined;
			}
		}
	}

	/* A program that met something undefined is not compared. */
	if (any_undefined != 0)
		return 0;

	/* The storage buffers match word for word (within the tolerance when allowed). */
	for (index = 0U; index < ir->uniform_count; index++) {
		if (ir->uniforms[index].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			continue;
		for (word = 0U; word < sizes[index] / 4U; word++) {
			memcpy(&gpu_word, &gpu_bytes[index][word * 4U], 4U);
			memcpy(&ref_word, &ref_bytes[index][word * 4U], 4U);
			match = words_match(gpu_word, ref_word, tolerant);
			if (match == 0) {
				printf("  buffer (set %u, binding %u) word %u: GFX10 0x%08x, reference 0x%08x\n",
				       ir->uniforms[index].set, ir->uniforms[index].binding, word, gpu_word, ref_word);
				return 1;
			}
		}
	}
	return 0;
}

/* The next word of a small random sequence. */
static uint32_t
random_word(
	uint32_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state;
}

/* Two words match: equal, or (tolerant) floats within a small relative difference. */
static int
words_match(
	uint32_t a,
	uint32_t b,
	int tolerant)
{
	float fa;
	float fb;

	if (a == b)
		return 1;
	if (tolerant == 0)
		return 0;
	memcpy(&fa, &a, 4U);
	memcpy(&fb, &b, 4U);
	if (fabsf(fa - fb) <= 1e-5f * (fabsf(fb) > 1.0f ? fabsf(fb) : 1.0f))
		return 1;
	return 0;
}

/* Reads a file of 32-bit words. */
static uint32_t *
read_file(
	const char *path,
	size_t *words)
{
	FILE *file;
	long size;
	uint32_t *data;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size + 4U);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(data);
		return NULL;
	}
	fclose(file);
	*words = (size_t)size / 4U;
	return data;
}
