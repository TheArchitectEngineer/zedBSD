/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's link: a vertex and a fragment shader are matched
 * (uniforms of one name must agree, every varying the fragment shader
 * reads must be one the vertex shader declares), the program's uniforms
 * are laid out once for both (std140 offsets in the default uniform
 * block, sampler bindings from 1), attributes and varyings get their
 * locations, and each stage is emitted as SPIR-V.
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

/* The binding of the first uniform block (the default block is 0, the samplers 1 to 16). */
#define LINK_FIRST_BLOCK_BINDING 32U

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
};

static void link_error(struct link_state *state, const char *format, ...);
static int link_same_type(const struct glsl_type *left, const struct glsl_type *right);
static void link_uniforms(struct link_state *state, struct glsl_shader *shader);
static void link_layout(struct link_state *state);
static void link_blocks(struct link_state *state, struct glsl_shader *vertex, struct glsl_shader *fragment);
static void link_attributes(struct link_state *state, struct glsl_shader *vertex, const struct glsl_binding *bindings, unsigned binding_count);
static void link_varyings(struct link_state *state, struct glsl_shader *vertex, struct glsl_shader *fragment);
static void link_outputs(struct link_state *state, struct glsl_shader *fragment);
static struct glsl_symbol *link_find(struct glsl_shader *shader, const char *name, unsigned where);
static struct glsl_symbol *link_find_varying(struct glsl_shader *vertex, struct glsl_symbol *input);
static int link_take_output(struct link_state *state, struct glsl_symbol *symbol, unsigned char *taken);
static void link_info(struct link_state *state, struct glsl_program *program);
static void link_leaves(struct link_state *state, const struct glsl_type *type, const char *name, struct glsl_uniform_info *out, unsigned *count, unsigned capacity);
static uint32_t *link_copy(const uint32_t *code, size_t words);

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
	struct link_state *volatile state;
	struct glsl_shader *vertex;
	struct glsl_shader *fragment;
	uint32_t *code;
	size_t words;

	/* Nothing yet. */
	*log = NULL;
	memset(program, 0, sizeof(*program));
	state = calloc(1U, sizeof(*state));
	if (state == NULL)
		return -1;
	state->arena.failure = &state->failure;

	/* The link records its decisions in the shaders' symbols. */
	vertex = (struct glsl_shader *)vertex_shader;
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

	/* The stages, and one language for both. */
	if (vertex->stage != GLSL_STAGE_VERTEX || fragment->stage != GLSL_STAGE_FRAGMENT)
		link_error(state, "a program needs a vertex and a fragment shader");
	if (vertex->es != fragment->es)
		link_error(state, "OpenGL ES and desktop GLSL shaders cannot be linked together");

	/* The uniforms, laid out; the attributes, varyings and outputs, located. */
	link_uniforms(state, vertex);
	link_uniforms(state, fragment);
	link_layout(state);
	link_blocks(state, vertex, fragment);
	link_attributes(state, vertex, bindings, binding_count);
	link_varyings(state, vertex, fragment);
	link_outputs(state, fragment);
	if (state->errors != 0U)
		longjmp(state->failure, 1);

	/* Each stage's SPIR-V, copied out of the arena. */
	code = glsl_emit(vertex, &state->arena, state->uniforms, state->uniform_count, &words);
	program->code[0] = link_copy(code, words);
	program->words[0] = words;
	code = glsl_emit(fragment, &state->arena, state->uniforms, state->uniform_count, &words);
	program->code[1] = link_copy(code, words);
	program->words[1] = words;
	if (program->code[0] == NULL || program->code[1] == NULL)
		longjmp(state->failure, 1);

	/* What the API reports of the uniforms. */
	link_info(state, program);

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

	/* The SPIR-V. */
	free(program->code[0]);
	free(program->code[1]);
	program->code[0] = NULL;
	program->code[1] = NULL;

	/* The uniforms' names and the list. */
	for (index = 0U; index < program->uniform_count; index++)
		free(program->uniforms[index].name);
	free(program->uniforms);
	program->uniforms = NULL;
	program->uniform_count = 0U;
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
 * Gives the uniform blocks their bindings: 32 on, in the order the vertex
 * shader and then the fragment shader use them; a block of one name in
 * both stages is one block (and must be the same).
 */
