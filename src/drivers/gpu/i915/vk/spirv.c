/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * SPIR-V parser producing the baseline, straight-line IR the compiler lowers.
 *
 * The opcode and enumerant numbers are from the public Khronos SPIR-V
 * specification.  Only the subset the vkdemo shaders use is decoded; any other
 * opcode fails the parse rather than being silently dropped.  Pointers created
 * by OpVariable and OpAccessChain are tracked so a load or store resolves to an
 * input, output or push-constant reference in the IR.
 */

#include "vk-internal.h"
#include "spirv.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* SPIR-V module header (Khronos SPIR-V spec, section 2.3). */
#define SPIRV_MAGIC 0x07230203U

/* Opcodes (Khronos SPIR-V spec, section 3.37). */
#define OP_EXT_INST_IMPORT 11U
#define OP_EXT_INST 12U
#define OP_ENTRY_POINT 15U
#define OP_DECORATE 71U
#define OP_MEMBER_DECORATE 72U
#define OP_TYPE_FLOAT 22U
#define OP_TYPE_VECTOR 23U
#define OP_TYPE_POINTER 32U
#define OP_CONSTANT 43U
#define OP_VARIABLE 59U
#define OP_LOAD 61U
#define OP_STORE 62U
#define OP_ACCESS_CHAIN 65U
#define OP_FUNCTION 54U
#define OP_FUNCTION_END 56U
#define OP_VECTOR_SHUFFLE 79U
#define OP_COMPOSITE_CONSTRUCT 80U
#define OP_COMPOSITE_EXTRACT 81U
#define OP_FNEGATE 127U
#define OP_FADD 129U
#define OP_FSUB 131U
#define OP_FMUL 133U
#define OP_VECTOR_TIMES_SCALAR 142U
#define OP_DOT 148U
#define OP_IMAGE_SAMPLE_IMPLICIT_LOD 87U

/* Storage classes (SPIR-V spec, section 3.7). */
#define SC_UNIFORM_CONSTANT 0U
#define SC_INPUT 1U
#define SC_OUTPUT 3U
#define SC_PUSH_CONSTANT 9U

/* Decorations (SPIR-V spec, section 3.20). */
#define DEC_BUILTIN 11U
#define DEC_LOCATION 30U
#define DEC_BINDING 33U
#define DEC_DESCRIPTOR_SET 34U

/* Execution models (SPIR-V spec, section 3.3). */
#define EM_VERTEX 0U
#define EM_FRAGMENT 4U

/* GLSL.std.450 extended instruction numbers used by the shaders. */
#define GLSL_SIN 13U
#define GLSL_COS 14U
#define GLSL_INVERSE_SQRT 32U

/* Pointer target kinds tracked per result id. */
#define PTR_NONE 0U
#define PTR_INPUT 1U
#define PTR_OUTPUT 2U
#define PTR_PUSH 3U
#define PTR_SAMPLER 4U
#define PTR_OUTPUT_BUILTIN 5U

/* Per result-id facts gathered in the first pass. */
struct spirv_id {
	uint16_t opcode;
	uint16_t storage;
	uint8_t ptr_kind;
	uint8_t has_location;
	uint8_t has_binding;
	uint8_t has_set;
	uint8_t is_builtin;
	uint8_t float_components;
	uint32_t location;
	uint32_t binding;
	uint32_t set;
	uint32_t type;
	uint32_t constant;
	uint32_t ptr_key;
	uint32_t ptr_offset;
};

/* The decode state threaded through both passes. */
struct spirv_parser {
	const uint32_t *code;
	uint32_t words;
	uint32_t bound;
	struct spirv_id *ids;
	struct i915_vk_shader_ir *ir;
	int error;
};

static int spirv_pass_declarations(struct spirv_parser *parser);
static int spirv_pass_body(struct spirv_parser *parser);
static void spirv_add_io(struct spirv_parser *parser, uint32_t id, int is_input);
static void spirv_add_uniform(struct spirv_parser *parser, uint32_t id);
static uint32_t spirv_components(const struct spirv_parser *parser, uint32_t type_id);
static void spirv_emit(struct spirv_parser *parser, enum i915_vk_ir_op op, uint32_t dst, uint32_t s0, uint32_t s1, uint32_t imm);

