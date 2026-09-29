/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws075-p021: compiles one SPIR-V module with the i915 executor's compiler on
 * the host and writes the kernel's EU code to a file, for Mesa's disassembler
 * (plan/ws075/tests/guard/run.sh).
 *
 *   shader-dump vertex|fragment FILE.spv OUT.bin
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The kernel's allocator, on the host. */
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

#include "../../../../src/drivers/gpu/i915/compiler/spirv.c"
#include "../../../../src/drivers/gpu/i915/compiler/eu.c"
#include "../../../../src/drivers/gpu/i915/compiler/compile.c"

int
main(
	int argc,
	char **argv)
{
	struct i915_compile_diagnostic diagnostic;
	struct i915_shader_ir *ir;
	struct i915_shader_binary *binary;
	enum i915_shader_stage stage;
	uint32_t *code;
	FILE *file;
	long size;
	int error;

	/* The stage, the module and the output. */
	if (argc != 4) {
		fprintf(stderr, "usage: shader-dump vertex|fragment FILE.spv OUT.bin\n");
		return 2;
	}
	stage = I915_STAGE_VERTEX;
	if (strcmp(argv[1], "fragment") == 0)
		stage = I915_STAGE_FRAGMENT;

	/* Reads the module. */
	file = fopen(argv[2], "rb");
	if (file == NULL)
		return 1;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	code = malloc((size_t)size);
	if (code == NULL || fread(code, 1U, (size_t)size, file) != (size_t)size)
		return 1;
	fclose(file);

	/* Parses and compiles it. */
	memset(&diagnostic, 0, sizeof(diagnostic));
	error = drv_i915_shader_parse(code, (size_t)size / 4U, stage, &ir, &diagnostic);
	if (error != 0) {
		fprintf(stderr, "%s: refused by the parser: %d (%s)\n", argv[2], error, diagnostic.reason != NULL ? diagnostic.reason : "?");
		return 1;
	}
	error = drv_i915_shader_compile(ir, &binary);
	if (error != 0) {
		fprintf(stderr, "%s: refused by the compiler: %d\n", argv[2], error);
		return 1;
	}

	/* Writes the kernel's code. */
	file = fopen(argv[3], "wb");
	if (file == NULL)
		return 1;
	fwrite(binary->code, 1U, binary->code_bytes, file);
	fclose(file);
	printf("%s: %u bytes\n", argv[3], binary->code_bytes);
	return 0;
}
