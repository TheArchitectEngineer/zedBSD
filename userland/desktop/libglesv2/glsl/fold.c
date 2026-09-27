/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GLSL compiler's constant expressions: the value of an expression
 * whose operands are constant, computed the way the GPU would (32-bit
 * floats and integers, component by component).  The checker folds every
 * expression it can, so that array sizes, const variables and case
 * labels have values, and the emitter writes constants instead of code.
 */

#include "internal.h"

#include <math.h>
#include <string.h>

/* The built-in functions folded here, by how many float arguments they take component by component. */
#define FOLD_NONE		0U
#define FOLD_UNARY		1U
#define FOLD_BINARY		2U
#define FOLD_TERNARY		3U

/*
 * A built-in function this file folds, and how.
 */
struct fold_function {
	const char *name;
	unsigned arguments;
	unsigned operation;
};

/* The operations of the folded built-ins. */
enum fold_operation {
	FOLD_RADIANS = 1,
	FOLD_DEGREES,
	FOLD_SIN,
	FOLD_COS,
	FOLD_TAN,
	FOLD_ASIN,
	FOLD_ACOS,
	FOLD_ATAN,
	FOLD_EXP,
	FOLD_LOG,
	FOLD_EXP2,
	FOLD_LOG2,
	FOLD_SQRT,
	FOLD_INVERSESQRT,
	FOLD_ABS,
	FOLD_SIGN,
	FOLD_FLOOR,
	FOLD_CEIL,
	FOLD_FRACT,
	FOLD_TRUNC,
	FOLD_ROUND,
	FOLD_POW,
	FOLD_ATAN2,
	FOLD_MOD,
	FOLD_MIN,
	FOLD_MAX,
	FOLD_STEP,
	FOLD_CLAMP,
	FOLD_MIX,
	FOLD_SMOOTHSTEP
};

/*
 * The built-ins folded component by component.
 */
static const struct fold_function fold_functions[] = {
	{ "radians", FOLD_UNARY, FOLD_RADIANS },
	{ "degrees", FOLD_UNARY, FOLD_DEGREES },
	{ "sin", FOLD_UNARY, FOLD_SIN },
	{ "cos", FOLD_UNARY, FOLD_COS },
	{ "tan", FOLD_UNARY, FOLD_TAN },
	{ "asin", FOLD_UNARY, FOLD_ASIN },
	{ "acos", FOLD_UNARY, FOLD_ACOS },
	{ "atan", FOLD_UNARY, FOLD_ATAN },
	{ "exp", FOLD_UNARY, FOLD_EXP },
	{ "log", FOLD_UNARY, FOLD_LOG },
	{ "exp2", FOLD_UNARY, FOLD_EXP2 },
	{ "log2", FOLD_UNARY, FOLD_LOG2 },
	{ "sqrt", FOLD_UNARY, FOLD_SQRT },
	{ "inversesqrt", FOLD_UNARY, FOLD_INVERSESQRT },
	{ "abs", FOLD_UNARY, FOLD_ABS },
	{ "sign", FOLD_UNARY, FOLD_SIGN },
	{ "floor", FOLD_UNARY, FOLD_FLOOR },
	{ "ceil", FOLD_UNARY, FOLD_CEIL },
	{ "fract", FOLD_UNARY, FOLD_FRACT },
	{ "trunc", FOLD_UNARY, FOLD_TRUNC },
	{ "round", FOLD_UNARY, FOLD_ROUND },
	{ "pow", FOLD_BINARY, FOLD_POW },
	{ "atan", FOLD_BINARY, FOLD_ATAN2 },
	{ "mod", FOLD_BINARY, FOLD_MOD },
	{ "min", FOLD_BINARY, FOLD_MIN },
	{ "max", FOLD_BINARY, FOLD_MAX },
	{ "step", FOLD_BINARY, FOLD_STEP },
	{ "clamp", FOLD_TERNARY, FOLD_CLAMP },
	{ "mix", FOLD_TERNARY, FOLD_MIX },
	{ "smoothstep", FOLD_TERNARY, FOLD_SMOOTHSTEP }
};

/* The geometric built-ins folded whole. */
#define FOLD_GEOMETRIC_NONE	0U
#define FOLD_GEOMETRIC_LENGTH	1U
#define FOLD_GEOMETRIC_DISTANCE	2U
#define FOLD_GEOMETRIC_DOT	3U
#define FOLD_GEOMETRIC_CROSS	4U
#define FOLD_GEOMETRIC_NORMALIZE 5U

