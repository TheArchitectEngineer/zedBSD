/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's built-in functions in SPIR-V: GLSL.std.450's
 * extended instructions, single SPIR-V instructions, and the ones built
 * of several (texture lookups, dot of scalars, any and all without OpAny
 * and OpAll, projective lookups as a division, faceforward as a select).
 */

#include "emit.h"

#include <string.h>

/* GLSL.std.450's refract, whose last operand stays a scalar. */
#define BUILTIN_REFRACT		72U

static struct emit_value builtin_ext(struct emit_state *state, struct glsl_node *node, struct emit_value *arguments, unsigned count);
static struct emit_value builtin_special(struct emit_state *state, struct glsl_node *node, struct emit_value *arguments, unsigned count);
static struct emit_value builtin_texture(struct emit_state *state, struct glsl_node *node, struct emit_value *arguments, unsigned count);
static struct emit_value builtin_texture_query(struct emit_state *state, struct glsl_node *node, struct emit_value *arguments);
static uint32_t builtin_coordinate(struct emit_state *state, struct emit_value coordinate, unsigned size, int project, uint32_t *reference, int shadow);
static unsigned builtin_dimension(const struct glsl_type *sampler);
static uint32_t builtin_image(struct emit_state *state, struct emit_value sampler);
static struct emit_value builtin_value(uint32_t id, const struct glsl_type *type);

/*
 * Emits a call of a built-in function on evaluated arguments.
 */
struct emit_value
glsl_emit_builtin(
	struct emit_state *state,
	struct glsl_node *node,
	struct emit_value *arguments,
	unsigned count)
{
	const struct glsl_builtin *builtin;
	struct emit_value result;
	uint32_t operands[8];
	unsigned index;

	/* The kinds of built-ins. */
	builtin = node->builtin;
	if (builtin->operation == GLSL_BI_EXT) {
		result = builtin_ext(state, node, arguments, count);
		return result;
	}

	/* The ones made of several instructions. */
	if (builtin->operation == GLSL_BI_SPECIAL) {
		result = builtin_special(state, node, arguments, count);
		return result;
	}

	/* One SPIR-V instruction on the arguments. */
	for (index = 0U; index < count; index++)
		operands[index] = arguments[index].id;
	result = builtin_value(glsl_emit_op(state, builtin->number, glsl_emit_type(state, node->type), operands, count), node->type);

	/* Succeeded: the result. */
	return result;
}

/* Emits a GLSL.std.450 instruction, a scalar argument spread to the result's vector (but refract's eta). */
static struct emit_value
builtin_ext(
	struct emit_state *state,
	struct glsl_node *node,
	struct emit_value *arguments,
	unsigned count)
{
	uint32_t operands[10];
	unsigned index;
	struct emit_value result;
	const char *generic;

	/* The set and the instruction number. */
	operands[0] = state->module->std450;
	operands[1] = node->builtin->number;

	/* Whether the result is generic (a scalar operand is then made a vector like it; unpacking is not). */
	generic = strchr("GIU", node->builtin->signature[0]);

	/* The operands, a scalar one made a vector when the result is a generic vector. */
	for (index = 0U; index < count; index++) {
		operands[2U + index] = arguments[index].id;
		if (node->builtin->number == BUILTIN_REFRACT && index == 2U)
			continue;
		if (generic == NULL)
			continue;
		if (arguments[index].type->kind == GLSL_KIND_SCALAR && node->type->kind == GLSL_KIND_VECTOR)
			operands[2U + index] = glsl_emit_splat(state, arguments[index].id, glsl_type_with_base(node->type, arguments[index].type->base));
	}

	/* Succeeded: OpExtInst. */
	result = builtin_value(glsl_emit_op(state, SPV_OP_EXT_INST, glsl_emit_type(state, node->type), operands, 2U + count), node->type);
	return result;
}

