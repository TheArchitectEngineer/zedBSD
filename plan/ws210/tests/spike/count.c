/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * WS210 p002 spike: compiles one candidate GPU canvas pipeline (a vertex
 * and a fragment SPIR-V module) with the bcm2711 shader compiler on the
 * host and prints its native size: instructions, the no-operation words,
 * registers, threads, uniforms and interface words, for the coordinate,
 * vertex and fragment programs (the fragment with and without the
 * shader's own premultiplied blend).  It only counts; it neither runs the
 * code nor says anything about the hardware's timing.
 *
 * Usage: count NAME VERTEX.spv FRAGMENT.spv
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/gpu/bcm2711/qpu.h"
#include "drivers/gpu/bcm2711/shader.h"
#include "drivers/gpu/compiler/spirv.h"

static int count_load(const char *path, uint32_t **words, size_t *count);
static void count_key(const struct drv_gpu_shader_ir *ir, struct bcm2711_shader_key *key);
static int count_program(const char *name, const char *stage_name, const uint32_t *words, size_t count, enum bcm2711_shader_stage stage, const struct bcm2711_shader_key *key, uint64_t nop);

/* Supplies the kernel allocation API through the host allocator. */
void *
kern_calloc(
	size_t count,
	size_t bytes)
{
	/* The host's own. */
	return calloc(count, bytes);
}

/* Supplies the kernel allocation API through the host allocator. */
void *
kern_malloc(
	size_t bytes)
{
	/* The host's own. */
	return malloc(bytes);
}

/* Supplies the kernel allocation API through the host allocator. */
void *
kern_realloc(
	void *pointer,
	size_t bytes)
{
	/* The host's own. */
	return realloc(pointer, bytes);
}

/* Supplies the kernel allocation API through the host allocator. */
void
kern_free(
	void *pointer)
{
	/* The host's own. */
	free(pointer);
}

/* Compiles the pipeline's programs and prints a line for each. */
int
main(
	int argc,
	char **argv)
{
	struct drv_gpu_compile_diagnostic diagnostic;
	struct bcm2711_qpu_instruction idle;
	struct bcm2711_shader_key key;
	struct drv_gpu_shader_ir *ir;
	uint32_t *vertex;
	uint32_t *fragment;
	size_t vertex_count;
	size_t fragment_count;
	uint64_t nop;
	int failed;
	int error;

	/* A name and the two modules. */
	if (argc != 4) {
		fprintf(stderr, "usage: count NAME VERTEX.spv FRAGMENT.spv\n");
		return 2;
	}
	if (count_load(argv[2], &vertex, &vertex_count) != 0 || count_load(argv[3], &fragment, &fragment_count) != 0)
		return 1;

	/* The word of an instruction that does nothing (both ALUs idle, no signal). */
	memset(&idle, 0, sizeof(idle));
	error = bcm2711_qpu_encode(&idle, &nop);
	if (error != 0) {
		fprintf(stderr, "count: no idle instruction (%d)\n", error);
		return 1;
	}

	/* The varyings the fragment program reads, with their interpolation. */
	error = drv_gpu_shader_parse(fragment, fragment_count, DRV_GPU_STAGE_FRAGMENT, &ir, &diagnostic);
	if (error != 0) {
		fprintf(stderr, "count: %s: fragment refused (%d) at word %u: %s\n", argv[1], error, diagnostic.word_offset, diagnostic.reason);
		return 1;
	}
	count_key(ir, &key);
	drv_gpu_shader_ir_free(ir);

	/* Each program; a refusal is printed and counted. */
	failed = 0;
	failed |= count_program(argv[1], "coordinate", vertex, vertex_count, BCM2711_SHADER_COORDINATE, &key, nop);
	failed |= count_program(argv[1], "vertex", vertex, vertex_count, BCM2711_SHADER_VERTEX, &key, nop);
	failed |= count_program(argv[1], "fragment", fragment, fragment_count, BCM2711_SHADER_FRAGMENT, &key, nop);

	/* Done. */
	free(vertex);
	free(fragment);
	return failed;
}

/* Reads a SPIR-V file into words; nonzero (with a message) when it cannot. */
static int
count_load(
	const char *path,
	uint32_t **words,
	size_t *count)
{
	FILE *file;
	long size;

	/* The whole file. */
	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		return -1;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || (size % 4) != 0 || fseek(file, 0, SEEK_SET) != 0) {
		fprintf(stderr, "count: %s: not a SPIR-V module\n", path);
		fclose(file);
		return -1;
	}
	*words = malloc((size_t)size);
	if (*words == NULL || fread(*words, 1, (size_t)size, file) != (size_t)size) {
		fprintf(stderr, "count: %s: cannot read\n", path);
		free(*words);
		fclose(file);
		return -1;
	}
	fclose(file);

	/* Succeeded. */
	*count = (size_t)size / 4U;
	return 0;
}

/* Makes the pipeline key from the fragment program's inputs, in location order. */
static void
count_key(
	const struct drv_gpu_shader_ir *ir,
	struct bcm2711_shader_key *key)
{
	struct bcm2711_shader_component *varying;
	uint32_t location;
	uint32_t index;
	uint32_t component;

	/* Each scalar of each input, with its interpolation. */
	memset(key, 0, sizeof(*key));
	for (location = 0; location < 16U; location++) {
		for (index = 0; index < ir->input_count; index++) {
			if (ir->inputs[index].location != location)
				continue;
			for (component = 0; component < ir->inputs[index].components; component++) {
				if (key->varying_count >= BCM2711_SHADER_INTERFACE_WORDS)
					return;
				varying = &key->varyings[key->varying_count++];
				varying->location = location;
				varying->component = component;
				varying->flat = ir->inputs[index].flat;
				varying->noperspective = ir->inputs[index].noperspective;
			}
		}
	}
}

/* Compiles one program and prints its counts; nonzero when it is refused. */
static int
count_program(
	const char *name,
	const char *stage_name,
	const uint32_t *words,
	size_t count,
	enum bcm2711_shader_stage stage,
	const struct bcm2711_shader_key *key,
	uint64_t nop)
{
	struct bcm2711_shader_diagnostic diagnostic;
	struct bcm2711_shader_binary *binary;
	uint32_t index;
	uint32_t nops;
	int error;

	/* The production compiler. */
	error = bcm2711_shader_compile(words, count, stage, key, &binary, &diagnostic);
	if (error != 0) {
		printf("%s %s REFUSED error=%d instruction=%u reason=%s\n", name, stage_name, error, diagnostic.instruction,
		       diagnostic.reason != NULL ? diagnostic.reason : "-");
		return 1;
	}

	/* The idle words among its instructions. */
	nops = 0;
	for (index = 0; index < binary->code_count; index++) {
		if (binary->code[index] == nop)
			nops++;
	}

	/* One line. */
	printf("%s %s instructions=%u nop=%u work=%u registers=%u threads=%u uniforms=%u inputs=%u varyings=%u\n", name, stage_name,
	       binary->code_count, nops, binary->code_count - nops, binary->registers_used, binary->threads, binary->uniform_count,
	       binary->input_count, binary->varying_count);
	bcm2711_shader_binary_free(binary);
	return 0;
}
