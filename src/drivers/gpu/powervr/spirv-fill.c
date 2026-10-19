/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The SPIR-V subset the PowerVR driver runs before it has a compiler.
 *
 * The parse walks the module once.  Outside the function it reads the
 * entry point, the workgroup size, the decorations, the constants and
 * the variables; inside the one function it follows each id as a value
 * a*x + c of x = gl_GlobalInvocationID.x (or as a pointer to the
 * invocation id, to a function variable, or to a word of the buffer),
 * through the integer operations whose result stays of that form.  The
 * one store into the buffer gives the index and the value.  Every other
 * instruction in the function refuses the shader.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/spirv-fill.h"

/*
 * The module header's magic and its words, the bits of an instruction's
 * first word, and the opcodes, enumerants and decorations the subset
 * reads.
 *
 * Values referenced:
 * Khronos SPIR-V Specification, unified 1.6 (section 2.3 "Physical
 * Layout of a SPIR-V Module and Instruction": magic 0x07230203, five
 * header words, word count in the high 16 bits and opcode in the low
 * 16; section 3.6 ExecutionModel GLCompute 5; 3.7 StorageClass Uniform
 * 2, Input 1, Function 7, StorageBuffer 12; 3.6.1 ExecutionMode
 * LocalSize 17; 3.20 Decoration ArrayStride 6, BuiltIn 11, Binding 33,
 * DescriptorSet 34, Offset 35; 3.21 BuiltIn GlobalInvocationId 28;
 * 3.52 opcodes as named below).
 */
#define SPIRV_MAGIC			0x07230203U
#define SPIRV_HEADER_WORDS		5U
#define SPIRV_COUNT_SHIFT		16U
#define SPIRV_OPCODE_MASK		0xffffU

#define OP_UNDEF			1U
#define OP_SOURCE_CONTINUED		2U
#define OP_SOURCE			3U
#define OP_SOURCE_EXTENSION		4U
#define OP_NAME				5U
#define OP_MEMBER_NAME			6U
#define OP_STRING			7U
#define OP_LINE				8U
#define OP_EXTENSION			10U
#define OP_EXT_INST_IMPORT		11U
#define OP_MEMORY_MODEL			14U
#define OP_ENTRY_POINT			15U
#define OP_EXECUTION_MODE		16U
#define OP_CAPABILITY			17U
#define OP_TYPE_FIRST			19U
#define OP_TYPE_LAST			39U
#define OP_CONSTANT_TRUE		41U
#define OP_CONSTANT_FALSE		42U
#define OP_CONSTANT			43U
#define OP_CONSTANT_COMPOSITE		44U
#define OP_CONSTANT_NULL		46U
#define OP_FUNCTION			54U
#define OP_FUNCTION_END			56U
#define OP_VARIABLE			59U
#define OP_LOAD				61U
#define OP_STORE			62U
#define OP_ACCESS_CHAIN			65U
#define OP_IN_BOUNDS_ACCESS_CHAIN	66U
#define OP_DECORATE			71U
#define OP_MEMBER_DECORATE		72U
#define OP_COMPOSITE_EXTRACT		81U
#define OP_COPY_OBJECT			83U
#define OP_BITCAST			124U
#define OP_IADD				128U
#define OP_ISUB				130U
#define OP_IMUL				132U
#define OP_SHIFT_LEFT_LOGICAL		196U
#define OP_LABEL			248U
#define OP_RETURN			253U
#define OP_NO_LINE			317U
#define OP_MODULE_PROCESSED		330U

#define MODEL_GL_COMPUTE		5U
#define MODE_LOCAL_SIZE			17U
#define CLASS_INPUT			1U
#define CLASS_UNIFORM			2U
#define CLASS_FUNCTION			7U
#define CLASS_STORAGE_BUFFER		12U
#define DECORATION_ARRAY_STRIDE		6U
#define DECORATION_BUILT_IN		11U
#define DECORATION_BINDING		33U
#define DECORATION_DESCRIPTOR_SET	34U
#define DECORATION_OFFSET		35U
#define BUILT_IN_GLOBAL_INVOCATION_ID	28U