/*
 * The geometric built-ins, by name and number of arguments.
 */
static const struct fold_function fold_geometrics[] = {
	{ "length", 1U, FOLD_GEOMETRIC_LENGTH },
	{ "distance", 2U, FOLD_GEOMETRIC_DISTANCE },
	{ "dot", 2U, FOLD_GEOMETRIC_DOT },
	{ "cross", 2U, FOLD_GEOMETRIC_CROSS },
	{ "normalize", 1U, FOLD_GEOMETRIC_NORMALIZE }
};

static struct glsl_constant *fold_literal(struct glsl_shader *shader, struct glsl_node *node);
static struct glsl_constant *fold_unary(struct glsl_shader *shader, struct glsl_node *node);
static struct glsl_constant *fold_binary(struct glsl_shader *shader, struct glsl_node *node);
static struct glsl_constant *fold_multiply(struct glsl_shader *shader, struct glsl_node *node, const struct glsl_constant *left, const struct glsl_constant *right);
static int fold_scalar(struct glsl_shader *shader, unsigned op, unsigned base, union glsl_scalar left, union glsl_scalar right, unsigned line, union glsl_scalar *result);
static int fold_equal(const struct glsl_constant *left, const struct glsl_constant *right);
static struct glsl_constant *fold_constructor(struct glsl_shader *shader, struct glsl_node *node);
static struct glsl_constant *fold_builtin(struct glsl_shader *shader, struct glsl_node *node);
static struct glsl_constant *fold_geometric(struct glsl_shader *shader, struct glsl_node *node, struct glsl_constant **arguments, unsigned count);
static float fold_float(unsigned operation, float x, float y, float z);
static int32_t fold_int(unsigned operation, int32_t x, int32_t y, int32_t z);
static struct glsl_constant *fold_select(struct glsl_shader *shader, struct glsl_node *node);
static union glsl_scalar fold_convert_scalar(union glsl_scalar value, unsigned from, unsigned to);
static unsigned fold_base(const struct glsl_type *type);
static unsigned fold_field_offset(const struct glsl_type *type, unsigned field);

/*
 * Makes a constant of a type with every scalar 0.
 */
struct glsl_constant *
glsl_constant_new(
	struct glsl_arena *arena,
	const struct glsl_type *type)
{
	struct glsl_constant *constant;

	/* The constant and its scalars (zeroed by the arena). */
	constant = glsl_alloc(arena, sizeof(*constant));
	constant->type = type;
	constant->count = glsl_type_scalars(type);
	constant->values = glsl_alloc(arena, (constant->count + 1U) * sizeof(*constant->values));

	/* Succeeded: the constant. */
	return constant;
}

/*
 * Converts a scalar or vector constant to another base of the same
 * shape (the target type).
 */
struct glsl_constant *
glsl_constant_convert(
	struct glsl_arena *arena,
	const struct glsl_constant *value,
	const struct glsl_type *type)
{
	struct glsl_constant *converted;
	unsigned from;
	unsigned to;
	unsigned index;

	/* The converted constant, scalar by scalar. */
	converted = glsl_constant_new(arena, type);
	from = fold_base(value->type);
	to = fold_base(type);
	for (index = 0U; index < converted->count && index < value->count; index++)
		converted->values[index] = fold_convert_scalar(value->values[index], from, to);

	/* Succeeded: the converted constant. */
	return converted;
}

/*
 * Computes the value of a checked expression whose operands are
 * constant, or returns NULL when it is not a constant expression.
 */
