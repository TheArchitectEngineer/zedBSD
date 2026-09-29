/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's link: a vertex and a fragment shader, and a
 * geometry shader between them when there is one, are matched (uniforms
 * of one name must agree, every input a stage reads must be an output
 * the stage before declares: a geometry shader's inputs are arrays of
 * the vertex shader's outputs), the program's uniforms are laid out once
 * for all (std140 offsets in the default uniform block, sampler bindings
 * from 1), attributes and varyings get their locations, and each stage
 * is emitted as SPIR-V.
 *
 * The link writes what it decides into the shaders' symbols (locations,
 * uniform indices, emission ids), so two links of the same shader must
 * not run at the same time.
 */

#include "emit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most uniforms and locations a program has. */
#define LINK_MAX_UNIFORMS	256U
#define LINK_MAX_LOCATIONS	32U

/* The most outputs a vertex shader captures (transform feedback). */
#define LINK_MAX_CAPTURES	64U

/* The binding of the first uniform block (the default block is 0, the samplers 1 to 16). */
#define LINK_FIRST_BLOCK_BINDING 32U

/* The most named uniform blocks a program has (libGLESv2 binds them from 32 up to 55). */
#define LINK_MAX_BLOCKS		24U

/* The most leaves the API lists of a program's uniforms. */
#define LINK_MAX_LEAVES		1024U

/*
 * A link in progress: its arena, where failures jump to, its log, and the
 * program's uniforms.
 */
struct link_state {
	struct glsl_arena arena;
	jmp_buf failure;
	struct glsl_text log;
	unsigned errors;

	/* The uniforms in the order they are laid out. */
	struct glsl_link_uniform uniforms[LINK_MAX_UNIFORMS];
	unsigned uniform_count;

	/* The outputs the vertex shader captures, and the words of a vertex's record. */
	struct glsl_link_capture captures[LINK_MAX_CAPTURES];
	unsigned capture_count;
	unsigned capture_stride;
};

static void link_error(struct link_state *state, const char *format, ...);
static int link_same_type(const struct glsl_type *left, const struct glsl_type *right);
static void link_uniforms(struct link_state *state, struct glsl_shader *shader);
static void link_layout(struct link_state *state);
static void link_blocks(struct link_state *state, struct glsl_shader **stages, unsigned count);
static void link_attributes(struct link_state *state, struct glsl_shader *vertex, const struct glsl_binding *bindings, unsigned binding_count);
static void link_varyings(struct link_state *state, struct glsl_shader *producer, struct glsl_shader *consumer);
static const struct glsl_type *link_input_type(const struct glsl_shader *consumer, const struct glsl_symbol *input);
static void link_outputs(struct link_state *state, struct glsl_shader *fragment);
static struct glsl_symbol *link_find(struct glsl_shader *shader, const char *name, unsigned where);
static struct glsl_symbol *link_find_varying(struct glsl_shader *producer, struct glsl_symbol *input, const struct glsl_type *type);
static int link_take_output(struct link_state *state, struct glsl_symbol *symbol, unsigned char *taken);
static void link_info(struct link_state *state, struct glsl_program *program);
static void link_leaves(struct link_state *state, const struct glsl_type *type, const char *name, struct glsl_uniform_info *out, unsigned *count, unsigned capacity);
static void link_block_infos(struct link_state *state, struct glsl_shader *shader, unsigned stage, struct glsl_program *program);
static void link_member_leaves(struct link_state *state, const struct glsl_type *type, const char *name, unsigned offset, unsigned row_major, int block, struct glsl_program *program);
static void link_captures(struct link_state *state, struct glsl_shader *vertex, const char *const *captures, unsigned capture_count);
static unsigned link_capture_words(const struct glsl_type *type);
static void link_capture_infos(struct link_state *state, struct glsl_program *program);
static char *link_name(struct link_state *state, const char *name);
static uint32_t *link_copy(const uint32_t *code, size_t words);
static void link_storages(struct link_state *state, struct glsl_shader *compute, struct glsl_program *program);

/*
 * Links a vertex and a fragment shader into SPIR-V.
 */
