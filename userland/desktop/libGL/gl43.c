/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libGL's calls of OpenGL 4.3 that its extensions bring to earlier
 * contexts: the vertex attribute binding (GL_ARB_vertex_attrib_binding).
 * Each attribute index is also a binding point (a buffer, an offset, a
 * stride and a divisor); an attribute takes its format and an offset in
 * the elements of the binding point it reads.  glVertexAttribPointer
 * sets both for the attribute's own binding point (draw.c), and every
 * change makes the attributes reading a binding point take its arrays
 * again (gles_vertex_binding_apply).
 */

#include "fixed.h"

/* The largest offset of an attribute in its binding point's elements, and the largest stride. */
#define GL43_RELATIVE_OFFSET_MAX	2047U
#define GL43_STRIDE_MAX			2048

static struct gles_state *gl43_state(struct zegl_context *context, GLuint index);
static int gl43_format(struct zegl_context *context, GLint size, GLenum type, int integer);

/*
 * Binds a buffer object's elements, from an offset and a stride apart, to
 * a vertex buffer binding point (buffer 0: none).
 */
GL_APICALL void GL_APIENTRY
glBindVertexBuffer(
	GLuint bindingindex,
	GLuint buffer,
	GLintptr offset,
	GLsizei stride)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *binding;
	struct gles_buffer *object;

	/* A context with its state and a binding point. */
	context = gles_context();
	state = gl43_state(context, bindingindex);
	if (state == NULL)
		return;

	/* An offset and a stride in range. */
	if (offset < 0 || stride < 0 || stride > GL43_STRIDE_MAX) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The buffer object of the name (made when the name is new). */
	object = NULL;
	if (buffer != 0U) {
		object = gles_buffer_named(context, state, buffer);
		if (object == NULL)
			return;
	}

	/* The binding point's arrays, the stride as it is (0: every element the same). */
	binding = &state->attribs[bindingindex];
	binding->binding_buffer = object;
	binding->binding_offset = offset;
	binding->binding_stride = stride;
	binding->binding_exact = 1;

	/* Succeeded: the attributes reading it take its arrays. */
	gles_vertex_binding_apply(state, bindingindex);
}

/*
 * Sets the format of an attribute's elements, read as floats, and their
 * offset in its binding point's elements.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribFormat(
	GLuint attribindex,
	GLint size,
	GLenum type,
	GLboolean normalized,
	GLuint relativeoffset)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;
	int status;

	/* A context with its state, an attribute, a format and an offset. */
	context = gles_context();
	state = gl43_state(context, attribindex);
	if (state == NULL)
		return;
	status = gl43_format(context, size, type, 0);
	if (status != 0)
		return;
	if (relativeoffset > GL43_RELATIVE_OFFSET_MAX) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The format. */
	attrib = &state->attribs[attribindex];
	attrib->size = size;
	attrib->type = type;
	attrib->normalized = normalized;
	attrib->integer = 0;
	attrib->relative_offset = relativeoffset;

	/* Succeeded: its arrays at the new offset. */
	gles_vertex_binding_apply(state, attrib->binding);
}

/*
 * Sets the format of an attribute's elements, integers the shader reads
 * as they are, and their offset in its binding point's elements.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribIFormat(
	GLuint attribindex,
	GLint size,
	GLenum type,
	GLuint relativeoffset)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_attrib *attrib;
	int status;

	/* A context with its state, an attribute, a format and an offset. */
	context = gles_context();
	state = gl43_state(context, attribindex);
	if (state == NULL)
		return;
	status = gl43_format(context, size, type, 1);
	if (status != 0)
		return;
	if (relativeoffset > GL43_RELATIVE_OFFSET_MAX) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The format. */
	attrib = &state->attribs[attribindex];
	attrib->size = size;
	attrib->type = type;
	attrib->normalized = GL_FALSE;
	attrib->integer = 1;
	attrib->relative_offset = relativeoffset;

	/* Succeeded: its arrays at the new offset. */
	gles_vertex_binding_apply(state, attrib->binding);
}

/*
 * Makes an attribute read the elements of a binding point.
 */
GL_APICALL void GL_APIENTRY
glVertexAttribBinding(
	GLuint attribindex,
	GLuint bindingindex)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state, an attribute and a binding point. */
	context = gles_context();
	state = gl43_state(context, attribindex);
	if (state == NULL)
		return;
	if (bindingindex >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Succeeded: the attribute takes the binding point's arrays. */
	state->attribs[attribindex].binding = bindingindex;
	gles_vertex_binding_apply(state, bindingindex);
}

/*
 * Makes a binding point's elements advance once every divisor instances
 * (0: once per vertex).
 */
GL_APICALL void GL_APIENTRY
glVertexBindingDivisor(
	GLuint bindingindex,
	GLuint divisor)
{
	struct zegl_context *context;
	struct gles_state *state;

	/* A context with its state and a binding point. */
	context = gles_context();
	state = gl43_state(context, bindingindex);
	if (state == NULL)
		return;

	/* Succeeded: the attributes reading it take the divisor. */
	state->attribs[bindingindex].binding_divisor = divisor;
	gles_vertex_binding_apply(state, bindingindex);
}

/*
 * Returns the current context's state for a call on an attribute or a
 * binding point of an index; NULL without a context, or with the error
 * recorded for an index past the last (GL_INVALID_VALUE) or, in a core
 * profile, the default vertex array object (GL_INVALID_OPERATION).
 */
static struct gles_state *
gl43_state(
	struct zegl_context *context,
	GLuint index)
{
	struct gles_state *state;
	GLint profile;

	/* A context with its state. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* An index there is. */
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* A core profile has no default vertex array object to change. */
	profile = gl_context_profile();
	if ((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 && state->vertex_array == 0U) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the state. */
	return state;
}

/*
 * Checks a format of an attribute's elements: a size of 1 to 4 and a type
 * of its kind (floats: every type; integers: the integer types).  Returns
 * 0, or -1 with the error recorded.
 */
static int
gl43_format(
	struct zegl_context *context,
	GLint size,
	GLenum type,
	int integer)
{
	/* A size of 1 to 4. */
	if (size < 1 || size > 4) {
		gles_error(context, GL_INVALID_VALUE);
		return -1;
	}

	/* A type of the kind. */
	switch (type) {
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_INT:
	case GL_UNSIGNED_INT:
		return 0;
	case GL_FIXED:
	case GL_FLOAT:
	case GL_HALF_FLOAT:
		if (integer) {
			gles_error(context, GL_INVALID_ENUM);
			return -1;
		}

		/* A type of floats. */
		return 0;
	case GL_INT_2_10_10_10_REV:
	case GL_UNSIGNED_INT_2_10_10_10_REV:
		if (integer) {
			gles_error(context, GL_INVALID_ENUM);
			return -1;
		}
		if (size != 4) {
			gles_error(context, GL_INVALID_OPERATION);
			return -1;
		}

		/* Four packed components. */
		return 0;
	default:
		break;
	}

	/* Any other type is an error. */
	gles_error(context, GL_INVALID_ENUM);
	return -1;
}
