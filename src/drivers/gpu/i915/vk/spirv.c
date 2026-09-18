/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * SPIR-V parser producing a straight-line, SCALAR IR.
 *
 * The opcode and enumerant numbers are from the public Khronos SPIR-V
 * specification.  Every IR value is one scalar float; a SPIR-V vector is a
 * group of up to four IR values, one per component.  That is what lets the
 * operations that only rearrange components -- OpCompositeConstruct,
 * OpCompositeExtract, OpVectorShuffle, component access chains -- be lowered
 * exactly: they emit no IR at all, they only name which scalar is which.
 *
 * Function-storage variables are not memory.  The accepted shaders are one
 * basic block, so a load reads the value of the latest store to that
 * component: the parser keeps, per local variable and component, the IR value
 * currently stored there (store-to-load forwarding).  A load of a component
 * that was never stored is refused.
 *
 * Inside a function body an instruction with execution semantics that is not
 * lowered FAILS the parse (ENOTSUP, with a diagnostic) -- it is never skipped,
 * because a skipped instruction is a different shader.  The same holds for
 * decorations: the ones that place or identify data are interpreted, the ones
 * listed as having no effect on this lowering are ignored by name, and any
 * other decoration is refused.  EINVAL is kept for malformed modules.
 */

#include "vk-internal.h"
#include "spirv.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* SPIR-V module header (Khronos SPIR-V spec, section 2.3). */
#define SPIRV_MAGIC 0x07230203U

/* Opcodes (Khronos SPIR-V spec, section 3.37). */
#define OP_NOP 0U
#define OP_SOURCE_CONTINUED 2U
#define OP_SOURCE 3U
#define OP_SOURCE_EXTENSION 4U
#define OP_NAME 5U
#define OP_MEMBER_NAME 6U
#define OP_STRING 7U
#define OP_LINE 8U
#define OP_EXTENSION 10U
#define OP_EXT_INST_IMPORT 11U
#define OP_EXT_INST 12U
#define OP_MEMORY_MODEL 14U
#define OP_ENTRY_POINT 15U
#define OP_EXECUTION_MODE 16U
#define OP_CAPABILITY 17U
#define OP_TYPE_VOID 19U
#define OP_TYPE_BOOL 20U
#define OP_TYPE_INT 21U
#define OP_TYPE_FLOAT 22U
#define OP_TYPE_VECTOR 23U
#define OP_TYPE_IMAGE 25U
#define OP_TYPE_SAMPLER 26U
#define OP_TYPE_SAMPLED_IMAGE 27U
#define OP_TYPE_ARRAY 28U
#define OP_TYPE_STRUCT 30U
#define OP_TYPE_POINTER 32U
#define OP_TYPE_FUNCTION 33U
#define OP_CONSTANT 43U
#define OP_FUNCTION 54U
#define OP_FUNCTION_END 56U
#define OP_VARIABLE 59U
#define OP_LOAD 61U
#define OP_STORE 62U
#define OP_ACCESS_CHAIN 65U
#define OP_DECORATE 71U
#define OP_MEMBER_DECORATE 72U
#define OP_VECTOR_SHUFFLE 79U
#define OP_COMPOSITE_CONSTRUCT 80U
#define OP_COMPOSITE_EXTRACT 81U
#define OP_IMAGE_SAMPLE_IMPLICIT_LOD 87U
#define OP_FNEGATE 127U
#define OP_FADD 129U
#define OP_FSUB 131U
#define OP_FMUL 133U
#define OP_VECTOR_TIMES_SCALAR 142U
#define OP_DOT 148U
#define OP_LABEL 248U
#define OP_RETURN 253U
#define OP_NO_LINE 317U
#define OP_MODULE_PROCESSED 330U

/* Storage classes (SPIR-V spec, section 3.7). */
#define SC_UNIFORM_CONSTANT 0U
#define SC_INPUT 1U
#define SC_OUTPUT 3U
#define SC_FUNCTION 7U
#define SC_PUSH_CONSTANT 9U

/* Decorations (SPIR-V spec, section 3.20). */
#define DEC_RELAXED_PRECISION 0U
#define DEC_BLOCK 2U
#define DEC_BUILTIN 11U
#define DEC_LOCATION 30U
#define DEC_BINDING 33U
#define DEC_DESCRIPTOR_SET 34U
#define DEC_OFFSET 35U

/* BuiltIn values (SPIR-V spec, section 3.21). */
#define BUILTIN_POSITION 0U

/* Execution models (SPIR-V spec, section 3.3). */
#define EM_VERTEX 0U
#define EM_FRAGMENT 4U

/* GLSL.std.450 extended instruction numbers. */
#define GLSL_SIN 13U
#define GLSL_COS 14U
#define GLSL_INVERSE_SQRT 32U

#define NO_VALUE 0xFFFFFFFFU
#define MAX_MEMBERS 8U