/* Parses a SPIR-V module into the baseline IR for one stage. */
int
i915_vk_spirv_parse(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out)
{
	struct spirv_parser parser;
	struct i915_vk_shader_ir *ir;
	int error;

	/* The caller receives nothing unless the whole module parses. */
	*out = NULL;

	/* A module must have a header and a matching magic. */
	if (words < 5U || code[0] != SPIRV_MAGIC)
		return EINVAL;

	/* The IR owns the interface lists and the instruction stream. */
	ir = kern_calloc(1U, sizeof(*ir));
	if (ir == NULL)
		return ENOMEM;
	ir->stage = stage;

	/* The id table has one slot per result id the module bounds. */
	parser.code = code;
	parser.words = (uint32_t)words;
	parser.bound = code[3];
	parser.ir = ir;
	parser.error = 0;
	parser.ids = kern_calloc(parser.bound == 0U ? 1U : parser.bound, sizeof(*parser.ids));
	if (parser.ids == NULL) {
		kern_free(ir);
		return ENOMEM;
	}

	/* Instruction storage and interface lists are bounded by the id count. */
	ir->instructions = kern_calloc(parser.bound == 0U ? 1U : parser.bound, sizeof(*ir->instructions));
	ir->inputs = kern_calloc(parser.bound == 0U ? 1U : parser.bound, sizeof(*ir->inputs));
	ir->outputs = kern_calloc(parser.bound == 0U ? 1U : parser.bound, sizeof(*ir->outputs));
	ir->uniforms = kern_calloc(parser.bound == 0U ? 1U : parser.bound, sizeof(*ir->uniforms));
	if (ir->instructions == NULL || ir->inputs == NULL || ir->outputs == NULL || ir->uniforms == NULL) {
		i915_vk_spirv_free(ir);
		kern_free(parser.ids);
		return ENOMEM;
	}
	ir->value_count = parser.bound;

	/* The first pass records types, decorations and interface variables. */
	error = spirv_pass_declarations(&parser);
	if (error != 0) {
		i915_vk_spirv_free(ir);
		kern_free(parser.ids);
		return error;
	}

	/* The second pass lowers the entry function body to IR instructions. */
	error = spirv_pass_body(&parser);
	if (error != 0) {
		i915_vk_spirv_free(ir);
		kern_free(parser.ids);
		return error;
	}

	/* The id table is scratch that both passes shared. */
	kern_free(parser.ids);

	/* Succeeded: the compiler can lower this shader. */
	*out = ir;
	return 0;
}

/* Releases a parsed shader IR and its lists. */
void
i915_vk_spirv_free(
	struct i915_vk_shader_ir *ir)
{
	/* A module that never parsed is nothing to release. */
	if (ir == NULL)
		return;

	if (ir->instructions != NULL)
		kern_free(ir->instructions);
	if (ir->inputs != NULL)
		kern_free(ir->inputs);
	if (ir->outputs != NULL)
		kern_free(ir->outputs);
	if (ir->uniforms != NULL)
		kern_free(ir->uniforms);
	kern_free(ir);
}

/* Records types, constants, decorations and interface variables. */
static int
spirv_pass_declarations(
	struct spirv_parser *parser)
{
	const uint32_t *word;
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	uint32_t id;

	/* Decoding starts after the five-word module header. */
	offset = 5U;
	while (offset < parser->words) {
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;

		/* A zero or overrunning instruction length ends the module badly. */
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		/* OpEntryPoint names the stage and the interface variable ids. */
		if (opcode == OP_ENTRY_POINT) {
			if (word[1] == EM_VERTEX)
				parser->ir->stage = I915_VK_STAGE_VERTEX;
			if (word[1] == EM_FRAGMENT)
				parser->ir->stage = I915_VK_STAGE_FRAGMENT;
		}

		/* OpDecorate attaches a location, binding, set or builtin to an id. */
		if (opcode == OP_DECORATE && count >= 3U) {
			id = word[1];
			if (id < parser->bound) {
				if (word[2] == DEC_LOCATION && count >= 4U) {
					parser->ids[id].has_location = 1U;
					parser->ids[id].location = word[3];
				}
				if (word[2] == DEC_BINDING && count >= 4U) {
					parser->ids[id].has_binding = 1U;
					parser->ids[id].binding = word[3];
				}
				if (word[2] == DEC_DESCRIPTOR_SET && count >= 4U) {
					parser->ids[id].has_set = 1U;
					parser->ids[id].set = word[3];
				}
				if (word[2] == DEC_BUILTIN)
					parser->ids[id].is_builtin = 1U;
			}
		}

		/* OpTypeFloat and OpTypeVector let the parser size interface slots. */
		if (opcode == OP_TYPE_FLOAT && count >= 2U) {
			id = word[1];
			if (id < parser->bound)
				parser->ids[id].float_components = 1U;
		}
		if (opcode == OP_TYPE_VECTOR && count >= 4U) {
			id = word[1];
			if (id < parser->bound) {
				parser->ids[id].opcode = OP_TYPE_VECTOR;
				parser->ids[id].type = word[2];
				parser->ids[id].float_components = (uint8_t)word[3];
			}
		}

		/* OpTypePointer records the storage class its variables inherit. */
		if (opcode == OP_TYPE_POINTER && count >= 4U) {
			id = word[1];
			if (id < parser->bound) {
				parser->ids[id].opcode = OP_TYPE_POINTER;
				parser->ids[id].storage = (uint16_t)word[2];
				parser->ids[id].type = word[3];
			}
		}

		/* OpConstant holds the integer indices access chains resolve with. */
		if (opcode == OP_CONSTANT && count >= 4U) {
			id = word[2];
			if (id < parser->bound) {
				parser->ids[id].opcode = OP_CONSTANT;
				parser->ids[id].constant = word[3];
			}
		}

		/* OpVariable in an interface storage class becomes an IR slot. */
		if (opcode == OP_VARIABLE && count >= 4U) {
			id = word[2];
			if (id < parser->bound) {
				parser->ids[id].opcode = OP_VARIABLE;
				parser->ids[id].storage = (uint16_t)word[3];
				parser->ids[id].type = word[1];
				if (word[3] == SC_INPUT && parser->ids[id].has_location != 0U) {
					parser->ids[id].ptr_kind = PTR_INPUT;
					spirv_add_io(parser, id, 1);
				} else if (word[3] == SC_OUTPUT && parser->ids[id].has_location != 0U) {
					parser->ids[id].ptr_kind = PTR_OUTPUT;
					spirv_add_io(parser, id, 0);
				} else if (word[3] == SC_OUTPUT) {
					parser->ids[id].ptr_kind = PTR_OUTPUT_BUILTIN;
				} else if (word[3] == SC_PUSH_CONSTANT) {
					parser->ids[id].ptr_kind = PTR_PUSH;
				} else if (word[3] == SC_UNIFORM_CONSTANT) {
					parser->ids[id].ptr_kind = PTR_SAMPLER;
					spirv_add_uniform(parser, id);
				}
			}
		}

		offset += count;
	}

