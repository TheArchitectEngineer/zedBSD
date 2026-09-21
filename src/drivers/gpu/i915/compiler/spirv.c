/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * SPIR-V parser producing a straight-line, SCALAR IR (see ir.h).
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

#include "compiler.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* SPIR-V module header (Khronos SPIR-V spec, section 2.3). */
#define SPIRV_MAGIC 0x07230203U

/* The words of the module header; the first instruction follows them. */
#define SPIRV_HEADER_WORDS 5U

/* The largest id bound the parser accepts. */
#define SPIRV_MAX_BOUND 65536U

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

/* An IR value slot that names no value. */
#define NO_VALUE 0xFFFFFFFFU

/* The most members a structure type may have. */
#define MAX_MEMBERS 8U

/* The OpVectorShuffle component index that means "undefined". */
#define SHUFFLE_UNDEFINED 0xFFFFFFFFU

/* The kind of sampled-image uniform the parser records. */
#define UNIFORM_KIND_SAMPLED_IMAGE 1U

/*
 * The most IR instructions one SPIR-V instruction lowers to, plus one: a
 * four-component dot product is four multiplies and three adds.
 */
#define IR_PER_INSTRUCTION 8U

/* Pointer target kinds. */
#define PTR_NONE 0U
#define PTR_INPUT 1U
#define PTR_OUTPUT 2U
#define PTR_OUTPUT_BLOCK 3U	/* gl_PerVertex: the members carry the builtins */
#define PTR_PUSH 4U
#define PTR_SAMPLER 5U
#define PTR_LOCAL 6U

/*
 * What a SPIR-V id is.
 *
 * Every id starts as ID_NONE; the declaration or instruction that defines it
 * sets the kind once.
 */
enum i915_spirv_id_kind {
	ID_NONE = 0,
	ID_TYPE_VOID,
	ID_TYPE_BOOL,
	ID_TYPE_INT,
	ID_TYPE_FLOAT,
	ID_TYPE_VECTOR,
	ID_TYPE_IMAGE,
	ID_TYPE_SAMPLER,
	ID_TYPE_SAMPLED_IMAGE,
	ID_TYPE_ARRAY,
	ID_TYPE_STRUCT,
	ID_TYPE_POINTER,
	ID_TYPE_FUNCTION,

	/* A scalar int or float constant. */
	ID_CONSTANT,

	/* An OpVariable. */
	ID_VARIABLE,

	/* An OpAccessChain result. */
	ID_POINTER,

	/* A float scalar or vector value: comp[] names the IR scalars. */
	ID_VALUE,

	/* A loaded combined image sampler. */
	ID_SAMPLED_IMAGE,

	ID_EXT_SET
};

/*
 * What the parser knows about one SPIR-V id.
 *
 * The parser holds one per id below the module's bound for the length of one
 * parse; the record of a local variable also carries the scalars currently
 * stored in it.
 */
struct i915_spirv_id {
	/* An enum i915_spirv_id_kind. */
	uint8_t kind;

	/* Int or float bit width. */
	uint8_t width;

	/* Vector component count; struct member count; value component count. */
	uint8_t count;

	uint8_t ptr_kind;
	uint8_t has_location;
	uint8_t has_binding;
	uint8_t has_set;
	uint8_t has_builtin;

	/* Pointer type or variable storage class. */
	uint16_t storage;

	/* Element, pointee or value type id. */
	uint32_t type;

	uint32_t location;
	uint32_t binding;
	uint32_t set;
	uint32_t builtin;

	/* Constant bits. */
	uint32_t constant;

	/* Structure types: member types and offsets. */
	uint32_t member_type[MAX_MEMBERS];
	uint32_t member_offset[MAX_MEMBERS];

	/* The member's builtin plus one; zero when the member is not a builtin. */
	uint32_t member_builtin[MAX_MEMBERS];

	uint8_t member_has_offset[MAX_MEMBERS];

	/* Values; locals keep their CURRENT stored scalars here too. */
	uint32_t comp[4];

	/* Pointers (variables and access chains): the OpVariable the pointer leads to. */
	uint32_t var;

	/* The type id of what the pointer addresses now. */
	uint32_t pointee;

	/* The struct member selected, or -1. */
	int32_t member;

	/* The vector component selected, or -1. */
	int32_t component;
};

/*
 * The decode state threaded through both passes of one parse.
 *
 * It lives on the stack of drv_i915_shader_parse() and owns the id table
 * until the parse ends.
 */
struct i915_spirv_parser {
	const uint32_t *code;
	uint32_t words;
	uint32_t bound;
	struct i915_spirv_id *ids;
	struct i915_shader_ir *ir;

	/* Instruction slots of ir->instructions. */
	uint32_t capacity;

	uint32_t body_instructions;

	/* A failure latched by an emitter that has no return value to carry it. */
	int error;

	struct i915_compile_diagnostic diag;
};