int
glsl_link(
	const struct glsl_shader *vertex_shader,
	const struct glsl_shader *fragment_shader,
	const struct glsl_binding *bindings,
	unsigned binding_count,
	struct glsl_program *program,
	char **log)
{
	int status;

	/* A link that captures nothing. */
	status = glsl_link_captured(vertex_shader, fragment_shader, bindings, binding_count, NULL, 0U, program, log);
	if (status != 0)
		return -1;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Links a vertex and a fragment shader into SPIR-V, the vertex shader
 * capturing the outputs of the names given.
 */
int
glsl_link_captured(
	const struct glsl_shader *vertex_shader,
	const struct glsl_shader *fragment_shader,
	const struct glsl_binding *bindings,
	unsigned binding_count,
	const char *const *captures,
	unsigned capture_count,
	struct glsl_program *program,
	char **log)
{
	int status;

	/* A link without a geometry shader. */
	status = glsl_link_stages(vertex_shader, NULL, fragment_shader, bindings, binding_count, captures, capture_count, program, log);
	if (status != 0)
		return -1;

	/* Succeeded: the program. */
	return 0;
}

/*
 * Links a vertex shader, a geometry shader (NULL for none) and a fragment
 * shader into SPIR-V, the vertex shader capturing the outputs of the
 * names given (only without a geometry shader).
 */
int
glsl_link_stages(
	const struct glsl_shader *vertex_shader,
	const struct glsl_shader *geometry_shader,
	const struct glsl_shader *fragment_shader,
	const struct glsl_binding *bindings,
	unsigned binding_count,
	const char *const *captures,
	unsigned capture_count,
	struct glsl_program *program,
	char **log)
{
	struct link_state *volatile state;
	struct glsl_shader *vertex;
	struct glsl_shader *geometry;
	struct glsl_shader *fragment;
	struct glsl_shader *stages[3];
	uint32_t *code;
	size_t words;
	unsigned count;

	/* Nothing yet. */
	*log = NULL;
	memset(program, 0, sizeof(*program));
	state = calloc(1U, sizeof(*state));
	if (state == NULL)
		return -1;
	state->arena.failure = &state->failure;

	/* The link records its decisions in the shaders' symbols. */
	vertex = (struct glsl_shader *)vertex_shader;
	geometry = (struct glsl_shader *)geometry_shader;
	fragment = (struct glsl_shader *)fragment_shader;

	/*
	 * Out of memory comes back here.  setjmp is the whole controlling
	 * expression, the form C allows it in.
	 */
	switch (setjmp(state->failure)) {
	case 0:
		break;
	default:
		/* The link failed: what it made goes, and the log (if any) tells why. */
		glsl_program_free(program);
		glsl_arena_free(&state->arena);
		*log = state->log.data;
		free(state);
		return -1;
	}

	/* The stages, and one language for all. */
	if (vertex->stage != GLSL_STAGE_VERTEX || fragment->stage != GLSL_STAGE_FRAGMENT)
		link_error(state, "a program needs a vertex and a fragment shader");
	if (geometry != NULL && geometry->stage != GLSL_STAGE_GEOMETRY)
		link_error(state, "the shader between the vertex and the fragment shader is not a geometry shader");
	if (vertex->es != fragment->es)
		link_error(state, "OpenGL ES and desktop GLSL shaders cannot be linked together");
	if (geometry != NULL && capture_count != 0U)
		link_error(state, "transform feedback with a geometry shader is not supported");
	if (state->errors != 0U)
		longjmp(state->failure, 1);

	/* The stages in order: the vertex, the geometry (if any) and the fragment shader. */
	count = 0U;
	stages[count++] = vertex;
	if (geometry != NULL)
		stages[count++] = geometry;
	stages[count++] = fragment;

	/* The uniforms, laid out; the attributes, varyings and outputs, located. */
	link_uniforms(state, vertex);
	if (geometry != NULL)
		link_uniforms(state, geometry);
	link_uniforms(state, fragment);
	link_layout(state);
	link_blocks(state, stages, count);
	link_attributes(state, vertex, bindings, binding_count);
	if (geometry != NULL) {
		link_varyings(state, vertex, geometry);
		link_varyings(state, geometry, fragment);
	} else {
		link_varyings(state, vertex, fragment);
	}

	/* The fragment outputs and the captured outputs. */
	link_outputs(state, fragment);
	link_captures(state, vertex, captures, capture_count);
	if (state->errors != 0U)
		longjmp(state->failure, 1);

	/* Each stage's SPIR-V (the vertex stage's capturing), copied out of the arena. */
	code = glsl_emit(vertex, &state->arena, state->uniforms, state->uniform_count, state->captures, state->capture_count,
			 state->capture_stride, &words);
	program->code[0] = link_copy(code, words);
	program->words[0] = words;
	code = glsl_emit(fragment, &state->arena, state->uniforms, state->uniform_count, NULL, 0U, 0U, &words);
	program->code[1] = link_copy(code, words);
	program->words[1] = words;
	if (program->code[0] == NULL || program->code[1] == NULL)
		longjmp(state->failure, 1);

	/* The geometry stage's, when there is one. */
	if (geometry != NULL) {
		code = glsl_emit(geometry, &state->arena, state->uniforms, state->uniform_count, NULL, 0U, 0U, &words);
		program->code[GLSL_STAGE_GEOMETRY] = link_copy(code, words);
		program->words[GLSL_STAGE_GEOMETRY] = words;
		if (program->code[GLSL_STAGE_GEOMETRY] == NULL)
			longjmp(state->failure, 1);
	}

	/* What the API reports of the uniforms, the uniform blocks and the captured outputs. */
	link_info(state, program);
	link_block_infos(state, vertex, GLSL_STAGE_VERTEX, program);
	link_block_infos(state, fragment, GLSL_STAGE_FRAGMENT, program);
	if (geometry != NULL)
		link_block_infos(state, geometry, GLSL_STAGE_GEOMETRY, program);
	link_capture_infos(state, program);

	/* Succeeded: the program. */
	glsl_arena_free(&state->arena);
	free(state->log.data);
	free(state);
	return 0;
}

/*
 * Frees what a successful link gave.
 */
void
glsl_program_free(
	struct glsl_program *program)
{
	unsigned index;

	/* The SPIR-V of each stage. */
	for (index = 0U; index < GLSL_STAGES; index++) {
		free(program->code[index]);
		program->code[index] = NULL;
		program->words[index] = 0U;
	}

	/* The uniforms' names and the list. */
	for (index = 0U; index < program->uniform_count; index++)
		free(program->uniforms[index].name);
	free(program->uniforms);
	program->uniforms = NULL;
	program->uniform_count = 0U;

	/* The blocks' names and the list. */
	for (index = 0U; index < program->block_count; index++)
		free(program->blocks[index].name);
	free(program->blocks);
	program->blocks = NULL;
	program->block_count = 0U;

	/* The captured outputs' names and the list. */
	for (index = 0U; index < program->capture_count; index++)
		free(program->captures[index].name);
	free(program->captures);
	program->captures = NULL;
	program->capture_count = 0U;
	program->capture_stride = 0U;

	/* A compute program's storage blocks' names and the list (ws101-p008). */
	for (index = 0U; index < program->storage_count; index++)
		free(program->storages[index].name);
	free(program->storages);
	program->storages = NULL;
	program->storage_count = 0U;
}

/* Reports a link error in the log. */
static void
link_error(
	struct link_state *state,
	const char *format,
	...)
{
	va_list arguments;

	/* The message and its end of line. */
	va_start(arguments, format);
	glsl_text_append(&state->log, "error: ", 7U, &state->failure);
	glsl_text_vprintf(&state->log, &state->failure, format, arguments);
	glsl_text_append(&state->log, "\n", 1U, &state->failure);
	va_end(arguments);

	/* Counted. */
	state->errors++;
}

/* Reports whether two types (possibly of two shaders) are the same type. */
static int
link_same_type(
	const struct glsl_type *left,
	const struct glsl_type *right)
{
	unsigned index;
	int differs;
	int same;

	/* Built-in types are the same objects. */
	if (left == right)
		return 1;
	if (left->kind != right->kind)
		return 0;

	/* Arrays of the same element and length. */
	if (left->kind == GLSL_KIND_ARRAY) {
		if (left->length != right->length)
			return 0;
		same = link_same_type(left->element, right->element);
		return same;
	}

	/* Structs of the same name and members. */
	if (left->kind != GLSL_KIND_STRUCT || left->field_count != right->field_count)
		return 0;
	differs = strcmp(left->name, right->name);
	if (differs != 0)
		return 0;
	for (index = 0U; index < left->field_count; index++) {
		differs = strcmp(left->fields[index].name, right->fields[index].name);
		if (differs != 0)
			return 0;
		same = link_same_type(left->fields[index].type, right->fields[index].type);
		if (!same)
			return 0;
	}

	/* Succeeded: the same. */
	return 1;
}

/* Adds a stage's used uniforms to the program's (one of a name, which both stages must agree on). */
static void
link_uniforms(
	struct link_state *state,
	struct glsl_shader *shader)
{
	struct glsl_symbol *symbol;
	unsigned index;
	int differs;
	int same;

	/* Each uniform the stage uses. */
	for (symbol = shader->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_UNIFORM)
			continue;

		/* The uniform of the same name the other stage added, if any. */
		for (index = 0U; index < state->uniform_count; index++) {
			differs = strcmp(state->uniforms[index].name, symbol->name);
			if (differs == 0)
				break;
		}

		/* One the other stage has: the same type. */
		if (index < state->uniform_count) {
			same = link_same_type(state->uniforms[index].type, symbol->type);
			if (!same)
				link_error(state, "uniform '%s' has different types in the two shaders", symbol->name);
			symbol->uniform = index;
			continue;
		}

		/* A new one. */
		if (state->uniform_count == LINK_MAX_UNIFORMS) {
			link_error(state, "too many uniforms");
			return;
		}

		/* The uniform, with its index. */
		state->uniforms[state->uniform_count].name = symbol->name;
		state->uniforms[state->uniform_count].type = symbol->type;
		state->uniforms[state->uniform_count].sampler = (symbol->type->kind == GLSL_KIND_SAMPLER);
		symbol->uniform = state->uniform_count;
		state->uniform_count++;
	}
}