/* Emits the built-ins made of several instructions. */
static struct emit_value
builtin_special(
	struct emit_state *state,
	struct glsl_node *node,
	struct emit_value *arguments,
	unsigned count)
{
	const struct glsl_type *type;
	const struct glsl_type *bool_type;
	struct emit_value result;
	struct emit_value negated;
	uint32_t operands[4];
	uint32_t columns[4];
	uint32_t id;
	unsigned column;
	struct emit_value left_column;
	struct emit_value right_column;

	/* The built-in. */
	type = node->type;
	bool_type = glsl_type_scalar(GLSL_BASE_BOOL);
	switch (node->builtin->number) {
	case GLSL_SPECIAL_TEXTURE:
	case GLSL_SPECIAL_TEXTURE_BIAS:
	case GLSL_SPECIAL_TEXTURE_PROJ:
	case GLSL_SPECIAL_TEXTURE_PROJ_BIAS:
	case GLSL_SPECIAL_TEXTURE_LOD:
	case GLSL_SPECIAL_TEXTURE_PROJ_LOD:
	case GLSL_SPECIAL_SHADOW:
	case GLSL_SPECIAL_SHADOW_PROJ:
	case GLSL_SPECIAL_TEXTURE_GRAD:
	case GLSL_SPECIAL_TEXTURE_OFFSET:
	case GLSL_SPECIAL_TEXTURE_OFFSET_BIAS:
	case GLSL_SPECIAL_TEXTURE_LOD_OFFSET:
		result = builtin_texture(state, node, arguments, count);
		return result;
	case GLSL_SPECIAL_TEXTURE_SIZE:
	case GLSL_SPECIAL_TEXEL_FETCH:
		result = builtin_texture_query(state, node, arguments);
		return result;
	case GLSL_SPECIAL_MOD:
		/* x - y * floor(x / y) is OpFMod (a scalar y spread first). */
		operands[0] = arguments[0].id;
		operands[1] = glsl_emit_splat(state, arguments[1].id, type);
		if (arguments[1].type->kind != GLSL_KIND_SCALAR)
			operands[1] = arguments[1].id;
		result = builtin_value(glsl_emit_op(state, SPV_OP_F_MOD, glsl_emit_type(state, type), operands, 2U), type);
		return result;
	case GLSL_SPECIAL_DOT:
		/* The dot product of scalars is their product. */
		operands[0] = arguments[0].id;
		operands[1] = arguments[1].id;
		if (arguments[0].type->kind == GLSL_KIND_SCALAR) {
			id = glsl_emit_op(state, SPV_OP_F_MUL, glsl_emit_type(state, type), operands, 2U);
		} else {
			id = glsl_emit_op(state, SPV_OP_DOT, glsl_emit_type(state, type), operands, 2U);
		}

		/* Succeeded: the dot product. */
		result = builtin_value(id, type);
		return result;
	case GLSL_SPECIAL_ANY:
	case GLSL_SPECIAL_ALL:
		id = glsl_emit_all(state, arguments[0].id, arguments[0].type->components, node->builtin->number == GLSL_SPECIAL_ANY);
		result = builtin_value(id, type);
		return result;
	case GLSL_SPECIAL_NOT:
		result = builtin_value(glsl_emit_op(state, SPV_OP_LOGICAL_NOT, glsl_emit_type(state, type), &arguments[0].id, 1U), type);
		return result;
	case GLSL_SPECIAL_MIX_BOOL:
		/* mix(x, y, a) with bools: y where a is true. */
		operands[0] = arguments[2].id;
		operands[1] = arguments[1].id;
		operands[2] = arguments[0].id;
		result = builtin_value(glsl_emit_op(state, SPV_OP_SELECT, glsl_emit_type(state, type), operands, 3U), type);
		return result;
	case GLSL_SPECIAL_MATRIX_COMP_MULT:
		/* The columns multiplied component by component. */
		for (column = 0U; column < type->columns; column++) {
			left_column = builtin_value(glsl_emit_extract(state, arguments[0], column), glsl_type_column(type));
			right_column = builtin_value(glsl_emit_extract(state, arguments[1], column), glsl_type_column(type));
			operands[0] = left_column.id;
			operands[1] = right_column.id;
			columns[column] = glsl_emit_op(state, SPV_OP_F_MUL, glsl_emit_type(state, glsl_type_column(type)), operands, 2U);
		}

		/* Succeeded: the matrix. */
		result = builtin_value(glsl_emit_op(state, SPV_OP_COMPOSITE_CONSTRUCT, glsl_emit_type(state, type), columns, type->columns), type);
		return result;
	case GLSL_SPECIAL_OUTER_PRODUCT:
		operands[0] = arguments[0].id;
		operands[1] = arguments[1].id;
		result = builtin_value(glsl_emit_op(state, SPV_OP_OUTER_PRODUCT, glsl_emit_type(state, type), operands, 2U), type);
		return result;
	case GLSL_SPECIAL_NOISE:
		/* Noise is 0 (the specification allows it). */
		result = builtin_value(glsl_emit_splat(state, glsl_emit_float(state, 0.0f), type), type);
		return result;
	default:
		break;
	}

	/* faceforward(N, I, Nref): N when dot(Nref, I) < 0, else -N (a select; i915 lacks FaceForward). */
	operands[0] = arguments[2].id;
	operands[1] = arguments[1].id;
	if (arguments[0].type->kind == GLSL_KIND_SCALAR) {
		id = glsl_emit_op(state, SPV_OP_F_MUL, glsl_emit_type(state, glsl_type_scalar(GLSL_BASE_FLOAT)), operands, 2U);
	} else {
		id = glsl_emit_op(state, SPV_OP_DOT, glsl_emit_type(state, glsl_type_scalar(GLSL_BASE_FLOAT)), operands, 2U);
	}

	/* Whether the reference faces away. */
	operands[0] = id;
	operands[1] = glsl_emit_float(state, 0.0f);
	id = glsl_emit_op(state, SPV_OP_F_ORD_LESS_THAN, glsl_emit_type(state, bool_type), operands, 2U);
	negated = builtin_value(glsl_emit_op(state, SPV_OP_F_NEGATE, glsl_emit_type(state, type), &arguments[0].id, 1U), type);
	operands[0] = glsl_emit_splat(state, id, glsl_type_with_base(type, GLSL_BASE_BOOL));
	operands[1] = arguments[0].id;
	operands[2] = negated.id;
	result = builtin_value(glsl_emit_op(state, SPV_OP_SELECT, glsl_emit_type(state, type), operands, 3U), type);

	/* Succeeded: the faced-forward vector. */
	return result;
}