static int i915_spirv_pass_declarations(struct i915_spirv_parser *parser);
static int i915_spirv_declare(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_entry_point(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_decoration(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_member_decoration(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_type(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_constant(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_declare_variable(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static void i915_spirv_add_io(struct i915_spirv_parser *parser, uint32_t id, int is_input);
static void i915_spirv_add_uniform(struct i915_spirv_parser *parser, uint32_t id);
static int i915_spirv_pass_body(struct i915_spirv_parser *parser);
static int i915_spirv_lower(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_variable(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_access_chain(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_load(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_load_push(struct i915_spirv_parser *parser, const uint32_t *word, struct i915_spirv_id *pointer, struct i915_spirv_id *variable, uint32_t count, uint32_t first, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_store(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_store_output(struct i915_spirv_parser *parser, struct i915_spirv_id *pointer, struct i915_spirv_id *variable, const uint32_t *scalars, uint32_t count, uint32_t first, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_arithmetic(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_vector_times_scalar(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_negate(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_dot(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_construct(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_extract(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_shuffle(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_extended(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_lower_sample(struct i915_spirv_parser *parser, const uint32_t *word, uint32_t count, uint32_t opcode, uint32_t offset);
static int i915_spirv_refuse(struct i915_spirv_parser *parser, uint32_t opcode, uint32_t word_offset, const char *reason);
static struct i915_spirv_id *i915_spirv_id(struct i915_spirv_parser *parser, uint32_t id);
static uint32_t i915_spirv_float_components(struct i915_spirv_parser *parser, uint32_t type_id);
static struct i915_shader_ir_inst *i915_spirv_emit(struct i915_spirv_parser *parser, enum i915_shader_ir_op op, uint32_t dst, uint32_t source0, uint32_t source1);
static uint32_t i915_spirv_new_value(struct i915_spirv_parser *parser);
static uint32_t i915_spirv_operand(struct i915_spirv_parser *parser, uint32_t id, uint32_t comp[4]);
static struct i915_spirv_id *i915_spirv_result(struct i915_spirv_parser *parser, uint32_t id, uint32_t type_id, uint32_t count, int fresh);

/*
 * Parses SPIR-V words into the scalar IR of one stage.
 *
 * Returns 0 and the IR in `*out`, EINVAL for a malformed module, ENOTSUP for
 * valid SPIR-V this parser does not lower, or ENOMEM.  On a failure `*out` is
 * NULL and, when `diagnostic` is not NULL, it names the refused instruction
 * (it is cleared on entry and stays clear for a failure that names none).
 */
int
drv_i915_shader_parse(
	const uint32_t *words,
	size_t word_count,
	enum i915_shader_stage stage,
	struct i915_shader_ir **out,
	struct i915_compile_diagnostic *diagnostic)
{
	struct i915_spirv_parser parser;
	struct i915_shader_ir *ir;
	uint32_t slots;
	int error;

	/* The caller receives nothing unless the whole module parses. */
	*out = NULL;
	if (diagnostic != NULL)
		memset(diagnostic, 0, sizeof(*diagnostic));

	/* A module must have a header and a matching magic. */
	if (word_count < SPIRV_HEADER_WORDS)
		return EINVAL;
	if (words[0] != SPIRV_MAGIC)
		return EINVAL;

	/* The id bound sizes the id table, so it must be present and modest. */
	if (words[3] == 0U)
		return EINVAL;
	if (words[3] > SPIRV_MAX_BOUND)
		return EINVAL;

	/* Allocates the IR the parse fills. */
	ir = kern_calloc(1U, sizeof(*ir));
	if (ir == NULL)
		return ENOMEM;
	ir->stage = stage;

	/* Prepares the decode state and its id table. */
	memset(&parser, 0, sizeof(parser));
	parser.code = words;
	parser.words = (uint32_t)word_count;
	parser.bound = words[3];
	parser.ir = ir;
	parser.ids = kern_calloc(parser.bound, sizeof(*parser.ids));
	if (parser.ids == NULL) {
		kern_free(ir);
		return ENOMEM;
	}

	/* Interface lists are bounded by the id count. */
	slots = parser.bound;

	/* Allocates the input list. */
	ir->inputs = kern_calloc(slots, sizeof(*ir->inputs));
	if (ir->inputs == NULL) {
		drv_i915_shader_ir_free(ir);
		kern_free(parser.ids);
		return ENOMEM;
	}

	/* Allocates the output list. */
	ir->outputs = kern_calloc(slots, sizeof(*ir->outputs));
	if (ir->outputs == NULL) {
		drv_i915_shader_ir_free(ir);
		kern_free(parser.ids);
		return ENOMEM;
	}

	/* Allocates the uniform list. */
	ir->uniforms = kern_calloc(slots, sizeof(*ir->uniforms));
	if (ir->uniforms == NULL) {
		drv_i915_shader_ir_free(ir);
		kern_free(parser.ids);
		return ENOMEM;
	}

	/* The first pass records types, constants, decorations and interface variables. */
	error = i915_spirv_pass_declarations(&parser);

	/*
	 * One SPIR-V instruction lowers to at most 7 IR instructions (a
	 * four-component dot product: four multiplies, three adds); the stream is
	 * sized for that, and running out is an error, never a silently shorter
	 * shader.
	 */
	if (error == 0) {
		parser.capacity = parser.body_instructions * IR_PER_INSTRUCTION + IR_PER_INSTRUCTION;
		ir->instructions = kern_calloc(parser.capacity, sizeof(*ir->instructions));
		if (ir->instructions == NULL)
			error = ENOMEM;
	}

	/* The second pass lowers the entry function body. */
	if (error == 0)
		error = i915_spirv_pass_body(&parser);

	/* A failure an emitter latched fails the parse as well. */
	if (error == 0 && parser.error != 0)
		error = parser.error;

	/* A failed parse reports the refused instruction and releases everything. */
	if (error != 0) {
		if (diagnostic != NULL)
			*diagnostic = parser.diag;
		drv_i915_shader_ir_free(ir);
		kern_free(parser.ids);
		return error;
	}

	/* The id table is only needed while parsing. */
	kern_free(parser.ids);

	/* Succeeded: the caller owns the IR. */
	*out = ir;
	return 0;
}

/*
 * Releases a parsed shader IR and its lists.
 */
void
drv_i915_shader_ir_free(
	struct i915_shader_ir *ir)
{
	/* Nothing was parsed. */
	if (ir == NULL)
		return;

	/* Releases each list the parser allocated. */
	if (ir->instructions != NULL)
		kern_free(ir->instructions);
	if (ir->inputs != NULL)
		kern_free(ir->inputs);
	if (ir->outputs != NULL)
		kern_free(ir->outputs);
	if (ir->uniforms != NULL)
		kern_free(ir->uniforms);

	/* Releases the IR itself. */
	kern_free(ir);
}

/* Records types, constants, decorations and interface variables. */
static int
i915_spirv_pass_declarations(
	struct i915_spirv_parser *parser)
{
	const uint32_t *word;
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	int in_function;
	int error;

	/* Walks every instruction after the header. */
	in_function = 0;
	offset = SPIRV_HEADER_WORDS;
	while (offset < parser->words) {
		/* Decodes the word count and the opcode of the instruction. */
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;

		/* An instruction must have a length and end inside the module. */
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		/*
		 * Function bodies are only counted here, to size the IR stream; the
		 * OpFunction and OpFunctionEnd instructions count as well.
		 */
		if (opcode == OP_FUNCTION)
			in_function = 1;
		if (in_function != 0) {
			parser->body_instructions++;
			if (opcode == OP_FUNCTION_END)
				in_function = 0;
			offset += count;
			continue;
		}

		/* Records what the module-level instruction declares. */
		error = i915_spirv_declare(parser, word, count, opcode, offset);
		if (error != 0)
			return error;

		offset += count;
	}

	/* Succeeded: every module-level instruction was interpreted. */
	return 0;
}

/* Records one module-level instruction. */
static int
i915_spirv_declare(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;

	/* Dispatches on what the instruction declares. */
	switch (opcode) {
	case OP_ENTRY_POINT:
		return i915_spirv_declare_entry_point(parser, word, count, opcode, offset);

	case OP_DECORATE:
		return i915_spirv_declare_decoration(parser, word, count, opcode, offset);

	case OP_MEMBER_DECORATE:
		return i915_spirv_declare_member_decoration(parser, word, count, opcode, offset);

	case OP_TYPE_VOID:
	case OP_TYPE_BOOL:
	case OP_TYPE_SAMPLER:
	case OP_TYPE_FUNCTION:
	case OP_TYPE_IMAGE:
	case OP_TYPE_INT:
	case OP_TYPE_FLOAT:
	case OP_TYPE_VECTOR:
	case OP_TYPE_SAMPLED_IMAGE:
	case OP_TYPE_ARRAY:
	case OP_TYPE_STRUCT:
	case OP_TYPE_POINTER:
		return i915_spirv_declare_type(parser, word, count, opcode, offset);

	case OP_CONSTANT:
		return i915_spirv_declare_constant(parser, word, count, opcode, offset);

	case OP_VARIABLE:
		return i915_spirv_declare_variable(parser, word, count, opcode, offset);

	case OP_EXT_INST_IMPORT:
		/* An extended instruction set: only its id is recorded. */
		record = NULL;
		if (count >= 2U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;
		record->kind = ID_EXT_SET;
		return 0;

	case OP_NOP:
	case OP_SOURCE_CONTINUED:
	case OP_SOURCE:
	case OP_SOURCE_EXTENSION:
	case OP_NAME:
	case OP_MEMBER_NAME:
	case OP_STRING:
	case OP_LINE:
	case OP_NO_LINE:
	case OP_MODULE_PROCESSED:
	case OP_CAPABILITY:
	case OP_EXTENSION:
	case OP_MEMORY_MODEL:
	case OP_EXECUTION_MODE:
		/* Module-level instructions without execution semantics. */
		return 0;

	default:
		break;
	}

	/* Anything else could change the shader and is refused. */
	return i915_spirv_refuse(parser, opcode, offset, "module-level instruction that is not interpreted");
}

/* Takes the stage from the entry point's execution model. */
static int
i915_spirv_declare_entry_point(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	/* The instruction must name an execution model and an entry. */
	if (count < 3U)
		return EINVAL;

	/* Only vertex and fragment shaders are lowered. */
	if (word[1] == EM_VERTEX) {
		parser->ir->stage = I915_STAGE_VERTEX;
	} else if (word[1] == EM_FRAGMENT) {
		parser->ir->stage = I915_STAGE_FRAGMENT;
	} else {
		return i915_spirv_refuse(parser, opcode, offset, "execution model other than Vertex / Fragment");
	}

	/* Succeeded: the stage is known. */
	return 0;
}

/*
 * Interprets one decoration.
 *
 * Interpreted: Location, Binding, DescriptorSet, BuiltIn, Block.  Without
 * effect on this lowering: RelaxedPrecision (a permission to lose precision,
 * never used here).  Anything else could place or qualify data -- strides,
 * components, interpolation -- and is refused.
 */
static int
i915_spirv_declare_decoration(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;

	/* Resolves the decorated id. */
	record = NULL;
	if (count >= 3U)
		record = i915_spirv_id(parser, word[1]);
	if (record == NULL)
		return EINVAL;

	/* Records the decoration, or refuses one that is not interpreted. */
	if (word[2] == DEC_LOCATION && count >= 4U) {
		record->has_location = 1U;
		record->location = word[3];
	} else if (word[2] == DEC_BINDING && count >= 4U) {
		record->has_binding = 1U;
		record->binding = word[3];
	} else if (word[2] == DEC_DESCRIPTOR_SET && count >= 4U) {
		record->has_set = 1U;
		record->set = word[3];
	} else if (word[2] == DEC_BUILTIN && count >= 4U) {
		record->has_builtin = 1U;
		record->builtin = word[3];
	} else if (word[2] != DEC_BLOCK && word[2] != DEC_RELAXED_PRECISION) {
		return i915_spirv_refuse(parser, opcode, offset, "decoration that is not interpreted");
	}

	/* Succeeded: the decoration is recorded or has no effect. */
	return 0;
}

/*
 * Interprets one structure member decoration.
 *
 * Interpreted: Offset and BuiltIn; RelaxedPrecision has no effect; anything
 * else is refused.
 */
static int
i915_spirv_declare_member_decoration(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t member;

	/* Resolves the decorated structure type. */
	record = NULL;
	if (count >= 4U)
		record = i915_spirv_id(parser, word[1]);
	if (record == NULL)
		return EINVAL;

	/* Refuses a member beyond the member table. */
	member = word[2];
	if (member >= MAX_MEMBERS)
		return i915_spirv_refuse(parser, opcode, offset, "structure with more members than supported");

	/* Records the decoration, or refuses one that is not interpreted. */
	if (word[3] == DEC_OFFSET && count >= 5U) {
		record->member_offset[member] = word[4];
		record->member_has_offset[member] = 1U;
	} else if (word[3] == DEC_BUILTIN && count >= 5U) {
		/* Stored plus one, so zero keeps meaning "not a builtin". */
		record->member_builtin[member] = word[4] + 1U;
	} else if (word[3] != DEC_RELAXED_PRECISION) {
		return i915_spirv_refuse(parser, opcode, offset, "member decoration that is not interpreted");
	}

	/* Succeeded: the decoration is recorded or has no effect. */
	return 0;
}

/* Records one type declaration. */
static int
i915_spirv_declare_type(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t index;

	/* Records the type by its kind. */
	switch (opcode) {
	case OP_TYPE_VOID:
	case OP_TYPE_BOOL:
	case OP_TYPE_SAMPLER:
	case OP_TYPE_FUNCTION:
	case OP_TYPE_IMAGE:
		/* Types whose operands this lowering never reads. */
		record = NULL;
		if (count >= 2U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;

		if (opcode == OP_TYPE_VOID) {
			record->kind = ID_TYPE_VOID;
		} else if (opcode == OP_TYPE_BOOL) {
			record->kind = ID_TYPE_BOOL;
		} else if (opcode == OP_TYPE_SAMPLER) {
			record->kind = ID_TYPE_SAMPLER;
		} else if (opcode == OP_TYPE_IMAGE) {
			record->kind = ID_TYPE_IMAGE;
		} else {
			record->kind = ID_TYPE_FUNCTION;
		}
		break;

	case OP_TYPE_INT:
	case OP_TYPE_FLOAT:
		/* Scalar types: the width decides which ones are lowered. */
		record = NULL;
		if (count >= 3U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;

		if (opcode == OP_TYPE_INT) {
			record->kind = ID_TYPE_INT;
		} else {
			record->kind = ID_TYPE_FLOAT;
		}
		record->width = (uint8_t)word[2];
		break;

	case OP_TYPE_VECTOR:
		/* A vector of two to four components. */
		record = NULL;
		if (count >= 4U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;
		if (word[3] < 2U || word[3] > 4U)
			return EINVAL;

		record->kind = ID_TYPE_VECTOR;
		record->type = word[2];
		record->count = (uint8_t)word[3];
		break;

	case OP_TYPE_SAMPLED_IMAGE:
		/* A combined image sampler of an image type. */
		record = NULL;
		if (count >= 3U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;

		record->kind = ID_TYPE_SAMPLED_IMAGE;
		record->type = word[2];
		break;

	case OP_TYPE_ARRAY:
		/* An array: only the element type is kept. */
		record = NULL;
		if (count >= 4U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;

		record->kind = ID_TYPE_ARRAY;
		record->type = word[2];
		break;

	case OP_TYPE_STRUCT:
		/* A structure with at most MAX_MEMBERS members. */
		record = NULL;
		if (count >= 2U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;
		if (count - 2U > MAX_MEMBERS)
			return i915_spirv_refuse(parser, opcode, offset, "structure with more members than supported");

		record->kind = ID_TYPE_STRUCT;
		record->count = (uint8_t)(count - 2U);

		/* Records the type of each member. */
		for (index = 0U; index < count - 2U; index++)
			record->member_type[index] = word[2U + index];
		break;

	case OP_TYPE_POINTER:
		/* A pointer: its storage class and pointee type. */
		record = NULL;
		if (count >= 4U)
			record = i915_spirv_id(parser, word[1]);
		if (record == NULL)
			return EINVAL;

		record->kind = ID_TYPE_POINTER;
		record->storage = (uint16_t)word[2];
		record->type = word[3];
		break;

	default:
		/* Only the opcodes above are routed here. */
		return EINVAL;
	}

	/* Succeeded: the type is recorded. */
	return 0;
}

/* Records a scalar constant: 32-bit int (access-chain indices) and float (operands). */
static int
i915_spirv_declare_constant(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;

	/* Resolves the constant's result id. */
	record = NULL;
	if (count >= 4U)
		record = i915_spirv_id(parser, word[2]);
	if (record == NULL)
		return EINVAL;

	/* A constant of more than one word is wider than any value lowered here. */
	if (count != 4U)
		return i915_spirv_refuse(parser, opcode, offset, "constant wider than 32 bits");

	/* Records the constant's type and bits. */
	record->kind = ID_CONSTANT;
	record->type = word[1];
	record->constant = word[3];

	/* Succeeded: the constant becomes IR the first time it is used. */
	return 0;
}

/* Records a module-level (interface) variable. */
static int
i915_spirv_declare_variable(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	struct i915_spirv_id *type;
	uint32_t storage;

	/* Resolves the variable and its pointer type. */
	record = NULL;
	if (count >= 4U)
		record = i915_spirv_id(parser, word[2]);
	if (record == NULL)
		return EINVAL;
	type = i915_spirv_id(parser, word[1]);
	if (type == NULL)
		return EINVAL;
	if (type->kind != ID_TYPE_POINTER)
		return EINVAL;

	/* An initializer would give the variable a value this lowering does not track. */
	if (count > 4U)
		return i915_spirv_refuse(parser, opcode, offset, "variable initializer is not lowered");

	/* Records the variable as a pointer to its pointee, no member or component selected. */
	storage = word[3];
	record->kind = ID_VARIABLE;
	record->storage = (uint16_t)storage;
	record->type = word[1];
	record->var = word[2];
	record->pointee = type->type;
	record->member = -1;
	record->component = -1;

	/* The storage class, and a location, decide what the variable is to the shader. */
	if (storage == SC_INPUT && record->has_location != 0U) {
		record->ptr_kind = PTR_INPUT;
		i915_spirv_add_io(parser, word[2], 1);
	} else if (storage == SC_OUTPUT && record->has_location != 0U) {
		record->ptr_kind = PTR_OUTPUT;
		i915_spirv_add_io(parser, word[2], 0);
	} else if (storage == SC_OUTPUT) {
		record->ptr_kind = PTR_OUTPUT_BLOCK;
	} else if (storage == SC_PUSH_CONSTANT) {
		record->ptr_kind = PTR_PUSH;
	} else if (storage == SC_UNIFORM_CONSTANT) {
		record->ptr_kind = PTR_SAMPLER;
		i915_spirv_add_uniform(parser, word[2]);
	} else {
		return i915_spirv_refuse(parser, opcode, offset, "variable in a storage class that is not lowered");
	}

	/* Succeeded: the variable is part of the interface. */
	return 0;
}

/* Adds an input or output interface slot for a variable id. */
static void
i915_spirv_add_io(
	struct i915_spirv_parser *parser,
	uint32_t id,
	int is_input)
{
	struct i915_shader_ir_io *slot;
	uint32_t components;

	/* Counts the float components of what the variable holds. */
	components = i915_spirv_float_components(parser, parser->ids[id].pointee);

	/* Takes the next slot of the input or the output list. */
	if (is_input != 0) {
		slot = &parser->ir->inputs[parser->ir->input_count];
		parser->ir->input_count++;
	} else {
		slot = &parser->ir->outputs[parser->ir->output_count];
		parser->ir->output_count++;
	}

	/* Records the location; a variable that is not a float vector counts as one component. */
	slot->location = parser->ids[id].location;
	if (components != 0U) {
		slot->components = components;
	} else {
		slot->components = 1U;
	}
	slot->type = 0U;
}

/* Adds a sampled-image uniform slot for a variable id. */
static void
i915_spirv_add_uniform(
	struct i915_spirv_parser *parser,
	uint32_t id)
{
	struct i915_shader_ir_uniform *slot;

	/* Takes the next slot of the uniform list. */
	slot = &parser->ir->uniforms[parser->ir->uniform_count];
	parser->ir->uniform_count++;

	/* Records the descriptor set and binding of the sampled image. */
	slot->set = parser->ids[id].set;
	slot->binding = parser->ids[id].binding;
	slot->kind = UNIFORM_KIND_SAMPLED_IMAGE;
	slot->offset = 0U;
	slot->size = 0U;
}

/* Lowers the body of the entry function. */
static int
i915_spirv_pass_body(
	struct i915_spirv_parser *parser)
{
	const uint32_t *word;
	uint32_t offset;
	uint32_t count;
	uint32_t opcode;
	uint32_t functions;
	int in_function;
	int error;

	/* Walks every instruction after the header, lowering those inside the function. */
	functions = 0U;
	in_function = 0;
	offset = SPIRV_HEADER_WORDS;
	while (offset < parser->words) {
		/* Decodes the word count and the opcode of the instruction. */
		word = parser->code + offset;
		count = word[0] >> 16;
		opcode = word[0] & 0xFFFFU;

		/* An instruction must have a length and end inside the module. */
		if (count == 0U || offset + count > parser->words)
			return EINVAL;

		/* A function opens the body; only one is accepted, since calls are not lowered. */
		if (opcode == OP_FUNCTION) {
			if (in_function != 0)
				return EINVAL;
			functions++;
			if (functions > 1U)
				return i915_spirv_refuse(parser, opcode, offset, "more than one function (calls are not lowered)");
			in_function = 1;
			offset += count;
			continue;
		}

		/* Module-level instructions were handled by the first pass. */
		if (in_function == 0) {
			offset += count;
			continue;
		}

		/* The end of the function closes the body. */
		if (opcode == OP_FUNCTION_END) {
			in_function = 0;
			offset += count;
			continue;
		}

		/* Lowers one instruction of the body. */
		error = i915_spirv_lower(parser, word, count, opcode, offset);
		if (error != 0)
			return error;

		offset += count;
	}

	/* A module must hold one complete function. */
	if (in_function != 0 || functions == 0U)
		return EINVAL;

	/* Succeeded: the whole body is lowered. */
	return 0;
}

/* Lowers one instruction of the function body. */
static int
i915_spirv_lower(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	/* Dispatches on the instruction. */
	switch (opcode) {
	case OP_NOP:
	case OP_LINE:
	case OP_NO_LINE:
	case OP_LABEL:
	case OP_RETURN:
		/* No execution semantics inside a body. */
		return 0;

	case OP_VARIABLE:
		return i915_spirv_lower_variable(parser, word, count, opcode, offset);

	case OP_ACCESS_CHAIN:
		return i915_spirv_lower_access_chain(parser, word, count, opcode, offset);

	case OP_LOAD:
		return i915_spirv_lower_load(parser, word, count, opcode, offset);

	case OP_STORE:
		return i915_spirv_lower_store(parser, word, count, opcode, offset);

	case OP_FADD:
	case OP_FSUB:
	case OP_FMUL:
		return i915_spirv_lower_arithmetic(parser, word, count, opcode, offset);

	case OP_VECTOR_TIMES_SCALAR:
		return i915_spirv_lower_vector_times_scalar(parser, word, count, opcode, offset);

	case OP_FNEGATE:
		return i915_spirv_lower_negate(parser, word, count, opcode, offset);

	case OP_DOT:
		return i915_spirv_lower_dot(parser, word, count, opcode, offset);

	case OP_COMPOSITE_CONSTRUCT:
		return i915_spirv_lower_construct(parser, word, count, opcode, offset);

	case OP_COMPOSITE_EXTRACT:
		return i915_spirv_lower_extract(parser, word, count, opcode, offset);

	case OP_VECTOR_SHUFFLE:
		return i915_spirv_lower_shuffle(parser, word, count, opcode, offset);

	case OP_EXT_INST:
		return i915_spirv_lower_extended(parser, word, count, opcode, offset);

	case OP_IMAGE_SAMPLE_IMPLICIT_LOD:
		return i915_spirv_lower_sample(parser, word, count, opcode, offset);

	default:
		break;
	}

	/* A skipped instruction would be a different shader, so it is refused. */
	return i915_spirv_refuse(parser, opcode, offset, "instruction with execution semantics is not lowered");
}

/*
 * Lowers a local variable: not memory, a set of "currently stored" scalars
 * (none yet).  Only float scalars and float vectors; a struct, array or
 * matrix local is refused.
 */
static int
i915_spirv_lower_variable(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	struct i915_spirv_id *type;
	uint32_t components;
	uint32_t index;

	/* Resolves the variable and its pointer type. */
	record = NULL;
	type = NULL;
	if (count >= 4U) {
		record = i915_spirv_id(parser, word[2]);
		type = i915_spirv_id(parser, word[1]);
	}

	/* The variable must be a fresh id with a pointer type. */
	if (record == NULL || type == NULL)
		return EINVAL;
	if (type->kind != ID_TYPE_POINTER || record->kind != ID_NONE)
		return EINVAL;

	/* A local with an initializer, or in another storage class, is refused. */
	if (word[3] != SC_FUNCTION || count > 4U)
		return i915_spirv_refuse(parser, opcode, offset, "local variable with an initializer or a non-Function storage class");

	/* Only float scalars and vectors can be tracked as scalars. */
	components = i915_spirv_float_components(parser, type->type);
	if (components == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "local variable that is not a float scalar or float vector");

	/* Records the local as a pointer to itself with nothing stored yet. */
	record->kind = ID_VARIABLE;
	record->storage = SC_FUNCTION;
	record->ptr_kind = PTR_LOCAL;
	record->type = word[1];
	record->var = word[2];
	record->pointee = type->type;
	record->member = -1;
	record->component = -1;
	record->count = (uint8_t)components;

	/* No component holds a value until it is stored. */
	for (index = 0U; index < 4U; index++)
		record->comp[index] = NO_VALUE;

	/* Succeeded: the local is ready for stores. */
	return 0;
}

/*
 * Lowers OpAccessChain with constant indices: a struct member, then or
 * instead a vector component.  Dynamic indices, arrays and deeper nesting
 * are refused.
 */
static int
i915_spirv_lower_access_chain(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	struct i915_spirv_id *base;
	struct i915_spirv_id *index_record;
	struct i915_spirv_id *pointee;
	uint32_t index;

	/* Resolves the result and the pointer the chain starts from. */
	record = NULL;
	base = NULL;
	if (count >= 4U) {
		record = i915_spirv_id(parser, word[2]);
		base = i915_spirv_id(parser, word[3]);
	}

	/* The result must be fresh and the base must be a pointer. */
	if (record == NULL || base == NULL || record->kind != ID_NONE)
		return EINVAL;
	if (base->kind != ID_VARIABLE && base->kind != ID_POINTER)
		return EINVAL;

	/* The chain starts as a copy of the base pointer. */
	*record = *base;
	record->kind = ID_POINTER;

	/* Each index selects a member, then a component. */
	for (index = 4U; index < count; index++) {
		/* Resolves the index and what the pointer addresses so far. */
		index_record = i915_spirv_id(parser, word[index]);
		pointee = i915_spirv_id(parser, record->pointee);
		if (index_record == NULL || pointee == NULL)
			return EINVAL;

		/* Only a constant index names one scalar at parse time. */
		if (index_record->kind != ID_CONSTANT)
			return i915_spirv_refuse(parser, opcode, offset, "access chain with a dynamic index");

		/* Selects a structure member first, then a vector component; anything deeper is refused. */
		if (pointee->kind == ID_TYPE_STRUCT && record->member < 0 && record->component < 0) {
			if (index_record->constant >= pointee->count)
				return EINVAL;
			record->member = (int32_t)index_record->constant;
			record->pointee = pointee->member_type[index_record->constant];
		} else if (pointee->kind == ID_TYPE_VECTOR && record->component < 0) {
			if (index_record->constant >= pointee->count)
				return EINVAL;
			record->component = (int32_t)index_record->constant;
			record->pointee = pointee->type;
		} else {
			return i915_spirv_refuse(parser, opcode, offset, "access chain into an array, matrix or nested aggregate");
		}
	}

	/* Succeeded: the pointer names one member or component. */
	return 0;
}

/* Lowers OpLoad from a sampler, a local, an input or a push constant. */
static int
i915_spirv_lower_load(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *base;
	struct i915_spirv_id *variable;
	struct i915_spirv_id *record;
	struct i915_shader_ir_inst *inst;
	uint32_t components;
	uint32_t loaded;
	uint32_t first;
	uint32_t index;
	int error;

	/* Resolves the pointer loaded through. */
	base = NULL;
	if (count >= 4U)
		base = i915_spirv_id(parser, word[3]);
	if (base == NULL)
		return EINVAL;
	if (base->kind != ID_VARIABLE && base->kind != ID_POINTER)
		return EINVAL;

	/* Resolves the variable the pointer leads to. */
	variable = i915_spirv_id(parser, base->var);
	if (variable == NULL)
		return EINVAL;

	/* A sampler load names the combined image sampler; it emits nothing. */
	if (base->ptr_kind == PTR_SAMPLER) {
		record = i915_spirv_id(parser, word[2]);
		if (record == NULL || record->kind != ID_NONE)
			return EINVAL;
		record->kind = ID_SAMPLED_IMAGE;
		record->binding = variable->binding;
		record->set = variable->set;
		return 0;
	}

	/* Anything else must load a float scalar or vector of the pointee's size. */
	components = i915_spirv_float_components(parser, base->pointee);
	if (components == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "load of something that is not a float scalar or float vector");
	loaded = i915_spirv_float_components(parser, word[1]);
	if (components != loaded)
		return i915_spirv_refuse(parser, opcode, offset, "load of something that is not a float scalar or float vector");

	/* A component access chain loads from its component onward. */
	first = 0U;
	if (base->component >= 0)
		first = (uint32_t)base->component;

	/* Loads by what the pointer addresses. */
	if (base->ptr_kind == PTR_LOCAL) {
		/* Store-to-load forwarding: the scalars currently stored in the local. */
		record = i915_spirv_result(parser, word[2], word[1], components, 0);
		if (record == NULL)
			return EINVAL;

		/* Each component must have been stored before it is read. */
		for (index = 0U; index < components; index++) {
			if (variable->comp[first + index] == NO_VALUE)
				return i915_spirv_refuse(parser, opcode, offset, "load of a local component that was never stored");
			record->comp[index] = variable->comp[first + index];
		}
	} else if (base->ptr_kind == PTR_INPUT) {
		/* Each component is a fresh value read from the input location. */
		record = i915_spirv_result(parser, word[2], word[1], components, 1);
		if (record == NULL)
			return EINVAL;

		/* Emits one input load per component. */
		for (index = 0U; index < components; index++) {
			inst = i915_spirv_emit(parser, I915_IR_LOAD_INPUT, record->comp[index], 0U, 0U);
			if (inst != NULL) {
				inst->location = variable->location;
				inst->component = first + index;
			}
		}
	} else if (base->ptr_kind == PTR_PUSH) {
		/* A push constant is read from the member's offset. */
		error = i915_spirv_lower_load_push(parser, word, base, variable, components, first, opcode, offset);
		if (error != 0)
			return error;
	} else {
		return i915_spirv_refuse(parser, opcode, offset, "load through a pointer that is not an input, push constant, sampler or local");
	}

	/* Succeeded: the loaded value names its scalars. */
	return 0;
}

/* Lowers a load of a push-constant member into one push load per component. */
static int
i915_spirv_lower_load_push(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	struct i915_spirv_id *pointer,
	struct i915_spirv_id *variable,
	uint32_t count,
	uint32_t first,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *type;
	struct i915_spirv_id *record;
	struct i915_shader_ir_inst *inst;
	uint32_t byte_offset;
	uint32_t index;

	/* The load must name a member of the push block that carries an Offset. */
	type = i915_spirv_id(parser, variable->pointee);
	if (pointer->member < 0 ||
	    type == NULL ||
	    type->kind != ID_TYPE_STRUCT ||
	    type->member_has_offset[pointer->member] == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "push-constant load that does not name a member with an Offset");

	/* Each component is a fresh value. */
	record = i915_spirv_result(parser, word[2], word[1], count, 1);
	if (record == NULL)
		return EINVAL;

	/* Emits one push load per component and widens the push bytes the shader reads. */
	for (index = 0U; index < count; index++) {
		byte_offset = type->member_offset[pointer->member] + 4U * (first + index);
		inst = i915_spirv_emit(parser, I915_IR_LOAD_PUSH, record->comp[index], 0U, 0U);
		if (inst != NULL)
			inst->immediate = byte_offset;
		if (byte_offset + 4U > parser->ir->push_bytes)
			parser->ir->push_bytes = byte_offset + 4U;
	}

	/* Succeeded: the push constant is loaded. */
	return 0;
}

/* Lowers OpStore to a local or an output. */
static int
i915_spirv_lower_store(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *base;
	struct i915_spirv_id *variable;
	uint32_t scalars[4];
	uint32_t components;
	uint32_t stored;
	uint32_t first;
	uint32_t index;
	int error;

	/* Resolves the pointer stored through. */
	base = NULL;
	if (count >= 3U)
		base = i915_spirv_id(parser, word[1]);
	if (base == NULL)
		return EINVAL;
	if (base->kind != ID_VARIABLE && base->kind != ID_POINTER)
		return EINVAL;

	/*
	 * Resolves the variable, the pointee's size and the stored scalars; the
	 * operand may emit a constant, and it does so before the variable is
	 * checked.
	 */
	variable = i915_spirv_id(parser, base->var);
	components = i915_spirv_float_components(parser, base->pointee);
	stored = i915_spirv_operand(parser, word[2], scalars);
	if (variable == NULL)
		return EINVAL;

	/* The stored value must be a float scalar or vector of the pointee's size. */
	if (components == 0U || stored != components)
		return i915_spirv_refuse(parser, opcode, offset, "store of something that is not a float scalar or float vector of the pointee's size");

	/* A component access chain stores from its component onward. */
	first = 0U;
	if (base->component >= 0)
		first = (uint32_t)base->component;

	/* Stores by what the pointer addresses. */
	if (base->ptr_kind == PTR_LOCAL) {
		/* A local only remembers which scalars it now holds. */
		for (index = 0U; index < components; index++)
			variable->comp[first + index] = scalars[index];
	} else if (base->ptr_kind == PTR_OUTPUT || base->ptr_kind == PTR_OUTPUT_BLOCK) {
		/* An output store becomes one output write per component. */
		error = i915_spirv_lower_store_output(parser, base, variable, scalars, components, first, opcode, offset);
		if (error != 0)
			return error;
	} else {
		return i915_spirv_refuse(parser, opcode, offset, "store through a pointer that is not an output or a local");
	}

	/* Succeeded: the store is lowered. */
	return 0;
}

/* Lowers a store to a located output or to the Position builtin. */
static int
i915_spirv_lower_store_output(
	struct i915_spirv_parser *parser,
	struct i915_spirv_id *pointer,
	struct i915_spirv_id *variable,
	const uint32_t *scalars,
	uint32_t count,
	uint32_t first,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *type;
	struct i915_shader_ir_inst *inst;
	uint32_t builtin;
	uint32_t index;
	int is_builtin;

	/* An output block is written only through its Position builtin. */
	builtin = 0U;
	is_builtin = 0;
	if (pointer->ptr_kind == PTR_OUTPUT_BLOCK) {
		/* The builtin is on the variable, or on the member the chain selected. */
		type = i915_spirv_id(parser, variable->pointee);
		if (variable->has_builtin != 0U) {
			builtin = variable->builtin;
			is_builtin = 1;
		} else if (pointer->member >= 0 &&
		    type != NULL &&
		    type->kind == ID_TYPE_STRUCT &&
		    type->member_builtin[pointer->member] != 0U) {
			builtin = type->member_builtin[pointer->member] - 1U;
			is_builtin = 1;
		}

		/* Any other builtin, or an unlocated plain output, is refused. */
		if (is_builtin == 0 || builtin != BUILTIN_POSITION)
			return i915_spirv_refuse(parser, opcode, offset, "store to an output that is neither located nor the Position builtin");
	}

	/* Emits one output write per component. */
	for (index = 0U; index < count; index++) {
		inst = i915_spirv_emit(parser, I915_IR_STORE_OUTPUT, 0U, scalars[index], 0U);
		if (inst == NULL)
			continue;

		/* The Position builtin is written to its own location. */
		if (is_builtin != 0) {
			inst->location = I915_IR_LOCATION_POSITION;
		} else {
			inst->location = variable->location;
		}
		inst->component = first + index;
	}

	/* Succeeded: the output is written. */
	return 0;
}

/* Lowers per-component OpFAdd, OpFSub and OpFMul. */
static int
i915_spirv_lower_arithmetic(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	enum i915_shader_ir_op op;
	uint32_t left[4];
	uint32_t right[4];
	uint32_t left_count;
	uint32_t right_count;
	uint32_t components;
	uint32_t index;

	/* The instruction must carry both operands. */
	if (count < 5U)
		return EINVAL;

	/* Resolves both operands; either may emit a constant. */
	left_count = i915_spirv_operand(parser, word[3], left);
	right_count = i915_spirv_operand(parser, word[4], right);

	/* Both operands and the result must be float scalars or vectors of one size. */
	if (left_count == 0U || left_count != right_count)
		return i915_spirv_refuse(parser, opcode, offset, "arithmetic on operands that are not float scalars / vectors of one size");
	components = i915_spirv_float_components(parser, word[1]);
	if (left_count != components)
		return i915_spirv_refuse(parser, opcode, offset, "arithmetic on operands that are not float scalars / vectors of one size");

	/* Declares the result as fresh scalars. */
	record = i915_spirv_result(parser, word[2], word[1], left_count, 1);
	if (record == NULL)
		return EINVAL;

	/* Chooses the IR operation of the SPIR-V one. */
	if (opcode == OP_FADD) {
		op = I915_IR_FADD;
	} else if (opcode == OP_FSUB) {
		op = I915_IR_FSUB;
	} else {
		op = I915_IR_FMUL;
	}

	/* Emits one operation per component. */
	for (index = 0U; index < left_count; index++)
		(void)i915_spirv_emit(parser, op, record->comp[index], left[index], right[index]);

	/* Succeeded: the arithmetic is lowered. */
	return 0;
}

/* Lowers OpVectorTimesScalar to one multiply per component. */
static int
i915_spirv_lower_vector_times_scalar(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t vector[4];
	uint32_t scalar[4];
	uint32_t vector_count;
	uint32_t scalar_count;
	uint32_t components;
	uint32_t index;

	/* The instruction must carry both operands. */
	if (count < 5U)
		return EINVAL;

	/* Resolves the vector and the scalar; either may emit a constant. */
	vector_count = i915_spirv_operand(parser, word[3], vector);
	scalar_count = i915_spirv_operand(parser, word[4], scalar);

	/* The operands must be a float vector and a float scalar, the result the vector's size. */
	if (vector_count < 2U || scalar_count != 1U)
		return i915_spirv_refuse(parser, opcode, offset, "OpVectorTimesScalar operands");
	components = i915_spirv_float_components(parser, word[1]);
	if (vector_count != components)
		return i915_spirv_refuse(parser, opcode, offset, "OpVectorTimesScalar operands");

	/* Declares the result as fresh scalars. */
	record = i915_spirv_result(parser, word[2], word[1], vector_count, 1);
	if (record == NULL)
		return EINVAL;

	/* Emits one multiply by the scalar per component. */
	for (index = 0U; index < vector_count; index++)
		(void)i915_spirv_emit(parser, I915_IR_FMUL, record->comp[index], vector[index], scalar[0]);

	/* Succeeded: the product is lowered. */
	return 0;
}

/* Lowers OpFNegate to one negation per component. */
static int
i915_spirv_lower_negate(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t operand[4];
	uint32_t operand_count;
	uint32_t components;
	uint32_t index;

	/* The instruction must carry its operand. */
	if (count < 4U)
		return EINVAL;

	/* Resolves the operand; it may emit a constant. */
	operand_count = i915_spirv_operand(parser, word[3], operand);

	/* The operand and the result must be float scalars or vectors of one size. */
	if (operand_count == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "OpFNegate operand");
	components = i915_spirv_float_components(parser, word[1]);
	if (operand_count != components)
		return i915_spirv_refuse(parser, opcode, offset, "OpFNegate operand");

	/* Declares the result as fresh scalars. */
	record = i915_spirv_result(parser, word[2], word[1], operand_count, 1);
	if (record == NULL)
		return EINVAL;

	/* Emits one negation per component. */
	for (index = 0U; index < operand_count; index++)
		(void)i915_spirv_emit(parser, I915_IR_FNEG, record->comp[index], operand[index], 0U);

	/* Succeeded: the negation is lowered. */
	return 0;
}

/* Lowers dot(a, b) = a0*b0 + a1*b1 + ...: multiplies, then a chain of adds. */
static int
i915_spirv_lower_dot(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t left[4];
	uint32_t right[4];
	uint32_t left_count;
	uint32_t right_count;
	uint32_t components;
	uint32_t product;
	uint32_t sum;
	uint32_t last;
	uint32_t index;

	/* The instruction must carry both operands. */
	if (count < 5U)
		return EINVAL;

	/* Resolves both operands; either may emit a constant. */
	left_count = i915_spirv_operand(parser, word[3], left);
	right_count = i915_spirv_operand(parser, word[4], right);

	/* The operands must be float vectors of one size, the result a float scalar. */
	if (left_count < 2U || left_count != right_count)
		return i915_spirv_refuse(parser, opcode, offset, "OpDot operands");
	components = i915_spirv_float_components(parser, word[1]);
	if (components != 1U)
		return i915_spirv_refuse(parser, opcode, offset, "OpDot operands");

	/* Declares the result; its scalar is the last sum. */
	record = i915_spirv_result(parser, word[2], word[1], 1U, 0);
	if (record == NULL)
		return EINVAL;

	/* Multiplies each component pair and adds each product to the running sum. */
	last = NO_VALUE;
	for (index = 0U; index < left_count; index++) {
		product = i915_spirv_new_value(parser);
		(void)i915_spirv_emit(parser, I915_IR_FMUL, product, left[index], right[index]);

		/* The first product starts the sum; each later one is added to it. */
		if (last == NO_VALUE) {
			last = product;
		} else {
			sum = i915_spirv_new_value(parser);
			(void)i915_spirv_emit(parser, I915_IR_FADD, sum, last, product);
			last = sum;
		}
	}

	/* The result names the final sum. */
	record->comp[0] = last;

	/* Succeeded: the dot product is lowered. */
	return 0;
}

/* Lowers OpCompositeConstruct of a float vector: no IR, only naming. */
static int
i915_spirv_lower_construct(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t constituent[4];
	uint32_t constituent_count;
	uint32_t components;
	uint32_t filled;
	uint32_t component;
	uint32_t index;

	/* The instruction must name its type and result. */
	if (count < 3U)
		return EINVAL;

	/* Only a float vector is constructed. */
	components = i915_spirv_float_components(parser, word[1]);
	if (components < 2U)
		return i915_spirv_refuse(parser, opcode, offset, "composite that is not a float vector");

	/* Declares the result; its scalars are named by the constituents. */
	record = i915_spirv_result(parser, word[2], word[1], components, 0);
	if (record == NULL)
		return EINVAL;

	/* Names the scalars of each constituent in order. */
	filled = 0U;
	for (index = 3U; index < count; index++) {
		constituent_count = i915_spirv_operand(parser, word[index], constituent);
		if (constituent_count == 0U)
			return i915_spirv_refuse(parser, opcode, offset, "composite constituent that is not a float scalar / vector");
		if (filled + constituent_count > components)
			return EINVAL;

		/* Copies the constituent's scalars into place. */
		for (component = 0U; component < constituent_count; component++)
			record->comp[filled + component] = constituent[component];
		filled += constituent_count;
	}

	/* The constituents must fill the vector exactly. */
	if (filled != components)
		return EINVAL;

	/* Succeeded: the vector names its scalars. */
	return 0;
}

/* Lowers OpCompositeExtract of one float from a float vector: no IR, only naming. */
static int
i915_spirv_lower_extract(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t composite[4];
	uint32_t composite_count;
	uint32_t components;

	/* The instruction must carry the composite and one index. */
	if (count < 5U)
		return EINVAL;

	/* A second index would reach into a nested aggregate. */
	if (count != 5U)
		return i915_spirv_refuse(parser, opcode, offset, "extract from a nested aggregate");

	/* Resolves the composite; it may emit a constant. */
	composite_count = i915_spirv_operand(parser, word[3], composite);

	/* The composite must be a float vector and the result a float scalar. */
	if (composite_count < 2U)
		return i915_spirv_refuse(parser, opcode, offset, "extract from something that is not a float vector");
	components = i915_spirv_float_components(parser, word[1]);
	if (components != 1U)
		return i915_spirv_refuse(parser, opcode, offset, "extract from something that is not a float vector");

	/* The index must name a component of the vector. */
	if (word[4] >= composite_count)
		return EINVAL;

	/* Declares the result as the selected scalar. */
	record = i915_spirv_result(parser, word[2], word[1], 1U, 0);
	if (record == NULL)
		return EINVAL;
	record->comp[0] = composite[word[4]];

	/* Succeeded: the scalar is named. */
	return 0;
}

/* Lowers OpVectorShuffle: no IR, only naming. */
static int
i915_spirv_lower_shuffle(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	uint32_t left[4];
	uint32_t right[4];
	uint32_t left_count;
	uint32_t right_count;
	uint32_t components;
	uint32_t selector;
	uint32_t index;

	/* The instruction must carry both vectors and at least one component. */
	if (count < 6U)
		return EINVAL;

	/* Resolves both vectors; either may emit a constant. */
	left_count = i915_spirv_operand(parser, word[3], left);
	right_count = i915_spirv_operand(parser, word[4], right);

	/* Both operands must be float vectors and the result as long as the selector list. */
	if (left_count < 2U || right_count < 2U)
		return i915_spirv_refuse(parser, opcode, offset, "OpVectorShuffle operands");
	components = i915_spirv_float_components(parser, word[1]);
	if (components != count - 5U)
		return i915_spirv_refuse(parser, opcode, offset, "OpVectorShuffle operands");

	/* Declares the result; its scalars are named by the selectors. */
	record = i915_spirv_result(parser, word[2], word[1], count - 5U, 0);
	if (record == NULL)
		return EINVAL;

	/* Names each selected scalar from the first or the second vector. */
	for (index = 5U; index < count; index++) {
		selector = word[index];
		if (selector == SHUFFLE_UNDEFINED)
			return i915_spirv_refuse(parser, opcode, offset, "OpVectorShuffle with an undefined component");
		if (selector >= left_count + right_count)
			return EINVAL;

		/* Selectors past the first vector index into the second. */
		if (selector < left_count) {
			record->comp[index - 5U] = left[selector];
		} else {
			record->comp[index - 5U] = right[selector - left_count];
		}
	}

	/* Succeeded: the shuffled vector names its scalars. */
	return 0;
}

/* Lowers the GLSL.std.450 transcendentals, per component. */
static int
i915_spirv_lower_extended(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *record;
	enum i915_shader_ir_op op;
	uint32_t operand[4];
	uint32_t operand_count;
	uint32_t components;
	uint32_t index;

	/* The instruction must carry the set, the number and one operand. */
	if (count < 6U)
		return EINVAL;

	/* Only sine, cosine and the inverse square root are lowered. */
	if (word[4] == GLSL_SIN) {
		op = I915_IR_SIN;
	} else if (word[4] == GLSL_COS) {
		op = I915_IR_COS;
	} else if (word[4] == GLSL_INVERSE_SQRT) {
		op = I915_IR_RSQ;
	} else {
		return i915_spirv_refuse(parser, opcode, offset, "extended instruction other than Sin / Cos / InverseSqrt");
	}

	/* Resolves the operand; it may emit a constant. */
	operand_count = i915_spirv_operand(parser, word[5], operand);

	/* The operand and the result must be float scalars or vectors of one size. */
	if (operand_count == 0U)
		return i915_spirv_refuse(parser, opcode, offset, "extended instruction operand");
	components = i915_spirv_float_components(parser, word[1]);
	if (operand_count != components)
		return i915_spirv_refuse(parser, opcode, offset, "extended instruction operand");

	/* Declares the result as fresh scalars. */
	record = i915_spirv_result(parser, word[2], word[1], operand_count, 1);
	if (record == NULL)
		return EINVAL;

	/* Emits one function per component. */
	for (index = 0U; index < operand_count; index++)
		(void)i915_spirv_emit(parser, op, record->comp[index], operand[index], 0U);

	/* Succeeded: the function is lowered. */
	return 0;
}

/* Lowers texture(sampler2D, vec2): one instruction, four result scalars dst .. dst + 3. */
static int
i915_spirv_lower_sample(
	struct i915_spirv_parser *parser,
	const uint32_t *word,
	uint32_t count,
	uint32_t opcode,
	uint32_t offset)
{
	struct i915_spirv_id *sampler;
	struct i915_spirv_id *record;
	struct i915_shader_ir_inst *inst;
	uint32_t coordinate[4];
	uint32_t coordinate_count;
	uint32_t components;

	/* The instruction must carry the sampler and the coordinate. */
	if (count < 5U)
		return EINVAL;

	/* Resolves the sampler and the coordinate; the coordinate may emit a constant. */
	sampler = i915_spirv_id(parser, word[3]);
	coordinate_count = i915_spirv_operand(parser, word[4], coordinate);

	/*
	 * Only a loaded combined sampler at a two-float coordinate, with no image
	 * operands, giving a four-float result, is lowered.
	 */
	if (sampler == NULL ||
	    sampler->kind != ID_SAMPLED_IMAGE ||
	    coordinate_count != 2U ||
	    count != 5U)
		return i915_spirv_refuse(parser, opcode, offset, "sample that is not texture(sampler2D, vec2) without operands");
	components = i915_spirv_float_components(parser, word[1]);
	if (components != 4U)
		return i915_spirv_refuse(parser, opcode, offset, "sample that is not texture(sampler2D, vec2) without operands");

	/* Declares the result as four fresh scalars. */
	record = i915_spirv_result(parser, word[2], word[1], 4U, 1);
	if (record == NULL)
		return EINVAL;

	/* The reply is four consecutive values, so the scalars must be consecutive. */
	if (record->comp[1] != record->comp[0] + 1U || record->comp[3] != record->comp[0] + 3U)
		return EINVAL;

	/* Emits the sample: set in `location`, binding in `immediate`. */
	inst = i915_spirv_emit(parser, I915_IR_SAMPLE, record->comp[0], coordinate[0], coordinate[1]);
	if (inst != NULL) {
		inst->immediate = sampler->binding;
		inst->location = sampler->set;
	}

	/* Succeeded: the sample is lowered. */
	return 0;
}

/* Records why the module is refused; the instruction is NOT skipped. */
static int
i915_spirv_refuse(
	struct i915_spirv_parser *parser,
	uint32_t opcode,
	uint32_t word_offset,
	const char *reason)
{
	/* Names the refused instruction for the caller's diagnostic. */
	parser->diag.opcode = opcode;
	parser->diag.word_offset = word_offset;
	parser->diag.reason = reason;

	/* Reports valid SPIR-V this parser does not lower. */
	return ENOTSUP;
}

/* Returns the id record, or NULL for an id outside the module's bound. */
static struct i915_spirv_id *
i915_spirv_id(
	struct i915_spirv_parser *parser,
	uint32_t id)
{
	/* An id at or past the bound does not exist. */
	if (id >= parser->bound)
		return NULL;

	/* Succeeded: the id has a record. */
	return &parser->ids[id];
}

/* Returns the component count of a float scalar or float vector type; 0 for anything else. */
static uint32_t
i915_spirv_float_components(
	struct i915_spirv_parser *parser,
	uint32_t type_id)
{
	struct i915_spirv_id *type;
	struct i915_spirv_id *element;

	/* Resolves the type. */
	type = i915_spirv_id(parser, type_id);
	if (type == NULL)
		return 0U;

	/* A scalar counts only as a 32-bit float. */
	if (type->kind == ID_TYPE_FLOAT) {
		if (type->width == 32U)
			return 1U;
		return 0U;
	}

	/* Anything else must be a vector of two to four components. */
	if (type->kind != ID_TYPE_VECTOR || type->count < 2U || type->count > 4U)
		return 0U;

	/* The vector's element must be a 32-bit float. */
	element = i915_spirv_id(parser, type->type);
	if (element == NULL)
		return 0U;
	if (element->kind != ID_TYPE_FLOAT || element->width != 32U)
		return 0U;

	/* Succeeded: a float vector counts its components. */
	return type->count;
}

/* Appends one IR instruction and returns it, or NULL (with the error latched). */
static struct i915_shader_ir_inst *
i915_spirv_emit(
	struct i915_spirv_parser *parser,
	enum i915_shader_ir_op op,
	uint32_t dst,
	uint32_t source0,
	uint32_t source1)
{
	struct i915_shader_ir_inst *inst;

	/* Running out of slots is never a silently shorter shader. */
	if (parser->ir->instruction_count >= parser->capacity) {
		parser->error = EINVAL;
		return NULL;
	}

	/* Takes the next slot. */
	inst = &parser->ir->instructions[parser->ir->instruction_count];
	parser->ir->instruction_count++;

	/* Fills the instruction; the caller sets the operation-specific fields. */
	memset(inst, 0, sizeof(*inst));
	inst->op = op;
	inst->dst = dst;
	inst->src[0] = source0;
	inst->src[1] = source1;

	/* Succeeded: the instruction is part of the stream. */
	return inst;
}

/* Returns a fresh IR scalar value. */
static uint32_t
i915_spirv_new_value(
	struct i915_spirv_parser *parser)
{
	uint32_t value;

	/* Values are numbered in order of definition. */
	value = parser->ir->value_count;
	parser->ir->value_count++;

	/* Succeeded: the value is defined by the caller's next instruction. */
	return value;
}

/*
 * Returns the IR scalars of a float operand: a value id, or a float constant
 * (which becomes a CONST instruction the first time it is used).  Returns the
 * component count, 0 if the id is not a float scalar or vector.
 */
static uint32_t
i915_spirv_operand(
	struct i915_spirv_parser *parser,
	uint32_t id,
	uint32_t comp[4])
{
	struct i915_spirv_id *record;
	struct i915_shader_ir_inst *inst;
	uint32_t components;
	uint32_t index;

	/* Resolves the operand. */
	record = i915_spirv_id(parser, id);
	if (record == NULL)
		return 0U;

	/* A float constant is one scalar, materialized on its first use. */
	if (record->kind == ID_CONSTANT) {
		/* Only a float scalar constant is an operand. */
		components = i915_spirv_float_components(parser, record->type);
		if (components != 1U)
			return 0U;

		/* The first use emits the constant; later uses share its value. */
		if (record->comp[0] == NO_VALUE || record->count == 0U) {
			record->comp[0] = i915_spirv_new_value(parser);
			record->count = 1U;
			inst = i915_spirv_emit(parser, I915_IR_CONST, record->comp[0], 0U, 0U);
			if (inst != NULL)
				inst->immediate = record->constant;
		}

		comp[0] = record->comp[0];
		return 1U;
	}

	/* Anything else must be a float value. */
	if (record->kind != ID_VALUE)
		return 0U;

	/* Copies the value's scalars. */
	for (index = 0U; index < record->count; index++)
		comp[index] = record->comp[index];

	/* Succeeded: the operand has this many components. */
	return record->count;
}

/*
 * Declares result id `id` as a float value of `count` scalars: fresh values
 * when `fresh` is nonzero, otherwise left for the caller to name.
 */
static struct i915_spirv_id *
i915_spirv_result(
	struct i915_spirv_parser *parser,
	uint32_t id,
	uint32_t type_id,
	uint32_t count,
	int fresh)
{
	struct i915_spirv_id *record;
	uint32_t index;

	/* SSA: a result id is defined once. */
	record = i915_spirv_id(parser, id);
	if (record == NULL || record->kind != ID_NONE)
		return NULL;

	/* Records the value's type and size. */
	record->kind = ID_VALUE;
	record->type = type_id;
	record->count = (uint8_t)count;

	/* Numbers each component in order, or leaves it unnamed. */
	for (index = 0U; index < 4U; index++) {
		if (index < count && fresh != 0) {
			record->comp[index] = i915_spirv_new_value(parser);
		} else {
			record->comp[index] = NO_VALUE;
		}
	}

	/* Succeeded: the result is declared. */
	return record;
}