/* Lays the uniforms out: std140 offsets in the block, bindings from 1 for samplers. */
static void
link_layout(
	struct link_state *state)
{
	struct glsl_link_uniform *uniform;
	uint32_t offset;
	uint32_t binding;
	unsigned alignment;
	unsigned index;

	/* Each uniform in order. */
	offset = 0U;
	binding = 1U;
	for (index = 0U; index < state->uniform_count; index++) {
		uniform = &state->uniforms[index];

		/* A sampler takes the next binding. */
		if (uniform->sampler) {
			uniform->binding = binding;
			binding++;
			continue;
		}

		/* Any other takes the next offset of its alignment. */
		alignment = glsl_std140_alignment(uniform->type);
		offset = (offset + alignment - 1U) & ~(alignment - 1U);
		uniform->offset = offset;
		offset += glsl_std140_size(uniform->type);
	}

	/* libGLESv2 binds sixteen texture units. */
	if (binding > 17U)
		link_error(state, "more than 16 samplers");
}

/*
 * Gives the uniform blocks their bindings: 32 on, in the order the stages
 * (vertex, geometry, fragment) use them; a block of one name in several
 * stages is one block (and must be the same).
 */
static void
link_blocks(
	struct link_state *state,
	struct glsl_shader **stages,
	unsigned count)
{
	struct glsl_symbol *symbol;
	struct glsl_symbol *other;
	unsigned binding;
	unsigned stage;
	unsigned earlier;
	int differs;
	int same;

	/* Each stage's blocks in order: an earlier stage's binding for a block of the same name, the next for a new one. */
	binding = LINK_FIRST_BLOCK_BINDING;
	for (stage = 0U; stage < count; stage++) {
		for (symbol = stages[stage]->globals; symbol != NULL; symbol = symbol->next_global) {
			if (!symbol->used || symbol->where != GLSL_VAR_BLOCK)
				continue;

			/* The same block in an earlier stage. */
			other = NULL;
			for (earlier = 0U; earlier < stage && other == NULL; earlier++) {
				for (other = stages[earlier]->globals; other != NULL; other = other->next_global) {
					if (!other->used || other->where != GLSL_VAR_BLOCK)
						continue;
					differs = strcmp(other->type->name, symbol->type->name);
					if (differs == 0)
						break;
				}
			}

			/* A new block takes the next binding. */
			if (other == NULL) {
				symbol->binding = binding;
				binding++;
				continue;
			}

			/* The same block: its binding, and the same members. */
			same = link_same_type(other->type, symbol->type);
			if (!same)
				link_error(state, "uniform block '%s' differs between the shaders", symbol->type->name);
			symbol->binding = other->binding;
		}
	}
}