	/* Succeeded: every declaration the body relies on is recorded. */
	return 0;
}

/* Adds an input or output interface slot for a variable id. */
static void
spirv_add_io(
	struct spirv_parser *parser,
	uint32_t id,
	int is_input)
{
	struct i915_vk_io *slot;
	uint32_t components;

	/* A builtin output (gl_Position) carries no location and is not listed. */
	if (is_input == 0 && parser->ids[id].is_builtin != 0U)
		return;

	/* The pointer's pointee type gives the component count. */
	components = spirv_components(parser, parser->ids[id].type);

	/* Inputs and outputs grow their own lists as variables appear. */
	if (is_input != 0) {
		slot = &parser->ir->inputs[parser->ir->input_count];
		parser->ir->input_count++;
	} else {
		slot = &parser->ir->outputs[parser->ir->output_count];
		parser->ir->output_count++;
	}
	slot->location = parser->ids[id].location;
	slot->components = components;
	slot->type = 0U;
}

/* Adds a sampled-image uniform slot for a variable id. */
static void
spirv_add_uniform(
	struct spirv_parser *parser,
	uint32_t id)
{
	struct i915_vk_uniform *slot;

	slot = &parser->ir->uniforms[parser->ir->uniform_count];
	parser->ir->uniform_count++;
	slot->set = parser->ids[id].set;
	slot->binding = parser->ids[id].binding;
	slot->kind = 1U;
	slot->offset = 0U;
	slot->size = 0U;
}

/* Resolves the component count behind a pointer type or vector type id. */
static uint32_t
spirv_components(
	const struct spirv_parser *parser,
	uint32_t type_id)
{
	uint32_t pointee;

	/* An out-of-range type resolves to a scalar. */
	if (type_id >= parser->bound)
		return 1U;

	/* A pointer type is followed to the value type it addresses. */
	pointee = type_id;
	if (parser->ids[type_id].opcode == OP_TYPE_POINTER)
		pointee = parser->ids[type_id].type;
	if (pointee >= parser->bound)
		return 1U;

	/* A vector reports its component count; anything else is a scalar. */
	if (parser->ids[pointee].opcode == OP_TYPE_VECTOR)
		return parser->ids[pointee].float_components;

	return 1U;
}

/* Lowers the entry function body into IR instructions. */
static int
spirv_pass_body(
	struct spirv_parser *parser)
{
	const uint32_t *word;
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	uint32_t base;
	uint32_t result;
	uint32_t ptr;