/* What an id is. */
enum id_kind {
	ID_NONE = 0,
	ID_TYPE_VOID, ID_TYPE_BOOL, ID_TYPE_INT, ID_TYPE_FLOAT, ID_TYPE_VECTOR, ID_TYPE_IMAGE,
	ID_TYPE_SAMPLER, ID_TYPE_SAMPLED_IMAGE, ID_TYPE_ARRAY, ID_TYPE_STRUCT, ID_TYPE_POINTER,
	ID_TYPE_FUNCTION,
	ID_CONSTANT,            /* scalar int or float constant */
	ID_VARIABLE,            /* OpVariable */
	ID_POINTER,             /* OpAccessChain result */
	ID_VALUE,               /* float scalar / vector value: comp[] names the IR scalars */
	ID_SAMPLED_IMAGE,       /* a loaded combined image sampler */
	ID_EXT_SET
};

/* Pointer target kinds. */
#define PTR_NONE 0U
#define PTR_INPUT 1U
#define PTR_OUTPUT 2U
#define PTR_OUTPUT_BLOCK 3U     /* gl_PerVertex: the members carry the builtins */
#define PTR_PUSH 4U
#define PTR_SAMPLER 5U
#define PTR_LOCAL 6U

struct spirv_id {
	uint8_t kind;               /* enum id_kind */
	uint8_t width;              /* int / float bit width */
	uint8_t count;              /* vector component count; struct member count; value component count */
	uint8_t ptr_kind;
	uint8_t has_location;
	uint8_t has_binding;
	uint8_t has_set;
	uint8_t has_builtin;
	uint16_t storage;           /* pointer type / variable storage class */
	uint32_t type;              /* element / pointee / value type id */
	uint32_t location;
	uint32_t binding;
	uint32_t set;
	uint32_t builtin;
	uint32_t constant;          /* constant bits */
	/* struct types */
	uint32_t member_type[MAX_MEMBERS];
	uint32_t member_offset[MAX_MEMBERS];
	uint32_t member_builtin[MAX_MEMBERS];   /* NO_VALUE when not a builtin */
	uint8_t member_has_offset[MAX_MEMBERS];
	/* values; locals keep their CURRENT stored scalars here too */
	uint32_t comp[4];
	/* pointers (variables and access chains) */
	uint32_t var;               /* the OpVariable the pointer leads to */
	uint32_t pointee;           /* type id of what the pointer addresses now */
	int32_t member;             /* struct member selected, or -1 */
	int32_t component;          /* vector component selected, or -1 */
};

/* The decode state threaded through both passes. */
struct spirv_parser {
	const uint32_t *code;
	uint32_t words;
	uint32_t bound;
	struct spirv_id *ids;
	struct i915_vk_shader_ir *ir;
	uint32_t capacity;          /* instruction slots */
	uint32_t body_instructions;
	int error;
	struct i915_vk_spirv_diag diag;
};

static int spirv_pass_declarations(struct spirv_parser *parser);
static int spirv_pass_body(struct spirv_parser *parser);
static void spirv_add_io(struct spirv_parser *parser, uint32_t id, int is_input);
static void spirv_add_uniform(struct spirv_parser *parser, uint32_t id);

/* Parses a SPIR-V module into the IR for one stage. */
int
i915_vk_spirv_parse(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out)
{
	return i915_vk_spirv_parse_diag(code, words, stage, out, NULL);
}

int
i915_vk_spirv_parse_diag(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out,
	struct i915_vk_spirv_diag *diag)
{
	struct spirv_parser parser;
	struct i915_vk_shader_ir *ir;
	uint32_t slots;
	int error;

	/* The caller receives nothing unless the whole module parses. */
	*out = NULL;
	if (diag != NULL)
		memset(diag, 0, sizeof(*diag));

	/* A module must have a header and a matching magic. */
	if (words < 5U || code[0] != SPIRV_MAGIC || code[3] == 0U || code[3] > 65536U)
		return EINVAL;

	ir = kern_calloc(1U, sizeof(*ir));
	if (ir == NULL)
		return ENOMEM;
	ir->stage = stage;

	memset(&parser, 0, sizeof(parser));
	parser.code = code;
	parser.words = (uint32_t)words;
	parser.bound = code[3];
	parser.ir = ir;
	parser.ids = kern_calloc(parser.bound, sizeof(*parser.ids));
	if (parser.ids == NULL) {
		kern_free(ir);
		return ENOMEM;
	}

	/* Interface lists are bounded by the id count. */
	slots = parser.bound;
	ir->inputs = kern_calloc(slots, sizeof(*ir->inputs));
	ir->outputs = kern_calloc(slots, sizeof(*ir->outputs));
	ir->uniforms = kern_calloc(slots, sizeof(*ir->uniforms));
	if (ir->inputs == NULL || ir->outputs == NULL || ir->uniforms == NULL) {
		i915_vk_spirv_free(ir);
		kern_free(parser.ids);
		return ENOMEM;
	}

	/* The first pass records types, constants, decorations and interface variables. */
	error = spirv_pass_declarations(&parser);