/* Gives the vertex shader's attributes their locations: the bound ones, then the lowest free ones in order. */
static void
link_attributes(
	struct link_state *state,
	struct glsl_shader *vertex,
	const struct glsl_binding *bindings,
	unsigned binding_count)
{
	struct glsl_symbol *symbol;
	unsigned char taken[LINK_MAX_LOCATIONS];
	unsigned locations;
	unsigned location;
	unsigned index;
	unsigned slot;
	int bound;
	int differs;
	int free_run;

	/* The attributes with a layout location first. */
	memset(taken, 0, sizeof(taken));
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		symbol->location = GLSL_NO_LOCATION;
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		if (symbol->explicit_location == GLSL_NO_LOCATION)
			continue;
		locations = glsl_type_locations(symbol->type);
		if (symbol->explicit_location + locations > 16U) {
			link_error(state, "attribute '%s' has a location beyond the 16", symbol->name);
			continue;
		}

		/* The location, taken. */
		symbol->location = symbol->explicit_location;
		for (slot = 0U; slot < locations; slot++)
			taken[symbol->location + slot] = 1U;
	}

	/* Then the bound attributes. */
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		if (symbol->location != GLSL_NO_LOCATION)
			continue;
		for (index = 0U; index < binding_count; index++) {
			differs = strcmp(bindings[index].name, symbol->name);
			if (differs != 0)
				continue;

			/* The bound location, inside the sixteen. */
			locations = glsl_type_locations(symbol->type);
			if (bindings[index].location + locations > 16U) {
				link_error(state, "attribute '%s' is bound beyond the 16 locations", symbol->name);
				break;
			}

			/* The bound location, taken. */
			symbol->location = bindings[index].location;
			for (slot = 0U; slot < locations; slot++)
				taken[symbol->location + slot] = 1U;
			break;
		}
	}

	/* The others at the lowest free locations that fit them. */
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		if (symbol->location != GLSL_NO_LOCATION)
			continue;
		locations = glsl_type_locations(symbol->type);
		bound = 0;
		for (location = 0U; location + locations <= 16U && !bound; location++) {
			free_run = 1;
			for (slot = 0U; slot < locations; slot++) {
				if (taken[location + slot])
					free_run = 0;
			}

			/* A run of taken locations is passed over. */
			if (!free_run)
				continue;

			/* The attribute takes the run. */
			symbol->location = location;
			for (slot = 0U; slot < locations; slot++)
				taken[location + slot] = 1U;
			bound = 1;
		}

		/* No run fits it. */
		if (!bound)
			link_error(state, "too many attributes (16 locations)");
	}
}

/*
 * Gives the varyings between two stages their locations: the producer's
 * outputs in declaration order (those either stage uses), and each input
 * of the consumer the location of the output of its name (a geometry
 * shader's input is an array of the output's type).
 */
static void
link_varyings(
	struct link_state *state,
	struct glsl_shader *producer,
	struct glsl_shader *consumer)
{
	struct glsl_symbol *symbol;
	struct glsl_symbol *output;
	const struct glsl_type *type;
	unsigned location;
	int same;

	/* An input needs an output of its name and type, which is then declared. */
	for (symbol = consumer->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		type = link_input_type(consumer, symbol);
		output = link_find_varying(producer, symbol, type);
		if (output == NULL) {
			link_error(state, "a shader reads '%s', which the shader before it does not declare", symbol->name);
			continue;
		}

		/* Of the same type. */
		same = link_same_type(output->type, type);
		if (!same) {
			link_error(state, "varying '%s' has different types in the two shaders", symbol->name);
			continue;
		}

		/* The output is declared even when the producer does not use it. */
		output->used = 1U;
	}

	/* The producer's outputs in order. */
	location = 0U;
	for (symbol = producer->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_OUTPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		symbol->location = location;
		location += glsl_type_locations(symbol->type);
	}

	/* Sixteen locations in all. */
	if (location > 16U)
		link_error(state, "too many varyings (16 locations)");

	/* The inputs take their outputs' locations (and interpolation). */
	for (symbol = consumer->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		type = link_input_type(consumer, symbol);
		output = link_find_varying(producer, symbol, type);
		if (output == NULL)
			continue;
		symbol->location = output->location;
		if (output->interpolation == GLSL_INTERP_FLAT)
			symbol->interpolation = GLSL_INTERP_FLAT;
	}
}

/* Returns the type of the output an input reads: its own, or a geometry shader's input array's element. */
static const struct glsl_type *
link_input_type(
	const struct glsl_shader *consumer,
	const struct glsl_symbol *input)
{
	/* A geometry shader reads an array of the vertices' values. */
	if (consumer->stage == GLSL_STAGE_GEOMETRY && input->type->kind == GLSL_KIND_ARRAY)
		return input->type->element;

	/* Succeeded: the input's own type. */
	return input->type;
}

/* Gives the fragment shader's outputs their locations: gl_FragColor and gl_FragData 0, the others in order. */
static void
link_outputs(
	struct link_state *state,
	struct glsl_shader *fragment)
{
	struct glsl_symbol *symbol;
	unsigned char taken[LINK_MAX_LOCATIONS];
	unsigned location;
	unsigned builtins;
	unsigned outputs;
	unsigned unlocated;