struct glsl_constant *
glsl_fold(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *value;
	struct glsl_constant *object;
	struct glsl_constant *index;
	unsigned element;
	unsigned size;
	unsigned offset;
	unsigned component;

	/* An expression that had an error has no value. */
	if (node->type == NULL || node->type->kind == GLSL_KIND_ERROR)
		return NULL;

	/* Each kind of expression. */
	switch (node->kind) {
	case GLSL_N_INT:
	case GLSL_N_UINT:
	case GLSL_N_FLOAT:
	case GLSL_N_BOOL:
		value = fold_literal(shader, node);
		return value;
	case GLSL_N_IDENTIFIER:
		if (node->symbol == NULL || node->symbol->where != GLSL_VAR_CONST)
			return NULL;
		return node->symbol->constant;
	case GLSL_N_CONVERT:
		if (node->child[0]->constant == NULL)
			return NULL;
		value = glsl_constant_convert(&shader->arena, node->child[0]->constant, node->type);
		return value;
	case GLSL_N_UNARY:
		value = fold_unary(shader, node);
		return value;
	case GLSL_N_BINARY:
		value = fold_binary(shader, node);
		return value;
	case GLSL_N_TERNARY:
		value = fold_select(shader, node);
		return value;
	case GLSL_N_CALL:
		if ((node->flags & GLSL_NODE_CONSTRUCTOR) != 0U) {
			value = fold_constructor(shader, node);
			return value;
		}

		/* A call of a built-in function. */
		value = fold_builtin(shader, node);
		return value;
	default:
		break;
	}

	/* A selection or a subscript of a constant. */
	if (node->child[0] == NULL)
		return NULL;
	object = node->child[0]->constant;
	if (object == NULL)
		return NULL;

	/* A struct member: its scalars inside the struct's. */
	value = glsl_constant_new(&shader->arena, node->type);
	if (node->kind == GLSL_N_FIELD && (node->flags & GLSL_NODE_SWIZZLE) == 0U) {
		offset = fold_field_offset(object->type, node->field);
		memcpy(value->values, object->values + offset, value->count * sizeof(*value->values));
		return value;
	}

	/* A swizzle: the components it names. */
	if (node->kind == GLSL_N_FIELD) {
		for (component = 0U; component < node->swizzle_count; component++)
			value->values[component] = object->values[node->swizzle[component]];
		return value;
	}

	/* Only a subscript remains, with a constant index in range. */
	if (node->kind != GLSL_N_INDEX)
		return NULL;
	index = node->child[1]->constant;
	if (index == NULL)
		return NULL;
	element = index->values[0].u;
	size = glsl_type_scalars(node->type);
	if ((element + 1U) * size > object->count)
		return NULL;

	/* The element's scalars (an array's element, a matrix's column, a vector's component). */
	memcpy(value->values, object->values + element * size, size * sizeof(*value->values));
	return value;
}

/* Folds a literal. */
static struct glsl_constant *
fold_literal(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *value;

	/* The value of the literal's type. */
	value = glsl_constant_new(&shader->arena, node->type);
	if (node->kind == GLSL_N_FLOAT) {
		value->values[0].f = node->number;
	} else {
		value->values[0].u = node->integer;
	}

	/* Succeeded: the literal's value. */
	return value;
}

/* Folds a unary operator. */
static struct glsl_constant *
fold_unary(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *operand;
	struct glsl_constant *value;
	unsigned base;
	unsigned index;

	/* A constant operand. */
	operand = node->child[0]->constant;
	if (operand == NULL)
		return NULL;
	value = glsl_constant_new(&shader->arena, node->type);
	base = fold_base(node->type);

	/* Each scalar. */
	for (index = 0U; index < value->count; index++) {
		value->values[index] = operand->values[index];

		/* Minus negates, not inverts a bool, complement inverts the bits; plus keeps. */
		if (node->op == GLSL_P_MINUS && base == GLSL_BASE_FLOAT) {
			value->values[index].f = -operand->values[index].f;
		} else if (node->op == GLSL_P_MINUS) {
			value->values[index].u = 0U - operand->values[index].u;
		} else if (node->op == GLSL_P_BANG) {
			value->values[index].u = (operand->values[index].u == 0U);
		} else if (node->op == GLSL_P_TILDE) {
			value->values[index].u = ~operand->values[index].u;
		}
	}

	/* Succeeded: the value. */
	return value;
}