/*
 * Emits a texture lookup: the coordinate (divided by its last component
 * for a projective lookup), the depth reference of a shadow lookup, a
 * bias or a level, and the sample instruction (an explicit level 0 in a
 * vertex shader, which has no implicit level).
 */
static struct emit_value
builtin_texture(
	struct emit_state *state,
	struct glsl_node *node,
	struct emit_value *arguments,
	unsigned count)
{
	const struct glsl_type *sampler;
	const struct glsl_type *texel;
	struct emit_value result;
	uint32_t operands[10];
	uint32_t coordinate;
	uint32_t reference;
	uint32_t opcode;
	uint32_t parts[4];
	unsigned special;
	unsigned operand_count;
	unsigned mask;
	uint32_t bias;
	uint32_t lod;
	uint32_t gradient_x;
	uint32_t gradient_y;
	uint32_t offset;
	int project;
	int shadow;
	int explicit_lod;

	/* What kind of lookup. */
	(void)count;
	special = node->builtin->number;
	sampler = arguments[0].type;
	shadow = (int)sampler->shadow;
	project = 0;
	if (special == GLSL_SPECIAL_TEXTURE_PROJ || special == GLSL_SPECIAL_TEXTURE_PROJ_BIAS ||
	    special == GLSL_SPECIAL_TEXTURE_PROJ_LOD || special == GLSL_SPECIAL_SHADOW_PROJ)
		project = 1;

	/* The operands after the coordinate, by the kind of lookup. */
	bias = 0U;
	lod = 0U;
	gradient_x = 0U;
	gradient_y = 0U;
	offset = 0U;
	switch (special) {
	case GLSL_SPECIAL_TEXTURE_BIAS:
	case GLSL_SPECIAL_TEXTURE_PROJ_BIAS:
		bias = arguments[2].id;
		break;
	case GLSL_SPECIAL_TEXTURE_LOD:
	case GLSL_SPECIAL_TEXTURE_PROJ_LOD:
		lod = arguments[2].id;
		break;
	case GLSL_SPECIAL_TEXTURE_GRAD:
		gradient_x = arguments[2].id;
		gradient_y = arguments[3].id;
		break;
	case GLSL_SPECIAL_TEXTURE_OFFSET:
		offset = arguments[2].id;
		break;
	case GLSL_SPECIAL_TEXTURE_OFFSET_BIAS:
		offset = arguments[2].id;
		bias = arguments[3].id;
		break;
	case GLSL_SPECIAL_TEXTURE_LOD_OFFSET:
		lod = arguments[2].id;
		offset = arguments[3].id;
		break;
	default:
		break;
	}

	/* The coordinate (and the depth reference). */
	reference = 0U;
	coordinate = builtin_coordinate(state, arguments[1], builtin_dimension(sampler), project, &reference, shadow);

	/* The instruction: an explicit level for level and gradient lookups, and in vertex shaders (a level 0). */
	explicit_lod = 0;
	if (lod != 0U || gradient_x != 0U || state->shader->stage == GLSL_STAGE_VERTEX)
		explicit_lod = 1;
	if (explicit_lod && lod == 0U && gradient_x == 0U)
		lod = glsl_emit_float(state, 0.0f);
	if (shadow) {
		opcode = SPV_OP_IMAGE_SAMPLE_DREF_IMPLICIT_LOD;
		if (explicit_lod)
			opcode = SPV_OP_IMAGE_SAMPLE_DREF_EXPLICIT_LOD;
	} else {
		opcode = SPV_OP_IMAGE_SAMPLE_IMPLICIT_LOD;
		if (explicit_lod)
			opcode = SPV_OP_IMAGE_SAMPLE_EXPLICIT_LOD;
	}

	/* The operands: the sampled image, the coordinate, the reference, then the bias or the level. */
	operands[0] = arguments[0].id;
	operands[1] = coordinate;
	operand_count = 2U;
	if (shadow) {
		operands[2] = reference;
		operand_count = 3U;
	}

	/* The image operands in the order of their bits: a bias, a level, gradients, a constant offset. */
	mask = 0U;
	if (bias != 0U)
		mask |= SPV_IMAGE_OPERAND_BIAS;
	if (lod != 0U)
		mask |= SPV_IMAGE_OPERAND_LOD;
	if (gradient_x != 0U)
		mask |= SPV_IMAGE_OPERAND_GRAD;
	if (offset != 0U)
		mask |= SPV_IMAGE_OPERAND_CONST_OFFSET;
	if (mask != 0U) {
		operands[operand_count] = mask;
		operand_count++;
	}

	/* The bias. */
	if (bias != 0U) {
		operands[operand_count] = bias;
		operand_count++;
	}

	/* The level. */
	if (lod != 0U) {
		operands[operand_count] = lod;
		operand_count++;
	}

	/* The gradients. */
	if (gradient_x != 0U) {
		operands[operand_count] = gradient_x;
		operands[operand_count + 1U] = gradient_y;
		operand_count += 2U;
	}

	/* The offset. */
	if (offset != 0U) {
		operands[operand_count] = offset;
		operand_count++;
	}

	/* A colour lookup gives its texel vector. */
	texel = glsl_type_vector(sampler->base, 4U);
	if (!shadow) {
		result = builtin_value(glsl_emit_op(state, opcode, glsl_emit_type(state, texel), operands, operand_count), texel);
		return result;
	}

	/* A shadow lookup gives a float; shadow1D and shadow2D make it (d, d, d, 1). */
	result = builtin_value(glsl_emit_op(state, opcode, glsl_emit_type(state, glsl_type_scalar(GLSL_BASE_FLOAT)), operands, operand_count),
			       glsl_type_scalar(GLSL_BASE_FLOAT));
	if (node->type->kind == GLSL_KIND_SCALAR)
		return result;
	parts[0] = result.id;
	parts[1] = result.id;
	parts[2] = result.id;
	parts[3] = glsl_emit_float(state, 1.0f);
	result = builtin_value(glsl_emit_op(state, SPV_OP_COMPOSITE_CONSTRUCT, glsl_emit_type(state, node->type), parts, 4U), node->type);

	/* Succeeded: the vector. */
	return result;
}

