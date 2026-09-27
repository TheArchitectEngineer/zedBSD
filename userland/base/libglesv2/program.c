/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Shaders and programs of zedBSD's OpenGL ES (WS068 p008).
 *
 * A shader is GLSL compiled by glCompileShader (glsl/, WS068 p015-p019:
 * checked when compiled, made SPIR-V when the program links, so that the
 * uniform block has one layout in both stages), or SPIR-V given by
 * glShaderBinary.  Linking reads both shaders' interfaces, applies the
 * attribute locations glBindAttribLocation gave (and the output locations
 * desktop GL's glBindFragDataLocation gave, libGL), gives each fragment
 * input the location of the vertex output of the same name, rewrites the
 * vertex shader's gl_Position for Vulkan, merges the two uniform blocks
 * by name, numbers the uniform locations, and makes the Vulkan shaders
 * and layouts.  The uniform values live in a copy of the block that each
 * draw hands to the GPU.
 *
 * Named uniform blocks (OpenGL ES 3, WS068 p024) are read from buffers:
 * the link lists each block at its binding (32 on) with its name, std140
 * size and members (from the GLSL compiler; a SPIR-V binary's blocks have
 * their type's name and no size or members), and a draw hands each block
 * the buffer range bound to the binding point glUniformBlockBinding gave
 * it.
 */

#include "gles.h"
#include "glsl/glsl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The length of a link or compile log. */
#define PROGRAM_LOG		1024U

/* The version of a GLSL source without #version: OpenGL ES's 1.00, desktop GL's 1.10 (libGL). */
#define PROGRAM_ES_VERSION	100U
#define PROGRAM_DESKTOP_VERSION	110U

/* The first version of GLSL ES that needs an OpenGL ES 3 context. */
#define PROGRAM_ES3_VERSION	300U

/* Desktop GL's names of its 1D sampler types, which OpenGL ES has none of. */
#define PROGRAM_SAMPLER_1D		0x8B5DU
#define PROGRAM_SAMPLER_1D_SHADOW	0x8B61U

/* The serial of the next link (0 is never one). */
static uint64_t program_serial = 1U;

static struct gles_shader *program_shader(struct zegl_context *context, GLuint name);
static struct gles_program *program_get(struct zegl_context *context, GLuint name);
static int program_link(struct gles_state *state, struct gles_program *program, char *log);
static int program_link_code(struct gles_state *state, struct gles_program *program, const uint32_t *vertex_input, size_t vertex_words, const uint32_t *fragment_input, size_t fragment_words, const uint32_t *geometry_input, size_t geometry_words, char *log);
static int program_link_geometry(struct gles_state *state, struct gles_program *program, const uint32_t *code, size_t words, char *log);
static int program_link_glsl(struct gles_program *program, struct glsl_program *linked, char *log);
static void program_glsl_captures(struct gles_program *program, const struct glsl_program *linked);
static void program_glsl_types(struct gles_program *program, const struct glsl_program *linked);
static GLenum program_sampler_type(const struct glsl_uniform_info *info);
static int program_buffer_sampler(GLenum type);
static int program_flat_inputs(const uint32_t *code, size_t words);
static int program_glsl_blocks(struct gles_program *program, const struct glsl_program *linked, char *log);
static int program_spirv_blocks(struct gles_program *program, const struct gles_spirv *spirv, unsigned stage, char *log);
static int program_merge(struct gles_program *program, struct gles_spirv *spirv, char *log);
static int program_layout(struct gles_state *state, struct gles_program *program);
static int program_module(struct gles_state *state, const uint32_t *code, size_t words, VkShaderModule *module);
static void program_unlink(struct gles_state *state, struct gles_program *program);
static void program_uniform(GLint location, GLsizei count, unsigned components, const void *values, int integers);
static void program_matrix(GLint location, GLsizei count, GLboolean transpose, const GLfloat *values, unsigned columns, unsigned rows);
static struct gles_uniform *program_location(struct zegl_context *context, GLint location, unsigned *element);
static void program_get_uniform(GLuint name, GLint location, int kind, void *params);
static struct gles_block *program_block(struct zegl_context *context, struct gles_program *program, GLuint index);
static int program_uniform_property(const struct gles_uniform *uniform, GLenum pname, GLint *value);
static void program_copy_string(const char *text, GLsizei size, GLsizei *length, GLchar *out);
static void program_log(char **log, const char *text);

/*
 * Frees a program's link and the program; shaders it had attached are let
 * go (and freed when they were waiting for that).
 */
void
gles_program_release(
	struct gles_state *state,
	struct gles_program *program)
{
	/* The link's objects. */
	program_unlink(state, program);

	/* The attached shaders let go. */
	if (program->vertex != NULL) {
		program->vertex->attached--;
		if (program->vertex->delete_pending && program->vertex->attached == 0U) {
			gles_names_remove(&state->objects, program->vertex->name);
			gles_shader_release(program->vertex);
		}
	}

	/* The fragment shader too. */
	if (program->fragment != NULL) {
		program->fragment->attached--;
		if (program->fragment->delete_pending && program->fragment->attached == 0U) {
			gles_names_remove(&state->objects, program->fragment->name);
			gles_shader_release(program->fragment);
		}
	}

	/* And the geometry shader. */
	if (program->geometry != NULL) {
		program->geometry->attached--;
		if (program->geometry->delete_pending && program->geometry->attached == 0U) {
			gles_names_remove(&state->objects, program->geometry->name);
			gles_shader_release(program->geometry);
		}
	}

	/* The program. */
	free(program->log);
	free(program);
}

/*
 * Frees a shader.
 */
void
gles_shader_release(
	struct gles_shader *shader)
{
	/* The code, the source, the compiled GLSL, the log and the shader. */
	free(shader->code);
	free(shader->source);
	glsl_shader_free(shader->glsl);
	free(shader->log);
	free(shader);
}

/*
 * Makes a shader of a stage.
 */
GL_APICALL GLuint GL_APIENTRY
glCreateShader(
	GLenum type)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_shader *shader;
	int status;

	/* A context with its state, and a stage. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return 0U;
	if (type != GL_VERTEX_SHADER &&
	    type != GL_FRAGMENT_SHADER &&
	    (type != GL_GEOMETRY_SHADER || gles_fixed == NULL)) {
		gles_error(context, GL_INVALID_ENUM);
		return 0U;
	}

	/* The shader under a free name of the shared namespace. */
	shader = calloc(1U, sizeof(*shader));
	if (shader == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return 0U;
	}

	/* The shader under the name. */
	shader->kind = GLES_KIND_SHADER;
	shader->type = type;
	shader->name = gles_names_free(&state->objects);
	status = gles_names_add(&state->objects, shader->name, shader);
	if (status != 0) {
		free(shader);
		gles_error(context, GL_OUT_OF_MEMORY);
		return 0U;
	}

	/* Succeeded: its name. */
	return shader->name;
}

/*
 * Deletes a shader, once no program has it attached.
 */
GL_APICALL void GL_APIENTRY
glDeleteShader(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_shader *shader;

	/* Name 0 is ignored. */
	if (name == 0U)
		return;

	/* The shader. */
	context = gles_context();
	state = gles_state(context);
	shader = program_shader(context, name);
	if (shader == NULL)
		return;

	/* Attached: it waits; else it goes now. */
	shader->delete_pending = 1;
	if (shader->attached != 0U)
		return;
	gles_names_remove(&state->objects, name);
	gles_shader_release(shader);
}

/*
 * Gives a shader its GLSL source (kept for glGetShaderSource and
 * glCompileShader).
 */
GL_APICALL void GL_APIENTRY
glShaderSource(
	GLuint name,
	GLsizei count,
	const GLchar *const *string,
	const GLint *length)
{
	struct zegl_context *context;
	struct gles_shader *shader;
	char *source;
	size_t total;
	size_t part;
	GLsizei index;

	/* The shader and a count. */
	context = gles_context();
	shader = program_shader(context, name);
	if (shader == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The parts' total length. */
	total = 0U;
	for (index = 0; index < count; index++) {
		if (length != NULL && length[index] >= 0) {
			total += (size_t)length[index];
		} else {
			total += strlen(string[index]);
		}
	}

	/* The parts joined. */
	source = malloc(total + 1U);
	if (source == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return;
	}

	/* The parts, one after another. */
	total = 0U;
	for (index = 0; index < count; index++) {
		if (length != NULL && length[index] >= 0) {
			part = (size_t)length[index];
		} else {
			part = strlen(string[index]);
		}

		/* The part. */
		memcpy(source + total, string[index], part);
		total += part;
	}

	/* Succeeded: the source replaces the old one. */
	source[total] = '\0';
	free(shader->source);
	shader->source = source;
}

/*
 * Compiles a shader's GLSL source (a shader given only a SPIR-V binary
 * stays compiled).  The log says what the compiler found.
 */
GL_APICALL void GL_APIENTRY
glCompileShader(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_shader *shader;
	struct glsl_shader *compiled;
	unsigned stage;
	unsigned version;
	unsigned shader_version;
	unsigned latest;
	char message[96];
	int es;
	char *log;

	/* The shader. */
	context = gles_context();
	shader = program_shader(context, name);
	if (shader == NULL)
		return;

	/* Without a source, a binary already given stays compiled. */
	if (shader->source == NULL) {
		shader->compiled = 0;
		if (shader->code != NULL)
			shader->compiled = 1;
		program_log(&shader->log, "");
		if (shader->code == NULL)
			program_log(&shader->log, "the shader has no source\n");
		return;
	}

	/* The source compiled for the shader's stage, in OpenGL ES's GLSL or (libGL) desktop GL's. */
	stage = GLSL_STAGE_VERTEX;
	if (shader->type == GL_FRAGMENT_SHADER)
		stage = GLSL_STAGE_FRAGMENT;
	if (shader->type == GL_GEOMETRY_SHADER)
		stage = GLSL_STAGE_GEOMETRY;
	version = PROGRAM_ES_VERSION;
	if (gles_fixed != NULL)
		version = PROGRAM_DESKTOP_VERSION;
	compiled = glsl_compile(stage, shader->source, version, &log);

	/* The log (errors, or the warnings of a shader that compiled). */
	if (log != NULL) {
		program_log(&shader->log, log);
		free(log);
	} else if (compiled == NULL) {
		program_log(&shader->log, "the compiler ran out of memory\n");
	} else {
		program_log(&shader->log, "");
	}

	/* A desktop GLSL version no later than the context's (libGL). */
	if (compiled != NULL && gles_fixed != NULL) {
		shader_version = glsl_shader_version(compiled, &es);
		latest = gles_fixed->glsl_version();
		if (!es && latest != 0U && shader_version > latest) {
			(void)snprintf(message, sizeof(message), "0:0: error: GLSL %u is later than the context's %u\n", shader_version,
				       latest);
			program_log(&shader->log, message);
			glsl_shader_free(compiled);
			compiled = NULL;
		}
	}

	/* GLSL ES 3.00 needs an OpenGL ES 3 context (EGL_CONTEXT_CLIENT_VERSION 3). */
	if (compiled != NULL && gles_fixed == NULL) {
		shader_version = glsl_shader_version(compiled, &es);
		if (es && shader_version >= PROGRAM_ES3_VERSION && context->version < 3) {
			program_log(&shader->log, "0:0: error: GLSL ES 3.00 needs an OpenGL ES 3 context\n");
			glsl_shader_free(compiled);
			compiled = NULL;
		}
	}

	/* A failed compile leaves the shader without code. */
	glsl_shader_free(shader->glsl);
	shader->glsl = compiled;
	if (compiled == NULL) {
		shader->compiled = 0;
		return;
	}

	/* Succeeded: the GLSL replaces any binary. */
	free(shader->code);
	shader->code = NULL;
	shader->words = 0U;
	shader->compiled = 1;
}

/*
 * Gives shaders a binary: SPIR-V, the one format offered.
 */
GL_APICALL void GL_APIENTRY
glShaderBinary(
	GLsizei count,
	const GLuint *shaders,
	GLenum binaryformat,
	const void *binary,
	GLsizei length)
{
	struct zegl_context *context;
	struct gles_shader *shader;
	uint32_t *code;
	GLsizei index;

	/* A context, the format, and whole words. */
	context = gles_context();
	if (context == NULL)
		return;
	if (binaryformat != GL_SHADER_BINARY_FORMAT_SPIR_V) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* Whole words, at least a header. */
	if (count < 0 || length < 20 || (length % 4) != 0 || binary == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each shader gets its own copy. */
	for (index = 0; index < count; index++) {
		shader = program_shader(context, shaders[index]);
		if (shader == NULL)
			return;
		code = malloc((size_t)length);
		if (code == NULL) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* The words replace any earlier ones (and compiled GLSL); the shader counts as compiled. */
		memcpy(code, binary, (size_t)length);
		free(shader->code);
		glsl_shader_free(shader->glsl);
		shader->glsl = NULL;
		shader->code = code;
		shader->words = (size_t)length / 4U;
		shader->compiled = 1;
		program_log(&shader->log, "");
	}
}

/*
 * Lets the compiler's resources go (it keeps none between compiles).
 */
GL_APICALL void GL_APIENTRY
glReleaseShaderCompiler(void)
{
	/* Nothing is held. */
	return;
}

/*
 * Reports a property of a shader.
 */
GL_APICALL void GL_APIENTRY
glGetShaderiv(
	GLuint name,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_shader *shader;

	/* The shader. */
	context = gles_context();
	shader = program_shader(context, name);
	if (shader == NULL)
		return;

	/* The property asked for. */
	switch (pname) {
	case GL_SHADER_TYPE:
		*params = (GLint)shader->type;
		return;
	case GL_DELETE_STATUS:
		*params = shader->delete_pending;
		return;
	case GL_COMPILE_STATUS:
		*params = shader->compiled;
		return;
	case GL_INFO_LOG_LENGTH:
		*params = 0;
		if (shader->log != NULL && shader->log[0] != '\0')
			*params = (GLint)strlen(shader->log) + 1;
		return;
	case GL_SHADER_SOURCE_LENGTH:
		*params = 0;
		if (shader->source != NULL)
			*params = (GLint)strlen(shader->source) + 1;
		return;
	default:
		break;
	}

	/* Any other is an error. */
	gles_error(context, GL_INVALID_ENUM);
}

/*
 * Copies a shader's log.
 */
GL_APICALL void GL_APIENTRY
glGetShaderInfoLog(
	GLuint name,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *infoLog)
{
	struct zegl_context *context;
	struct gles_shader *shader;

	/* The shader's log (empty when none). */
	context = gles_context();
	shader = program_shader(context, name);
	if (shader == NULL)
		return;
	program_copy_string(shader->log, bufSize, length, infoLog);
}

/*
 * Copies a shader's source.
 */
GL_APICALL void GL_APIENTRY
glGetShaderSource(
	GLuint name,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *source)
{
	struct zegl_context *context;
	struct gles_shader *shader;

	/* The shader's source (empty when none). */
	context = gles_context();
	shader = program_shader(context, name);
	if (shader == NULL)
		return;
	program_copy_string(shader->source, bufSize, length, source);
}

/*
 * Reports the range and precision of a shader precision: every precision
 * is a 32-bit float or int.
 */
GL_APICALL void GL_APIENTRY
glGetShaderPrecisionFormat(
	GLenum shadertype,
	GLenum precisiontype,
	GLint *range,
	GLint *precision)
{
	/* IEEE single precision for floats, 32-bit two's complement for ints. */
	(void)shadertype;
	range[0] = 127;
	range[1] = 127;
	*precision = 23;
	if (precisiontype == GL_LOW_INT || precisiontype == GL_MEDIUM_INT || precisiontype == GL_HIGH_INT) {
		range[0] = 31;
		range[1] = 30;
		*precision = 0;
	}
}

/*
 * Reports whether a name is a shader.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsShader(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_shader *shader;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return GL_FALSE;

	/* The name's object, if a shader. */
	shader = gles_names_get(&state->objects, name);
	if (shader == NULL || shader->kind != GLES_KIND_SHADER)
		return GL_FALSE;
	return GL_TRUE;
}

/*
 * Makes a program.
 */
GL_APICALL GLuint GL_APIENTRY
glCreateProgram(void)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	int status;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return 0U;

	/* The program under a free name of the shared namespace. */
	program = calloc(1U, sizeof(*program));
	if (program == NULL) {
		gles_error(context, GL_OUT_OF_MEMORY);
		return 0U;
	}

	/* The program under the name, capturing interleaved outputs by default. */
	program->kind = GLES_KIND_PROGRAM;
	program->feedback_mode = GL_INTERLEAVED_ATTRIBS;
	program->name = gles_names_free(&state->objects);
	status = gles_names_add(&state->objects, program->name, program);
	if (status != 0) {
		free(program);
		gles_error(context, GL_OUT_OF_MEMORY);
		return 0U;
	}

	/* Succeeded: its name. */
	return program->name;
}

/*
 * Deletes a program, once it is no longer current.
 */
GL_APICALL void GL_APIENTRY
glDeleteProgram(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;

	/* Name 0 is ignored. */
	if (name == 0U)
		return;

	/* The program. */
	context = gles_context();
	state = gles_state(context);
	program = program_get(context, name);
	if (program == NULL)
		return;

	/* Current: it waits; else it goes now. */
	program->delete_pending = 1;
	if (state->program == program)
		return;
	gles_names_remove(&state->objects, name);
	gles_program_release(state, program);
}

/*
 * Attaches a shader to a program.
 */
GL_APICALL void GL_APIENTRY
glAttachShader(
	GLuint program_name,
	GLuint shader_name)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_shader *shader;
	struct gles_shader **slot;

	/* The program and the shader. */
	context = gles_context();
	program = program_get(context, program_name);
	if (program == NULL)
		return;
	shader = program_shader(context, shader_name);
	if (shader == NULL)
		return;

	/* Its stage's slot, which must be empty. */
	slot = &program->fragment;
	if (shader->type == GL_VERTEX_SHADER)
		slot = &program->vertex;
	if (shader->type == GL_GEOMETRY_SHADER)
		slot = &program->geometry;
	if (*slot != NULL) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Attached. */
	*slot = shader;
	shader->attached++;
}

