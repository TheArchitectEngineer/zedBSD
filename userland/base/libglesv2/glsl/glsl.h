/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zedBSD's GLSL compiler (WS068 p015-p019): what libGLESv2, libGL and the
 * host tests see of it.
 *
 * A shader is compiled on its own (preprocessed, parsed and checked), and
 * the SPIR-V is made when a vertex and a fragment shader are linked
 * together, because the default uniform block must have one layout in
 * both stages.  The SPIR-V is in the form libGLESv2's translation layer
 * takes (plan/ws068/phase008/phase.md): the uniforms other than samplers
 * in one std140 block at set 0 binding 0, the samplers at set 0 from
 * binding 1, attributes and varyings by location and name.
 *
 * Nothing here depends on the GL headers, so the compiler builds on the
 * host as it is.
 */

#ifndef GLSL_H
#define GLSL_H

#include <stddef.h>
#include <stdint.h>

/* The stages a shader may be of. */
#define GLSL_STAGE_VERTEX	0U
#define GLSL_STAGE_FRAGMENT	1U

/* The scalar kinds of an interface variable (the same numbers as libGLESv2's gles_uniform.base). */
#define GLSL_INFO_FLOAT		0U
#define GLSL_INFO_INT		1U
#define GLSL_INFO_UINT		2U
#define GLSL_INFO_BOOL		3U

/* The kinds of samplers, by the dimension of the image they read. */
#define GLSL_SAMPLER_NONE	0U
#define GLSL_SAMPLER_1D		1U
#define GLSL_SAMPLER_2D		2U
#define GLSL_SAMPLER_3D		3U
#define GLSL_SAMPLER_CUBE	4U

/* Desktop GLSL 1.40's rectangle samplers (a 2D image read in texels) and buffer samplers (a texel buffer). */
#define GLSL_SAMPLER_RECT	5U
#define GLSL_SAMPLER_BUFFER	6U

/*
 * One active uniform of a linked program as the API reports it: a leaf of
 * the default uniform block (named the way libGLESv2's SPIR-V reflection
 * names it: "s.field", "a[1].field"), a sampler, or a leaf of a named
 * uniform block (named as the GL API names it: "Block.member" for a block
 * with an instance name, "member" for one without).
 */
struct glsl_uniform_info {
	/* The name, allocated with the program. */
	char *name;

	/* GLSL_INFO_*, the components of a column, the columns (1 unless a matrix), the elements (1 unless an array). */
	unsigned base;
	unsigned components;
	unsigned columns;
	unsigned size;

	/* GLSL_SAMPLER_* for a sampler, whether it compares depth (a shadow sampler), and whether it reads an array of layers. */
	unsigned sampler;
	unsigned shadow;
	unsigned arrayed;

	/* The named block a block member is in (its index among the program's blocks), -1 for the default block and samplers. */
	int block;

	/* A block member's std140 offset in its block, its array and matrix strides (0 when not an array or a matrix), and whether its matrices are row-major. */
	unsigned offset;
	unsigned array_stride;
	unsigned matrix_stride;
	unsigned row_major;
};

/*
 * One active named uniform block of a linked program: what the API
 * reports of it, and where the SPIR-V reads it from.
 */
struct glsl_block_info {
	/* The block's name (not its instance name), allocated with the program. */
	char *name;

	/* Its binding at descriptor set 0 in both stages' SPIR-V (32 on), and its std140 size in bytes. */
	unsigned binding;
	unsigned size;

	/* The stages that read it: bit 0 the vertex stage, bit 1 the fragment stage. */
	unsigned stages;

	/* How many of the program's uniforms are its members. */
	unsigned member_count;
};

/*
 * The storage buffer a vertex shader writes the outputs it captures into
 * (transform feedback), at descriptor set 0: words of 32 bits, the first
 * GLSL_CAPTURE_HEADER a header whose word 0 is how many vertices each
 * instance has, then each vertex's record (instance * vertices + vertex)
 * of the captured outputs one after another, each component a word (a
 * float's or an int's bits).
 */
#define GLSL_CAPTURE_BINDING	48U
#define GLSL_CAPTURE_HEADER	4U

/*
 * One output a linked program's vertex shader captures, as the API
 * reports it (glGetTransformFeedbackVarying) and where its words are in a
 * vertex's record.
 */
struct glsl_capture_info {
	/* The name, allocated with the program. */
	char *name;

	/* GLSL_INFO_*, the components of a column, the columns (1 unless a matrix), the elements (1 unless an array). */
	unsigned base;
	unsigned components;
	unsigned columns;
	unsigned size;

	/* Its first word in a vertex's record, and how many words it has. */
	unsigned offset;
	unsigned words;
};

/*
 * A linked program: the SPIR-V of both stages and what the API says of
 * the uniforms.  glsl_program_free releases it.
 */
struct glsl_program {
	/* The SPIR-V words of the vertex and the fragment stage. */
	uint32_t *code[2];
	size_t words[2];

	/* The active uniforms: the default block's leaves and the samplers, then the named blocks' members. */
	struct glsl_uniform_info *uniforms;
	unsigned uniform_count;

	/* The active named uniform blocks, in the order of their bindings. */
	struct glsl_block_info *blocks;
	unsigned block_count;

	/* The outputs the vertex shader captures (transform feedback), and the words of a vertex's record (0: none). */
	struct glsl_capture_info *captures;
	unsigned capture_count;
	unsigned capture_stride;
};

/*
 * A location glBindAttribLocation gave to an attribute name.
 */
struct glsl_binding {
	const char *name;
	unsigned location;
};

/* A compiled shader; opaque outside the compiler. */
struct glsl_shader;

/*
 * Compiles a shader of a stage.  default_version is the version of a
 * source without #version (100 for OpenGL ES, 110 for desktop GL).
 * Returns the shader, or NULL when the source has errors.  *log receives
 * a malloc'ed info log (errors, or warnings of a shader that compiled),
 * or NULL when there is nothing to say.
 */
struct glsl_shader *glsl_compile(unsigned stage, const char *source, unsigned default_version, char **log);

/* Frees a compiled shader. */
void glsl_shader_free(struct glsl_shader *shader);

/* Reports a compiled shader's stage. */
unsigned glsl_shader_stage(const struct glsl_shader *shader);

/* Reports a compiled shader's GLSL version (100, 110 .. 330, 300) and whether it is OpenGL ES's language. */
unsigned glsl_shader_version(const struct glsl_shader *shader, int *es);

/*
 * Links a vertex and a fragment shader into SPIR-V.  Returns 0 and fills
 * *program, or -1 with *log a malloc'ed info log saying why.
 */
int glsl_link(const struct glsl_shader *vertex, const struct glsl_shader *fragment, const struct glsl_binding *bindings, unsigned binding_count, struct glsl_program *program, char **log);

/*
 * Links as glsl_link does, with the vertex shader capturing the outputs
 * of the names given (transform feedback, GLSL_CAPTURE_BINDING) in that
 * order.  Returns 0 and fills *program, or -1 with *log a malloc'ed info
 * log saying why.
 */
int glsl_link_captured(const struct glsl_shader *vertex, const struct glsl_shader *fragment, const struct glsl_binding *bindings, unsigned binding_count, const char *const *captures, unsigned capture_count, struct glsl_program *program, char **log);

/* Frees what a successful link gave. */
void glsl_program_free(struct glsl_program *program);

#endif