/* Folds a binary operator. */
static struct glsl_constant *
fold_binary(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *left;
	struct glsl_constant *right;
	struct glsl_constant *value;
	union glsl_scalar result;
	unsigned base;
	unsigned index;
	unsigned left_index;
	unsigned right_index;
	int equal;
	int status;

	/* Two constant operands. */
	left = node->child[0]->constant;
	right = node->child[1]->constant;
	if (left == NULL || right == NULL)
		return NULL;
	value = glsl_constant_new(&shader->arena, node->type);

	/* The logical operators. */
	switch (node->op) {
	case GLSL_P_AND_AND:
		value->values[0].u = (left->values[0].u != 0U && right->values[0].u != 0U);
		return value;
	case GLSL_P_OR_OR:
		value->values[0].u = (left->values[0].u != 0U || right->values[0].u != 0U);
		return value;
	case GLSL_P_XOR_XOR:
		value->values[0].u = ((left->values[0].u != 0U) != (right->values[0].u != 0U));
		return value;
	case GLSL_P_EQ:
	case GLSL_P_NE:
		equal = fold_equal(left, right);
		value->values[0].u = (unsigned)equal;
		if (node->op == GLSL_P_NE)
			value->values[0].u = (unsigned)!equal;
		return value;
	default:
		break;
	}

	/* The linear algebra product of a matrix with a matrix or a vector. */
	if (node->op == GLSL_P_STAR) {
		if (left->type->kind == GLSL_KIND_MATRIX && right->type->kind != GLSL_KIND_SCALAR) {
			value = fold_multiply(shader, node, left, right);
			return value;
		}

		/* A vector or matrix times a matrix. */
		if (right->type->kind == GLSL_KIND_MATRIX && left->type->kind != GLSL_KIND_SCALAR) {
			value = fold_multiply(shader, node, left, right);
			return value;
		}
	}

	/* The operands' base (the result of a comparison is bool, so the operands decide). */
	base = fold_base(left->type);

	/* Component by component, a scalar operand standing for every component. */
	for (index = 0U; index < value->count; index++) {
		left_index = index;
		if (left->count == 1U)
			left_index = 0U;
		right_index = index;
		if (right->count == 1U)
			right_index = 0U;
		status = fold_scalar(shader, node->op, base, left->values[left_index], right->values[right_index], node->line, &result);
		if (status != 0)
			return NULL;
		value->values[index] = result;
	}

	/* Succeeded: the value. */
	return value;
}

/* Folds the linear algebra product of a matrix with a matrix or a vector. */
static struct glsl_constant *
fold_multiply(
	struct glsl_shader *shader,
	struct glsl_node *node,
	const struct glsl_constant *left,
	const struct glsl_constant *right)
{
	struct glsl_constant *value;
	unsigned rows;
	unsigned inner;
	unsigned column;
	unsigned row;
	unsigned k;
	float sum;

	/* The result, and the rows of the left operand (a vector on the left is one row). */
	value = glsl_constant_new(&shader->arena, node->type);
	rows = left->type->components;
	if (left->type->kind == GLSL_KIND_VECTOR)
		rows = 1U;
	inner = left->type->columns;
	if (left->type->kind == GLSL_KIND_VECTOR)
		inner = left->type->components;

	/* Each result component: result[column][row] = sum over k of left[k][row] * right[column][k]. */
	for (column = 0U; column * rows < value->count; column++) {
		for (row = 0U; row < rows; row++) {
			sum = 0.0f;
			for (k = 0U; k < inner; k++)
				sum += left->values[k * rows + row].f * right->values[column * inner + k].f;
			value->values[column * rows + row].f = sum;
		}
	}

	/* Succeeded: the product. */
	return value;
}

/* Applies a binary operator to two scalars of a base; nonzero when the operation has no value. */
static int
fold_scalar(
	struct glsl_shader *shader,
	unsigned op,
	unsigned base,
	union glsl_scalar left,
	union glsl_scalar right,
	unsigned line,
	union glsl_scalar *result)
{
	/* Floats. */
	result->u = 0U;
	if (base == GLSL_BASE_FLOAT) {
		switch (op) {
		case GLSL_P_PLUS:
			result->f = left.f + right.f;
			return 0;
		case GLSL_P_MINUS:
			result->f = left.f - right.f;
			return 0;
		case GLSL_P_STAR:
			result->f = left.f * right.f;
			return 0;
		case GLSL_P_SLASH:
			result->f = left.f / right.f;
			return 0;
		case GLSL_P_LT:
			result->u = (left.f < right.f);
			return 0;
		case GLSL_P_GT:
			result->u = (left.f > right.f);
			return 0;
		case GLSL_P_LE:
			result->u = (left.f <= right.f);
			return 0;
		case GLSL_P_GE:
			result->u = (left.f >= right.f);
			return 0;
		default:
			return -1;
		}
	}

	/* Integers: division and remainder by zero have no value. */
	if ((op == GLSL_P_SLASH || op == GLSL_P_PERCENT) && right.u == 0U) {
		glsl_warning(shader, line, "integer division by zero");
		return -1;
	}

