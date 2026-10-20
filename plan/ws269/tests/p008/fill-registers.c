/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host simulator's registers of the compiled fill (vkcompute's fill
 * shader, workgroup 0, its buffer inlined at the address given) after
 * its first N groups, N in steps of 4 up to all but the end, for lanes
 * 0, 1 and 31: the lines to compare with the board's "dump N/M lane L"
 * lines of the compute check (compute-check.c run_prefixes).
 *   fill-registers ADDRESS   (the board's run address + 0x1c0000, hex)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/powervr/shader.h"
#include "drivers/gpu/powervr/shader-private.h"
#include "machine-run.h"

#include "userland/tests/vkcompute/shader.h"

#define PREFIX_STEP	4U
#define DUMP_REGISTERS	16U
#define MEMORY_WORDS	64U

static uint32_t memory[MEMORY_WORDS];

/* Inlined values only: no shared word is read. */
static uint32_t
shared_word(const struct powervr_shader_program *program, uint32_t request, uint32_t word)
{
	(void)program;
	(void)request;
	(void)word;
	return 0xdead5000U;
}

static uint32_t
input_word(unsigned lane, uint32_t reg)
{
	(void)lane;
	(void)reg;
	return 0xdead2000U;
}

static uint32_t
iterate_word(unsigned lane, uint32_t coefficient, unsigned flags)
{
	(void)lane;
	(void)coefficient;
	(void)flags;
	return 0xdead3000U;
}

/* Workgroup 0: the coefficients the data sequencer writes. */
static uint32_t
coefficient_word(uint32_t reg)
{
	(void)reg;
	return 0;
}

static void
sample_words(uint32_t image, uint32_t sampler, uint32_t u, uint32_t v, uint32_t result[4])
{
	(void)image;
	(void)sampler;
	(void)u;
	(void)v;
	memset(result, 0, 4 * sizeof(result[0]));
}

/* Runs the backend's first keep groups (all when keep is the count) over 32 lanes. */
static int
run_prefix(const struct powervr_shader_program *program, const struct shader_backend *backend, struct shader_backend *copy, unsigned keep, uint64_t address, struct machine_run *run)
{
	memcpy(copy, backend, sizeof(*copy));
	copy->operation_count = keep;
	if (keep < backend->operation_count) {
		memset(&copy->operations[keep], 0, sizeof(copy->operations[keep]));
		copy->operations[keep].kind = MACHINE_END;
		copy->operation_count = keep + 1U;
	}
	memset(run, 0, sizeof(*run));
	run->program = program;
	run->shared_word = shared_word;
	run->input_word = input_word;
	run->iterate_word = iterate_word;
	run->sample_words = sample_words;
	run->coefficient_word = coefficient_word;
	run->memory = memory;
	run->memory_base = address;
	run->memory_words = MEMORY_WORDS;
	return machine_run_program(run, copy, 0xffffffffU);
}

int
main(int argc, char **argv)
{
	static const unsigned lanes[] = { 0, 1, 31 };
	static struct powervr_shader_workspace workspace;
	static struct powervr_shader_program program;
	static struct shader_backend copy;
	static struct machine_run run;
	static uint8_t code[8192];
	struct powervr_ir_shader *shader;
	const struct powervr_shader_request *request;
	uint32_t values[POWERVR_SHADER_REQUESTS * 4U];
	uint64_t address;
	unsigned keep;
	unsigned last;
	unsigned index;
	unsigned word;
	unsigned good;
	unsigned i;
	int error;

	if (argc != 2) {
		fprintf(stderr, "usage: fill-registers ADDRESS\n");
		return 2;
	}
	address = strtoull(argv[1], NULL, 16);

	/* What the shader asks for, as the compute check fills it. */
	error = powervr_shader_read(vkcompute_fill_shader, sizeof(vkcompute_fill_shader) / 4U, POWERVR_SHADER_COMPUTE, NULL, &workspace, &program, &shader);
	if (error != 0) {
		fprintf(stderr, "read: %d\n", error);
		return 1;
	}
	memset(values, 0, sizeof(values));
	for (index = 0; index < program.request_count; index++) {
		request = &program.requests[index];
		for (word = 0; word < request->words; word++)
			values[request->shared + word] = request->kind == POWERVR_SHADER_REQUEST_NUM_WORKGROUPS ? 1U : 0U;
		if (request->kind == POWERVR_SHADER_REQUEST_DESCRIPTOR) {
			values[request->shared] = (uint32_t)address;
			values[request->shared + 1U] = (uint32_t)(address >> 32);
		}
	}
	error = powervr_shader_compile_inline(vkcompute_fill_shader, sizeof(vkcompute_fill_shader) / 4U, POWERVR_SHADER_COMPUTE, NULL, values, program.shareds, &workspace, &program, code, sizeof(code));
	if (error != 0) {
		fprintf(stderr, "compile: %d\n", error);
		return 1;
	}
	printf("compiled fill: %u bytes, %u temps, %u groups\n", (unsigned)program.length, (unsigned)program.temps, workspace.backend.operation_count);

	/* Each prefix's registers, as the board's dump lines. */
	last = workspace.backend.operation_count - 1U;
	for (keep = PREFIX_STEP;; keep += PREFIX_STEP) {
		if (keep > last)
			keep = last;
		memset(memory, 0, sizeof(memory));
		error = run_prefix(&program, &workspace.backend, &copy, keep, address, &run);
		for (i = 0; i < sizeof(lanes) / sizeof(lanes[0]); i++) {
			printf("dump %u/%u lane %u (%d):", keep, last, lanes[i], error);
			for (index = 0; index < DUMP_REGISTERS; index++)
				printf(" %08x", run.temps[index][lanes[i]]);
			printf("\n");
		}
		if (keep == last)
			break;
	}

	/* The whole program's words. */
	memset(memory, 0, sizeof(memory));
	error = run_prefix(&program, &workspace.backend, &copy, workspace.backend.operation_count, address, &run);
	good = 0;
	for (word = 0; word < 32U; word++)
		good += memory[word] == word * 3U + 7U;
	printf("whole program (%d): %u/32 words hold 3x + 7\n", error, good);
	return good == 32U ? 0 : 1;
}
