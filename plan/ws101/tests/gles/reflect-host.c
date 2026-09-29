/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws101-p009: libGLESv2's reading of a linked compute shader's SPIR-V on
 * the host.  Each file is compiled and linked by the GLSL compiler as
 * glLinkProgram does (glsl_link_compute), then read by gles_spirv_reflect
 * (spirv.c): the stage must be GLCompute, the storage blocks and shared
 * variables must be left out of the uniforms' interface, and the default
 * block's uniforms and the samplers must be there.  It prints each
 * shader's uniforms, named blocks and storage blocks.
 *
 *   reflect-host FILE.comp...
 */

#include "../../../../userland/desktop/libglesv2/gles.h"
#include "../../../../userland/desktop/libglesv2/glsl/glsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPIR-V's execution model of a compute shader. */
#define REFLECT_MODEL_COMPUTE	5U

static char *reflect_read(const char *path);
static int reflect_file(const char *path);
static void reflect_log_print(char *log);

/* Reads each file; the status is the number of files that failed. */
int
main(
	int argc,
	char **argv)
{
	int failures;
	int index;
	int status;

	/* Each file. */
	failures = 0;
	for (index = 1; index < argc; index++) {
		status = reflect_file(argv[index]);
		if (status != 0)
			failures++;
	}

	/* Reports how many failed. */
	printf("reflect-host: %d of %d failed\n", failures, argc - 1);
	return failures;
}

/* Reads a whole file into a string the caller frees; NULL when it cannot. */
static char *
reflect_read(
	const char *path)
{
	FILE *file;
	char *text;
	size_t done;
	long size;

	/* The file, measured. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Read whole. */
	text = calloc((size_t)size + 1U, 1U);
	if (text == NULL) {
		fclose(file);
		return NULL;
	}

	/* Its bytes, all of them or nothing. */
	done = fread(text, 1U, (size_t)size, file);
	fclose(file);
	if (done != (size_t)size) {
		free(text);
		return NULL;
	}

	/* Succeeded: the text. */
	return text;
}

/* Compiles, links and reads one compute shader; nonzero when a step fails. */
static int
reflect_file(
	const char *path)
{
	struct glsl_program program;
	struct glsl_shader *shader;
	struct gles_spirv spirv;
	char reflect_log[1024];
	unsigned index;
	char *source;
	char *log;
	int status;

	/* The source, compiled. */
	source = reflect_read(path);
	if (source == NULL) {
		printf("%s: FAIL cannot read\n", path);
		return -1;
	}

	/* Compiled as a compute shader. */
	shader = glsl_compile(GLSL_STAGE_COMPUTE, source, 100U, &log);
	free(source);
	if (shader == NULL) {
		printf("%s: FAIL compile\n", path);
		reflect_log_print(log);
		return -1;
	}

	/* Its warnings are not needed. */
	free(log);

	/* Linked alone. */
	status = glsl_link_compute(shader, &program, &log);
	glsl_shader_free(shader);
	if (status != 0) {
		printf("%s: FAIL link\n", path);
		reflect_log_print(log);
		return -1;
	}

	/* Read as libGLESv2 reads it. */
	reflect_log[0] = '\0';
	status = gles_spirv_reflect(program.code[GLSL_STAGE_COMPUTE], program.words[GLSL_STAGE_COMPUTE], &spirv, reflect_log,
				    sizeof(reflect_log));
	if (status != 0) {
		printf("%s: FAIL reflect: %s\n", path, reflect_log);
		glsl_program_free(&program);
		return -1;
	}

	/* The stage, the uniforms, the named blocks and the storage blocks. */
	printf("%s: model=%u uniforms=%u named=%u storages=%u local=%u,%u,%u\n", path, (unsigned)spirv.model,
	       spirv.uniform_count, spirv.named_count, program.storage_count, program.local_size[0], program.local_size[1],
	       program.local_size[2]);
	for (index = 0U; index < spirv.uniform_count; index++) {
		printf("  uniform %s type=0x%x offset=%u binding=%u sampler=%d\n", spirv.uniforms[index].name,
		       (unsigned)spirv.uniforms[index].type, (unsigned)spirv.uniforms[index].offset,
		       (unsigned)spirv.uniforms[index].binding, spirv.uniforms[index].sampler);
	}

	/* The storage blocks the link listed. */
	for (index = 0U; index < program.storage_count; index++) {
		printf("  storage %s binding=%u size=%u stride=%u readonly=%u\n", program.storages[index].name,
		       program.storages[index].binding, program.storages[index].size, program.storages[index].array_stride,
		       program.storages[index].readonly);
	}

	/* A compute shader's. */
	status = 0;
	if (spirv.model != REFLECT_MODEL_COMPUTE) {
		printf("%s: FAIL model %u\n", path, (unsigned)spirv.model);
		status = -1;
	}

	/* What was read goes. */
	gles_spirv_free(&spirv);
	glsl_program_free(&program);
	return status;
}

/* Prints a compiler's log (NULL: none) and frees it. */
static void
reflect_log_print(
	char *log)
{
	/* Nothing to say. */
	if (log == NULL)
		return;

	/* The log, then it goes. */
	printf("%s", log);
	free(log);
}