	/* The integer operators (unsigned arithmetic is the same bits as signed but for division, comparison and shifts). */
	switch (op) {
	case GLSL_P_PLUS:
		result->u = left.u + right.u;
		return 0;
	case GLSL_P_MINUS:
		result->u = left.u - right.u;
		return 0;
	case GLSL_P_STAR:
		result->u = left.u * right.u;
		return 0;
	case GLSL_P_AMP:
		result->u = left.u & right.u;
		return 0;
	case GLSL_P_BAR:
		result->u = left.u | right.u;
		return 0;
	case GLSL_P_CARET:
		result->u = left.u ^ right.u;
		return 0;
	case GLSL_P_SHL:
		result->u = left.u << (right.u & 31U);
		return 0;
	default:
		break;
	}

	/* Signed. */
	if (base == GLSL_BASE_INT) {
		switch (op) {
		case GLSL_P_SLASH:
			result->i = left.i / right.i;
			return 0;
		case GLSL_P_PERCENT:
			result->i = left.i % right.i;
			return 0;
		case GLSL_P_SHR:
			result->i = left.i >> (right.u & 31U);
			return 0;
		case GLSL_P_LT:
			result->u = (left.i < right.i);
			return 0;
		case GLSL_P_GT:
			result->u = (left.i > right.i);
			return 0;
		case GLSL_P_LE:
			result->u = (left.i <= right.i);
			return 0;
		case GLSL_P_GE:
			result->u = (left.i >= right.i);
			return 0;
		default:
			return -1;
		}
	}

	/* Unsigned. */
	switch (op) {
	case GLSL_P_SLASH:
		result->u = left.u / right.u;
		return 0;
	case GLSL_P_PERCENT:
		result->u = left.u % right.u;
		return 0;
	case GLSL_P_SHR:
		result->u = left.u >> (right.u & 31U);
		return 0;
	case GLSL_P_LT:
		result->u = (left.u < right.u);
		return 0;
	case GLSL_P_GT:
		result->u = (left.u > right.u);
		return 0;
	case GLSL_P_LE:
		result->u = (left.u <= right.u);
		return 0;
	case GLSL_P_GE:
		result->u = (left.u >= right.u);
		return 0;
	default:
		break;
	}

	/* An operator without a value on these operands. */
	return -1;
}

/* Reports whether two constants of one type are equal (floats by value, the rest by bits). */
static int
fold_equal(
	const struct glsl_constant *left,
	const struct glsl_constant *right)
{
	unsigned base;
	unsigned index;

	/* Each scalar. */
	base = fold_base(left->type);
	for (index = 0U; index < left->count && index < right->count; index++) {
		if (base == GLSL_BASE_FLOAT && left->values[index].f != right->values[index].f)
			return 0;
		if (base != GLSL_BASE_FLOAT && left->values[index].u != right->values[index].u)
			return 0;
	}

	/* Succeeded: equal. */
	return 1;
}