/*
 * Detaches a shader from a program.
 */
GL_APICALL void GL_APIENTRY
glDetachShader(
	GLuint program_name,
	GLuint shader_name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	struct gles_shader *shader;

	/* The program and the shader. */
	context = gles_context();
	state = gles_state(context);
	program = program_get(context, program_name);
	if (program == NULL)
		return;
	shader = program_shader(context, shader_name);
	if (shader == NULL)
		return;

	/* It must be attached. */
	if (program->vertex == shader) {
		program->vertex = NULL;
	} else if (program->fragment == shader) {
		program->fragment = NULL;
	} else if (program->geometry == shader) {
		program->geometry = NULL;
	} else {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Let go; a deleted shader goes with its last program. */
	shader->attached--;
	if (shader->delete_pending && shader->attached == 0U) {
		gles_names_remove(&state->objects, shader_name);
		gles_shader_release(shader);
	}
}

/*
 * Links a program's shaders.
 */
GL_APICALL void GL_APIENTRY
glLinkProgram(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	char log[PROGRAM_LOG];
	int status;

	/* The program. */
	context = gles_context();
	state = gles_state(context);
	program = program_get(context, name);
	if (program == NULL)
		return;

	/* The earlier link goes, then the new one. */
	program_unlink(state, program);
	log[0] = '\0';
	status = program_link(state, program, log);
	if (status != 0)
		program_unlink(state, program);

	/* The result and its log. */
	program->linked = 0;
	if (status == 0)
		program->linked = 1;
	program_log(&program->log, log);
}

/*
 * Makes a program current (0: none), letting a deleted one go.
 */
GL_APICALL void GL_APIENTRY
glUseProgram(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;
	struct gles_program *previous;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return;

	/* The program, which must be linked. */
	program = NULL;
	if (name != 0U) {
		program = program_get(context, name);
		if (program == NULL)
			return;
		if (!program->linked) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}
	}

	/* Current; the one before goes if it was deleted. */
	previous = state->program;
	state->program = program;
	if (previous != NULL && previous != program && previous->delete_pending) {
		gles_names_remove(&state->objects, previous->name);
		gles_program_release(state, previous);
	}
}

/*
 * Checks a program for the current state: a linked program is valid.
 */
GL_APICALL void GL_APIENTRY
glValidateProgram(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_program *program;

	/* The program; its link says it all. */
	context = gles_context();
	program = program_get(context, name);
	(void)program;
}

/*
 * Reports a property of a program.
 */
GL_APICALL void GL_APIENTRY
glGetProgramiv(
	GLuint name,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned index;
	GLint longest;
	GLint length;

	/* The program. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;

	/* The property asked for. */
	switch (pname) {
	case GL_DELETE_STATUS:
		*params = program->delete_pending;
		return;
	case GL_LINK_STATUS:
	case GL_VALIDATE_STATUS:
		*params = program->linked;
		return;
	case GL_INFO_LOG_LENGTH:
		*params = 0;
		if (program->log != NULL && program->log[0] != '\0')
			*params = (GLint)strlen(program->log) + 1;
		return;
	case GL_ATTACHED_SHADERS:
		*params = 0;
		if (program->vertex != NULL)
			*params += 1;
		if (program->fragment != NULL)
			*params += 1;
		if (program->geometry != NULL)
			*params += 1;
		return;
	case GL_GEOMETRY_VERTICES_OUT:
	case GL_GEOMETRY_INPUT_TYPE:
	case GL_GEOMETRY_OUTPUT_TYPE:
		/* A linked program's geometry shader's (desktop GL, libGL). */
		if (gles_fixed == NULL) {
			gles_error(context, GL_INVALID_ENUM);
			return;
		}

		/* Linked with a geometry shader. */
		if (!program->linked || program->geometry_module == VK_NULL_HANDLE) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}

		/* The most vertices, or a primitive. */
		*params = program->geometry_vertices;
		if (pname == GL_GEOMETRY_INPUT_TYPE)
			*params = (GLint)program->geometry_input;
		if (pname == GL_GEOMETRY_OUTPUT_TYPE)
			*params = (GLint)program->geometry_output;
		return;
	case GL_ACTIVE_ATTRIBUTES:
		*params = (GLint)program->attribute_count;
		return;
	case GL_ACTIVE_UNIFORMS:
		*params = (GLint)program->uniform_count;
		return;
	case GL_ACTIVE_UNIFORM_BLOCKS:
		*params = (GLint)program->block_count;
		return;
	case GL_TRANSFORM_FEEDBACK_VARYINGS:
		*params = (GLint)program->capture_count;
		return;
	case GL_TRANSFORM_FEEDBACK_BUFFER_MODE:
		*params = (GLint)program->capture_mode;
		if (program->capture_count == 0U)
			*params = (GLint)program->feedback_mode;
		return;
	case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
	case GL_ACTIVE_UNIFORM_MAX_LENGTH:
	case GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH:
	case GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH:
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The longest name with its terminator (and "[0]" for arrays). */
	longest = 0;
	if (pname == GL_TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH) {
		for (index = 0U; index < program->capture_count; index++) {
			length = (GLint)strlen(program->captures[index].name) + 1;
			if (length > longest)
				longest = length;
		}
	} else if (pname == GL_ACTIVE_ATTRIBUTE_MAX_LENGTH) {
		for (index = 0U; index < program->attribute_count; index++) {
			length = (GLint)strlen(program->attributes[index].name) + 1;
			if (length > longest)
				longest = length;
		}
	} else if (pname == GL_ACTIVE_UNIFORM_MAX_LENGTH) {
		for (index = 0U; index < program->uniform_count; index++) {
			length = (GLint)strlen(program->uniforms[index].name) + 4;
			if (length > longest)
				longest = length;
		}
	} else {
		for (index = 0U; index < program->block_count; index++) {
			length = (GLint)strlen(program->blocks[index].name) + 1;
			if (length > longest)
				longest = length;
		}
	}

	/* The longest. */
	*params = longest;
}