	/* gl_FragColor and gl_FragData are colour output 0. */
	builtins = 0U;
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_OUTPUT)
			continue;
		if (symbol->builtin == GLSL_BUILTIN_FRAG_COLOR || symbol->builtin == GLSL_BUILTIN_FRAG_DATA) {
			symbol->location = 0U;
			builtins++;
		}
	}

	/* The shader's own outputs: those with a layout location first. */
	memset(taken, 0, sizeof(taken));
	outputs = 0U;
	unlocated = 0U;
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_OUTPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		outputs++;
		symbol->location = GLSL_NO_LOCATION;
		if (symbol->explicit_location == GLSL_NO_LOCATION) {
			unlocated++;
			continue;
		}

		/* The layout's location, taken. */
		symbol->location = symbol->explicit_location;
		(void)link_take_output(state, symbol, taken);
	}

	/* Then the others at the lowest free locations, in order. */
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_OUTPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		if (symbol->location != GLSL_NO_LOCATION)
			continue;
		for (location = 0U; location < LINK_MAX_LOCATIONS; location++) {
			if (taken[location])
				continue;
			symbol->location = location;
			break;
		}

		/* The locations taken. */
		(void)link_take_output(state, symbol, taken);
	}

	/* OpenGL ES 3.00 wants every output located when there are several. */
	if (fragment->es && outputs > 1U && unlocated != 0U)
		link_error(state, "with several fragment shader outputs, each needs a layout location");

	/* Both kinds cannot be written together. */
	if (builtins > 1U || (builtins != 0U && outputs != 0U))
		link_error(state, "the fragment shader writes more than one of gl_FragColor, gl_FragData and its own outputs");
}

/* Marks the locations a fragment output takes; -1 (reported) when one is taken already or out of range. */
static int
link_take_output(
	struct link_state *state,
	struct glsl_symbol *symbol,
	unsigned char *taken)
{
	unsigned locations;
	unsigned slot;

	/* Inside the range. */
	locations = glsl_type_locations(symbol->type);
	if (symbol->location + locations > LINK_MAX_LOCATIONS) {
		link_error(state, "output '%s' has no room for its locations", symbol->name);
		return -1;
	}

	/* Each location once. */
	for (slot = 0U; slot < locations; slot++) {
		if (taken[symbol->location + slot]) {
			link_error(state, "two fragment shader outputs share location %u", symbol->location + slot);
			return -1;
		}

		/* Taken now. */
		taken[symbol->location + slot] = 1U;
	}

	/* Succeeded: taken. */
	return 0;
}

/*
 * Finds the output of the stage before that an input (of the type it
 * reads: a geometry shader's input array's element) reads: a block by
 * its block name, anything else by its name.
 */
static struct glsl_symbol *
link_find_varying(
	struct glsl_shader *producer,
	struct glsl_symbol *input,
	const struct glsl_type *type)
{
	struct glsl_symbol *symbol;
	int differs;

	/* Anything but a block, by name. */
	if (type->kind != GLSL_KIND_STRUCT || type->block == 0U) {
		symbol = link_find(producer, input->name, GLSL_VAR_OUTPUT);
		return symbol;
	}

	/* A block: the output block of the same block name. */
	for (symbol = producer->globals; symbol != NULL; symbol = symbol->next_global) {
		if (symbol->where != GLSL_VAR_OUTPUT || symbol->type->kind != GLSL_KIND_STRUCT || symbol->type->block == 0U)
			continue;
		differs = strcmp(symbol->type->name, type->name);
		if (differs == 0)
			return symbol;
	}

	/* None. */
	return NULL;
}

/* Finds a global of a stage by name and kind (GLSL_VAR_*). */
static struct glsl_symbol *
link_find(
	struct glsl_shader *shader,
	const char *name,
	unsigned where)
{
	struct glsl_symbol *symbol;
	int differs;

	/* The globals in order. */
	for (symbol = shader->globals; symbol != NULL; symbol = symbol->next_global) {
		if (symbol->where != where)
			continue;
		differs = strcmp(symbol->name, name);
		if (differs == 0)
			return symbol;
	}

	/* None. */
	return NULL;
}

/* Lists the uniforms as the API reports them: the block's leaves by their reflected names, and the samplers. */
static void
link_info(
	struct link_state *state,
	struct glsl_program *program)
{
	struct glsl_uniform_info *infos;
	unsigned index;

	/* Room for every leaf (a struct array's leaves are many), the blocks' members included. */
	infos = calloc(LINK_MAX_LEAVES, sizeof(*infos));
	if (infos == NULL)
		longjmp(state->failure, 1);
	program->uniforms = infos;

	/* Each uniform's leaves, counted as they are named so that a failure frees every name. */
	for (index = 0U; index < state->uniform_count; index++)
		link_leaves(state, state->uniforms[index].type, state->uniforms[index].name, infos, &program->uniform_count, LINK_MAX_LEAVES);

	/* None of them is in a named block. */
	for (index = 0U; index < program->uniform_count; index++)
		infos[index].block = -1;
}

/*
 * Lists the named uniform blocks a stage reads, with their members: a
 * block the other stage listed already only gains the stage.  A block's
 * index among the program's blocks is its binding less the first
 * block's binding (link_blocks numbered them in this order).
 */
static void
link_block_infos(
	struct link_state *state,
	struct glsl_shader *shader,
	unsigned stage,
	struct glsl_program *program)
{
	struct glsl_block_info *blocks;
	struct glsl_block_info *block;
	struct glsl_symbol *symbol;
	const struct glsl_type *type;
	unsigned first_member;
	unsigned index;
	int differs;

	/* Room for every block, made at the first stage. */
	if (program->blocks == NULL) {
		blocks = calloc(LINK_MAX_BLOCKS, sizeof(*blocks));
		if (blocks == NULL)
			longjmp(state->failure, 1);
		program->blocks = blocks;
	}

	/* Each block the stage reads. */
	for (symbol = shader->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_BLOCK)
			continue;