/* Folds a constructor whose arguments are all constant. */
static struct glsl_constant *
fold_constructor(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	const struct glsl_type *type;
	struct glsl_constant *value;
	struct glsl_constant *argument;
	struct glsl_node *node_argument;
	union glsl_scalar one;
	unsigned target;
	unsigned count;
	unsigned index;
	unsigned column;
	unsigned row;
	unsigned from_columns;
	unsigned from_rows;

	/* Every argument constant. */
	for (node_argument = node->child[1]; node_argument != NULL; node_argument = node_argument->next) {
		if (node_argument->constant == NULL)
			return NULL;
	}

	/* The value, and the base its scalars take. */
	type = node->type;
	value = glsl_constant_new(&shader->arena, type);
	target = fold_base(type);
	argument = node->child[1]->constant;

	/* A struct or an array: the arguments' values one after another. */
	if (type->kind == GLSL_KIND_STRUCT || type->kind == GLSL_KIND_ARRAY) {
		count = 0U;
		for (node_argument = node->child[1]; node_argument != NULL; node_argument = node_argument->next) {
			argument = node_argument->constant;
			memcpy(value->values + count, argument->values, argument->count * sizeof(*value->values));
			count += argument->count;
		}

		/* Succeeded: the aggregate's value. */
		return value;
	}

	/* A matrix from one scalar: the diagonal. */
	if (type->kind == GLSL_KIND_MATRIX && node->child[1]->next == NULL && argument->count == 1U) {
		one = fold_convert_scalar(argument->values[0], fold_base(argument->type), GLSL_BASE_FLOAT);
		for (column = 0U; column < type->columns; column++) {
			if (column < type->components)
				value->values[column * type->components + column] = one;
		}

		/* Succeeded: the diagonal matrix. */
		return value;
	}

	/* A matrix from a matrix: its columns and rows that exist, the identity elsewhere. */
	if (type->kind == GLSL_KIND_MATRIX && argument->type->kind == GLSL_KIND_MATRIX) {
		from_columns = argument->type->columns;
		from_rows = argument->type->components;
		for (column = 0U; column < type->columns; column++) {
			for (row = 0U; row < type->components; row++) {
				if (column < from_columns && row < from_rows) {
					value->values[column * type->components + row] = argument->values[column * from_rows + row];
				} else if (column == row) {
					value->values[column * type->components + row].f = 1.0f;
				}
			}
		}

		/* Succeeded: the resized matrix. */
		return value;
	}

	/* A scalar or vector from one scalar: every component the scalar. */
	if (type->kind != GLSL_KIND_MATRIX && node->child[1]->next == NULL && argument->count == 1U) {
		for (index = 0U; index < value->count; index++)
			value->values[index] = fold_convert_scalar(argument->values[0], fold_base(argument->type), target);
		return value;
	}

	/* Otherwise the arguments' scalars in order, as many as the type has. */
	count = 0U;
	for (node_argument = node->child[1]; node_argument != NULL; node_argument = node_argument->next) {
		argument = node_argument->constant;
		for (index = 0U; index < argument->count && count < value->count; index++) {
			value->values[count] = fold_convert_scalar(argument->values[index], fold_base(argument->type), target);
			count++;
		}
	}

	/* Succeeded: the constructed value. */
	return value;
}

/* Folds a call of a built-in function whose arguments are all constant, when it is one this file computes. */
static struct glsl_constant *
fold_builtin(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *arguments[4];
	struct glsl_constant *value;
	struct glsl_node *argument;
	const struct fold_function *function;
	unsigned count;
	unsigned index;
	unsigned slot;
	unsigned base;
	size_t entry;
	float operands[3];
	int32_t integers[3];
	int differs;

	/* A built-in with constant arguments (at most four). */
	if (node->builtin == NULL)
		return NULL;
	count = 0U;
	for (argument = node->child[1]; argument != NULL; argument = argument->next) {
		if (argument->constant == NULL || count == 4U)
			return NULL;
		arguments[count] = argument->constant;
		count++;
	}

	/* The geometric functions have results of their own shape. */
	value = fold_geometric(shader, node, arguments, count);
	if (value != NULL)
		return value;

	/* The component-by-component built-ins of this arity. */
	function = NULL;
	for (entry = 0U; entry < sizeof(fold_functions) / sizeof(fold_functions[0]); entry++) {
		if (fold_functions[entry].arguments != count)
			continue;
		differs = strcmp(fold_functions[entry].name, node->builtin->name);
		if (differs != 0)
			continue;
		function = &fold_functions[entry];
		break;
	}

	/* Only those built-ins fold. */
	if (function == NULL)
		return NULL;

	/* Each component, a scalar argument standing for every component. */
	value = glsl_constant_new(&shader->arena, node->type);
	base = fold_base(node->type);
	for (index = 0U; index < value->count; index++) {
		for (slot = 0U; slot < count; slot++) {
			operands[slot] = arguments[slot]->values[0].f;
			integers[slot] = arguments[slot]->values[0].i;
			if (arguments[slot]->count > 1U) {
				operands[slot] = arguments[slot]->values[index].f;
				integers[slot] = arguments[slot]->values[index].i;
			}
		}

		/* The arguments a built-in does not have are 0. */
		for (slot = count; slot < 3U; slot++) {
			operands[slot] = 0.0f;
			integers[slot] = 0;
		}

		/* The operation on floats or on integers. */
		if (base == GLSL_BASE_FLOAT) {
			value->values[index].f = fold_float(function->operation, operands[0], operands[1], operands[2]);
		} else {
			value->values[index].i = fold_int(function->operation, integers[0], integers[1], integers[2]);
		}
	}

	/* Succeeded: the value. */
	return value;
}