	/*
	 * One SPIR-V instruction lowers to at most 7 IR instructions (a four-component
	 * dot product: four multiplies, three adds); the stream is sized for that, and
	 * running out is an error, never a silently shorter shader.
	 */
	if (error == 0) {
		parser.capacity = parser.body_instructions * 8U + 8U;
		ir->instructions = kern_calloc(parser.capacity, sizeof(*ir->instructions));
		if (ir->instructions == NULL)
			error = ENOMEM;
	}

	/* The second pass lowers the entry function body. */
	if (error == 0)
		error = spirv_pass_body(&parser);
	if (error == 0 && parser.error != 0)
		error = parser.error;
	if (error != 0) {
		if (diag != NULL)
			*diag = parser.diag;
		i915_vk_spirv_free(ir);
		kern_free(parser.ids);
		return error;
	}

	kern_free(parser.ids);
	*out = ir;
	return 0;
}

/* Releases a parsed shader IR and its lists. */
void
i915_vk_spirv_free(
	struct i915_vk_shader_ir *ir)
{
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

/* Records why the module is refused; the instruction is NOT skipped. */
static int
spirv_refuse(
	struct spirv_parser *parser,
	uint32_t opcode,
	uint32_t word_offset,
	const char *reason)
{
	parser->diag.opcode = opcode;
	parser->diag.word_offset = word_offset;
	parser->diag.reason = reason;
	return ENOTSUP;
}

/* The id record, or NULL for an id outside the module's bound. */
static struct spirv_id *
spirv_id(
	struct spirv_parser *parser,
	uint32_t id)
{
	return id < parser->bound ? &parser->ids[id] : NULL;
}

/* Component count of a float scalar / float vector type; 0 for anything else. */
static uint32_t
spirv_float_components(
	struct spirv_parser *parser,
	uint32_t type_id)
{
	struct spirv_id *type = spirv_id(parser, type_id);
	struct spirv_id *element;

	if (type == NULL)
		return 0U;
	if (type->kind == ID_TYPE_FLOAT)
		return type->width == 32U ? 1U : 0U;
	if (type->kind != ID_TYPE_VECTOR || type->count < 2U || type->count > 4U)
		return 0U;
	element = spirv_id(parser, type->type);
	if (element == NULL || element->kind != ID_TYPE_FLOAT || element->width != 32U)
		return 0U;
	return type->count;
}

/* Records types, constants, decorations and interface variables. */
static int
spirv_pass_declarations(
	struct spirv_parser *parser)
{
	const uint32_t *word;
	struct spirv_id *rec;
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	uint32_t index;
	int in_function = 0;