static void
link_blocks(
	struct link_state *state,
	struct glsl_shader *vertex,
	struct glsl_shader *fragment)
{
	struct glsl_symbol *symbol;
	struct glsl_symbol *other;
	unsigned binding;
	int differs;
	int same;

	/* The vertex shader's blocks in order. */
	binding = LINK_FIRST_BLOCK_BINDING;
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_BLOCK)
			continue;
		symbol->binding = binding;
		binding++;
	}

	/* The fragment shader's: the vertex shader's binding for a block of the same name, the next for a new one. */
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_BLOCK)
			continue;
		symbol->binding = binding;
		for (other = vertex->globals; other != NULL; other = other->next_global) {
			if (!other->used || other->where != GLSL_VAR_BLOCK)
				continue;
			differs = strcmp(other->type->name, symbol->type->name);
			if (differs != 0)
				continue;

			/* The same block: its binding, and the same members. */
			same = link_same_type(other->type, symbol->type);
			if (!same)
				link_error(state, "uniform block '%s' differs between the two shaders", symbol->type->name);
			symbol->binding = other->binding;
			break;
		}

		/* A new block takes the next binding. */
		if (other == NULL)
			binding++;
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
 * Gives the varyings their locations: the vertex shader's outputs in
 * declaration order (those either stage uses), and each fragment input
 * the location of the vertex output of its name.
 */
static void
link_varyings(
	struct link_state *state,
	struct glsl_shader *vertex,
	struct glsl_shader *fragment)
{
	struct glsl_symbol *symbol;
	struct glsl_symbol *output;
	unsigned location;
	int same;

	/* A fragment input needs a vertex output of its name and type, which is then declared. */
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		output = link_find_varying(vertex, symbol);
		if (output == NULL) {
			link_error(state, "the fragment shader reads '%s', which the vertex shader does not declare", symbol->name);
			continue;
		}

		/* Of the same type. */
		same = link_same_type(output->type, symbol->type);
		if (!same) {
			link_error(state, "varying '%s' has different types in the two shaders", symbol->name);
			continue;
		}

		/* The vertex output is declared even when the vertex shader does not use it. */
		output->used = 1U;
	}

	/* The vertex outputs in order. */
	location = 0U;
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_OUTPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		symbol->location = location;
		location += glsl_type_locations(symbol->type);
	}

	/* Sixteen locations in all. */
	if (location > 16U)
		link_error(state, "too many varyings (16 locations)");

	/* The fragment inputs take their outputs' locations (and interpolation). */
	for (symbol = fragment->globals; symbol != NULL; symbol = symbol->next_global) {
		if (!symbol->used || symbol->where != GLSL_VAR_INPUT || symbol->builtin != GLSL_BUILTIN_NONE)
			continue;
		output = link_find_varying(vertex, symbol);
		if (output == NULL)
			continue;
		symbol->location = output->location;
		if (output->interpolation == GLSL_INTERP_FLAT)
			symbol->interpolation = GLSL_INTERP_FLAT;
	}
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
 * Finds the vertex shader output a fragment shader input reads: a block
 * by its block name, anything else by its name.
 */
static struct glsl_symbol *
link_find_varying(
	struct glsl_shader *vertex,
	struct glsl_symbol *input)
{
	struct glsl_symbol *symbol;
	int differs;

	/* Anything but a block, by name. */
	if (input->type->kind != GLSL_KIND_STRUCT || input->type->block == 0U) {
		symbol = link_find(vertex, input->name, GLSL_VAR_OUTPUT);
		return symbol;
	}

	/* A block: the output block of the same block name. */
	for (symbol = vertex->globals; symbol != NULL; symbol = symbol->next_global) {
		if (symbol->where != GLSL_VAR_OUTPUT || symbol->type->kind != GLSL_KIND_STRUCT || symbol->type->block == 0U)
			continue;
		differs = strcmp(symbol->type->name, input->type->name);
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
	unsigned count;
	unsigned index;

	/* Room for every leaf (a struct array's leaves are many). */
	infos = calloc(1024U, sizeof(*infos));
	if (infos == NULL)
		longjmp(state->failure, 1);
	program->uniforms = infos;

	/* Each uniform's leaves. */
	count = 0U;
	for (index = 0U; index < state->uniform_count; index++)
		link_leaves(state, state->uniforms[index].type, state->uniforms[index].name, infos, &count, 1024U);
	program->uniform_count = count;
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