		/* A binding outside the ones libGLESv2 binds is an error. */
		index = symbol->binding - LINK_FIRST_BLOCK_BINDING;
		if (symbol->binding < LINK_FIRST_BLOCK_BINDING || index >= LINK_MAX_BLOCKS) {
			link_error(state, "more than %u uniform blocks", LINK_MAX_BLOCKS);
			longjmp(state->failure, 1);
		}

		/* A block listed already (the vertex stage's) gains the stage. */
		block = &program->blocks[index];
		if (block->name != NULL) {
			block->stages |= 1U << stage;
			continue;
		}

		/* A new block: its name, binding and size. */
		type = symbol->type;
		block->name = link_name(state, type->name);
		block->binding = symbol->binding;
		block->size = glsl_std140_size(type);
		block->stages = 1U << stage;
		if (index + 1U > program->block_count)
			program->block_count = index + 1U;

		/* Its members, named after the block when it has an instance name, else by their own names. */
		first_member = program->uniform_count;
		differs = strcmp(symbol->name, type->name);
		if (differs != 0) {
			link_member_leaves(state, type, type->name, 0U, 0U, (int)index, program);
		} else {
			link_member_leaves(state, type, NULL, 0U, 0U, (int)index, program);
		}

		/* How many members it has. */
		block->member_count = program->uniform_count - first_member;
	}
}

/*
 * Adds the leaves of a named block's member of a type at a std140 offset
 * under a name (NULL: the block itself without an instance name, whose
 * members take their own names): struct members "name.member", struct
 * array elements "name[i]", and a leaf with its offset and strides.
 */
static void
link_member_leaves(
	struct link_state *state,
	const struct glsl_type *type,
	const char *name,
	unsigned offset,
	unsigned row_major,
	int block,
	struct glsl_program *program)
{
	struct glsl_uniform_info *info;
	const struct glsl_type *leaf;
	char member[256];
	unsigned alignment;
	unsigned stride;
	unsigned index;

	/* A struct: each member at its offset, with its own matrix order. */
	if (type->kind == GLSL_KIND_STRUCT) {
		for (index = 0U; index < type->field_count; index++) {
			alignment = glsl_std140_alignment(type->fields[index].type);
			offset = (offset + alignment - 1U) & ~(alignment - 1U);
			if (name != NULL) {
				(void)snprintf(member, sizeof(member), "%s.%s", name, type->fields[index].name);
			} else {
				(void)snprintf(member, sizeof(member), "%s", type->fields[index].name);
			}

			/* The member, then the next member's offset. */
			link_member_leaves(state, type->fields[index].type, member, offset, type->fields[index].row_major, block, program);
			offset += glsl_std140_member_size(type->fields[index].type, type->fields[index].row_major);
		}

		/* The members are in. */
		return;
	}

	/* An array of structs: each element at its stride. */
	if (type->kind == GLSL_KIND_ARRAY && type->element->kind == GLSL_KIND_STRUCT) {
		stride = glsl_std140_stride(type);
		for (index = 0U; index < type->length; index++) {
			(void)snprintf(member, sizeof(member), "%s[%u]", name, index);
			link_member_leaves(state, type->element, member, offset + index * stride, row_major, block, program);
		}

		/* The elements are in. */
		return;
	}

	/* A leaf (an array of leaves is one uniform of its length); without room it is left out. */
	if (program->uniform_count == LINK_MAX_LEAVES)
		return;
	info = &program->uniforms[program->uniform_count];
	info->name = link_name(state, name);
	program->uniform_count++;

	/* Its size, and the stride of an array's elements. */
	leaf = type;
	info->size = 1U;
	if (type->kind == GLSL_KIND_ARRAY) {
		leaf = type->element;
		info->size = type->length;
		info->array_stride = glsl_std140_member_stride(type, row_major);
	}

	/* Its kind, as a leaf of the default block has it. */
	info->components = leaf->components;
	info->columns = leaf->columns;
	info->base = GLSL_INFO_FLOAT;
	if (leaf->base == GLSL_BASE_INT)
		info->base = GLSL_INFO_INT;
	if (leaf->base == GLSL_BASE_UINT)
		info->base = GLSL_INFO_UINT;
	if (leaf->base == GLSL_BASE_BOOL)
		info->base = GLSL_INFO_BOOL;

	/* Where it is in the block: its offset, and a matrix's columns (or rows) 16 bytes apart. */
	info->block = block;
	info->offset = offset;
	if (leaf->kind == GLSL_KIND_MATRIX) {
		info->matrix_stride = 16U;
		info->row_major = row_major;
	}
}

/*
 * Finds the vertex shader's outputs of the names a program captures
 * (transform feedback), in order, and lays out a vertex's record of
 * them; the shader's vertex and instance numbers, which place the
 * records, are used too.  Errors go to the log.
 */