/* The words of a buffer element the subset writes, and the largest workgroup. */
#define FILL_STRIDE			4U
#define FILL_LOCAL_MAX			32U

/* What an id stands for. */
enum value_kind {
	VALUE_NONE = 0,
	VALUE_LINEAR,
	VALUE_INVOCATION_VARIABLE,
	VALUE_INVOCATION_VECTOR,
	VALUE_INVOCATION_COMPONENT,
	VALUE_BUFFER_VARIABLE,
	VALUE_BUFFER_WORD,
	VALUE_LOCAL_VARIABLE
};

/* The flags of an id: its decorations, and whether a local variable holds a value. */
#define FLAG_INVOCATION_ID		0x01U
#define FLAG_SET			0x02U
#define FLAG_BINDING			0x04U
#define FLAG_HELD			0x08U

/* What the walk has seen so far. */
struct parse_state {
	struct powervr_spirv_value *values;
	uint32_t bound;
	uint32_t entry;
	uint32_t local_x;
	uint32_t buffers;
	uint32_t buffer;
	uint32_t stores;
	uint32_t functions;
	uint32_t stride_seen;
	int in_function;
	struct powervr_spirv_fill *fill;
};

static int module_instruction(struct parse_state *state, uint32_t opcode, const uint32_t *operands, uint32_t count);
static int function_instruction(struct parse_state *state, uint32_t opcode, const uint32_t *operands, uint32_t count);
static int decorate(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int declare_variable(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int load(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int store(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int access_chain(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int composite_extract(struct parse_state *state, const uint32_t *operands, uint32_t count);
static int arithmetic(struct parse_state *state, uint32_t opcode, const uint32_t *operands, uint32_t count);
static struct powervr_spirv_value *value_of(struct parse_state *state, uint32_t id);
static int linear_of(struct parse_state *state, uint32_t id, uint32_t *a, uint32_t *c);
static int set_linear(struct parse_state *state, uint32_t id, uint32_t a, uint32_t c);

/*
 * Reads a shader of the subset: what it writes and where.
 *
 * values is a table of POWERVR_SPIRV_FILL_IDS entries the parse uses
 * for its own bookkeeping.  Returns EINVAL for words that are not a
 * module, ENOTSUP for a module outside the subset.
 */
int
powervr_spirv_fill_parse(
	const uint32_t *words,
	size_t count,
	struct powervr_spirv_value *values,
	struct powervr_spirv_fill *fill)
{
	struct parse_state state;
	uint32_t opcode;
	uint32_t length;
	uint32_t index;
	size_t at;
	int error;

	/* Refuses missing words or results and a header that is not a module's. */
	if (words == NULL || values == NULL || fill == NULL)
		return EINVAL;
	if (count < SPIRV_HEADER_WORDS || words[0] != SPIRV_MAGIC)
		return EINVAL;

	/* Refuses more ids than the table holds. */
	if (words[3] == 0 || words[3] > POWERVR_SPIRV_FILL_IDS)
		return ENOTSUP;

	/* Starts with nothing known of any id. */
	for (index = 0; index < POWERVR_SPIRV_FILL_IDS; index++) {
		values[index].kind = VALUE_NONE;
		values[index].flags = 0;
		values[index].a = 0;
		values[index].c = 0;
		values[index].set = 0;
		values[index].binding = 0;
	}

	state.values = values;
	state.bound = words[3];
	state.entry = 0;
	state.local_x = 0;
	state.buffers = 0;
	state.buffer = 0;
	state.stores = 0;
	state.functions = 0;
	state.stride_seen = 0;
	state.in_function = 0;
	state.fill = fill;

	/* Walks the instructions after the header, each within the module. */
	at = SPIRV_HEADER_WORDS;
	while (at < count) {
		length = words[at] >> SPIRV_COUNT_SHIFT;
		opcode = words[at] & SPIRV_OPCODE_MASK;
		if (length == 0 || length > count - at)
			return EINVAL;

		/* The function's body, or the module's declarations. */
		if (state.in_function) {
			error = function_instruction(&state, opcode, words + at + 1, length - 1U);
		} else {
			error = module_instruction(&state, opcode, words + at + 1, length - 1U);
		}

		if (error != 0)
			return error;
		at += length;
	}

	/* One compute entry point of a known size, one function, one buffer and one store into it. */
	if (state.entry == 0 || state.local_x == 0 || state.functions != 1 || state.in_function)
		return ENOTSUP;
	if (state.buffers != 1 || state.stores != 1 || state.stride_seen == 0)
		return ENOTSUP;

	/* The buffer's set and binding (zero when not decorated). */
	fill->local_x = state.local_x;
	fill->set = values[state.buffer].set;
	fill->binding = values[state.buffer].binding;

	/* Succeeded: the store's index and value are in the fill. */
	return 0;
}

/* Reads one instruction outside the function. */
static int
module_instruction(
	struct parse_state *state,
	uint32_t opcode,
	const uint32_t *operands,
	uint32_t count)
{
	int error;

	/* The declarations the subset reads or passes over. */
	error = 0;
	switch (opcode) {
	case OP_UNDEF:
	case OP_SOURCE_CONTINUED:
	case OP_SOURCE:
	case OP_SOURCE_EXTENSION:
	case OP_NAME:
	case OP_MEMBER_NAME:
	case OP_STRING:
	case OP_LINE:
	case OP_EXTENSION:
	case OP_EXT_INST_IMPORT:
	case OP_MEMORY_MODEL:
	case OP_CAPABILITY:
	case OP_CONSTANT_TRUE:
	case OP_CONSTANT_FALSE:
	case OP_CONSTANT_COMPOSITE:
	case OP_CONSTANT_NULL:
	case OP_NO_LINE:
	case OP_MODULE_PROCESSED:
		break;
	case OP_ENTRY_POINT:
		/* One compute entry point: its model and its function. */
		if (count < 2 || operands[0] != MODEL_GL_COMPUTE || state->entry != 0)
			return ENOTSUP;
		state->entry = operands[1];
		break;
	case OP_EXECUTION_MODE:
		/* The workgroup's size, in x only and within one task; other modes are refused. */
		if (count < 2 || operands[1] != MODE_LOCAL_SIZE)
			return ENOTSUP;
		if (count != 5 || operands[2] == 0 || operands[2] > FILL_LOCAL_MAX || operands[3] != 1 || operands[4] != 1)
			return ENOTSUP;
		state->local_x = operands[2];
		break;
	case OP_DECORATE:
		error = decorate(state, operands, count);
		break;
	case OP_MEMBER_DECORATE:
		/* The array is the block's first member, at offset 0. */
		if (count >= 4 && operands[2] == DECORATION_OFFSET && (operands[1] != 0 || operands[3] != 0))
			return ENOTSUP;
		break;
	case OP_CONSTANT:
		/* A 32-bit constant: a value with no x in it. */
		if (count != 3)
			return ENOTSUP;
		error = set_linear(state, operands[1], 0, operands[2]);
		break;
	case OP_VARIABLE:
		error = declare_variable(state, operands, count);
		break;
	case OP_FUNCTION:
		/* The one function, the entry point's. */
		if (count < 2 || operands[1] != state->entry || state->functions != 0)
			return ENOTSUP;
		state->functions++;
		state->in_function = 1;
		break;
	default:
		/* Types pass; anything else is outside the subset. */
		if (opcode < OP_TYPE_FIRST || opcode > OP_TYPE_LAST)
			return ENOTSUP;
		break;
	}

	/* An instruction that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the instruction is read. */
	return 0;
}

/* Reads one instruction of the function's body. */
static int
function_instruction(
	struct parse_state *state,
	uint32_t opcode,
	const uint32_t *operands,
	uint32_t count)
{
	int error;

	/* The instructions whose results stay in the subset. */
	error = 0;
	switch (opcode) {
	case OP_LABEL:
	case OP_RETURN:
	case OP_LINE:
	case OP_NO_LINE:
		break;
	case OP_FUNCTION_END:
		state->in_function = 0;
		break;
	case OP_VARIABLE:
		error = declare_variable(state, operands, count);
		break;
	case OP_LOAD:
		error = load(state, operands, count);
		break;
	case OP_STORE:
		error = store(state, operands, count);
		break;
	case OP_ACCESS_CHAIN:
	case OP_IN_BOUNDS_ACCESS_CHAIN:
		error = access_chain(state, operands, count);
		break;
	case OP_COMPOSITE_EXTRACT:
		error = composite_extract(state, operands, count);
		break;
	case OP_COPY_OBJECT:
	case OP_BITCAST:
	case OP_IADD:
	case OP_ISUB:
	case OP_IMUL:
	case OP_SHIFT_LEFT_LOGICAL:
		error = arithmetic(state, opcode, operands, count);
		break;
	default:
		/* Any other instruction is outside the subset. */
		error = ENOTSUP;
		break;
	}

	/* An instruction that was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the instruction is followed. */
	return 0;
}

/* Reads one decoration: the invocation id, a set, a binding, an array stride. */
static int
decorate(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *target;

	/* The decorated id. */
	if (count < 2)
		return EINVAL;
	target = value_of(state, operands[0]);
	if (target == NULL)
		return EINVAL;

	/* The decorations the subset reads; others pass. */
	if (operands[1] == DECORATION_BUILT_IN && count >= 3 && operands[2] == BUILT_IN_GLOBAL_INVOCATION_ID) {
		target->flags |= FLAG_INVOCATION_ID;
	} else if (operands[1] == DECORATION_DESCRIPTOR_SET && count >= 3) {
		target->flags |= FLAG_SET;
		target->set = operands[2];
	} else if (operands[1] == DECORATION_BINDING && count >= 3) {
		target->flags |= FLAG_BINDING;
		target->binding = operands[2];
	} else if (operands[1] == DECORATION_ARRAY_STRIDE && count >= 3) {
		/* Arrays of 32-bit words only. */
		if (operands[2] != FILL_STRIDE)
			return ENOTSUP;
		state->stride_seen = 1;
	}

	/* Succeeded: the decoration is read. */
	return 0;
}

/*
 * Declares a variable: the invocation id, the one buffer, or a variable
 * of the function.
 */
static int
declare_variable(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *variable;
	uint32_t a;
	uint32_t c;
	int error;

	/* The variable and its storage class. */
	if (count < 3)
		return EINVAL;
	variable = value_of(state, operands[1]);
	if (variable == NULL)
		return EINVAL;

	/* The invocation id among the inputs; no other input. */
	if (operands[2] == CLASS_INPUT) {
		if ((variable->flags & FLAG_INVOCATION_ID) == 0)
			return ENOTSUP;
		variable->kind = VALUE_INVOCATION_VARIABLE;
		return 0;
	}

	/* The one buffer. */
	if (operands[2] == CLASS_STORAGE_BUFFER || operands[2] == CLASS_UNIFORM) {
		if (state->in_function)
			return ENOTSUP;
		variable->kind = VALUE_BUFFER_VARIABLE;
		state->buffers++;
		state->buffer = operands[1];
		return 0;
	}

	/* A variable of the function, with the value of its initializer if it has one. */
	if (operands[2] == CLASS_FUNCTION && state->in_function) {
		variable->kind = VALUE_LOCAL_VARIABLE;
		if (count >= 4) {
			error = linear_of(state, operands[3], &a, &c);
			if (error != 0)
				return error;
			variable->flags |= FLAG_HELD;
			variable->a = a;
			variable->c = c;
		}
		return 0;
	}

	/* Every other variable is outside the subset. */
	return ENOTSUP;
}

/* Loads the invocation id, one of its components, or a variable of the function. */
static int
load(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *pointer;
	struct powervr_spirv_value *result;

	/* The result and the pointer. */
	if (count < 3)
		return EINVAL;
	result = value_of(state, operands[1]);
	pointer = value_of(state, operands[2]);
	if (result == NULL || pointer == NULL)
		return EINVAL;

	/* The whole invocation id. */
	if (pointer->kind == VALUE_INVOCATION_VARIABLE) {
		result->kind = VALUE_INVOCATION_VECTOR;
		return 0;
	}

	/* One component: x, or y and z, which are zero in a dispatch of one row. */
	if (pointer->kind == VALUE_INVOCATION_COMPONENT) {
		result->kind = VALUE_LINEAR;
		result->a = pointer->a;
		result->c = 0;
		return 0;
	}

	/* A variable of the function that holds a value. */
	if (pointer->kind == VALUE_LOCAL_VARIABLE && (pointer->flags & FLAG_HELD) != 0) {
		result->kind = VALUE_LINEAR;
		result->a = pointer->a;
		result->c = pointer->c;
		return 0;
	}

	/* Reading the buffer, or anything else, is outside the subset. */
	return ENOTSUP;
}

/* Stores into a variable of the function, or the one store into the buffer. */
static int
store(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *pointer;
	uint32_t a;
	uint32_t c;
	int error;

	/* The pointer and the value. */
	if (count < 2)
		return EINVAL;
	pointer = value_of(state, operands[0]);
	if (pointer == NULL)
		return EINVAL;
	error = linear_of(state, operands[1], &a, &c);
	if (error != 0)
		return error;

	/* A variable of the function now holds the value. */
	if (pointer->kind == VALUE_LOCAL_VARIABLE) {
		pointer->flags |= FLAG_HELD;
		pointer->a = a;
		pointer->c = c;
		return 0;
	}

	/* The buffer's word: once. */
	if (pointer->kind != VALUE_BUFFER_WORD || state->stores != 0)
		return ENOTSUP;
	state->stores++;
	state->fill->index_addend = pointer->c;
	state->fill->multiplier = a;
	state->fill->addend = c;

	/* Succeeded: the store is recorded. */
	return 0;
}

/*
 * Follows an access chain: a component of the invocation id, or the
 * buffer's array (its first member) at an index of x plus a constant.
 */
static int
access_chain(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *base;
	struct powervr_spirv_value *result;
	uint32_t a;
	uint32_t c;
	int error;

	/* The result and the base. */
	if (count < 4)
		return EINVAL;
	result = value_of(state, operands[1]);
	base = value_of(state, operands[2]);
	if (result == NULL || base == NULL)
		return EINVAL;

	/* A component of the invocation id: x is x, y and z are zero. */
	if (base->kind == VALUE_INVOCATION_VARIABLE && count == 4) {
		error = linear_of(state, operands[3], &a, &c);
		if (error != 0)
			return error;
		if (a != 0 || c > 2)
			return ENOTSUP;
		result->kind = VALUE_INVOCATION_COMPONENT;
		result->a = 0;
		if (c == 0)
			result->a = 1;
		return 0;
	}

	/* The buffer's first member, then the element at x plus a constant. */
	if (base->kind != VALUE_BUFFER_VARIABLE || count != 5)
		return ENOTSUP;
	error = linear_of(state, operands[3], &a, &c);
	if (error != 0)
		return error;
	if (a != 0 || c != 0)
		return ENOTSUP;
	error = linear_of(state, operands[4], &a, &c);
	if (error != 0)
		return error;
	if (a != 1)
		return ENOTSUP;
	result->kind = VALUE_BUFFER_WORD;
	result->a = 1;
	result->c = c;

	/* Succeeded: the pointer is followed. */
	return 0;
}

/* Extracts a component of the loaded invocation id. */
static int
composite_extract(
	struct parse_state *state,
	const uint32_t *operands,
	uint32_t count)
{
	struct powervr_spirv_value *composite;
	struct powervr_spirv_value *result;

	/* One index into the invocation id. */
	if (count != 4)
		return ENOTSUP;
	result = value_of(state, operands[1]);
	composite = value_of(state, operands[2]);
	if (result == NULL || composite == NULL)
		return EINVAL;
	if (composite->kind != VALUE_INVOCATION_VECTOR || operands[3] > 2)
		return ENOTSUP;

	/* x is x; y and z are zero in a dispatch of one row. */
	result->kind = VALUE_LINEAR;
	result->a = 0;
	if (operands[3] == 0)
		result->a = 1;
	result->c = 0;

	/* Succeeded: the component is a value. */
	return 0;
}

/*
 * Follows an integer operation whose result stays a*x + c: a copy, a
 * bitcast, a sum, a difference, a product with a constant, a left shift
 * by a constant.
 */
static int
arithmetic(
	struct parse_state *state,
	uint32_t opcode,
	const uint32_t *operands,
	uint32_t count)
{
	uint32_t first_a;
	uint32_t first_c;
	uint32_t second_a;
	uint32_t second_c;
	int error;

	/* The first operand. */
	if (count < 3)
		return EINVAL;
	error = linear_of(state, operands[2], &first_a, &first_c);
	if (error != 0)
		return error;

	/* A copy or a bitcast keeps the value. */
	if (opcode == OP_COPY_OBJECT || opcode == OP_BITCAST)
		return set_linear(state, operands[1], first_a, first_c);

	/* The second operand. */
	if (count < 4)
		return EINVAL;
	error = linear_of(state, operands[3], &second_a, &second_c);
	if (error != 0)
		return error;

	/* Each operation on the two values, modulo 2^32. */
	if (opcode == OP_IADD)
		return set_linear(state, operands[1], first_a + second_a, first_c + second_c);
	if (opcode == OP_ISUB)
		return set_linear(state, operands[1], first_a - second_a, first_c - second_c);
	if (opcode == OP_IMUL) {
		if (first_a != 0 && second_a != 0)
			return ENOTSUP;
		return set_linear(state, operands[1], first_a * second_c + second_a * first_c, first_c * second_c);
	}

	/* A left shift by a constant below the word's width. */
	if (second_a != 0 || second_c >= 32U)
		return ENOTSUP;

	/* Succeeded: the shifted value. */
	return set_linear(state, operands[1], first_a << second_c, first_c << second_c);
}

/* Finds an id's entry, or NULL for an id past the bound. */
static struct powervr_spirv_value *
value_of(
	struct parse_state *state,
	uint32_t id)
{
	/* Ids run from 1 to the bound less one. */
	if (id == 0 || id >= state->bound)
		return NULL;

	/* Succeeded: the entry. */
	return &state->values[id];
}

/* Reads an id's value a*x + c; ENOTSUP for an id that holds none. */
static int
linear_of(
	struct parse_state *state,
	uint32_t id,
	uint32_t *a,
	uint32_t *c)
{
	const struct powervr_spirv_value *value;

	/* An id that holds a value. */
	value = value_of(state, id);
	if (value == NULL)
		return EINVAL;
	if (value->kind != VALUE_LINEAR)
		return ENOTSUP;

	/* Succeeded: the value. */
	*a = value->a;
	*c = value->c;
	return 0;
}

/* Makes an id hold the value a*x + c. */
static int
set_linear(
	struct parse_state *state,
	uint32_t id,
	uint32_t a,
	uint32_t c)
{
	struct powervr_spirv_value *value;

	/* An id within the bound. */
	value = value_of(state, id);
	if (value == NULL)
		return EINVAL;

	/* Succeeded: the id holds the value. */
	value->kind = VALUE_LINEAR;
	value->a = a;
	value->c = c;
	return 0;
}
