/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Prints what the driver's first-dispatch check embeds: the code words,
 * the register values and the descriptor places of a compute module,
 * compiled by the amdrdna compiler.
 *
 * usage: amdrdna-dispatch-gen MODULE.spv
 */

#include "drivers/gpu/amdrdna/compiler/gfx10-compile.h"
#include "drivers/gpu/amdrdna/compiler/gfx10-program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_malloc(size_t size);
void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
static uint32_t *read_module(const char *path, size_t *words);

/* The host's allocator stands in for the kernel's. */
void *
kern_malloc(
	size_t size)
{
	return malloc(size);
}

/* The host's zeroing allocator stands in for the kernel's. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	return calloc(count, size);
}

/* The host's free stands in for the kernel's. */
void
kern_free(
	void *pointer)
{
	free(pointer);
}

/* Compiles the module and prints the program. */
int
main(
	int argc,
	char **argv)
{
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic diagnostic;
	struct amdrdna_gfx10_registers registers;
	uint32_t *words;
	size_t count;
	uint32_t index;
	int error;

	/* The module path. */
	if (argc != 2) {
		fprintf(stderr, "usage: %s MODULE.spv\n", argv[0]);
		return 2;
	}

	/* Reads the module. */
	words = read_module(argv[1], &count);
	if (words == NULL) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 1;
	}

	/* Compiles it. */
	program = NULL;
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = amdrdna_gfx10_compile(words, count, DRV_GPU_STAGE_COMPUTE, &program, &diagnostic);
	free(words);
	if (error != 0) {
		fprintf(stderr, "refused at %u: %s\n", diagnostic.instruction, diagnostic.reason);
		return 1;
	}

	/* The register values. */
	amdrdna_gfx10_program_registers(program, &registers);

	/* Prints the description. */
	printf("words %u vgprs %u sgprs %u user %u local %u %u %u shared %u table %u counts 0x%08x inputs 0x%08x\n",
	    program->code_words,
	    program->vgpr_count,
	    program->sgpr_count,
	    program->user_sgprs,
	    program->local_size[0],
	    program->local_size[1],
	    program->local_size[2],
	    program->shared_bytes,
	    program->table_bytes,
	    registers.counts,
	    registers.inputs);
	for (index = 0U; index < program->storage_used; index++) {
		printf("storage set 0x%x binding %u at %u\n",
		    program->storage[index].set,
		    program->storage[index].binding,
		    program->storage[index].table_offset);
	}

	/* Prints the code, four words to a line. */
	for (index = 0U; index < program->code_words; index++) {
		if (index % 4U == 0U)
			printf("\t");
		printf("0x%08xU,", program->code[index]);
		if (index % 4U == 3U || index + 1U == program->code_words)
			printf("\n");
		else
			printf(" ");
	}

	/* Frees the program. */
	amdrdna_gfx10_program_free(program);

	/* Succeeded: the program is printed. */
	return 0;
}

/* Reads a SPIR-V module into memory, its size in words. */
static uint32_t *
read_module(
	const char *path,
	size_t *words)
{
	FILE *file;
	uint32_t *buffer;
	long size;

	/* Opens the file and measures it. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Reads it whole. */
	buffer = malloc((size_t)size);
	if (buffer == NULL) {
		fclose(file);
		return NULL;
	}
	if (fread(buffer, 1U, (size_t)size, file) != (size_t)size) {
		free(buffer);
		fclose(file);
		return NULL;
	}
	fclose(file);

	/* Succeeded: the module's words. */
	*words = (size_t)size / 4U;
	return buffer;
}