/* Folds length, distance, dot, cross and normalize; NULL for any other built-in. */
static struct glsl_constant *
fold_geometric(
	struct glsl_shader *shader,
	struct glsl_node *node,
	struct glsl_constant **arguments,
	unsigned count)
{
	struct glsl_constant *value;
	unsigned operation;
	unsigned index;
	size_t entry;
	float sum;
	float difference;
	int differs;

	/* Which one it is. */
	operation = FOLD_GEOMETRIC_NONE;
	for (entry = 0U; entry < sizeof(fold_geometrics) / sizeof(fold_geometrics[0]); entry++) {
		if (fold_geometrics[entry].arguments != count)
			continue;
		differs = strcmp(fold_geometrics[entry].name, node->builtin->name);
		if (differs != 0)
			continue;
		operation = fold_geometrics[entry].operation;
		break;
	}

	/* Only those built-ins fold. */
	if (operation == FOLD_GEOMETRIC_NONE)
		return NULL;
	value = glsl_constant_new(&shader->arena, node->type);

	/* The cross product. */
	if (operation == FOLD_GEOMETRIC_CROSS) {
		value->values[0].f = arguments[0]->values[1].f * arguments[1]->values[2].f - arguments[0]->values[2].f * arguments[1]->values[1].f;
		value->values[1].f = arguments[0]->values[2].f * arguments[1]->values[0].f - arguments[0]->values[0].f * arguments[1]->values[2].f;
		value->values[2].f = arguments[0]->values[0].f * arguments[1]->values[1].f - arguments[0]->values[1].f * arguments[1]->values[0].f;
		return value;
	}

	/* The sum the others are made of: of squared differences, of products, or of squares. */
	sum = 0.0f;
	for (index = 0U; index < arguments[0]->count; index++) {
		if (operation == FOLD_GEOMETRIC_DISTANCE) {
			difference = arguments[0]->values[index].f - arguments[1]->values[index].f;
			sum += difference * difference;
		} else if (operation == FOLD_GEOMETRIC_DOT) {
			sum += arguments[0]->values[index].f * arguments[1]->values[index].f;
		} else {
			sum += arguments[0]->values[index].f * arguments[0]->values[index].f;
		}
	}

	/* The dot product is the sum. */
	if (operation == FOLD_GEOMETRIC_DOT) {
		value->values[0].f = sum;
		return value;
	}

	/* A length or a distance is its root. */
	if (operation != FOLD_GEOMETRIC_NORMALIZE) {
		value->values[0].f = sqrtf(sum);
		return value;
	}

	/* A normalized vector: each component over the length. */
	for (index = 0U; index < value->count; index++)
		value->values[index].f = arguments[0]->values[index].f / sqrtf(sum);

	/* Succeeded: the normalized vector. */
	return value;
}

/* Computes one float component of a folded built-in. */
static float
fold_float(
	unsigned operation,
	float x,
	float y,
	float z)
{
	float t;

	/* The operation. */
	switch (operation) {
	case FOLD_RADIANS:
		return x * 0.017453292519943295f;
	case FOLD_DEGREES:
		return x * 57.29577951308232f;
	case FOLD_SIN:
		return sinf(x);
	case FOLD_COS:
		return cosf(x);
	case FOLD_TAN:
		return tanf(x);
	case FOLD_ASIN:
		return asinf(x);
	case FOLD_ACOS:
		return acosf(x);
	case FOLD_ATAN:
		return atanf(x);
	case FOLD_EXP:
		return expf(x);
	case FOLD_LOG:
		return logf(x);
	case FOLD_EXP2:
		return exp2f(x);
	case FOLD_LOG2:
		return log2f(x);
	case FOLD_SQRT:
		return sqrtf(x);
	case FOLD_INVERSESQRT:
		return 1.0f / sqrtf(x);
	case FOLD_ABS:
		return fabsf(x);
	case FOLD_SIGN:
		if (x > 0.0f)
			return 1.0f;
		if (x < 0.0f)
			return -1.0f;
		return 0.0f;
	case FOLD_FLOOR:
		return floorf(x);
	case FOLD_CEIL:
		return ceilf(x);
	case FOLD_FRACT:
		return x - floorf(x);
	case FOLD_TRUNC:
		return truncf(x);
	case FOLD_ROUND:
		return roundf(x);
	case FOLD_POW:
		return powf(x, y);
	case FOLD_ATAN2:
		return atan2f(x, y);
	case FOLD_MOD:
		return x - y * floorf(x / y);
	case FOLD_MIN:
		if (y < x)
			return y;
		return x;
	case FOLD_MAX:
		if (y > x)
			return y;
		return x;
	case FOLD_STEP:
		if (y < x)
			return 0.0f;
		return 1.0f;
	case FOLD_CLAMP:
		if (x < y)
			return y;
		if (x > z)
			return z;
		return x;
	case FOLD_MIX:
		return x * (1.0f - z) + y * z;
	case FOLD_SMOOTHSTEP:
		t = (z - x) / (y - x);
		if (t < 0.0f)
			t = 0.0f;
		if (t > 1.0f)
			t = 1.0f;
		return t * t * (3.0f - 2.0f * t);
	default:
		break;
	}

	/* An operation without a float form. */
	return 0.0f;
}

