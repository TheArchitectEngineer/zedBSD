/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libGL's calls of OpenGL 4.0 that its extensions bring to earlier
 * contexts and that the translation's own files do not hold: the indirect
 * draws (GL_ARB_draw_indirect).  A command is read from the buffer bound
 * to GL_DRAW_INDIRECT_BUFFER on the CPU (read back first when the device
 * wrote the buffer) and drawn as the direct call of the same counts;
 * OpenGL 4.0 keeps a command's base instance 0.
 */

#include "fixed.h"

#include <string.h>

/* The words of a DrawArraysIndirectCommand and of a DrawElementsIndirectCommand. */
#define GL40_ARRAYS_WORDS	4U
#define GL40_ELEMENTS_WORDS	5U

static int gl40_command(struct zegl_context *context, const void *indirect, unsigned words, GLuint *command);

/*
 * Draws arrays with the counts of a command in the indirect buffer: its
 * count, instances, first vertex and base instance.
 */
GL_APICALL void GL_APIENTRY
glDrawArraysIndirect(
	GLenum mode,
	const void *indirect)
{
	struct zegl_context *context;
	GLuint command[GL40_ARRAYS_WORDS];
	int status;

	/* The command. */
	context = gles_context();
	status = gl40_command(context, indirect, GL40_ARRAYS_WORDS, command);
	if (status != 0)
		return;

	/* The draw it describes. */
	glDrawArraysInstanced(mode, (GLint)command[2], (GLsizei)command[0], (GLsizei)command[1]);
}

/*
 * Draws elements of the element buffer with the counts of a command in
 * the indirect buffer: its count, instances, first index, base vertex and
 * base instance.
 */
GL_APICALL void GL_APIENTRY
glDrawElementsIndirect(
	GLenum mode,
	GLenum type,
	const void *indirect)
{
	struct zegl_context *context;
	struct gles_state *state;
	GLuint command[GL40_ELEMENTS_WORDS];
	uintptr_t offset;
	int status;

	/* The command, and an element buffer (the indices are its). */
	context = gles_context();
	status = gl40_command(context, indirect, GL40_ELEMENTS_WORDS, command);
	if (status != 0)
		return;
	state = gles_state(context);
	if (state->element_buffer == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The first index's offset in the element buffer, by the index type. */
	offset = (uintptr_t)command[2];
	if (type == GL_UNSIGNED_SHORT)
		offset *= 2U;
	if (type == GL_UNSIGNED_INT)
		offset *= 4U;

	/* The draw it describes (glDrawElementsInstancedBaseVertex checks the type). */
	glDrawElementsInstancedBaseVertex(mode, (GLsizei)command[0], type, (const void *)offset, (GLsizei)command[1],
					  (GLint)command[3]);
}

/*
 * Reads a command of some words from the indirect buffer at an offset (a
 * multiple of 4, the command inside the buffer); the device's writes are
 * read back first.  Returns 0, or -1 with the error recorded (no buffer
 * bound or a command outside it: GL_INVALID_OPERATION; a misaligned
 * offset: GL_INVALID_VALUE).
 */
static int
gl40_command(
	struct zegl_context *context,
	const void *indirect,
	unsigned words,
	GLuint *command)
{
	struct gles_state *state;
	struct gles_buffer *buffer;
	uintptr_t offset;
	int status;

	/* A context with its state, and a buffer bound. */
	state = gles_state(context);
	if (state == NULL)
		return -1;
	buffer = state->draw_indirect_buffer;
	if (buffer == NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* An aligned offset, with the command inside the buffer. */
	offset = (uintptr_t)indirect;
	if ((offset % 4U) != 0U) {
		gles_error(context, GL_INVALID_VALUE);
		return -1;
	}
	if (offset > buffer->size || (size_t)words * 4U > buffer->size - offset) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* The bytes as the device left them (an indirect buffer is often a compute shader's output). */
	status = gles_buffer_fetch(context, buffer);
	if (status != 0) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return -1;
	}

	/* Succeeded: the command's words. */
	memcpy(command, buffer->data + offset, (size_t)words * 4U);
	return 0;
}