static void
link_captures(
	struct link_state *state,
	struct glsl_shader *vertex,
	const char *const *captures,
	unsigned capture_count)
{
	struct glsl_symbol *symbol;
	struct glsl_symbol *vertex_id;
	struct glsl_symbol *instance_id;
	unsigned index;
	unsigned other;
	unsigned words;

	/* Nothing captured. */
	state->capture_count = 0U;
	state->capture_stride = 0U;
	if (capture_count == 0U)
		return;
	if (capture_count > LINK_MAX_CAPTURES) {
		link_error(state, "too many transform feedback varyings");
		return;
	}

	/* The vertex's and the instance's numbers (GLSL ES 3.00 and GLSL 1.30 on have them). */
	vertex_id = link_find(vertex, "gl_VertexID", GLSL_VAR_INPUT);
	instance_id = link_find(vertex, "gl_InstanceID", GLSL_VAR_INPUT);
	if (vertex_id == NULL || instance_id == NULL) {
		link_error(state, "transform feedback needs a vertex shader of GLSL ES 3.00 or GLSL 1.30 on");
		return;
	}

	/* Emitted: the records are placed by them. */
	vertex_id->used = 1;
	instance_id->used = 1;

	/* Each name: an output of the vertex shader of numbers, named once. */
	for (index = 0U; index < capture_count; index++) {
		symbol = link_find(vertex, captures[index], GLSL_VAR_OUTPUT);
		if (symbol == NULL) {
			link_error(state, "transform feedback varying '%s' is not an output of the vertex shader", captures[index]);
			continue;
		}

		/* The words it takes. */
		words = link_capture_words(symbol->type);
		if (words == 0U) {
			link_error(state, "transform feedback varying '%s' cannot be captured", captures[index]);
			continue;
		}

		/* Named once. */
		for (other = 0U; other < state->capture_count; other++) {
			if (state->captures[other].symbol == symbol)
				link_error(state, "transform feedback varying '%s' is named twice", captures[index]);
		}

		/* Its place in the record; it is emitted even when the shader does not write it. */
		symbol->used = 1;
		state->captures[state->capture_count].symbol = symbol;
		state->captures[state->capture_count].offset = state->capture_stride;
		state->capture_count++;
		state->capture_stride += words;
	}
}

/* Returns the words a captured output of a type takes (a scalar, vector or matrix of numbers, or an array of one), 0 for a type that cannot be captured. */
static unsigned
link_capture_words(
	const struct glsl_type *type)
{
	unsigned words;

	/* An array: its elements' words. */
	if (type->kind == GLSL_KIND_ARRAY) {
		if (type->element->kind == GLSL_KIND_ARRAY)
			return 0U;
		words = link_capture_words(type->element);
		return words * type->length;
	}

	/* Numbers only: floats, ints and unsigned ints. */
	if (type->kind != GLSL_KIND_SCALAR && type->kind != GLSL_KIND_VECTOR && type->kind != GLSL_KIND_MATRIX)
		return 0U;
	if (type->base != GLSL_BASE_FLOAT && type->base != GLSL_BASE_INT && type->base != GLSL_BASE_UINT)
		return 0U;

	/* Succeeded: a word per component. */
	return type->components * type->columns;
}

/* Lists the captured outputs as the API reports them, with their places in a vertex's record. */
static void
link_capture_infos(
	struct link_state *state,
	struct glsl_program *program)
{
	struct glsl_capture_info *info;
	const struct glsl_type *type;
	unsigned index;

	/* Nothing captured. */
	if (state->capture_count == 0U)
		return;

	/* The list. */
	program->captures = calloc(state->capture_count, sizeof(*program->captures));
	if (program->captures == NULL)
		longjmp(state->failure, 1);

	/* Each output: its name, its type (an array's element's, with the length), and its place. */
	for (index = 0U; index < state->capture_count; index++) {
		info = &program->captures[index];
		type = state->captures[index].symbol->type;
		info->size = 1U;
		if (type->kind == GLSL_KIND_ARRAY) {
			info->size = type->length;
			type = type->element;
		}

		/* Its name. */
		info->name = link_name(state, state->captures[index].symbol->name);
		program->capture_count = index + 1U;
		info->base = GLSL_INFO_FLOAT;
		if (type->base == GLSL_BASE_INT)
			info->base = GLSL_INFO_INT;
		if (type->base == GLSL_BASE_UINT)
			info->base = GLSL_INFO_UINT;
		info->components = type->components;
		info->columns = type->columns;
		info->offset = state->captures[index].offset;
		info->words = type->components * type->columns * info->size;
	}

	/* The record's words. */
	program->capture_stride = state->capture_stride;
}

/* Copies a name into memory of its own, which glsl_program_free frees. */
static char *
link_name(
	struct link_state *state,
	const char *name)
{
	char *copy;
	size_t length;

	/* The copy with its terminator. */
	length = strlen(name);
	copy = malloc(length + 1U);
	if (copy == NULL)
		longjmp(state->failure, 1);
	memcpy(copy, name, length + 1U);

	/* Succeeded: the copy. */
	return copy;
}

/* Adds the leaves of a uniform of a type under a name: struct members "name.member", struct array elements "name[i]". */
static void
link_leaves(
	struct link_state *state,
	const struct glsl_type *type,
	const char *name,
	struct glsl_uniform_info *out,
	unsigned *count,
	unsigned capacity)
{
	struct glsl_uniform_info *info;
	const struct glsl_type *leaf;
	char member[256];
	unsigned index;
	size_t length;

	/* A struct: each member. */
	if (type->kind == GLSL_KIND_STRUCT) {
		for (index = 0U; index < type->field_count; index++) {
			(void)snprintf(member, sizeof(member), "%s.%s", name, type->fields[index].name);
			link_leaves(state, type->fields[index].type, member, out, count, capacity);
		}

		/* The members are in. */
		return;
	}

	/* An array of structs: each element. */
	if (type->kind == GLSL_KIND_ARRAY && type->element->kind == GLSL_KIND_STRUCT) {
		for (index = 0U; index < type->length; index++) {
			(void)snprintf(member, sizeof(member), "%s[%u]", name, index);
			link_leaves(state, type->element, member, out, count, capacity);
		}

		/* The elements are in. */
		return;
	}

	/* A leaf (an array of leaves is one uniform of its length). */
	if (*count == capacity)
		return;
	info = &out[*count];
	leaf = type;
	info->size = 1U;
	if (type->kind == GLSL_KIND_ARRAY) {
		leaf = type->element;
		info->size = type->length;
	}

	/* Its name. */
	length = strlen(name);
	info->name = malloc(length + 1U);
	if (info->name == NULL)
		longjmp(state->failure, 1);
	memcpy(info->name, name, length + 1U);
	(*count)++;

	/* Its kind. */
	info->components = leaf->components;
	info->columns = leaf->columns;
	info->sampler = leaf->sampler;
	info->shadow = leaf->shadow;
	info->arrayed = leaf->arrayed;
	switch (leaf->base) {
	case GLSL_BASE_INT:
		info->base = GLSL_INFO_INT;
		break;
	case GLSL_BASE_UINT:
		info->base = GLSL_INFO_UINT;
		break;
	case GLSL_BASE_BOOL:
		info->base = GLSL_INFO_BOOL;
		break;
	default:
		info->base = GLSL_INFO_FLOAT;
		break;
	}
}