	offset = 5U;
	while (offset < parser->words) {
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		if (opcode == OP_FUNCTION)
			in_function = 1;
		if (in_function != 0) {
			parser->body_instructions++;
			if (opcode == OP_FUNCTION_END)
				in_function = 0;
			offset += count;
			continue;
		}

		switch (opcode) {
		case OP_ENTRY_POINT:
			if (count < 3U)
				return EINVAL;
			if (word[1] == EM_VERTEX)
				parser->ir->stage = I915_VK_STAGE_VERTEX;
			else if (word[1] == EM_FRAGMENT)
				parser->ir->stage = I915_VK_STAGE_FRAGMENT;
			else
				return spirv_refuse(parser, opcode, offset, "execution model other than Vertex / Fragment");
			break;

		/*
		 * Decorations.  Interpreted: Location, Binding, DescriptorSet, BuiltIn, Block,
		 * Offset (members).  Without effect on this lowering: RelaxedPrecision (a
		 * permission to lose precision, never used here).  Anything else could place or
		 * qualify data -- strides, components, interpolation -- and is refused.
		 */
		case OP_DECORATE:
			rec = count >= 3U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			if (word[2] == DEC_LOCATION && count >= 4U) {
				rec->has_location = 1U;
				rec->location = word[3];
			} else if (word[2] == DEC_BINDING && count >= 4U) {
				rec->has_binding = 1U;
				rec->binding = word[3];
			} else if (word[2] == DEC_DESCRIPTOR_SET && count >= 4U) {
				rec->has_set = 1U;
				rec->set = word[3];
			} else if (word[2] == DEC_BUILTIN && count >= 4U) {
				rec->has_builtin = 1U;
				rec->builtin = word[3];
			} else if (word[2] != DEC_BLOCK && word[2] != DEC_RELAXED_PRECISION) {
				return spirv_refuse(parser, opcode, offset, "decoration that is not interpreted");
			}
			break;
		case OP_MEMBER_DECORATE:
			rec = count >= 4U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			if (word[2] >= MAX_MEMBERS)
				return spirv_refuse(parser, opcode, offset, "structure with more members than supported");
			if (word[3] == DEC_OFFSET && count >= 5U) {
				rec->member_offset[word[2]] = word[4];
				rec->member_has_offset[word[2]] = 1U;
			} else if (word[3] == DEC_BUILTIN && count >= 5U) {
				rec->member_builtin[word[2]] = word[4] + 1U;     /* stored + 1: 0 = none */
			} else if (word[3] != DEC_RELAXED_PRECISION) {
				return spirv_refuse(parser, opcode, offset, "member decoration that is not interpreted");
			}
			break;

		/* Types. */
		case OP_TYPE_VOID: case OP_TYPE_BOOL: case OP_TYPE_SAMPLER: case OP_TYPE_FUNCTION:
		case OP_TYPE_IMAGE:
			rec = count >= 2U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = opcode == OP_TYPE_VOID ? ID_TYPE_VOID : opcode == OP_TYPE_BOOL ? ID_TYPE_BOOL :
				opcode == OP_TYPE_SAMPLER ? ID_TYPE_SAMPLER : opcode == OP_TYPE_IMAGE ? ID_TYPE_IMAGE :
				ID_TYPE_FUNCTION;
			break;
		case OP_TYPE_INT: case OP_TYPE_FLOAT:
			rec = count >= 3U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = opcode == OP_TYPE_INT ? ID_TYPE_INT : ID_TYPE_FLOAT;
			rec->width = (uint8_t)word[2];
			break;
		case OP_TYPE_VECTOR:
			rec = count >= 4U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL || word[3] < 2U || word[3] > 4U)
				return EINVAL;
			rec->kind = ID_TYPE_VECTOR;
			rec->type = word[2];
			rec->count = (uint8_t)word[3];
			break;
		case OP_TYPE_SAMPLED_IMAGE:
			rec = count >= 3U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = ID_TYPE_SAMPLED_IMAGE;
			rec->type = word[2];
			break;
		case OP_TYPE_ARRAY:
			rec = count >= 4U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = ID_TYPE_ARRAY;
			rec->type = word[2];
			break;
		case OP_TYPE_STRUCT:
			rec = count >= 2U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			if (count - 2U > MAX_MEMBERS)
				return spirv_refuse(parser, opcode, offset, "structure with more members than supported");
			rec->kind = ID_TYPE_STRUCT;
			rec->count = (uint8_t)(count - 2U);
			for (index = 0U; index < count - 2U; index++)
				rec->member_type[index] = word[2U + index];
			break;
		case OP_TYPE_POINTER:
			rec = count >= 4U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = ID_TYPE_POINTER;
			rec->storage = (uint16_t)word[2];
			rec->type = word[3];
			break;

		/* Scalar constants: 32-bit int (access-chain indices) and float (operands). */
		case OP_CONSTANT:
			rec = count >= 4U ? spirv_id(parser, word[2]) : NULL;
			if (rec == NULL)
				return EINVAL;
			if (count != 4U)
				return spirv_refuse(parser, opcode, offset, "constant wider than 32 bits");
			rec->kind = ID_CONSTANT;
			rec->type = word[1];
			rec->constant = word[3];
			break;

		/* Interface variables. */
		case OP_VARIABLE:
			rec = count >= 4U ? spirv_id(parser, word[2]) : NULL;
			if (rec == NULL || spirv_id(parser, word[1]) == NULL ||
			    parser->ids[word[1]].kind != ID_TYPE_POINTER)
				return EINVAL;
			if (count > 4U)
				return spirv_refuse(parser, opcode, offset, "variable initializer is not lowered");
			rec->kind = ID_VARIABLE;
			rec->storage = (uint16_t)word[3];
			rec->type = word[1];
			rec->var = word[2];
			rec->pointee = parser->ids[word[1]].type;
			rec->member = -1;
			rec->component = -1;
			if (word[3] == SC_INPUT && rec->has_location != 0U) {
				rec->ptr_kind = PTR_INPUT;
				spirv_add_io(parser, word[2], 1);
			} else if (word[3] == SC_OUTPUT && rec->has_location != 0U) {
				rec->ptr_kind = PTR_OUTPUT;
				spirv_add_io(parser, word[2], 0);
			} else if (word[3] == SC_OUTPUT) {
				rec->ptr_kind = PTR_OUTPUT_BLOCK;
			} else if (word[3] == SC_PUSH_CONSTANT) {
				rec->ptr_kind = PTR_PUSH;
			} else if (word[3] == SC_UNIFORM_CONSTANT) {
				rec->ptr_kind = PTR_SAMPLER;
				spirv_add_uniform(parser, word[2]);
			} else {
				return spirv_refuse(parser, opcode, offset, "variable in a storage class that is not lowered");
			}
			break;

		case OP_EXT_INST_IMPORT:
			rec = count >= 2U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL)
				return EINVAL;
			rec->kind = ID_EXT_SET;
			break;

		/* Module-level instructions without execution semantics. */
		case OP_NOP: case OP_SOURCE_CONTINUED: case OP_SOURCE: case OP_SOURCE_EXTENSION: case OP_NAME:
		case OP_MEMBER_NAME: case OP_STRING: case OP_LINE: case OP_NO_LINE: case OP_MODULE_PROCESSED:
		case OP_CAPABILITY: case OP_EXTENSION: case OP_MEMORY_MODEL: case OP_EXECUTION_MODE:
			break;