/*
 * Copies a program's link log.
 */
GL_APICALL void GL_APIENTRY
glGetProgramInfoLog(
	GLuint name,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *infoLog)
{

	struct zegl_context *context;
	struct gles_program *program;

	/* The program's log (empty when none). */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	program_copy_string(program->log, bufSize, length, infoLog);
}

/*
 * Reports whether a name is a program.
 */
GL_APICALL GLboolean GL_APIENTRY
glIsProgram(
	GLuint name)
{
	struct zegl_context *context;
	struct gles_state *state;
	struct gles_program *program;

	/* A context with its state. */
	context = gles_context();
	state = gles_state(context);
	if (state == NULL)
		return GL_FALSE;

	/* The name's object, if a program. */
	program = gles_names_get(&state->objects, name);
	if (program == NULL || program->kind != GLES_KIND_PROGRAM)
		return GL_FALSE;
	return GL_TRUE;
}

/*
 * Reports the shaders attached to a program.
 */
GL_APICALL void GL_APIENTRY
glGetAttachedShaders(
	GLuint name,
	GLsizei maxCount,
	GLsizei *count,
	GLuint *shaders)
{
	struct zegl_context *context;
	struct gles_program *program;
	GLsizei found;

	/* The program. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;

	/* The vertex shader, then the fragment shader, as many as fit. */
	found = 0;
	if (program->vertex != NULL && found < maxCount)
		shaders[found++] = program->vertex->name;
	if (program->fragment != NULL && found < maxCount)
		shaders[found++] = program->fragment->name;
	if (program->geometry != NULL && found < maxCount)
		shaders[found++] = program->geometry->name;
	if (count != NULL)
		*count = found;
}

/*
 * Gives an attribute a location for the program's next link.
 */
GL_APICALL void GL_APIENTRY
glBindAttribLocation(
	GLuint name,
	GLuint index,
	const GLchar *attribute)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned slot;
	int differs;

	/* The program and a location it can have. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (index >= GLES_ATTRIBS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The name's earlier binding, else a new one. */
	for (slot = 0U; slot < program->bound_count; slot++) {
		differs = strcmp(program->bound_names[slot], attribute);
		if (differs == 0)
			break;
	}

	/* A new binding when the name had none. */
	if (slot == program->bound_count) {
		if (program->bound_count == GLES_ATTRIBS) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* One more binding. */
		program->bound_count++;
	}

	/* Recorded for the next link. */
	(void)snprintf(program->bound_names[slot], GLES_NAME, "%s", attribute);
	program->bound_locations[slot] = index;
}

/*
 * Returns the location of an active attribute, or -1.
 */
GL_APICALL GLint GL_APIENTRY
glGetAttribLocation(
	GLuint name,
	const GLchar *attribute)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned index;
	int differs;

	/* A linked program. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return -1;
	if (!program->linked) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* The attribute of that name. */
	for (index = 0U; index < program->attribute_count; index++) {
		differs = strcmp(program->attributes[index].name, attribute);
		if (differs == 0)
			return (GLint)program->attributes[index].location;
	}

	/* None. */
	return -1;
}

/*
 * Gives a fragment shader output a draw buffer for the program's next
 * link (desktop GL 3.0, libGL).
 */
GL_APICALL void GL_APIENTRY
glBindFragDataLocation(
	GLuint name,
	GLuint color,
	const GLchar *output)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned slot;
	int differs;

	/* The program, and a draw buffer. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (color >= GLES_DRAW_BUFFERS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A name that is not a built-in's. */
	differs = strncmp(output, "gl_", 3U);
	if (differs == 0) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The name's earlier binding, else a new one. */
	for (slot = 0U; slot < program->frag_bound_count; slot++) {
		differs = strcmp(program->frag_bound_names[slot], output);
		if (differs == 0)
			break;
	}

	/* A new binding when the name had none. */
	if (slot == program->frag_bound_count) {
		if (program->frag_bound_count == GLES_DRAW_BUFFERS) {
			gles_error(context, GL_OUT_OF_MEMORY);
			return;
		}

		/* One more binding. */
		program->frag_bound_count++;
	}

	/* Recorded for the next link. */
	(void)snprintf(program->frag_bound_names[slot], GLES_NAME, "%s", output);
	program->frag_bound_locations[slot] = color;
}

/*
 * Returns the location of a fragment shader output of a linked program,
 * or -1.
 */
GL_APICALL GLint GL_APIENTRY
glGetFragDataLocation(
	GLuint name,
	const GLchar *output)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned index;
	int differs;

	/* A linked program. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return -1;
	if (!program->linked) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* The output of that name. */
	for (index = 0U; index < program->output_count; index++) {
		differs = strcmp(program->output_names[index], output);
		if (differs == 0)
			return program->output_locations[index];
	}

	/* None. */
	return -1;
}

/*
 * Sets a program parameter: GL_PROGRAM_BINARY_RETRIEVABLE_HINT is taken
 * and has no effect (no binary format is offered).
 */
GL_APICALL void GL_APIENTRY
glProgramParameteri(
	GLuint name,
	GLenum pname,
	GLint value)
{
	struct zegl_context *context;
	struct gles_program *program;

	/* A program, the one parameter, and a boolean. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (pname != GL_PROGRAM_BINARY_RETRIEVABLE_HINT) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* A boolean. */
	if (value != GL_FALSE && value != GL_TRUE)
		gles_error(context, GL_INVALID_VALUE);
}

/*
 * Refuses to give a program's binary: GL_NUM_PROGRAM_BINARY_FORMATS is 0.
 */
GL_APICALL void GL_APIENTRY
glGetProgramBinary(
	GLuint name,
	GLsizei bufSize,
	GLsizei *length,
	GLenum *binaryFormat,
	void *binary)
{
	struct zegl_context *context;
	struct gles_program *program;

	/* A program; its binary has no bytes. */
	(void)bufSize;
	(void)binaryFormat;
	(void)binary;
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (length != NULL)
		*length = 0;
	gles_error(context, GL_INVALID_OPERATION);
}

/*
 * Refuses a program binary: no binary format is offered.
 */
GL_APICALL void GL_APIENTRY
glProgramBinary(
	GLuint name,
	GLenum binaryFormat,
	const void *binary,
	GLsizei length)
{
	struct zegl_context *context;
	struct gles_program *program;

	/* A program; no format is one. */
	(void)binaryFormat;
	(void)binary;
	(void)length;
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	gles_error(context, GL_INVALID_ENUM);
}

/*
 * Names the outputs the program's vertex shader captures at its next link
 * (transform feedback), and whether they go into one buffer interleaved
 * or each into its own.
 */
GL_APICALL void GL_APIENTRY
glTransformFeedbackVaryings(
	GLuint name,
	GLsizei count,
	const GLchar *const *varyings,
	GLenum bufferMode)
{
	struct zegl_context *context;
	struct gles_program *program;
	GLsizei index;

	/* A program, a buffer mode, and no more names than there are places for. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (bufferMode != GL_INTERLEAVED_ATTRIBS && bufferMode != GL_SEPARATE_ATTRIBS) {
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* No more names than there are places for. */
	if (count < 0 || count > (GLsizei)GLES_CAPTURES) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Separate outputs each have a binding point. */
	if (bufferMode == GL_SEPARATE_ATTRIBS && count > (GLsizei)GLES_FEEDBACK_BINDINGS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Kept for the next link. */
	for (index = 0; index < count; index++)
		(void)snprintf(program->feedback_names[index], GLES_NAME, "%s", varyings[index]);
	program->feedback_count = (unsigned)count;
	program->feedback_mode = bufferMode;
}

/*
 * Reports one of the outputs a linked program captures.
 */
GL_APICALL void GL_APIENTRY
glGetTransformFeedbackVarying(
	GLuint name,
	GLuint index,
	GLsizei bufSize,
	GLsizei *length,
	GLsizei *size,
	GLenum *type,
	GLchar *varying)
{
	struct zegl_context *context;
	struct gles_program *program;
	const struct gles_capture *capture;
	size_t written;

	/* A linked program and one of its captured outputs. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (!program->linked) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* One of its captured outputs. */
	if (index >= program->capture_count) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Its size and type. */
	capture = &program->captures[index];
	*size = capture->size;
	*type = capture->type;

	/* Its name, cut to the room given. */
	written = 0U;
	if (bufSize > 0 && varying != NULL) {
		(void)snprintf(varying, (size_t)bufSize, "%s", capture->name);
		written = strlen(varying);
	}

	/* The name's length. */
	if (length != NULL)
		*length = (GLsizei)written;
}

/*
 * Reports one of a program's active attributes.
 */
GL_APICALL void GL_APIENTRY
glGetActiveAttrib(
	GLuint name,
	GLuint index,
	GLsizei bufSize,
	GLsizei *length,
	GLint *size,
	GLenum *type,
	GLchar *attribute)
{
	struct zegl_context *context;
	struct gles_program *program;

	/* The program and an attribute it has. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (index >= program->attribute_count) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Its size, type and name. */
	*size = program->attributes[index].size;
	*type = program->attributes[index].type;
	program_copy_string(program->attributes[index].name, bufSize, length, attribute);
}

/*
 * Reports one of a program's active uniforms.
 */
GL_APICALL void GL_APIENTRY
glGetActiveUniform(
	GLuint name,
	GLuint index,
	GLsizei bufSize,
	GLsizei *length,
	GLint *size,
	GLenum *type,
	GLchar *uniform)
{
	struct zegl_context *context;
	struct gles_program *program;
	char full[GLES_NAME + 4U];

	/* The program and a uniform it has. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (index >= program->uniform_count) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Its size, type and name (an array's with "[0]"). */
	*size = program->uniforms[index].size;
	*type = program->uniforms[index].type;
	(void)snprintf(full, sizeof(full), "%s", program->uniforms[index].name);
	if (program->uniforms[index].size > 1)
		(void)snprintf(full, sizeof(full), "%s[0]", program->uniforms[index].name);
	program_copy_string(full, bufSize, length, uniform);
}

/*
 * Returns the location of a uniform (or of an element: "name[i]"), or -1.
 */
GL_APICALL GLint GL_APIENTRY
glGetUniformLocation(
	GLuint name,
	const GLchar *uniform)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_uniform *entry;
	char base[GLES_NAME];
	const char *bracket;
	unsigned long element;
	char *end;
	size_t length;
	unsigned index;
	int differs;

	/* A linked program. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return -1;
	if (!program->linked) {
		gles_error(context, GL_INVALID_OPERATION);
		return -1;
	}

	/* The name without a last "[i]", and i. */
	element = 0UL;
	length = strlen(uniform);
	bracket = strrchr(uniform, '[');
	if (bracket != NULL && length > 0U && uniform[length - 1U] == ']') {
		element = strtoul(bracket + 1, &end, 10);
		if (end != uniform + length - 1U)
			return -1;
		length = (size_t)(bracket - uniform);
	}

	/* A name that fits. */
	if (length >= sizeof(base))
		return -1;
	memcpy(base, uniform, length);
	base[length] = '\0';

	/* The uniform of that name (a named block's members have no location), and an element it has. */
	for (index = 0U; index < program->uniform_count; index++) {
		entry = &program->uniforms[index];
		if (entry->block >= 0)
			continue;
		differs = strcmp(entry->name, base);
		if (differs != 0)
			continue;
		if (element >= (unsigned long)entry->size)
			return -1;
		return entry->location + (GLint)element;
	}

	/* None. */
	return -1;
}