/* Computes one integer component of a folded built-in (abs, sign, min, max, clamp). */
static int32_t
fold_int(
	unsigned operation,
	int32_t x,
	int32_t y,
	int32_t z)
{
	/* The operation. */
	switch (operation) {
	case FOLD_ABS:
		if (x < 0)
			return -x;
		return x;
	case FOLD_SIGN:
		if (x > 0)
			return 1;
		if (x < 0)
			return -1;
		return 0;
	case FOLD_MIN:
		if (y < x)
			return y;
		return x;
	case FOLD_MAX:
		if (y > x)
			return y;
		return x;
	case FOLD_CLAMP:
		if (x < y)
			return y;
		if (x > z)
			return z;
		return x;
	default:
		break;
	}

	/* An operation without an integer form. */
	return 0;
}

/* Folds "condition ? a : b" when all three are constant. */
static struct glsl_constant *
fold_select(
	struct glsl_shader *shader,
	struct glsl_node *node)
{
	struct glsl_constant *condition;

	/* Three constants. */
	(void)shader;
	condition = node->child[0]->constant;
	if (condition == NULL || node->child[1]->constant == NULL || node->child[2]->constant == NULL)
		return NULL;

	/* The chosen one. */
	if (condition->values[0].u != 0U)
		return node->child[1]->constant;

	/* The other one. */
	return node->child[2]->constant;
}

/* Converts one scalar between bases. */
static union glsl_scalar
fold_convert_scalar(
	union glsl_scalar value,
	unsigned from,
	unsigned to)
{
	union glsl_scalar result;

	/* The same base is the same bits. */
	result = value;
	if (from == to)
		return result;

	/* To bool: whether the value is nonzero. */
	if (to == GLSL_BASE_BOOL) {
		if (from == GLSL_BASE_FLOAT) {
			result.u = (value.f != 0.0f);
		} else {
			result.u = (value.u != 0U);
		}

		/* Succeeded: the bool. */
		return result;
	}

	/* To float: the value's number. */
	if (to == GLSL_BASE_FLOAT) {
		if (from == GLSL_BASE_INT) {
			result.f = (float)value.i;
		} else {
			result.f = (float)value.u;
		}

		/* Succeeded: the float. */
		return result;
	}

	/* To an integer from a float: truncated. */
	if (from == GLSL_BASE_FLOAT) {
		if (to == GLSL_BASE_INT) {
			result.i = (int32_t)value.f;
		} else {
			result.u = (uint32_t)value.f;
		}

		/* Succeeded: the truncated integer. */
		return result;
	}

	/* Between int, uint and bool the bits stay (a bool is 0 or 1). */
	return result;
}

/* Returns the scalar base of a type's values (a struct's or array's has none: GLSL_BASE_NONE). */
static unsigned
fold_base(
	const struct glsl_type *type)
{
	/* An array's elements'. */
	if (type->kind == GLSL_KIND_ARRAY) {
		type = type->element;
		if (type->kind == GLSL_KIND_ARRAY || type->kind == GLSL_KIND_STRUCT)
			return GLSL_BASE_NONE;
	}

	/* The type's own. */
	return type->base;
}

/* Returns where a struct member's scalars start in the struct's flattened scalars. */
static unsigned
fold_field_offset(
	const struct glsl_type *type,
	unsigned field)
{
	unsigned offset;
	unsigned index;

	/* The scalars of the members before it. */
	offset = 0U;
	for (index = 0U; index < field && index < type->field_count; index++)
		offset += glsl_type_scalars(type->fields[index].type);

	/* Succeeded: the offset. */
	return offset;
}