		default:
			return spirv_refuse(parser, opcode, offset, "module-level instruction that is not interpreted");
		}
		offset += count;
	}
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

	components = spirv_float_components(parser, parser->ids[id].pointee);
	if (is_input != 0) {
		slot = &parser->ir->inputs[parser->ir->input_count];
		parser->ir->input_count++;
	} else {
		slot = &parser->ir->outputs[parser->ir->output_count];
		parser->ir->output_count++;
	}
	slot->location = parser->ids[id].location;
	slot->components = components != 0U ? components : 1U;
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

/* Appends one IR instruction and returns it, or NULL (with the error latched). */
static struct i915_vk_inst *
spirv_emit(
	struct spirv_parser *parser,
	enum i915_vk_ir_op op,
	uint32_t dst,
	uint32_t s0,
	uint32_t s1)
{
	struct i915_vk_inst *inst;

	if (parser->ir->instruction_count >= parser->capacity) {
		parser->error = EINVAL;         /* never a silently shorter shader */
		return NULL;
	}
	inst = &parser->ir->instructions[parser->ir->instruction_count];
	parser->ir->instruction_count++;
	memset(inst, 0, sizeof(*inst));
	inst->op = op;
	inst->dst = dst;
	inst->src[0] = s0;
	inst->src[1] = s1;
	return inst;
}

/* A fresh IR scalar value. */
static uint32_t
spirv_new_value(
	struct spirv_parser *parser)
{
	return parser->ir->value_count++;
}

/*
 * The IR scalars of a float operand: a value id, or a float constant (which becomes a
 * CONST instruction the first time it is used).  Returns the component count, 0 if the
 * id is not a float scalar / vector.
 */
static uint32_t
spirv_operand(
	struct spirv_parser *parser,
	uint32_t id,
	uint32_t comp[4])
{
	struct spirv_id *rec = spirv_id(parser, id);
	struct i915_vk_inst *inst;
	uint32_t index;

	if (rec == NULL)
		return 0U;
	if (rec->kind == ID_CONSTANT) {
		if (spirv_float_components(parser, rec->type) != 1U)
			return 0U;
		if (rec->comp[0] == NO_VALUE || rec->count == 0U) {
			rec->comp[0] = spirv_new_value(parser);
			rec->count = 1U;
			inst = spirv_emit(parser, I915_VK_IR_CONST, rec->comp[0], 0U, 0U);
			if (inst != NULL)
				inst->immediate = rec->constant;
		}
		comp[0] = rec->comp[0];
		return 1U;
	}
	if (rec->kind != ID_VALUE)
		return 0U;
	for (index = 0U; index < rec->count; index++)
		comp[index] = rec->comp[index];
	return rec->count;
}

/* Declares result id `id` as a float value of `count` fresh scalars. */
static struct spirv_id *
spirv_result(
	struct spirv_parser *parser,
	uint32_t id,
	uint32_t type_id,
	uint32_t count,
	int fresh)
{
	struct spirv_id *rec = spirv_id(parser, id);
	uint32_t index;

	if (rec == NULL || rec->kind != ID_NONE)
		return NULL;
	rec->kind = ID_VALUE;
	rec->type = type_id;
	rec->count = (uint8_t)count;
	for (index = 0U; index < 4U; index++)
		rec->comp[index] = index < count && fresh != 0 ? spirv_new_value(parser) : NO_VALUE;
	return rec;
}

/* Lowers the body of the entry function. */
static int
spirv_pass_body(
	struct spirv_parser *parser)
{
	const uint32_t *word;
	struct spirv_id *rec;
	struct spirv_id *base;
	struct spirv_id *var;
	struct spirv_id *type;
	struct i915_vk_inst *inst;
	uint32_t a[4];
	uint32_t b[4];
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	uint32_t n;
	uint32_t m;
	uint32_t index;
	uint32_t first;
	uint32_t last;
	uint32_t functions = 0U;
	int in_function = 0;

