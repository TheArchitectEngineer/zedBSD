/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's entry for compiling one shader: preprocess, parse
 * and check, with the info log, and the shader kept for the link.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static void glsl_give_log(struct glsl_shader *shader, char **log);

/*
 * Compiles a shader of a stage.  Returns the shader, or NULL with the
 * reasons in *log.
 */
struct glsl_shader *
glsl_compile(
	unsigned stage,
	const char *source,
	unsigned default_version,
	char **log)
{
	struct glsl_shader *shader;

	/* No log until there is something to say. */
	*log = NULL;

	/* The shader, whose arena jumps back here when memory runs out. */
	shader = calloc(1U, sizeof(*shader));
	if (shader == NULL)
		return NULL;
	shader->arena.failure = &shader->failure;
	shader->stage = stage;

	/*
	 * Out of memory and fatal errors come back here.  setjmp is the whole
	 * controlling expression, the one form C allows it in besides a few
	 * comparisons.
	 */
	switch (setjmp(shader->failure)) {
	case 0:
		break;
	default:
		/* The compile ended early: an error was fatal, or an allocation failed (no error counted, no log). */
		glsl_give_log(shader, log);
		glsl_shader_free(shader);
		return NULL;
	}

	/* The three passes, each only after the one before succeeded. */
	glsl_preprocess(shader, source, default_version);
	if (shader->errors == 0U)
		glsl_parse(shader);
	if (shader->errors == 0U)
		glsl_check(shader);

	/* A shader with errors is not kept. */
	glsl_give_log(shader, log);
	if (shader->errors != 0U) {
		glsl_shader_free(shader);
		return NULL;
	}

	/* Succeeded: the checked shader. */
	return shader;
}

/*
 * Frees a compiled shader and everything its compile made.
 */
void
glsl_shader_free(
	struct glsl_shader *shader)
{
	/* Nothing to free. */
	if (shader == NULL)
		return;

	/* The arena, the log and the shader. */
	glsl_arena_free(&shader->arena);
	free(shader->log.data);
	free(shader);
}

/*
 * Reports a compiled shader's stage.
 */
unsigned
glsl_shader_stage(
	const struct glsl_shader *shader)
{
	/* The stage it was compiled for. */
	return shader->stage;
}

/* Hands the shader's log to the caller (NULL when empty), leaving the shader without one. */
static void
glsl_give_log(
	struct glsl_shader *shader,
	char **log)
{
	/* An empty log is no log. */
	if (shader->log.length == 0U)
		return;

	/* The log changes hands. */
	*log = shader->log.data;
	shader->log.data = NULL;
	shader->log.length = 0U;
	shader->log.capacity = 0U;
}