	/* Decoding again from the top; only the function body emits IR. */
	offset = 5U;
	while (offset < parser->words) {
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		/* OpAccessChain derives a pointer from a base and constant indices. */
		if (opcode == OP_ACCESS_CHAIN && count >= 4U) {
			result = word[2];
			base = word[3];
			if (result < parser->bound && base < parser->bound) {
				parser->ids[result].ptr_kind = parser->ids[base].ptr_kind;
				parser->ids[result].ptr_key = parser->ids[base].ptr_key;
				parser->ids[result].ptr_offset = parser->ids[base].ptr_offset;
				if (count >= 5U && word[4] < parser->bound)
					parser->ids[result].ptr_offset += parser->ids[word[4]].constant;
			}
		}

		/* OpLoad through an interface pointer becomes an input or push read. */
		if (opcode == OP_LOAD && count >= 4U) {
			result = word[2];
			ptr = word[3];
			if (result < parser->bound && ptr < parser->bound) {
				if (parser->ids[ptr].ptr_kind == PTR_INPUT)
					spirv_emit(parser, I915_VK_IR_LOAD_INPUT, result, parser->ids[ptr].location, 0U, 0U);
				else if (parser->ids[ptr].ptr_kind == PTR_PUSH)
					spirv_emit(parser, I915_VK_IR_LOAD_PUSH, result, 0U, 0U, parser->ids[ptr].ptr_offset);
				else if (parser->ids[ptr].ptr_kind == PTR_SAMPLER)
					parser->ids[result].ptr_key = parser->ids[ptr].binding;
			}
		}

		/* OpStore through an output pointer becomes an output write. */
		if (opcode == OP_STORE && count >= 3U) {
			ptr = word[1];
			if (ptr < parser->bound) {
				if (parser->ids[ptr].ptr_kind == PTR_OUTPUT)
					spirv_emit(parser, I915_VK_IR_STORE_OUTPUT, 0U, word[2], parser->ids[ptr].location, 0U);
				else if (parser->ids[ptr].ptr_kind == PTR_OUTPUT_BUILTIN)
					spirv_emit(parser, I915_VK_IR_STORE_OUTPUT, 0U, word[2], 0xFFFFFFFFU, 0U);
			}
		}

		/* Arithmetic and the sampled read map one instruction to one IR op. */
		if (opcode == OP_FADD)
			spirv_emit(parser, I915_VK_IR_FADD, word[2], word[3], word[4], 0U);
		if (opcode == OP_FSUB)
			spirv_emit(parser, I915_VK_IR_FSUB, word[2], word[3], word[4], 0U);
		if (opcode == OP_FMUL || opcode == OP_VECTOR_TIMES_SCALAR)
			spirv_emit(parser, I915_VK_IR_FMUL, word[2], word[3], word[4], 0U);
		if (opcode == OP_DOT)
			spirv_emit(parser, I915_VK_IR_DOT, word[2], word[3], word[4], 0U);
		if (opcode == OP_COMPOSITE_CONSTRUCT)
			spirv_emit(parser, I915_VK_IR_COMPOSE, word[2], count >= 4U ? word[3] : 0U, count >= 5U ? word[4] : 0U, 0U);
		if (opcode == OP_COMPOSITE_EXTRACT && count >= 5U)
			spirv_emit(parser, I915_VK_IR_EXTRACT, word[2], word[3], 0U, word[4]);
		if (opcode == OP_IMAGE_SAMPLE_IMPLICIT_LOD && count >= 5U)
			spirv_emit(parser, I915_VK_IR_SAMPLE, word[2], parser->ids[word[3]].ptr_key, word[4], 0U);

		/* OpExtInst carries the GLSL.std.450 transcendentals. */
		if (opcode == OP_EXT_INST && count >= 5U) {
			if (word[4] == GLSL_SIN)
				spirv_emit(parser, I915_VK_IR_SIN, word[2], word[5 <= count - 1U ? 5U : 4U], 0U, 0U);
			if (word[4] == GLSL_COS)
				spirv_emit(parser, I915_VK_IR_COS, word[2], word[5 <= count - 1U ? 5U : 4U], 0U, 0U);
			if (word[4] == GLSL_INVERSE_SQRT)
				spirv_emit(parser, I915_VK_IR_RSQ, word[2], word[5 <= count - 1U ? 5U : 4U], 0U, 0U);
		}

		offset += count;
	}

	/* Succeeded: the body is a straight-line IR instruction list. */
	return 0;
}

/* Appends one IR instruction. */
static void
spirv_emit(
	struct spirv_parser *parser,
	enum i915_vk_ir_op op,
	uint32_t dst,
	uint32_t s0,
	uint32_t s1,
	uint32_t imm)
{
	struct i915_vk_inst *inst;

	/* The instruction array is bounded by the id count and cannot overflow. */
	if (parser->ir->instruction_count >= parser->bound)
		return;

	inst = &parser->ir->instructions[parser->ir->instruction_count];
	parser->ir->instruction_count++;
	inst->op = op;
	inst->dst = dst;
	inst->src[0] = s0;
	inst->src[1] = s1;
	inst->src[2] = 0U;
	inst->src[3] = 0U;
	inst->immediate = imm;
	inst->swizzle = 0U;
}