/* Emits textureSize and texelFetch, which read the image the sampler holds. */
static struct emit_value
builtin_texture_query(
	struct emit_state *state,
	struct glsl_node *node,
	struct emit_value *arguments)
{
	struct emit_value result;
	uint32_t operands[4];
	uint32_t image;

	/* The image. */
	image = builtin_image(state, arguments[0]);

	/* textureSize: the level's size. */
	if (node->builtin->number == GLSL_SPECIAL_TEXTURE_SIZE) {
		if (!state->module->image_query) {
			operands[0] = SPV_CAPABILITY_IMAGE_QUERY;
			glsl_words_add(state->module, &state->module->capabilities, SPV_OP_CAPABILITY, operands, 1U);
			state->module->image_query = 1U;
		}

		/* The size of the level. */
		operands[0] = image;
		operands[1] = arguments[1].id;
		result = builtin_value(glsl_emit_op(state, SPV_OP_IMAGE_QUERY_SIZE_LOD, glsl_emit_type(state, node->type), operands, 2U), node->type);
		return result;
	}

	/* texelFetch: the texel at the coordinate of the level. */
	operands[0] = image;
	operands[1] = arguments[1].id;
	operands[2] = SPV_IMAGE_OPERAND_LOD;
	operands[3] = arguments[2].id;
	result = builtin_value(glsl_emit_op(state, SPV_OP_IMAGE_FETCH, glsl_emit_type(state, node->type), operands, 4U), node->type);

