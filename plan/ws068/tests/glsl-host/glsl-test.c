/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws068-p016..p018: runs zedBSD's GLSL compiler on the host.
 *
 *   glsl-test compile vert|frag FILE [DEFAULT_VERSION]
 *       compiles one shader and prints "compiled" or the info log
 *   glsl-test expect vert|frag FILE [DEFAULT_VERSION]
 *       compiles a shader that must fail, and checks the log against the
 *       file's "// expect: TEXT" lines (each TEXT must be in the log)
 *   glsl-test link VERT FRAG OUT_PREFIX [DEFAULT_VERSION] [NAME=LOCATION ...]
 *       compiles and links two shaders, writes OUT_PREFIX.vert.spv and
 *       OUT_PREFIX.frag.spv, and prints the uniforms
 */

#include "../../../../userland/base/libglesv2/glsl/glsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *test_read(const char *path);
static unsigned test_stage(const char *name);
static int test_compile(int argc, char **argv);
static int test_expect(int argc, char **argv);
static int test_link(int argc, char **argv);
static int test_write(const char *path, const uint32_t *code, size_t words);

/*
 * Runs one command.
 */
int
main(
	int argc,
	char **argv)
{
	int status;
	int differs;

	/* A command. */
	if (argc < 2) {
		fprintf(stderr, "usage: glsl-test compile|expect|link ...\n");
		return 2;
	}

	/* The commands. */
	differs = strcmp(argv[1], "compile");
	if (differs == 0) {
		status = test_compile(argc, argv);
		return status;
	}
	differs = strcmp(argv[1], "expect");
	if (differs == 0) {
		status = test_expect(argc, argv);
		return status;
	}
	differs = strcmp(argv[1], "link");
	if (differs == 0) {
		status = test_link(argc, argv);
		return status;
	}

	/* Anything else. */
	fprintf(stderr, "glsl-test: unknown command %s\n", argv[1]);
	return 2;
}

/* Reads a whole file as a string (NULL when it cannot be read). */
static char *
test_read(
	const char *path)
{
	FILE *file;
	char *text;
	long size;
	size_t got;

	/* The file and its size. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	size = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);

	/* The bytes and a terminator. */
	text = malloc((size_t)size + 1U);
	if (text == NULL) {
		(void)fclose(file);
		return NULL;
	}
	got = fread(text, 1U, (size_t)size, file);
	(void)fclose(file);
	text[got] = '\0';

	/* Succeeded: the text. */
	return text;
}

/* Returns the stage a name says. */
static unsigned
test_stage(
	const char *name)
{
	int differs;

	/* "frag" is the fragment stage; anything else the vertex stage. */
	differs = strcmp(name, "frag");
	if (differs == 0)
		return GLSL_STAGE_FRAGMENT;

	/* The vertex stage. */
	return GLSL_STAGE_VERTEX;
}

/* compile STAGE FILE [VERSION]. */
static int
test_compile(
	int argc,
	char **argv)
{
	struct glsl_shader *shader;
	unsigned version;
	char *source;
	char *log;

	/* The arguments and the source. */
	if (argc < 4)
		return 2;
	version = 100U;
	if (argc > 4)
		version = (unsigned)atoi(argv[4]);
	source = test_read(argv[3]);
	if (source == NULL) {
		printf("%s: cannot read\n", argv[3]);
		return 1;
	}

	/* The compile, and its log. */
	shader = glsl_compile(test_stage(argv[2]), source, version, &log);
	free(source);
	if (log != NULL)
		printf("%s", log);
	free(log);
	if (shader == NULL) {
		printf("%s: FAILED\n", argv[3]);
		return 1;
	}

	/* Succeeded. */
	glsl_shader_free(shader);
	printf("%s: compiled\n", argv[3]);
	return 0;
}