	offset = 5U;
	while (offset < parser->words) {
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		if (opcode == OP_FUNCTION) {
			if (in_function != 0)
				return EINVAL;
			if (++functions > 1U)
				return spirv_refuse(parser, opcode, offset, "more than one function (calls are not lowered)");
			in_function = 1;
			offset += count;
			continue;
		}
		if (in_function == 0) {
			offset += count;
			continue;
		}

		switch (opcode) {
		case OP_FUNCTION_END:
			in_function = 0;
			break;

		/* No execution semantics inside a body. */
		case OP_NOP: case OP_LINE: case OP_NO_LINE: case OP_LABEL: case OP_RETURN:
			break;

		/*
		 * A local: not memory, a set of "currently stored" scalars (none yet).  Only
		 * float scalars and float vectors; a struct, array or matrix local is refused.
		 */
		case OP_VARIABLE:
			rec = count >= 4U ? spirv_id(parser, word[2]) : NULL;
			type = count >= 4U ? spirv_id(parser, word[1]) : NULL;
			if (rec == NULL || type == NULL || type->kind != ID_TYPE_POINTER || rec->kind != ID_NONE)
				return EINVAL;
			if (word[3] != SC_FUNCTION || count > 4U)
				return spirv_refuse(parser, opcode, offset, "local variable with an initializer or a non-Function storage class");
			n = spirv_float_components(parser, type->type);
			if (n == 0U)
				return spirv_refuse(parser, opcode, offset, "local variable that is not a float scalar or float vector");
			rec->kind = ID_VARIABLE;
			rec->storage = SC_FUNCTION;
			rec->ptr_kind = PTR_LOCAL;
			rec->type = word[1];
			rec->var = word[2];
			rec->pointee = type->type;
			rec->member = -1;
			rec->component = -1;
			rec->count = (uint8_t)n;
			for (index = 0U; index < 4U; index++)
				rec->comp[index] = NO_VALUE;
			break;

		/*
		 * OpAccessChain with constant indices: a struct member, then / or a vector
		 * component.  Dynamic indices, arrays and deeper nesting are refused.
		 */
		case OP_ACCESS_CHAIN:
			rec = count >= 4U ? spirv_id(parser, word[2]) : NULL;
			base = count >= 4U ? spirv_id(parser, word[3]) : NULL;
			if (rec == NULL || base == NULL || rec->kind != ID_NONE ||
			    (base->kind != ID_VARIABLE && base->kind != ID_POINTER))
				return EINVAL;
			*rec = *base;
			rec->kind = ID_POINTER;
			for (index = 4U; index < count; index++) {
				struct spirv_id *idx = spirv_id(parser, word[index]);
				struct spirv_id *pointee = spirv_id(parser, rec->pointee);

				if (idx == NULL || pointee == NULL)
					return EINVAL;
				if (idx->kind != ID_CONSTANT)
					return spirv_refuse(parser, opcode, offset, "access chain with a dynamic index");
				if (pointee->kind == ID_TYPE_STRUCT && rec->member < 0 && rec->component < 0) {
					if (idx->constant >= pointee->count)
						return EINVAL;
					rec->member = (int32_t)idx->constant;
					rec->pointee = pointee->member_type[idx->constant];
				} else if (pointee->kind == ID_TYPE_VECTOR && rec->component < 0) {
					if (idx->constant >= pointee->count)
						return EINVAL;
					rec->component = (int32_t)idx->constant;
					rec->pointee = pointee->type;
				} else {
					return spirv_refuse(parser, opcode, offset, "access chain into an array, matrix or nested aggregate");
				}
			}
			break;

		case OP_LOAD:
			base = count >= 4U ? spirv_id(parser, word[3]) : NULL;
			if (base == NULL || (base->kind != ID_VARIABLE && base->kind != ID_POINTER))
				return EINVAL;
			var = spirv_id(parser, base->var);
			if (var == NULL)
				return EINVAL;
			if (base->ptr_kind == PTR_SAMPLER) {
				rec = spirv_id(parser, word[2]);
				if (rec == NULL || rec->kind != ID_NONE)
					return EINVAL;
				rec->kind = ID_SAMPLED_IMAGE;
				rec->binding = var->binding;
				rec->set = var->set;
				break;
			}
			n = spirv_float_components(parser, base->pointee);
			if (n == 0U || n != spirv_float_components(parser, word[1]))
				return spirv_refuse(parser, opcode, offset, "load of something that is not a float scalar or float vector");
			first = base->component >= 0 ? (uint32_t)base->component : 0U;
			if (base->ptr_kind == PTR_LOCAL) {
				/* store-to-load forwarding: the scalars currently stored in the local */
				rec = spirv_result(parser, word[2], word[1], n, 0);
				if (rec == NULL)
					return EINVAL;
				for (index = 0U; index < n; index++) {
					if (var->comp[first + index] == NO_VALUE)
						return spirv_refuse(parser, opcode, offset, "load of a local component that was never stored");
					rec->comp[index] = var->comp[first + index];
				}
			} else if (base->ptr_kind == PTR_INPUT) {
				rec = spirv_result(parser, word[2], word[1], n, 1);
				if (rec == NULL)
					return EINVAL;
				for (index = 0U; index < n; index++) {
					inst = spirv_emit(parser, I915_VK_IR_LOAD_INPUT, rec->comp[index], 0U, 0U);
					if (inst != NULL) {
						inst->location = var->location;
						inst->component = first + index;
					}
				}
			} else if (base->ptr_kind == PTR_PUSH) {
				type = spirv_id(parser, var->pointee);
				if (base->member < 0 || type == NULL || type->kind != ID_TYPE_STRUCT ||
				    type->member_has_offset[base->member] == 0U)
					return spirv_refuse(parser, opcode, offset, "push-constant load that does not name a member with an Offset");
				rec = spirv_result(parser, word[2], word[1], n, 1);
				if (rec == NULL)
					return EINVAL;
				for (index = 0U; index < n; index++) {
					inst = spirv_emit(parser, I915_VK_IR_LOAD_PUSH, rec->comp[index], 0U, 0U);
					if (inst != NULL)
						inst->immediate = type->member_offset[base->member] + 4U * (first + index);
					if (type->member_offset[base->member] + 4U * (first + index) + 4U > parser->ir->push_bytes)
						parser->ir->push_bytes = type->member_offset[base->member] + 4U * (first + index) + 4U;
				}
			} else {
				return spirv_refuse(parser, opcode, offset, "load through a pointer that is not an input, push constant, sampler or local");
			}
			break;

		case OP_STORE:
			base = count >= 3U ? spirv_id(parser, word[1]) : NULL;
			if (base == NULL || (base->kind != ID_VARIABLE && base->kind != ID_POINTER))
				return EINVAL;
			var = spirv_id(parser, base->var);
			n = spirv_float_components(parser, base->pointee);
			m = spirv_operand(parser, word[2], a);
			if (var == NULL)
				return EINVAL;
			if (n == 0U || m != n)
				return spirv_refuse(parser, opcode, offset, "store of something that is not a float scalar or float vector of the pointee's size");
			first = base->component >= 0 ? (uint32_t)base->component : 0U;
			if (base->ptr_kind == PTR_LOCAL) {
				for (index = 0U; index < n; index++)
					var->comp[first + index] = a[index];
			} else if (base->ptr_kind == PTR_OUTPUT || base->ptr_kind == PTR_OUTPUT_BLOCK) {
				uint32_t builtin = 0U;
				int is_builtin = 0;

				if (base->ptr_kind == PTR_OUTPUT_BLOCK) {
					type = spirv_id(parser, var->pointee);
					if (var->has_builtin != 0U) {
						builtin = var->builtin;
						is_builtin = 1;
					} else if (base->member >= 0 && type != NULL && type->kind == ID_TYPE_STRUCT &&
					    type->member_builtin[base->member] != 0U) {
						builtin = type->member_builtin[base->member] - 1U;
						is_builtin = 1;
					}
					if (is_builtin == 0 || builtin != BUILTIN_POSITION)
						return spirv_refuse(parser, opcode, offset, "store to an output that is neither located nor the Position builtin");
				}
				for (index = 0U; index < n; index++) {
					inst = spirv_emit(parser, I915_VK_IR_STORE_OUTPUT, 0U, a[index], 0U);
					if (inst != NULL) {
						inst->location = is_builtin != 0 ? I915_VK_IR_LOCATION_POSITION : var->location;
						inst->component = first + index;
					}
				}
			} else {
				return spirv_refuse(parser, opcode, offset, "store through a pointer that is not an output or a local");
			}
			break;

		/* Per-component arithmetic. */
		case OP_FADD: case OP_FSUB: case OP_FMUL:
			if (count < 5U)
				return EINVAL;
			n = spirv_operand(parser, word[3], a);
			m = spirv_operand(parser, word[4], b);
			if (n == 0U || n != m || n != spirv_float_components(parser, word[1]))
				return spirv_refuse(parser, opcode, offset, "arithmetic on operands that are not float scalars / vectors of one size");
			rec = spirv_result(parser, word[2], word[1], n, 1);
			if (rec == NULL)
				return EINVAL;
			for (index = 0U; index < n; index++)
				(void)spirv_emit(parser, opcode == OP_FADD ? I915_VK_IR_FADD : opcode == OP_FSUB ?
					I915_VK_IR_FSUB : I915_VK_IR_FMUL, rec->comp[index], a[index], b[index]);
			break;
		case OP_VECTOR_TIMES_SCALAR:
			if (count < 5U)
				return EINVAL;
			n = spirv_operand(parser, word[3], a);
			m = spirv_operand(parser, word[4], b);
			if (n < 2U || m != 1U || n != spirv_float_components(parser, word[1]))
				return spirv_refuse(parser, opcode, offset, "OpVectorTimesScalar operands");
			rec = spirv_result(parser, word[2], word[1], n, 1);
			if (rec == NULL)
				return EINVAL;
			for (index = 0U; index < n; index++)
				(void)spirv_emit(parser, I915_VK_IR_FMUL, rec->comp[index], a[index], b[0]);
			break;
		case OP_FNEGATE:
			if (count < 4U)
				return EINVAL;
			n = spirv_operand(parser, word[3], a);
			if (n == 0U || n != spirv_float_components(parser, word[1]))
				return spirv_refuse(parser, opcode, offset, "OpFNegate operand");
			rec = spirv_result(parser, word[2], word[1], n, 1);
			if (rec == NULL)
				return EINVAL;
			for (index = 0U; index < n; index++)
				(void)spirv_emit(parser, I915_VK_IR_FNEG, rec->comp[index], a[index], 0U);
			break;

		/* dot(a, b) = a0*b0 + a1*b1 + ...: multiplies, then a chain of adds. */
		case OP_DOT:
			if (count < 5U)
				return EINVAL;
			n = spirv_operand(parser, word[3], a);
			m = spirv_operand(parser, word[4], b);
			if (n < 2U || n != m || spirv_float_components(parser, word[1]) != 1U)
				return spirv_refuse(parser, opcode, offset, "OpDot operands");
			rec = spirv_result(parser, word[2], word[1], 1U, 0);
			if (rec == NULL)
				return EINVAL;
			last = NO_VALUE;
			for (index = 0U; index < n; index++) {
				uint32_t product = spirv_new_value(parser);

				(void)spirv_emit(parser, I915_VK_IR_FMUL, product, a[index], b[index]);
				if (last == NO_VALUE) {
					last = product;
				} else {
					uint32_t sum = spirv_new_value(parser);

					(void)spirv_emit(parser, I915_VK_IR_FADD, sum, last, product);
					last = sum;
				}
			}
			rec->comp[0] = last;
			break;

		/* Pure rearrangement of components: no IR, only naming. */
		case OP_COMPOSITE_CONSTRUCT:
			if (count < 3U)
				return EINVAL;
			n = spirv_float_components(parser, word[1]);
			if (n < 2U)
				return spirv_refuse(parser, opcode, offset, "composite that is not a float vector");
			rec = spirv_result(parser, word[2], word[1], n, 0);
			if (rec == NULL)
				return EINVAL;
			first = 0U;
			for (index = 3U; index < count; index++) {
				m = spirv_operand(parser, word[index], a);
				if (m == 0U || first + m > n)
					return m == 0U ? spirv_refuse(parser, opcode, offset, "composite constituent that is not a float scalar / vector") : EINVAL;
				for (last = 0U; last < m; last++)
					rec->comp[first + last] = a[last];
				first += m;
			}
			if (first != n)
				return EINVAL;          /* the constituents must fill the vector exactly */
			break;
		case OP_COMPOSITE_EXTRACT:
			if (count < 5U)
				return EINVAL;
			if (count != 5U)
				return spirv_refuse(parser, opcode, offset, "extract from a nested aggregate");
			n = spirv_operand(parser, word[3], a);
			if (n < 2U || spirv_float_components(parser, word[1]) != 1U)
				return spirv_refuse(parser, opcode, offset, "extract from something that is not a float vector");
			if (word[4] >= n)
				return EINVAL;
			rec = spirv_result(parser, word[2], word[1], 1U, 0);
			if (rec == NULL)
				return EINVAL;
			rec->comp[0] = a[word[4]];
			break;
		case OP_VECTOR_SHUFFLE:
			if (count < 6U)
				return EINVAL;
			n = spirv_operand(parser, word[3], a);
			m = spirv_operand(parser, word[4], b);
			if (n < 2U || m < 2U || spirv_float_components(parser, word[1]) != count - 5U)
				return spirv_refuse(parser, opcode, offset, "OpVectorShuffle operands");
			rec = spirv_result(parser, word[2], word[1], count - 5U, 0);
			if (rec == NULL)
				return EINVAL;
			for (index = 5U; index < count; index++) {
				if (word[index] == 0xFFFFFFFFU)
					return spirv_refuse(parser, opcode, offset, "OpVectorShuffle with an undefined component");
				if (word[index] >= n + m)
					return EINVAL;
				rec->comp[index - 5U] = word[index] < n ? a[word[index]] : b[word[index] - n];
			}
			break;

		/* GLSL.std.450 transcendentals, per component. */
		case OP_EXT_INST:
			if (count < 6U)
				return EINVAL;
			if (word[4] != GLSL_SIN && word[4] != GLSL_COS && word[4] != GLSL_INVERSE_SQRT)
				return spirv_refuse(parser, opcode, offset, "extended instruction other than Sin / Cos / InverseSqrt");
			n = spirv_operand(parser, word[5], a);
			if (n == 0U || n != spirv_float_components(parser, word[1]))
				return spirv_refuse(parser, opcode, offset, "extended instruction operand");
			rec = spirv_result(parser, word[2], word[1], n, 1);
			if (rec == NULL)
				return EINVAL;
			for (index = 0U; index < n; index++)
				(void)spirv_emit(parser, word[4] == GLSL_SIN ? I915_VK_IR_SIN : word[4] == GLSL_COS ?
					I915_VK_IR_COS : I915_VK_IR_RSQ, rec->comp[index], a[index], 0U);
			break;

		/* texture(sampler2D, vec2): one instruction, four result scalars dst .. dst + 3. */
		case OP_IMAGE_SAMPLE_IMPLICIT_LOD:
			if (count < 5U)
				return EINVAL;
			base = spirv_id(parser, word[3]);
			n = spirv_operand(parser, word[4], a);
			if (base == NULL || base->kind != ID_SAMPLED_IMAGE || n != 2U || count != 5U ||
			    spirv_float_components(parser, word[1]) != 4U)
				return spirv_refuse(parser, opcode, offset, "sample that is not texture(sampler2D, vec2) without operands");
			rec = spirv_result(parser, word[2], word[1], 4U, 1);
			if (rec == NULL)
				return EINVAL;
			if (rec->comp[1] != rec->comp[0] + 1U || rec->comp[3] != rec->comp[0] + 3U)
				return EINVAL;
			inst = spirv_emit(parser, I915_VK_IR_SAMPLE, rec->comp[0], a[0], a[1]);
			if (inst != NULL) {
				inst->immediate = base->binding;
				inst->location = base->set;
			}
			break;

		default:
			return spirv_refuse(parser, opcode, offset, "instruction with execution semantics is not lowered");
		}
		offset += count;
	}
	if (in_function != 0 || functions == 0U)
		return EINVAL;
	return 0;
}