/* The glUniform calls: each writes values of a size into the current program's uniform at a location. */

GL_APICALL void GL_APIENTRY
glUniform1f(
	GLint location,
	GLfloat v0)
{
	/* One float. */
	program_uniform(location, 1, 1U, &v0, 0);
}

GL_APICALL void GL_APIENTRY
glUniform2f(
	GLint location,
	GLfloat v0,
	GLfloat v1)
{
	GLfloat values[2];

	/* Two floats. */
	values[0] = v0;
	values[1] = v1;
	program_uniform(location, 1, 2U, values, 0);
}

GL_APICALL void GL_APIENTRY
glUniform3f(
	GLint location,
	GLfloat v0,
	GLfloat v1,
	GLfloat v2)
{
	GLfloat values[3];

	/* Three floats. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	program_uniform(location, 1, 3U, values, 0);
}

GL_APICALL void GL_APIENTRY
glUniform4f(
	GLint location,
	GLfloat v0,
	GLfloat v1,
	GLfloat v2,
	GLfloat v3)
{
	GLfloat values[4];

	/* Four floats. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	values[3] = v3;
	program_uniform(location, 1, 4U, values, 0);
}

GL_APICALL void GL_APIENTRY
glUniform1i(
	GLint location,
	GLint v0)
{
	/* One int. */
	program_uniform(location, 1, 1U, &v0, 1);
}

GL_APICALL void GL_APIENTRY
glUniform2i(
	GLint location,
	GLint v0,
	GLint v1)
{
	GLint values[2];

	/* Two ints. */
	values[0] = v0;
	values[1] = v1;
	program_uniform(location, 1, 2U, values, 1);
}

GL_APICALL void GL_APIENTRY
glUniform3i(
	GLint location,
	GLint v0,
	GLint v1,
	GLint v2)
{
	GLint values[3];

	/* Three ints. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	program_uniform(location, 1, 3U, values, 1);
}

GL_APICALL void GL_APIENTRY
glUniform4i(
	GLint location,
	GLint v0,
	GLint v1,
	GLint v2,
	GLint v3)
{
	GLint values[4];

	/* Four ints. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	values[3] = v3;
	program_uniform(location, 1, 4U, values, 1);
}

GL_APICALL void GL_APIENTRY
glUniform1fv(
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Floats, one per element. */
	program_uniform(location, count, 1U, value, 0);
}

GL_APICALL void GL_APIENTRY
glUniform2fv(
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Floats, two per element. */
	program_uniform(location, count, 2U, value, 0);
}

GL_APICALL void GL_APIENTRY
glUniform3fv(
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Floats, three per element. */
	program_uniform(location, count, 3U, value, 0);
}

GL_APICALL void GL_APIENTRY
glUniform4fv(
	GLint location,
	GLsizei count,
	const GLfloat *value)
{
	/* Floats, four per element. */
	program_uniform(location, count, 4U, value, 0);
}

GL_APICALL void GL_APIENTRY
glUniform1iv(
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Ints, one per element. */
	program_uniform(location, count, 1U, value, 1);
}

GL_APICALL void GL_APIENTRY
glUniform2iv(
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Ints, two per element. */
	program_uniform(location, count, 2U, value, 1);
}

GL_APICALL void GL_APIENTRY
glUniform3iv(
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Ints, three per element. */
	program_uniform(location, count, 3U, value, 1);
}

GL_APICALL void GL_APIENTRY
glUniform4iv(
	GLint location,
	GLsizei count,
	const GLint *value)
{
	/* Ints, four per element. */
	program_uniform(location, count, 4U, value, 1);
}

GL_APICALL void GL_APIENTRY
glUniform1ui(
	GLint location,
	GLuint v0)
{
	/* One unsigned int. */
	program_uniform(location, 1, 1U, &v0, 2);
}

GL_APICALL void GL_APIENTRY
glUniform2ui(
	GLint location,
	GLuint v0,
	GLuint v1)
{
	GLuint values[2];

	/* Two unsigned ints. */
	values[0] = v0;
	values[1] = v1;
	program_uniform(location, 1, 2U, values, 2);
}

GL_APICALL void GL_APIENTRY
glUniform3ui(
	GLint location,
	GLuint v0,
	GLuint v1,
	GLuint v2)
{
	GLuint values[3];

	/* Three unsigned ints. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	program_uniform(location, 1, 3U, values, 2);
}

GL_APICALL void GL_APIENTRY
glUniform4ui(
	GLint location,
	GLuint v0,
	GLuint v1,
	GLuint v2,
	GLuint v3)
{
	GLuint values[4];

	/* Four unsigned ints. */
	values[0] = v0;
	values[1] = v1;
	values[2] = v2;
	values[3] = v3;
	program_uniform(location, 1, 4U, values, 2);
}

GL_APICALL void GL_APIENTRY
glUniform1uiv(
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Unsigned ints, one per element. */
	program_uniform(location, count, 1U, value, 2);
}

GL_APICALL void GL_APIENTRY
glUniform2uiv(
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Unsigned ints, two per element. */
	program_uniform(location, count, 2U, value, 2);
}

GL_APICALL void GL_APIENTRY
glUniform3uiv(
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Unsigned ints, three per element. */
	program_uniform(location, count, 3U, value, 2);
}