	/* Succeeded: the texel. */
	return result;
}

/*
 * Returns the coordinate a lookup of a dimension uses: the first
 * components of the argument, divided by its last one when projective.
 * A shadow lookup's reference is the component after them (the third of
 * a 2D shadow coordinate, always the third for 1D), divided the same way.
 */
static uint32_t
builtin_coordinate(
	struct emit_state *state,
	struct emit_value coordinate,
	unsigned size,
	int project,
	uint32_t *reference,
	int shadow)
{
	const struct glsl_type *float_type;
	const struct glsl_type *result_type;
	struct emit_value divisor;
	uint32_t parts[4];
	uint32_t operands[2];
	uint32_t id;
	unsigned components;
	unsigned index;
	unsigned reference_index;

	/* The argument's components. */
	float_type = glsl_type_scalar(GLSL_BASE_FLOAT);
	components = coordinate.type->components;
	if (coordinate.type->kind == GLSL_KIND_SCALAR)
		components = 1U;

	/* The reference of a shadow lookup: the component after the coordinate (the third, at least). */
	reference_index = size;
	if (reference_index < 2U)
		reference_index = 2U;
	if (shadow)
		*reference = glsl_emit_extract(state, coordinate, reference_index);

	/* A plain lookup whose argument is the coordinate. */
	if (!project && components == size)
		return coordinate.id;

	/* The first components. */
	for (index = 0U; index < size; index++) {
		if (components == 1U) {
			parts[index] = coordinate.id;
		} else {
			parts[index] = glsl_emit_extract(state, coordinate, index);
		}
	}

	/* A projective lookup divides them (and the reference) by the last component. */
	if (project) {
		divisor = builtin_value(glsl_emit_extract(state, coordinate, components - 1U), float_type);
		for (index = 0U; index < size; index++) {
			operands[0] = parts[index];
			operands[1] = divisor.id;
			parts[index] = glsl_emit_op(state, SPV_OP_F_DIV, glsl_emit_type(state, float_type), operands, 2U);
		}

		/* The reference divided too. */
		if (shadow) {
			operands[0] = *reference;
			operands[1] = divisor.id;
			*reference = glsl_emit_op(state, SPV_OP_F_DIV, glsl_emit_type(state, float_type), operands, 2U);
		}
	}

	/* One component is a scalar coordinate. */
	if (size == 1U)
		return parts[0];

	/* Succeeded: the vector of them. */
	result_type = glsl_type_vector(GLSL_BASE_FLOAT, size);
	id = glsl_emit_op(state, SPV_OP_COMPOSITE_CONSTRUCT, glsl_emit_type(state, result_type), parts, size);
	return id;
}

/* Returns how many components a sampler's coordinate has. */
static unsigned
builtin_dimension(
	const struct glsl_type *sampler)
{
	/* 1D one, 2D two (and a layer for an array), 3D and cube three. */
	if (sampler->sampler == GLSL_SAMPLER_1D)
		return 1U;
	if (sampler->sampler == GLSL_SAMPLER_2D && sampler->arrayed)
		return 3U;
	if (sampler->sampler == GLSL_SAMPLER_2D)
		return 2U;

	/* 3D and cube maps. */
	return 3U;
}

/* Returns the image a sampled image holds (OpImage). */
static uint32_t
builtin_image(
	struct emit_state *state,
	struct emit_value sampler)
{
	uint32_t image_type;
	uint32_t id;

	/* OpImage of the sampler's image type. */
	image_type = glsl_emit_image_type(state, sampler.type);
	id = glsl_emit_op(state, SPV_OP_IMAGE, image_type, &sampler.id, 1U);

	/* Succeeded: the image. */
	return id;
}

/* Makes a value from an id and a type. */
static struct emit_value
builtin_value(
	uint32_t id,
	const struct glsl_type *type)
{
	struct emit_value value;

	/* The pair. */
	value.id = id;
	value.type = type;

	/* Succeeded: the value. */
	return value;
}
