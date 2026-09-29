/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws101-p008: runs zedBSD's GLSL compiler on a compute shader on the host.
 *
 *   glsl-compute link FILE OUT.spv
 *       compiles (GLSL ES 1.00 unless #version says) and links a compute
 *       shader, writes its SPIR-V, and prints its workgroup size and its
 *       storage blocks
 *   glsl-compute expect FILE
 *       compiles and links a shader that must fail, and checks the log
 *       against the file's "// expect: TEXT" lines (each TEXT must be in it)
 */

#include "../../../../userland/desktop/libglesv2/glsl/glsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *test_read(const char *path);
static int test_link(const char *path, const char *out);
static int test_expect(const char *path);

/* Runs one command. */
int
main(
	int argc,
	char **argv)
{
	/* The commands. */
	if (argc == 4 && strcmp(argv[1], "link") == 0)
		return test_link(argv[2], argv[3]);
	if (argc == 3 && strcmp(argv[1], "expect") == 0)
		return test_expect(argv[2]);
	fprintf(stderr, "usage: glsl-compute link FILE OUT.spv | glsl-compute expect FILE\n");
	return 2;
}

/* Reads a whole file into a string the caller frees; NULL when it cannot. */
static char *
test_read(
	const char *path)
{
	FILE *file;
	char *text;
	long size;

	/* The file, measured and read whole. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	text = calloc((size_t)size + 1U, 1U);
	if (text != NULL && fread(text, 1U, (size_t)size, file) != (size_t)size) {
		free(text);
		text = NULL;
	}
	fclose(file);
	return text;
}

/* Compiles and links a compute shader and writes its SPIR-V. */
static int
test_link(
	const char *path,
	const char *out)
{
	struct glsl_program program;
	struct glsl_shader *shader;
	unsigned index;
	char *source;
	char *log;
	FILE *file;
	int status;

	/* The shader. */
	source = test_read(path);
	if (source == NULL) {
		printf("%s: cannot be read\n", path);
		return 1;
	}
	shader = glsl_compile(GLSL_STAGE_COMPUTE, source, 100U, &log);
	free(source);
	if (shader == NULL) {
		printf("%s: FAIL compile:\n%s", path, log != NULL ? log : "(no log)\n");
		free(log);
		return 1;
	}
	free(log);

	/* The link. */
	status = glsl_link_compute(shader, &program, &log);
	glsl_shader_free(shader);
	if (status != 0) {
		printf("%s: FAIL link:\n%s", path, log != NULL ? log : "(no log)\n");
		free(log);
		return 1;
	}

	/* What the link says. */
	printf("%s: local size %u %u %u, %u storage blocks\n", path, program.local_size[0], program.local_size[1],
	       program.local_size[2], program.storage_count);
	for (index = 0U; index < program.storage_count; index++) {
		printf("%s: storage %s binding=%u size=%u array_stride=%u readonly=%u\n", path, program.storages[index].name,
		       program.storages[index].binding, program.storages[index].size, program.storages[index].array_stride,
		       program.storages[index].readonly);
	}

	/* The SPIR-V. */
	file = fopen(out, "wb");
	if (file == NULL) {
		glsl_program_free(&program);
		return 1;
	}
	fwrite(program.code[GLSL_STAGE_COMPUTE], sizeof(uint32_t), program.words[GLSL_STAGE_COMPUTE], file);
	fclose(file);
	glsl_program_free(&program);
	return 0;
}

/* Compiles and links a shader that must fail, and checks the log's expected texts. */
static int
test_expect(
	const char *path)
{
	struct glsl_program program;
	struct glsl_shader *shader;
	const char *line;
	const char *end;
	char want[256];
	char *source;
	char *log;
	size_t length;
	int status;
	int found;

	/* The shader must fail to compile or to link. */
	source = test_read(path);
	if (source == NULL) {
		printf("%s: cannot be read\n", path);
		return 1;
	}
	shader = glsl_compile(GLSL_STAGE_COMPUTE, source, 100U, &log);
	if (shader != NULL) {
		free(log);
		status = glsl_link_compute(shader, &program, &log);
		glsl_shader_free(shader);
		if (status == 0) {
			glsl_program_free(&program);
			printf("%s: FAIL compiled and linked, but must fail\n", path);
			free(source);
			return 1;
		}
	}

	/* Each "// expect: TEXT" must be in the log. */
	status = 0;
	for (line = strstr(source, "// expect: "); line != NULL; line = strstr(end, "// expect: ")) {
		line += strlen("// expect: ");
		end = strchr(line, '\n');
		if (end == NULL)
			end = line + strlen(line);
		length = (size_t)(end - line);
		if (length >= sizeof(want))
			length = sizeof(want) - 1U;
		memcpy(want, line, length);
		want[length] = '\0';
		found = (log != NULL && strstr(log, want) != NULL);
		if (!found) {
			printf("%s: FAIL the log lacks \"%s\":\n%s", path, want, log != NULL ? log : "(no log)\n");
			status = 1;
		}
	}
	if (status == 0)
		printf("%s: fails as expected\n", path);
	free(log);
	free(source);
	return status;
}