/* Copies words out of the arena into memory of their own (NULL when there is none). */
static uint32_t *
link_copy(
	const uint32_t *code,
	size_t words)
{
	uint32_t *copy;

	/* The copy. */
	copy = malloc(words * sizeof(uint32_t));
	if (copy == NULL)
		return NULL;
	memcpy(copy, code, words * sizeof(uint32_t));

	/* Succeeded: the copy. */
	return copy;
}

/*
 * Links a compute shader alone into SPIR-V (ws101-p008): its default
 * uniform block and uniform blocks are laid out as a draw program's, its
 * storage blocks take their descriptor bindings from their layouts, and
 * the one stage is emitted.
 */
int
glsl_link_compute(
	const struct glsl_shader *compute_shader,
	struct glsl_program *program,
	char **log)
{
	struct link_state *volatile state;
	struct glsl_shader *compute;
	struct glsl_shader *stages[1];
	uint32_t *code;
	size_t words;

	/* Nothing yet. */
	*log = NULL;
	memset(program, 0, sizeof(*program));
	state = calloc(1U, sizeof(*state));
	if (state == NULL)
		return -1;
	state->arena.failure = &state->failure;
	compute = (struct glsl_shader *)compute_shader;

	/*
	 * Out of memory comes back here.  setjmp is the whole controlling
	 * expression, the form C allows it in.
	 */
	switch (setjmp(state->failure)) {
	case 0:
		break;
	default:
		/* The link failed: what it made goes, and the log (if any) tells why. */
		glsl_program_free(program);
		glsl_arena_free(&state->arena);
		*log = state->log.data;
		free(state);
		return -1;
	}

	/* A compute shader alone. */
	if (compute->stage != GLSL_STAGE_COMPUTE)
		link_error(state, "a compute program needs a compute shader and no other");
	if (state->errors != 0U)
		longjmp(state->failure, 1);

	/* The uniforms and uniform blocks, laid out; the storage blocks, bound. */
	stages[0] = compute;
	link_uniforms(state, compute);
	link_layout(state);
	link_blocks(state, stages, 1U);
	link_storages(state, compute, program);
	if (state->errors != 0U)
		longjmp(state->failure, 1);

	/* The stage's SPIR-V, copied out of the arena, and its workgroup size. */
	code = glsl_emit(compute, &state->arena, state->uniforms, state->uniform_count, NULL, 0U, 0U, &words);
	program->code[GLSL_STAGE_COMPUTE] = link_copy(code, words);
	program->words[GLSL_STAGE_COMPUTE] = words;
	if (program->code[GLSL_STAGE_COMPUTE] == NULL)
		longjmp(state->failure, 1);
	program->local_size[0] = compute->local_size[0];
	program->local_size[1] = compute->local_size[1];
	program->local_size[2] = compute->local_size[2];

	/* What the API reports of the uniforms and the uniform blocks. */
	link_info(state, program);
	link_block_infos(state, compute, GLSL_STAGE_COMPUTE, program);

	/* Succeeded: the program. */
	glsl_arena_free(&state->arena);
	free(state->log.data);
	free(state);
	return 0;
}

/*
 * Binds a compute shader's storage blocks (ws101-p008): each at set 0,
 * GLSL_STORAGE_FIRST_BINDING plus its layout's binding, one block a
 * binding; lists them for the API: name, binding, the bytes before a
 * run-time array, its stride, and whether it is readonly.
 */
static void
link_storages(
	struct link_state *state,
	struct glsl_shader *compute,
	struct glsl_program *program)
{
	struct glsl_storage_info *infos;
	struct glsl_storage_info *info;
	struct glsl_symbol *symbol;
	const struct glsl_type *type;
	const struct glsl_type *last;
	unsigned taken;

	/* Room for every binding. */
	infos = calloc(GLSL_STORAGE_BINDINGS, sizeof(*infos));
	if (infos == NULL)
		longjmp(state->failure, 1);
	program->storages = infos;

	/* Each block the code uses. */
	taken = 0U;
	for (symbol = compute->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_BUFFER)
			continue;

		/* One block a binding. */
		if ((taken & (1U << symbol->layout_binding)) != 0U) {
			link_error(state, "two buffer blocks at binding %u", symbol->layout_binding);
			continue;
		}
		taken |= 1U << symbol->layout_binding;
		symbol->binding = GLSL_STORAGE_FIRST_BINDING + symbol->layout_binding;

		/* What the API reports of it. */
		type = symbol->type;
		info = &infos[program->storage_count];
		info->name = link_name(state, type->name);
		info->binding = symbol->layout_binding;
		info->size = glsl_std430_size(type);
		info->array_stride = 0U;
		if (type->field_count != 0U) {
			last = type->fields[type->field_count - 1U].type;
			if (last->kind == GLSL_KIND_ARRAY && last->length == 0U)
				info->array_stride = glsl_std430_stride(last);
		}
		info->readonly = ((symbol->memory & GLSL_MEMORY_READONLY) != 0U);
		program->storage_count++;
	}
}