GL_APICALL void GL_APIENTRY
glUniform4uiv(
	GLint location,
	GLsizei count,
	const GLuint *value)
{
	/* Unsigned ints, four per element. */
	program_uniform(location, count, 4U, value, 2);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix2fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* 2x2 matrices. */
	program_matrix(location, count, transpose, value, 2U, 2U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix3fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* 3x3 matrices. */
	program_matrix(location, count, transpose, value, 3U, 3U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix4fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* 4x4 matrices. */
	program_matrix(location, count, transpose, value, 4U, 4U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix2x3fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Two columns of three rows. */
	program_matrix(location, count, transpose, value, 2U, 3U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix3x2fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Three columns of two rows. */
	program_matrix(location, count, transpose, value, 3U, 2U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix2x4fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Two columns of four rows. */
	program_matrix(location, count, transpose, value, 2U, 4U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix4x2fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Four columns of two rows. */
	program_matrix(location, count, transpose, value, 4U, 2U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix3x4fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Three columns of four rows. */
	program_matrix(location, count, transpose, value, 3U, 4U);
}

GL_APICALL void GL_APIENTRY
glUniformMatrix4x3fv(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *value)
{
	/* Four columns of three rows. */
	program_matrix(location, count, transpose, value, 4U, 3U);
}

/*
 * Reads a uniform's value of the given program as floats.
 */
GL_APICALL void GL_APIENTRY
glGetUniformfv(
	GLuint name,
	GLint location,
	GLfloat *params)
{
	/* Floats. */
	program_get_uniform(name, location, 0, params);
}

/*
 * Reads a uniform's value of the given program as ints.
 */
GL_APICALL void GL_APIENTRY
glGetUniformiv(
	GLuint name,
	GLint location,
	GLint *params)
{
	/* Ints. */
	program_get_uniform(name, location, 1, params);
}

/*
 * Reads a uniform's value of the given program as unsigned ints.
 */
GL_APICALL void GL_APIENTRY
glGetUniformuiv(
	GLuint name,
	GLint location,
	GLuint *params)
{
	/* Unsigned ints. */
	program_get_uniform(name, location, 2, params);
}

/*
 * Returns the index of a program's named uniform block, or
 * GL_INVALID_INDEX.
 */
GL_APICALL GLuint GL_APIENTRY
glGetUniformBlockIndex(
	GLuint name,
	const GLchar *block)
{
	struct zegl_context *context;
	struct gles_program *program;
	unsigned index;
	int differs;

	/* The program (an unlinked one has no blocks). */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return GL_INVALID_INDEX;

	/* The block of that name. */
	for (index = 0U; index < program->block_count; index++) {
		if (program->blocks[index].stages == 0U)
			continue;
		differs = strcmp(program->blocks[index].name, block);
		if (differs == 0)
			return index;
	}

	/* None. */
	return GL_INVALID_INDEX;
}

/*
 * Reports a property of one of a program's named uniform blocks.
 */
GL_APICALL void GL_APIENTRY
glGetActiveUniformBlockiv(
	GLuint name,
	GLuint index,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_block *block;
	unsigned uniform;
	unsigned found;

	/* The program and the block. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	block = program_block(context, program, index);
	if (block == NULL)
		return;

	/* The property asked for. */
	switch (pname) {
	case GL_UNIFORM_BLOCK_BINDING:
		*params = (GLint)block->buffer_binding;
		return;
	case GL_UNIFORM_BLOCK_DATA_SIZE:
		*params = (GLint)block->size;
		return;
	case GL_UNIFORM_BLOCK_NAME_LENGTH:
		*params = (GLint)strlen(block->name) + 1;
		return;
	case GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS:
		*params = (GLint)block->member_count;
		return;
	case GL_UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER:
		*params = (GLint)(block->stages & 1U);
		return;
	case GL_UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER:
		*params = (GLint)((block->stages >> 1) & 1U);
		return;
	case GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES:
		break;
	default:
		gles_error(context, GL_INVALID_ENUM);
		return;
	}

	/* The indices of its members among the program's uniforms. */
	found = 0U;
	for (uniform = 0U; uniform < program->uniform_count; uniform++) {
		if (program->uniforms[uniform].block != (GLint)index)
			continue;
		params[found] = (GLint)uniform;
		found++;
	}
}

/*
 * Copies the name of one of a program's named uniform blocks.
 */
GL_APICALL void GL_APIENTRY
glGetActiveUniformBlockName(
	GLuint name,
	GLuint index,
	GLsizei bufSize,
	GLsizei *length,
	GLchar *uniformBlockName)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_block *block;

	/* The program and the block. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	block = program_block(context, program, index);
	if (block == NULL)
		return;

	/* Its name. */
	program_copy_string(block->name, bufSize, length, uniformBlockName);
}

/*
 * Makes one of a program's named uniform blocks read the buffer bound to
 * an indexed uniform buffer binding point.
 */
GL_APICALL void GL_APIENTRY
glUniformBlockBinding(
	GLuint name,
	GLuint index,
	GLuint binding)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_block *block;

	/* The program and the block. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	block = program_block(context, program, index);
	if (block == NULL)
		return;

	/* A binding point there is. */
	if (binding >= GLES_UNIFORM_BINDINGS) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* The next draws read that binding point's buffer. */
	block->buffer_binding = binding;
}

/*
 * Finds the indices of a program's active uniforms by name
 * (GL_INVALID_INDEX for a name that is not one).
 */
GL_APICALL void GL_APIENTRY
glGetUniformIndices(
	GLuint name,
	GLsizei count,
	const GLchar *const *names,
	GLuint *indices)
{
	struct zegl_context *context;
	struct gles_program *program;
	const char *wanted;
	size_t length;
	size_t own;
	unsigned index;
	GLsizei which;
	int differs;

	/* The program, and a count. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Each name: a uniform's own, or an array's with "[0]". */
	for (which = 0; which < count; which++) {
		wanted = names[which];
		length = strlen(wanted);
		indices[which] = GL_INVALID_INDEX;
		for (index = 0U; index < program->uniform_count; index++) {
			own = strlen(program->uniforms[index].name);
			differs = strncmp(program->uniforms[index].name, wanted, own);
			if (differs != 0)
				continue;

			/* The whole name, or the array's with "[0]". */
			if (length == own) {
				indices[which] = index;
				break;
			}

			/* An array's first element names the array. */
			differs = strcmp(wanted + own, "[0]");
			if (differs == 0 && program->uniforms[index].size > 1) {
				indices[which] = index;
				break;
			}
		}
	}
}

/*
 * Reports a property of each of a list of a program's active uniforms.
 */
GL_APICALL void GL_APIENTRY
glGetActiveUniformsiv(
	GLuint name,
	GLsizei count,
	const GLuint *indices,
	GLenum pname,
	GLint *params)
{
	struct zegl_context *context;
	struct gles_program *program;
	GLint value;
	GLsizei which;
	int status;

	/* The program, a count, and indices of its uniforms. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Every index must be one of the program's uniforms. */
	for (which = 0; which < count; which++) {
		if (indices[which] >= program->uniform_count) {
			gles_error(context, GL_INVALID_VALUE);
			return;
		}
	}

	/* Each uniform's property. */
	for (which = 0; which < count; which++) {
		status = program_uniform_property(&program->uniforms[indices[which]], pname, &value);
		if (status != 0) {
			gles_error(context, GL_INVALID_ENUM);
			return;
		}

		/* The value. */
		params[which] = value;
	}
}

/* Returns the shader of a name, recording the error when the name is not one. */
static struct gles_shader *
program_shader(
	struct zegl_context *context,
	GLuint name)
{
	struct gles_state *state;
	struct gles_shader *shader;

	/* A context with its state. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* The name's object: none is GL_INVALID_VALUE, a program GL_INVALID_OPERATION. */
	shader = gles_names_get(&state->objects, name);
	if (shader == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* A shader, not a program. */
	if (shader->kind != GLES_KIND_SHADER) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the shader. */
	return shader;
}

/* Returns the program of a name, recording the error when the name is not one. */
static struct gles_program *
program_get(
	struct zegl_context *context,
	GLuint name)
{
	struct gles_state *state;
	struct gles_program *program;

	/* A context with its state. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;

	/* The name's object: none is GL_INVALID_VALUE, a shader GL_INVALID_OPERATION. */
	program = gles_names_get(&state->objects, name);
	if (program == NULL) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* A program, not a shader. */
	if (program->kind != GLES_KIND_PROGRAM) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the program. */
	return program;
}

/*
 * Links a program: its shaders' SPIR-V (made by the GLSL compiler's link
 * when both are GLSL, or given as binaries), then the interfaces, the
 * uniforms and the Vulkan objects.  Returns 0, or -1 with the log.
 */
static int
program_link(
	struct gles_state *state,
	struct gles_program *program,
	char *log)
{
	struct glsl_program linked;
	unsigned input;
	unsigned output;
	unsigned vertices;
	int status;

	/* Two compiled shaders. */
	if (program->vertex == NULL || program->fragment == NULL) {
		(void)snprintf(log, PROGRAM_LOG, "a program needs a vertex and a fragment shader\n");
		return -1;
	}

	/* Both compiled, and a geometry shader too (GLSL only). */
	if (!program->vertex->compiled || !program->fragment->compiled) {
		(void)snprintf(log, PROGRAM_LOG, "a shader of the program is not compiled\n");
		return -1;
	}

	/* A geometry shader compiled from GLSL. */
	if (program->geometry != NULL && (!program->geometry->compiled || program->geometry->glsl == NULL)) {
		(void)snprintf(log, PROGRAM_LOG, "the geometry shader is not compiled GLSL\n");
		return -1;
	}

	/* Two binaries are linked as they are. */
	if (program->vertex->glsl == NULL && program->fragment->glsl == NULL && program->geometry == NULL) {
		status = program_link_code(state, program, program->vertex->code, program->vertex->words, program->fragment->code,
					   program->fragment->words, NULL, 0U, log);
		return status;
	}

	/* GLSL is made SPIR-V by the compiler's link first, with what its vertex shader captures. */
	status = program_link_glsl(program, &linked, log);
	if (status != 0)
		return -1;
	program_glsl_captures(program, &linked);

	/* A geometry shader's primitives and most vertices, as GL names them. */
	program->geometry_input = GL_NONE;
	program->geometry_output = GL_NONE;
	program->geometry_vertices = 0;
	if (program->geometry != NULL) {
		glsl_geometry_layout(program->geometry->glsl, &input, &output, &vertices);
		program->geometry_input = input;
		program->geometry_output = output;
		program->geometry_vertices = (GLint)vertices;
	}

	/* The SPIR-V linked, then the uniforms' GL types the SPIR-V does not tell, then the named blocks' members. */
	status = program_link_code(state, program, linked.code[0], linked.words[0], linked.code[1], linked.words[1],
				   linked.code[GLSL_STAGE_GEOMETRY], linked.words[GLSL_STAGE_GEOMETRY], log);
	if (status == 0)
		program_glsl_types(program, &linked);
	if (status == 0)
		status = program_glsl_blocks(program, &linked, log);
	glsl_program_free(&linked);

	/* Reports why the link failed. */
	if (status != 0)
		return -1;

	/* Succeeded: the program is linked. */
	return 0;
}

/*
 * Links both shaders' GLSL into SPIR-V, with the attribute locations
 * glBindAttribLocation gave.  Returns 0, or -1 with the log.
 */
static int
program_link_glsl(
	struct gles_program *program,
	struct glsl_program *linked,
	char *log)
{
	struct glsl_binding bindings[GLES_ATTRIBS];
	const char *captures[GLES_CAPTURES];
	const struct glsl_shader *geometry;
	unsigned index;
	char *glsl_log;
	int status;

	/* Both shaders GLSL (a GLSL shader does not link with a SPIR-V binary). */
	if (program->vertex->glsl == NULL || program->fragment->glsl == NULL) {
		(void)snprintf(log, PROGRAM_LOG, "a GLSL shader cannot be linked with a SPIR-V binary\n");
		return -1;
	}

	/* The bound attribute locations. */
	for (index = 0U; index < program->bound_count; index++) {
		bindings[index].name = program->bound_names[index];
		bindings[index].location = program->bound_locations[index];
	}

	/* The outputs transform feedback captures, by name. */
	for (index = 0U; index < program->feedback_count; index++)
		captures[index] = program->feedback_names[index];

	/* The compiler's link (with the geometry shader between the two when there is one). */
	geometry = NULL;
	if (program->geometry != NULL)
		geometry = program->geometry->glsl;
	status = glsl_link_stages(program->vertex->glsl, geometry, program->fragment->glsl, bindings, program->bound_count,
				  captures, program->feedback_count, linked, &glsl_log);
	if (status != 0) {
		if (glsl_log != NULL) {
			(void)snprintf(log, PROGRAM_LOG, "%s", glsl_log);
		} else {
			(void)snprintf(log, PROGRAM_LOG, "the GLSL link ran out of memory\n");
		}

		/* The compiler's log is copied. */
		free(glsl_log);
		return -1;
	}

	/* Succeeded: the SPIR-V of both stages. */
	return 0;
}

/* Keeps what the linked vertex shader captures (transform feedback): each output's name, GL type, size and place, and the link's buffer mode. */
static void
program_glsl_captures(
	struct gles_program *program,
	const struct glsl_program *linked)
{
	const struct glsl_capture_info *info;
	struct gles_capture *capture;
	unsigned index;

	/* Each captured output. */
	program->capture_count = 0U;
	for (index = 0U; index < linked->capture_count && index < GLES_CAPTURES; index++) {
		info = &linked->captures[index];
		capture = &program->captures[index];
		(void)snprintf(capture->name, GLES_NAME, "%s", info->name);
		capture->type = gles_gl_type(info->base, info->components, info->columns);
		capture->size = (GLint)info->size;
		capture->offset = info->offset;
		capture->words = info->words;
		program->capture_count = index + 1U;
	}

	/* The record's words and the buffer mode the link was made with. */
	program->capture_stride = linked->capture_stride;
	program->capture_mode = program->feedback_mode;
}

/*
 * Gives the uniforms the GL types the SPIR-V does not say: bools (which
 * the block holds as uints) and the kinds of samplers.
 */
static void
program_glsl_types(
	struct gles_program *program,
	const struct glsl_program *linked)
{
	static const GLenum bools[4] = { GL_BOOL, GL_BOOL_VEC2, GL_BOOL_VEC3, GL_BOOL_VEC4 };
	const struct glsl_uniform_info *info;
	struct gles_uniform *uniform;
	unsigned index;
	unsigned other;
	int differs;

	/* Each uniform of the program, by its name among the compiler's. */
	for (index = 0U; index < program->uniform_count; index++) {
		uniform = &program->uniforms[index];
		info = NULL;
		for (other = 0U; other < linked->uniform_count; other++) {
			differs = strcmp(linked->uniforms[other].name, uniform->name);
			if (differs != 0)
				continue;
			info = &linked->uniforms[other];
			break;
		}

		/* A uniform the compiler does not know needs nothing. */
		if (info == NULL)
			continue;

		/* A bool: its values are bools. */
		if (info->base == GLSL_INFO_BOOL && info->components >= 1U && info->components <= 4U) {
			uniform->base = 3U;
			uniform->type = bools[info->components - 1U];
			continue;
		}

		/* A sampler: its dimension, layers, comparison and texel kind. */
		if (info->sampler != GLSL_SAMPLER_NONE)
			uniform->type = program_sampler_type(info);
	}
}

/* Returns GL's type of a sampler the compiler describes (its dimension, layers, comparison and texel kind). */
static GLenum
program_sampler_type(
	const struct glsl_uniform_info *info)
{
	/* A shadow sampler compares depth. */
	if (info->shadow && info->sampler != GLSL_SAMPLER_RECT) {
		if (info->sampler == GLSL_SAMPLER_CUBE)
			return GL_SAMPLER_CUBE_SHADOW;
		if (info->sampler == GLSL_SAMPLER_1D)
			return PROGRAM_SAMPLER_1D_SHADOW;
		if (info->arrayed)
			return GL_SAMPLER_2D_ARRAY_SHADOW;
		return GL_SAMPLER_2D_SHADOW;
	}

	/* The dimension, for each kind of texel (desktop GL's rectangle and buffer samplers too). */
	switch (info->sampler) {
	case GLSL_SAMPLER_RECT:
		if (info->shadow)
			return GL_SAMPLER_2D_RECT_SHADOW;
		if (info->base == GLSL_INFO_INT)
			return GL_INT_SAMPLER_2D_RECT;
		if (info->base == GLSL_INFO_UINT)
			return GL_UNSIGNED_INT_SAMPLER_2D_RECT;
		return GL_SAMPLER_2D_RECT;
	case GLSL_SAMPLER_BUFFER:
		if (info->base == GLSL_INFO_INT)
			return GL_INT_SAMPLER_BUFFER;
		if (info->base == GLSL_INFO_UINT)
			return GL_UNSIGNED_INT_SAMPLER_BUFFER;
		return GL_SAMPLER_BUFFER;
	case GLSL_SAMPLER_CUBE:
		if (info->base == GLSL_INFO_INT)
			return GL_INT_SAMPLER_CUBE;
		if (info->base == GLSL_INFO_UINT)
			return GL_UNSIGNED_INT_SAMPLER_CUBE;
		return GL_SAMPLER_CUBE;
	case GLSL_SAMPLER_3D:
		if (info->base == GLSL_INFO_INT)
			return GL_INT_SAMPLER_3D;
		if (info->base == GLSL_INFO_UINT)
			return GL_UNSIGNED_INT_SAMPLER_3D;
		return GL_SAMPLER_3D;
	case GLSL_SAMPLER_1D:
		return PROGRAM_SAMPLER_1D;
	default:
		break;
	}

	/* A 2D sampler, or one of an array of 2D layers. */
	if (info->arrayed) {
		if (info->base == GLSL_INFO_INT)
			return GL_INT_SAMPLER_2D_ARRAY;
		if (info->base == GLSL_INFO_UINT)
			return GL_UNSIGNED_INT_SAMPLER_2D_ARRAY;
		return GL_SAMPLER_2D_ARRAY;
	}

	/* A plain 2D one. */
	if (info->base == GLSL_INFO_INT)
		return GL_INT_SAMPLER_2D;
	if (info->base == GLSL_INFO_UINT)
		return GL_UNSIGNED_INT_SAMPLER_2D;
	return GL_SAMPLER_2D;
}

/*
 * Gives the named uniform blocks what the GLSL compiler knows of them
 * (their names, sizes and stages, which the SPIR-V's reflection gave only
 * in part) and adds their members to the uniforms, without locations.
 * Returns 0, or -1 with the log when there is no memory.
 */
static int
program_glsl_blocks(
	struct gles_program *program,
	const struct glsl_program *linked,
	char *log)
{
	const struct glsl_block_info *info;
	const struct glsl_uniform_info *member;
	struct gles_uniform *uniforms;
	struct gles_uniform *uniform;
	struct gles_block *block;
	unsigned members;
	unsigned index;

	/* Each block the compiler listed, at its index. */
	for (index = 0U; index < linked->block_count && index < GLES_NAMED_BLOCKS; index++) {
		info = &linked->blocks[index];
		if (info->name == NULL)
			continue;
		block = &program->blocks[index];
		(void)snprintf(block->name, sizeof(block->name), "%s", info->name);
		block->binding = info->binding;
		block->size = info->size;
		block->stages = info->stages;
		block->member_count = info->member_count;
		if (index + 1U > program->block_count)
			program->block_count = index + 1U;
	}

	/* How many members there are. */
	members = 0U;
	for (index = 0U; index < linked->uniform_count; index++) {
		if (linked->uniforms[index].block >= 0)
			members++;
	}

	/* No members: nothing to add. */
	if (members == 0U)
		return 0;

	/* Room for them after the other uniforms. */
	uniforms = realloc(program->uniforms, (program->uniform_count + members) * sizeof(*uniforms));
	if (uniforms == NULL) {
		(void)snprintf(log, PROGRAM_LOG, "out of memory\n");
		return -1;
	}

	/* The larger table is the program's. */
	program->uniforms = uniforms;

	/* Each member, with its block, offset and strides and no location. */
	for (index = 0U; index < linked->uniform_count; index++) {
		member = &linked->uniforms[index];
		if (member->block < 0)
			continue;
		uniform = &program->uniforms[program->uniform_count];
		memset(uniform, 0, sizeof(*uniform));
		(void)snprintf(uniform->name, sizeof(uniform->name), "%s", member->name);
		uniform->type = gles_gl_type(member->base, member->components, member->columns);
		uniform->size = (GLint)member->size;
		uniform->base = member->base;
		uniform->components = member->components;
		uniform->columns = member->columns;
		uniform->offset = member->offset;
		uniform->array_stride = member->array_stride;
		uniform->matrix_stride = member->matrix_stride;
		uniform->binding = GLES_FIRST_BLOCK_BINDING + (uint32_t)member->block;
		uniform->location = -1;
		uniform->block = member->block;
		uniform->row_major = (int)member->row_major;
		program->uniform_count++;
	}

	/* Succeeded: the members are in. */
	return 0;
}

/*
 * Records the named uniform blocks a stage's SPIR-V reads, at their
 * bindings (32 on): the block's type name, and the stage.  Returns 0, or
 * -1 with the log for a binding outside the ones a program has.
 */
static int
program_spirv_blocks(
	struct gles_program *program,
	const struct gles_spirv *spirv,
	unsigned stage,
	char *log)
{
	struct gles_block *block;
	uint32_t binding;
	unsigned index;
	unsigned slot;

	/* Each named block of the stage. */
	for (index = 0U; index < spirv->named_count; index++) {
		binding = spirv->named_bindings[index];
		slot = binding - GLES_FIRST_BLOCK_BINDING;
		if (binding < GLES_FIRST_BLOCK_BINDING || slot >= GLES_NAMED_BLOCKS) {
			(void)snprintf(log, PROGRAM_LOG, "a uniform block is at binding %u, outside %u to %u\n", (unsigned)binding,
				       GLES_FIRST_BLOCK_BINDING, GLES_FIRST_BLOCK_BINDING + GLES_NAMED_BLOCKS - 1U);
			return -1;
		}

		/* The block at the binding: named by the first stage that has it, read by each. */
		block = &program->blocks[slot];
		if (block->stages == 0U)
			(void)snprintf(block->name, sizeof(block->name), "%s", spirv->named_names[index]);
		block->binding = binding;
		block->stages |= 1U << stage;
		if (slot + 1U > program->block_count)
			program->block_count = slot + 1U;
	}

	/* Succeeded: the stage's blocks are recorded. */
	return 0;
}

/*
 * Links SPIR-V of the two stages (and of a geometry stage between them,
 * NULL for none): the interfaces, the attribute and varying locations,
 * gl_Position made Vulkan's (in the last stage before the rasterizer),
 * the uniforms, the shader modules and the layouts.  Returns 0, or -1
 * with the log.
 */
static int
program_link_code(
	struct gles_state *state,
	struct gles_program *program,
	const uint32_t *vertex_input,
	size_t vertex_words,
	const uint32_t *fragment_input,
	size_t fragment_words,
	const uint32_t *geometry_input,
	size_t geometry_words,
	char *log)
{
	struct gles_spirv vertex;
	struct gles_spirv fragment;
	struct gles_spirv_variable *input;
	uint32_t *vertex_code;
	uint32_t *fragment_code;
	uint32_t *patched;
	uint32_t *patched_fbo;
	size_t patched_words;
	size_t patched_fbo_words;
	unsigned index;
	unsigned other;
	int status;
	int differs;

	/* The two interfaces. */
	status = gles_spirv_reflect(vertex_input, vertex_words, &vertex, log, PROGRAM_LOG);
	if (status != 0)
		return -1;
	status = gles_spirv_reflect(fragment_input, fragment_words, &fragment, log, PROGRAM_LOG);
	if (status != 0) {
		gles_spirv_free(&vertex);
		return -1;
	}

	/* The named uniform blocks of both stages, at their bindings. */
	status = program_spirv_blocks(program, &vertex, 0U, log);
	if (status == 0)
		status = program_spirv_blocks(program, &fragment, 1U, log);
	if (status != 0) {
		gles_spirv_free(&vertex);
		gles_spirv_free(&fragment);
		return -1;
	}

	/* The stages must be the ones the shaders say. */
	if (vertex.model != 0U || fragment.model != 4U) {
		(void)snprintf(log, PROGRAM_LOG, "the SPIR-V stages do not match the shader types\n");
		gles_spirv_free(&vertex);
		gles_spirv_free(&fragment);
		return -1;
	}

	/* Copies of the code that the link may change. */
	vertex_code = malloc(vertex_words * sizeof(uint32_t));
	fragment_code = malloc(fragment_words * sizeof(uint32_t));
	if (vertex_code == NULL || fragment_code == NULL) {
		free(vertex_code);
		free(fragment_code);
		gles_spirv_free(&vertex);
		gles_spirv_free(&fragment);
		(void)snprintf(log, PROGRAM_LOG, "out of memory\n");
		return -1;
	}

	/* The codes copied. */
	memcpy(vertex_code, vertex_input, vertex_words * sizeof(uint32_t));
	memcpy(fragment_code, fragment_input, fragment_words * sizeof(uint32_t));

	/* The locations glBindFragDataLocation gave the fragment shader's outputs (libGL). */
	for (index = 0U; index < fragment.output_count; index++) {
		for (other = 0U; other < program->frag_bound_count; other++) {
			differs = strcmp(fragment.outputs[index].name, program->frag_bound_names[other]);
			if (differs != 0 || fragment.outputs[index].location_word == 0U)
				continue;
			fragment.outputs[index].location = program->frag_bound_locations[other];
			fragment_code[fragment.outputs[index].location_word] = program->frag_bound_locations[other];
		}
	}

	/* Whether a fragment input is flat, so draws follow GL's provoking vertex. */
	program->flat_inputs = program_flat_inputs(fragment_code, fragment_words);

	/* The fragment shader's outputs, by name and location. */
	program->output_count = 0U;
	for (index = 0U; index < fragment.output_count && program->output_count < GLES_DRAW_BUFFERS; index++) {
		(void)snprintf(program->output_names[program->output_count], GLES_NAME, "%s", fragment.outputs[index].name);
		program->output_locations[program->output_count] = (GLint)fragment.outputs[index].location;
		program->output_count++;
	}

	/* The attribute locations glBindAttribLocation gave. */
	for (index = 0U; index < vertex.input_count; index++) {
		for (other = 0U; other < program->bound_count; other++) {
			differs = strcmp(vertex.inputs[index].name, program->bound_names[other]);
			if (differs != 0)
				continue;
			vertex.inputs[index].location = program->bound_locations[other];
			vertex_code[vertex.inputs[index].location_word] = program->bound_locations[other];
		}
	}

	/* Each fragment input takes the location of the vertex output of its name (the GLSL link located them past a geometry stage). */
	for (index = 0U; index < fragment.input_count && geometry_input == NULL; index++) {
		input = &fragment.inputs[index];
		for (other = 0U; other < vertex.output_count; other++) {
			differs = strcmp(input->name, vertex.outputs[other].name);
			if (differs != 0 || input->name[0] == '\0')
				continue;
			input->location = vertex.outputs[other].location;
			fragment_code[input->location_word] = vertex.outputs[other].location;
		}
	}

	/* The attributes. */
	program->attribute_count = 0U;
	for (index = 0U; index < vertex.input_count && index < GLES_ATTRIBS; index++) {
		if (vertex.inputs[index].location >= GLES_ATTRIBS)
			continue;
		(void)snprintf(program->attributes[program->attribute_count].name, GLES_NAME, "%s", vertex.inputs[index].name);
		program->attributes[program->attribute_count].type = vertex.inputs[index].type;
		program->attributes[program->attribute_count].size = vertex.inputs[index].size;
		program->attributes[program->attribute_count].location = vertex.inputs[index].location;
		program->attributes[program->attribute_count].components = vertex.inputs[index].components;
		program->attribute_count++;
	}

	/* The vertex shader's gl_Position made Vulkan's: for a window, y turned over (unchanged before a geometry stage, which does it). */
	patched = gles_spirv_position(vertex_code, vertex_words, 1, &patched_words);
	if (geometry_input != NULL && patched != NULL) {
		memcpy(patched, vertex_code, vertex_words * sizeof(uint32_t));
		patched_words = vertex_words;
	}

	/* A copy that could not be made. */
	if (patched == NULL) {
		free(vertex_code);
		free(fragment_code);
		gles_spirv_free(&vertex);
		gles_spirv_free(&fragment);
		(void)snprintf(log, PROGRAM_LOG, "the vertex shader's gl_Position could not be rewritten\n");
		return -1;
	}

	/* And for a framebuffer object, whose image keeps GL's rows. */
	patched_fbo = gles_spirv_position(vertex_code, vertex_words, 0, &patched_fbo_words);
	if (geometry_input != NULL && patched_fbo != NULL) {
		memcpy(patched_fbo, vertex_code, vertex_words * sizeof(uint32_t));
		patched_fbo_words = vertex_words;
	}

	/* The code is in the copies now. */
	free(vertex_code);
	if (patched_fbo == NULL) {
		free(patched);
		free(fragment_code);
		gles_spirv_free(&vertex);
		gles_spirv_free(&fragment);
		(void)snprintf(log, PROGRAM_LOG, "the vertex shader's gl_Position could not be rewritten\n");
		return -1;
	}

	/* The uniforms of both stages, merged by name. */
	status = program_merge(program, &vertex, log);
	if (status == 0)
		status = program_merge(program, &fragment, log);
	gles_spirv_free(&vertex);
	gles_spirv_free(&fragment);

	/* The shader modules. */
	if (status == 0)
		status = program_module(state, patched, patched_words, &program->vertex_module);
	if (status == 0)
		status = program_module(state, patched_fbo, patched_fbo_words, &program->vertex_module_fbo);
	if (status == 0)
		status = program_module(state, fragment_code, fragment_words, &program->fragment_module);
	free(patched);
	free(patched_fbo);
	free(fragment_code);
	if (status != 0) {
		if (log[0] == '\0')
			(void)snprintf(log, PROGRAM_LOG, "the device refused a shader\n");
		return -1;
	}

	/* The stages that read descriptors, and the geometry stage (its uniforms, blocks and modules). */
	program->stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	if (geometry_input != NULL) {
		status = program_link_geometry(state, program, geometry_input, geometry_words, log);
		if (status != 0)
			return -1;
		program->stages |= VK_SHADER_STAGE_GEOMETRY_BIT;
	}

	/* The layouts. */
	status = program_layout(state, program);
	if (status != 0) {
		(void)snprintf(log, PROGRAM_LOG, "the device refused the program's layout\n");
		return -1;
	}

	/* Succeeded: a serial of its own. */
	program->serial = program_serial++;
	return 0;
}

/*
 * Links a program's geometry stage (after the vertex and the fragment
 * stage): its named blocks and uniforms added to the program's, and its
 * modules with gl_Position made Vulkan's at each vertex it emits (for a
 * window and for a framebuffer object).  Returns 0, or -1 with the log.
 */
static int
program_link_geometry(
	struct gles_state *state,
	struct gles_program *program,
	const uint32_t *code,
	size_t words,
	char *log)
{
	struct gles_spirv geometry;
	uint32_t *patched;
	size_t patched_words;
	int status;

	/* Its interface, blocks and uniforms. */
	status = gles_spirv_reflect(code, words, &geometry, log, PROGRAM_LOG);
	if (status != 0)
		return -1;
	status = program_spirv_blocks(program, &geometry, GLSL_STAGE_GEOMETRY, log);
	if (status == 0)
		status = program_merge(program, &geometry, log);
	gles_spirv_free(&geometry);
	if (status != 0)
		return -1;

	/* The module for a window: y turned over. */
	patched = gles_spirv_position(code, words, 1, &patched_words);
	if (patched == NULL) {
		(void)snprintf(log, PROGRAM_LOG, "the geometry shader's gl_Position could not be rewritten\n");
		return -1;
	}

	/* Its module. */
	status = program_module(state, patched, patched_words, &program->geometry_module);
	free(patched);
	if (status != 0) {
		(void)snprintf(log, PROGRAM_LOG, "the device refused the geometry shader\n");
		return -1;
	}

	/* And for a framebuffer object. */
	patched = gles_spirv_position(code, words, 0, &patched_words);
	if (patched == NULL) {
		(void)snprintf(log, PROGRAM_LOG, "the geometry shader's gl_Position could not be rewritten\n");
		return -1;
	}

	/* Its module. */
	status = program_module(state, patched, patched_words, &program->geometry_module_fbo);
	free(patched);
	if (status != 0) {
		(void)snprintf(log, PROGRAM_LOG, "the device refused the geometry shader\n");
		return -1;
	}

	/* Succeeded: the geometry stage is linked. */
	return 0;
}

/*
 * Adds a stage's uniforms to a program: a name already there must be at
 * the same place; the block grows to hold every leaf; each new uniform
 * gets locations for its elements.  Nonzero with the log when the stages
 * disagree.
 */
static int
program_merge(
	struct gles_program *program,
	struct gles_spirv *spirv,
	char *log)
{
	struct gles_uniform *uniforms;
	struct gles_location *locations;
	struct gles_uniform *uniform;
	unsigned char *data;
	uint32_t end;
	uint32_t size;
	unsigned index;
	unsigned other;
	GLint element;
	int differs;

	/* Each uniform of the stage. */
	for (index = 0U; index < spirv->uniform_count; index++) {
		uniform = &spirv->uniforms[index];

		/* One the other stage has already. */
		for (other = 0U; other < program->uniform_count; other++) {
			differs = strcmp(program->uniforms[other].name, uniform->name);
			if (differs == 0)
				break;
		}

		/* Already there: it must be at the same place. */
		if (other < program->uniform_count) {
			if (program->uniforms[other].offset != uniform->offset || program->uniforms[other].binding != uniform->binding) {
				(void)snprintf(log, PROGRAM_LOG, "uniform %s is not at the same place in both stages\n", uniform->name);
				return -1;
			}

			continue;
		}

		/* A new one, with locations for its elements. */
		uniforms = realloc(program->uniforms, (program->uniform_count + 1U) * sizeof(*uniforms));
		if (uniforms == NULL)
			return -1;
		program->uniforms = uniforms;
		locations = realloc(program->locations, (program->location_count + (unsigned)uniform->size) * sizeof(*locations));
		if (locations == NULL)
			return -1;
		program->locations = locations;
		uniform->location = (GLint)program->location_count;
		for (element = 0; element < uniform->size; element++) {
			locations[program->location_count].uniform = program->uniform_count;
			locations[program->location_count].element = (unsigned)element;
			program->location_count++;
		}

		/* The uniform, a leaf of the default block or a sampler. */
		program->uniforms[program->uniform_count] = *uniform;
		program->uniforms[program->uniform_count].block = -1;
		program->uniforms[program->uniform_count].row_major = 0;
		program->uniform_count++;

		/* A leaf of the block makes the block at least that large. */
		if (uniform->sampler)
			continue;
		program->uniform_binding = spirv->block_binding;
		end = uniform->offset + (uint32_t)(uniform->size - 1) * uniform->array_stride;
		if (uniform->columns > 1U) {
			end += uniform->columns * uniform->matrix_stride;
		} else {
			end += uniform->components * 4U;
		}

		/* The block's size, rounded to 16 bytes. */
		size = (end + 15U) & ~15U;
		if (size > program->uniform_size) {
			data = realloc(program->uniform_data, size);
			if (data == NULL)
				return -1;
			memset(data + program->uniform_size, 0, size - program->uniform_size);
			program->uniform_data = data;
			program->uniform_size = size;
		}
	}

	/* Succeeded: the stage's uniforms are in. */
	return 0;
}

/* Makes a program's descriptor set layout (the default block, each sampler, each named block) and pipeline layout; nonzero on failure. */
static int
program_layout(
	struct gles_state *state,
	struct gles_program *program)
{
	VkDescriptorSetLayoutBinding bindings[GLES_UNITS + 2U + GLES_NAMED_BLOCKS];
	VkDescriptorSetLayoutCreateInfo set;
	VkPipelineLayoutCreateInfo layout;
	uint32_t count;
	unsigned index;
	VkResult result;
	int buffer;

	/* The block, when the program has one (dynamic: each draw's copy is at its own offset). */
	count = 0U;
	memset(bindings, 0, sizeof(bindings));
	if (program->uniform_data != NULL) {
		bindings[count].binding = program->uniform_binding;
		bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		bindings[count].descriptorCount = 1U;
		bindings[count].stageFlags = program->stages;
		count++;
	}

	/* Each sampler. */
	for (index = 0U; index < program->uniform_count; index++) {
		if (!program->uniforms[index].sampler)
			continue;
		if (count == GLES_UNITS + 1U)
			return -1;
		bindings[count].binding = program->uniforms[index].binding;
		bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		buffer = program_buffer_sampler(program->uniforms[index].type);
		if (buffer)
			bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		bindings[count].descriptorCount = 1U;
		bindings[count].stageFlags = program->stages;
		count++;
	}

	/* The storage buffer the vertex shader writes captured outputs into (transform feedback). */
	if (program->capture_count != 0U) {
		bindings[count].binding = GLES_CAPTURE_BINDING;
		bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[count].descriptorCount = 1U;
		bindings[count].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		count++;
	}

	/* Each named block a stage reads: a buffer range of its own (not dynamic). */
	for (index = 0U; index < program->block_count; index++) {
		if (program->blocks[index].stages == 0U)
			continue;
		bindings[count].binding = program->blocks[index].binding;
		bindings[count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[count].descriptorCount = 1U;
		bindings[count].stageFlags = program->stages;
		count++;
	}

	/* The set layout. */
	memset(&set, 0, sizeof(set));
	set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set.bindingCount = count;
	set.pBindings = bindings;
	result = vkCreateDescriptorSetLayout(state->device, &set, NULL, &program->set_layout);
	if (result != VK_SUCCESS)
		return -1;

	/* The pipeline layout with the one set. */
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.setLayoutCount = 1U;
	layout.pSetLayouts = &program->set_layout;
	result = vkCreatePipelineLayout(state->device, &layout, NULL, &program->layout);
	if (result != VK_SUCCESS)
		return -1;

	/* Succeeded: the layouts. */
	return 0;
}

/* Makes a shader module; nonzero when the device refuses the code. */
static int
program_module(
	struct gles_state *state,
	const uint32_t *code,
	size_t words,
	VkShaderModule *module)
{
	VkShaderModuleCreateInfo create;
	VkResult result;

	/* The module over the words. */
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create.codeSize = words * sizeof(uint32_t);
	create.pCode = code;
	result = vkCreateShaderModule(state->device, &create, NULL, module);
	if (result != VK_SUCCESS) {
		gles_report("vkCreateShaderModule", (int)result);
		return -1;
	}

	/* Succeeded: the module. */
	return 0;
}

/* Undoes a program's link: its Vulkan objects wait for the frame, its pipelines go, its tables are freed. */
static void
program_unlink(
	struct gles_state *state,
	struct gles_program *program)
{
	struct gles_garbage objects;

	/* The pipelines made for it, and its Vulkan objects. */
	if (program->serial != 0U)
		gles_pipelines_forget(state, program->serial);
	memset(&objects, 0, sizeof(objects));
	objects.layout = program->layout;
	objects.set_layout = program->set_layout;
	objects.modules[0] = program->vertex_module;
	objects.modules[1] = program->fragment_module;
	objects.modules[2] = program->vertex_module_fbo;
	objects.modules[3] = program->geometry_module;
	objects.modules[4] = program->geometry_module_fbo;
	gles_garbage_keep(state, &objects);

	/* The tables. */
	free(program->uniforms);
	free(program->locations);
	free(program->uniform_data);

	/* Nothing linked. */
	program->layout = VK_NULL_HANDLE;
	program->set_layout = VK_NULL_HANDLE;
	program->vertex_module = VK_NULL_HANDLE;
	program->vertex_module_fbo = VK_NULL_HANDLE;
	program->fragment_module = VK_NULL_HANDLE;
	program->geometry_module = VK_NULL_HANDLE;
	program->geometry_module_fbo = VK_NULL_HANDLE;
	program->uniforms = NULL;
	program->uniform_count = 0U;
	program->locations = NULL;
	program->location_count = 0U;
	program->uniform_data = NULL;
	program->uniform_size = 0U;
	program->attribute_count = 0U;
	program->serial = 0U;
	program->linked = 0;
	memset(program->blocks, 0, sizeof(program->blocks));
	program->block_count = 0U;
	program->capture_count = 0U;
	program->capture_stride = 0U;
}

/*
 * Writes values of a vector size into the current program's uniform at a
 * location, count elements (a sampler takes its texture unit).
 */
static void
program_uniform(
	GLint location,
	GLsizei count,
	unsigned components,
	const void *values,
	int integers)
{
	struct zegl_context *context;
	struct gles_uniform *uniform;
	unsigned char *data;
	unsigned element;
	unsigned component;
	GLsizei index;
	GLint integer;
	GLuint unsigned_integer;
	GLfloat number;
	uint32_t word;

	/* Location -1 is ignored. */
	if (location == -1)
		return;

	/* The uniform and element of the location. */
	context = gles_context();
	uniform = program_location(context, location, &element);
	if (uniform == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A sampler takes one texture unit, as an int. */
	if (uniform->sampler) {
		if (integers != 1 || components != 1U) {
			gles_error(context, GL_INVALID_OPERATION);
			return;
		}

		/* The unit. */
		uniform->unit = ((const GLint *)values)[0];
		return;
	}

	/* The size must match; a vector or scalar only. */
	if (uniform->components != components || uniform->columns != 1U || (count > 1 && uniform->size == 1)) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Each element, each component converted to the uniform's base type. */
	for (index = 0; index < count && element + (unsigned)index < (unsigned)uniform->size; index++) {
		data = gles_state(context)->program->uniform_data + uniform->offset + (element + (unsigned)index) * uniform->array_stride;
		for (component = 0U; component < components; component++) {
			/* The value given: an int, an unsigned int, or a float. */
			if (integers == 1) {
				integer = ((const GLint *)values)[(size_t)index * components + component];
				number = (GLfloat)integer;
			} else if (integers == 2) {
				unsigned_integer = ((const GLuint *)values)[(size_t)index * components + component];
				integer = (GLint)unsigned_integer;
				number = (GLfloat)unsigned_integer;
			} else {
				number = ((const GLfloat *)values)[(size_t)index * components + component];
				integer = (GLint)number;
				if (uniform->base == 2U)
					integer = (GLint)(GLuint)number;
			}

			/* As a float, an int, or a bool (0 or 1). */
			if (uniform->base == 0U) {
				memcpy(data + component * 4U, &number, 4U);
				continue;
			}

			/* An int or an unsigned int. */
			word = (uint32_t)integer;
			if (uniform->base == 3U && number != 0.0f)
				word = 1U;
			else if (uniform->base == 3U)
				word = 0U;
			memcpy(data + component * 4U, &word, 4U);
		}
	}
}

/*
 * Writes matrices of columns x rows into the current program's uniform at
 * a location, column by column at the block's matrix stride (the values
 * given row by row when transpose is set, which OpenGL ES 2 does not
 * allow).
 */
static void
program_matrix(
	GLint location,
	GLsizei count,
	GLboolean transpose,
	const GLfloat *values,
	unsigned columns,
	unsigned rows)
{
	struct zegl_context *context;
	struct gles_uniform *uniform;
	const GLfloat *matrix;
	unsigned char *data;
	unsigned element;
	unsigned column;
	unsigned row;
	GLsizei index;
	GLfloat value;

	/* Location -1 is ignored. */
	if (location == -1)
		return;

	/* The uniform and element of the location. */
	context = gles_context();
	uniform = program_location(context, location, &element);
	if (uniform == NULL)
		return;
	if (count < 0) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* Transposed values need OpenGL ES 3 (or desktop GL). */
	if (transpose != GL_FALSE &&
	    gles_fixed == NULL &&
	    context->version < 3) {
		gles_error(context, GL_INVALID_VALUE);
		return;
	}

	/* A matrix of that size. */
	if (uniform->sampler ||
	    uniform->columns != columns ||
	    uniform->components != rows ||
	    (count > 1 && uniform->size == 1)) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* Each matrix, each column at the stride, each row of it a float. */
	for (index = 0; index < count && element + (unsigned)index < (unsigned)uniform->size; index++) {
		data = gles_state(context)->program->uniform_data + uniform->offset + (element + (unsigned)index) * uniform->array_stride;
		matrix = values + (size_t)index * columns * rows;
		for (column = 0U; column < columns; column++) {
			for (row = 0U; row < rows; row++) {
				value = matrix[column * rows + row];
				if (transpose != GL_FALSE)
					value = matrix[row * columns + column];
				memcpy(data + column * uniform->matrix_stride + row * 4U, &value, sizeof(value));
			}
		}
	}
}

/* Returns the uniform and element of a location of the current program, recording the error when there is none. */
static struct gles_uniform *
program_location(
	struct zegl_context *context,
	GLint location,
	unsigned *element)
{
	struct gles_state *state;
	struct gles_program *program;

	/* A current program. */
	state = gles_state(context);
	if (state == NULL)
		return NULL;
	program = state->program;
	if (program == NULL || location < 0 || (unsigned)location >= program->location_count) {
		gles_error(context, GL_INVALID_OPERATION);
		return NULL;
	}

	/* Succeeded: the uniform and the element. */
	*element = program->locations[location].element;
	return &program->uniforms[program->locations[location].uniform];
}

/*
 * Reads a uniform's value of a program into the application's array: as
 * floats (kind 0), ints (1) or unsigned ints (2), each component converted
 * from the uniform's own type.
 */
static void
program_get_uniform(
	GLuint name,
	GLint location,
	int kind,
	void *params)
{
	struct zegl_context *context;
	struct gles_program *program;
	struct gles_uniform *uniform;
	struct gles_location *place;
	const unsigned char *data;
	unsigned column;
	unsigned component;
	unsigned slot;
	uint32_t word;
	GLfloat number;
	GLint integer;

	/* A linked program and one of its locations. */
	context = gles_context();
	program = program_get(context, name);
	if (program == NULL)
		return;
	if (!program->linked ||
	    location < 0 ||
	    (unsigned)location >= program->location_count) {
		gles_error(context, GL_INVALID_OPERATION);
		return;
	}

	/* The uniform and element of the location. */
	place = &program->locations[location];
	uniform = &program->uniforms[place->uniform];

	/* A sampler's value is its unit. */
	if (uniform->sampler) {
		if (kind == 0)
			((GLfloat *)params)[0] = (GLfloat)uniform->unit;
		else
			((GLint *)params)[0] = uniform->unit;
		return;
	}

	/* Each component of each column, from the uniform's type into the kind asked for. */
	data = program->uniform_data + uniform->offset + place->element * uniform->array_stride;
	for (column = 0U; column < uniform->columns; column++) {
		for (component = 0U; component < uniform->components; component++) {
			slot = column * uniform->components + component;
			memcpy(&word, data + column * uniform->matrix_stride + component * 4U, 4U);

			/* The value as a float and as an int: a float's, an int's, or an unsigned int's (a bool's is 0 or 1). */
			if (uniform->base == 0U) {
				memcpy(&number, &word, 4U);
				integer = (GLint)number;
			} else if (uniform->base == 1U) {
				integer = (GLint)word;
				number = (GLfloat)integer;
			} else {
				integer = (GLint)word;
				number = (GLfloat)word;
			}

			/* Written as the kind asked for. */
			if (kind == 0) {
				((GLfloat *)params)[slot] = number;
			} else {
				((GLint *)params)[slot] = integer;
			}
		}
	}
}

/* Returns a program's named uniform block of an index, recording the error when there is none. */
static struct gles_block *
program_block(
	struct zegl_context *context,
	struct gles_program *program,
	GLuint index)
{
	/* An index among the blocks the link found. */
	if (index >= program->block_count) {
		gles_error(context, GL_INVALID_VALUE);
		return NULL;
	}

	/* Succeeded: the block. */
	return &program->blocks[index];
}

/* Reads one property glGetActiveUniformsiv asks for of a uniform; nonzero for a name that is not one. */
static int
program_uniform_property(
	const struct gles_uniform *uniform,
	GLenum pname,
	GLint *value)
{
	/* The property: a default-block uniform or sampler has no block, offset or strides (-1). */
	switch (pname) {
	case GL_UNIFORM_TYPE:
		*value = (GLint)uniform->type;
		return 0;
	case GL_UNIFORM_SIZE:
		*value = uniform->size;
		return 0;
	case GL_UNIFORM_NAME_LENGTH:
		*value = (GLint)strlen(uniform->name) + 1;
		if (uniform->size > 1)
			*value += 3;
		return 0;
	case GL_UNIFORM_BLOCK_INDEX:
		*value = uniform->block;
		return 0;
	case GL_UNIFORM_OFFSET:
		*value = -1;
		if (uniform->block >= 0)
			*value = (GLint)uniform->offset;
		return 0;
	case GL_UNIFORM_ARRAY_STRIDE:
		*value = -1;
		if (uniform->block >= 0)
			*value = (GLint)uniform->array_stride;
		return 0;
	case GL_UNIFORM_MATRIX_STRIDE:
		*value = -1;
		if (uniform->block >= 0)
			*value = (GLint)uniform->matrix_stride;
		return 0;
	case GL_UNIFORM_IS_ROW_MAJOR:
		*value = uniform->row_major;
		return 0;
	default:
		break;
	}

	/* Not a property. */
	return -1;
}

/* Copies a string into an application's buffer of a size, reporting the length copied. */
static void
program_copy_string(
	const char *text,
	GLsizei size,
	GLsizei *length,
	GLchar *out)
{
	size_t copied;

	/* Nothing fits in no buffer. */
	if (text == NULL)
		text = "";
	copied = 0U;
	if (size > 0 && out != NULL) {
		copied = strlen(text);
		if (copied > (size_t)size - 1U)
			copied = (size_t)size - 1U;
		memcpy(out, text, copied);
		out[copied] = '\0';
	}

	/* The length without the terminator. */
	if (length != NULL)
		*length = (GLsizei)copied;
}

/* Replaces a log with a copy of a text. */
static void
program_log(
	char **log,
	const char *text)
{
	char *copy;

	/* The copy; without memory the log is left empty. */
	copy = malloc(strlen(text) + 1U);
	if (copy != NULL)
		memcpy(copy, text, strlen(text) + 1U);
	free(*log);
	*log = copy;
}

/* Reports whether a sampler type reads a buffer texture (desktop GL's samplerBuffer of each kind), which is a texel buffer. */
static int
program_buffer_sampler(
	GLenum type)
{
	/* The three kinds of buffer samplers. */
	switch (type) {
	case GL_SAMPLER_BUFFER:
	case GL_INT_SAMPLER_BUFFER:
	case GL_UNSIGNED_INT_SAMPLER_BUFFER:
		return 1;
	default:
		break;
	}

	/* A sampler of an image. */
	return 0;
}

/* Reports whether a fragment shader's SPIR-V decorates anything Flat (an input taking the provoking vertex's value). */
static int
program_flat_inputs(
	const uint32_t *code,
	size_t words)
{
	size_t at;
	uint32_t length;

	/* Each instruction after the header: an OpDecorate (71) of Flat (14). */
	for (at = 5U; at < words; at += length) {
		length = code[at] >> 16;
		if (length == 0U)
			return 0;
		if ((code[at] & 0xffffU) == 71U && length >= 3U && code[at + 2U] == 14U)
			return 1;
	}

	/* None is flat. */
	return 0;
}