/* expect STAGE FILE [VERSION]: the compile must fail with every "// expect:" text in the log. */
static int
test_expect(
	int argc,
	char **argv)
{
	struct glsl_shader *shader;
	unsigned version;
	char *source;
	char *log;
	char *line;
	char *end;
	char want[256];
	size_t length;
	int missing;

	/* The arguments and the source. */
	if (argc < 4)
		return 2;
	version = 100U;
	if (argc > 4)
		version = (unsigned)atoi(argv[4]);
	source = test_read(argv[3]);
	if (source == NULL) {
		printf("%s: cannot read\n", argv[3]);
		return 1;
	}

	/* The compile must fail. */
	shader = glsl_compile(test_stage(argv[2]), source, version, &log);
	if (shader != NULL) {
		printf("%s: compiled, but should have failed\n", argv[3]);
		glsl_shader_free(shader);
		free(log);
		free(source);
		return 1;
	}
	if (log == NULL) {
		printf("%s: failed without a log\n", argv[3]);
		free(source);
		return 1;
	}

	/* Each expected text must be in the log. */
	missing = 0;
	for (line = strstr(source, "// expect: "); line != NULL; line = strstr(line + 1, "// expect: ")) {
		line += strlen("// expect: ");
		end = strchr(line, '\n');
		length = strlen(line);
		if (end != NULL)
			length = (size_t)(end - line);
		if (length >= sizeof(want))
			length = sizeof(want) - 1U;
		memcpy(want, line, length);
		want[length] = '\0';
		if (strstr(log, want) == NULL) {
			printf("%s: the log lacks \"%s\"\n", argv[3], want);
			missing = 1;
		}
	}

	/* The result. */
	if (missing)
		printf("%s", log);
	else
		printf("%s: failed as expected\n", argv[3]);
	free(log);
	free(source);
	return missing;
}

/* link VERT FRAG OUT_PREFIX [VERSION] [NAME=LOCATION ...]. */
static int
test_link(
	int argc,
	char **argv)
{
	struct glsl_binding bindings[16];
	struct glsl_program program;
	struct glsl_shader *vertex;
	struct glsl_shader *fragment;
	unsigned version;
	unsigned count;
	unsigned index;
	char path[1024];
	char *source;
	char *log;
	char *equals;
	int status;
	int argument;

	/* The arguments. */
	if (argc < 5)
		return 2;
	version = 100U;
	if (argc > 5)
		version = (unsigned)atoi(argv[5]);
	count = 0U;
	for (argument = 6; argument < argc && count < 16U; argument++) {
		equals = strchr(argv[argument], '=');
		if (equals == NULL)
			continue;
		*equals = '\0';
		bindings[count].name = argv[argument];
		bindings[count].location = (unsigned)atoi(equals + 1);
		count++;
	}

	/* The two shaders. */
	source = test_read(argv[2]);
	if (source == NULL)
		return 1;
	vertex = glsl_compile(GLSL_STAGE_VERTEX, source, version, &log);
	free(source);
	if (log != NULL)
		printf("%s", log);
	free(log);
	source = test_read(argv[3]);
	if (source == NULL)
		return 1;
	fragment = glsl_compile(GLSL_STAGE_FRAGMENT, source, version, &log);
	free(source);
	if (log != NULL)
		printf("%s", log);
	free(log);
	if (vertex == NULL || fragment == NULL) {
		printf("link: a shader did not compile\n");
		return 1;
	}

	/* The link. */
	status = glsl_link(vertex, fragment, bindings, count, &program, &log);
	glsl_shader_free(vertex);
	glsl_shader_free(fragment);
	if (status != 0) {
		printf("link FAILED: %s", log);
		free(log);
		return 1;
	}

	/* The uniforms. */
	for (index = 0U; index < program.uniform_count; index++) {
		printf("uniform %s base=%u components=%u columns=%u size=%u sampler=%u\n", program.uniforms[index].name,
		       program.uniforms[index].base, program.uniforms[index].components, program.uniforms[index].columns,
		       program.uniforms[index].size, program.uniforms[index].sampler);
	}

	/* The SPIR-V of both stages. */
	(void)snprintf(path, sizeof(path), "%s.vert.spv", argv[4]);
	status = test_write(path, program.code[0], program.words[0]);
	(void)snprintf(path, sizeof(path), "%s.frag.spv", argv[4]);
	status |= test_write(path, program.code[1], program.words[1]);
	glsl_program_free(&program);
	if (status != 0)
		return 1;

	/* Succeeded. */
	printf("linked\n");
	return 0;
}

/* Writes words to a file. */
static int
test_write(
	const char *path,
	const uint32_t *code,
	size_t words)
{
	FILE *file;
	size_t written;

	/* The file. */
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;
	written = fwrite(code, sizeof(uint32_t), words, file);
	(void)fclose(file);
	if (written != words)
		return -1;

	/* Succeeded. */
	return 0;
}
