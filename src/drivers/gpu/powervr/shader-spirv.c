/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR shader compiler's SPIR-V reader: from a module's entry
 * point to the scalar intermediate form.
 *
 * The first pass reads the module's declarations: decorations, types,
 * constants, variables and where each function and block starts.  The
 * second pass walks the entry point's structured control flow from its
 * first block, inlining calls, splitting vectors into words, turning
 * phis into moves on the incoming edges and local variables into plain
 * values, and writes the instructions in the order the shader nests
 * them.  Anything outside the supported subset is refused with ENOTSUP
 * rather than guessed at.
 */

#include <stddef.h>
#include <stdint.h>

#include <uapi/errno.h>

#include "drivers/gpu/powervr/shader-compile.h"
#include "drivers/gpu/powervr/shader-ir.h"
#include "drivers/gpu/powervr/shader-private.h"

/*
 * The module's header and the opcodes, decorations, built-ins, storage
 * classes and execution models the reader knows.
 *
 * Values referenced:
 * The Khronos SPIR-V 1.6 specification, sections 2.3 (physical layout)
 * and 3 (binary form: opcodes, Decoration, BuiltIn, StorageClass,
 * ExecutionModel, ExecutionMode); the GLSL.std.450 extended
 * instruction set's numbers.
 */
#define SPIRV_MAGIC			0x07230203U
#define SPIRV_HEADER_WORDS		5U

#define OP_UNDEF			1U
#define OP_EXT_INST_IMPORT		11U
#define OP_EXT_INST			12U
#define OP_ENTRY_POINT			15U
#define OP_EXECUTION_MODE		16U
#define OP_TYPE_VOID			19U
#define OP_TYPE_BOOL			20U
#define OP_TYPE_INT			21U
#define OP_TYPE_FLOAT			22U
#define OP_TYPE_VECTOR			23U
#define OP_TYPE_MATRIX			24U
#define OP_TYPE_IMAGE			25U
#define OP_TYPE_SAMPLER			26U
#define OP_TYPE_SAMPLED_IMAGE		27U
#define OP_TYPE_ARRAY			28U
#define OP_TYPE_RUNTIME_ARRAY		29U
#define OP_TYPE_STRUCT			30U
#define OP_TYPE_POINTER			32U
#define OP_TYPE_FUNCTION		33U
#define OP_CONSTANT_TRUE		41U
#define OP_CONSTANT_FALSE		42U
#define OP_CONSTANT			43U
#define OP_CONSTANT_COMPOSITE		44U
#define OP_CONSTANT_NULL		46U
#define OP_SPEC_CONSTANT_TRUE		48U
#define OP_SPEC_CONSTANT_FALSE		49U
#define OP_SPEC_CONSTANT		50U
#define OP_SPEC_CONSTANT_COMPOSITE	51U
#define OP_FUNCTION			54U
#define OP_FUNCTION_PARAMETER		55U
#define OP_FUNCTION_END			56U
#define OP_FUNCTION_CALL		57U
#define OP_VARIABLE			59U
#define OP_LOAD				61U
#define OP_STORE			62U
#define OP_ACCESS_CHAIN			65U
#define OP_IN_BOUNDS_ACCESS_CHAIN	66U
#define OP_DECORATE			71U
#define OP_MEMBER_DECORATE		72U
#define OP_VECTOR_SHUFFLE		79U
#define OP_COMPOSITE_CONSTRUCT		80U
#define OP_COMPOSITE_EXTRACT		81U
#define OP_COMPOSITE_INSERT		82U
#define OP_COPY_OBJECT			83U
#define OP_SAMPLED_IMAGE		86U
#define OP_IMAGE_SAMPLE_IMPLICIT_LOD	87U
#define OP_CONVERT_F_TO_U		109U
#define OP_CONVERT_F_TO_S		110U
#define OP_CONVERT_S_TO_F		111U
#define OP_CONVERT_U_TO_F		112U
#define OP_BITCAST			124U
#define OP_S_NEGATE			126U
#define OP_F_NEGATE			127U
#define OP_I_ADD			128U
#define OP_F_ADD			129U
#define OP_I_SUB			130U
#define OP_F_SUB			131U
#define OP_I_MUL			132U
#define OP_F_MUL			133U
#define OP_F_DIV			136U
#define OP_VECTOR_TIMES_SCALAR		142U
#define OP_MATRIX_TIMES_SCALAR		143U
#define OP_MATRIX_TIMES_VECTOR		145U
#define OP_DOT				148U
#define OP_LOGICAL_EQUAL		164U
#define OP_LOGICAL_NOT_EQUAL		165U
#define OP_LOGICAL_OR			166U
#define OP_LOGICAL_AND			167U
#define OP_LOGICAL_NOT			168U
#define OP_SELECT			169U
#define OP_I_EQUAL			170U
#define OP_I_NOT_EQUAL			171U
#define OP_U_GREATER_THAN		172U
#define OP_S_GREATER_THAN		173U
#define OP_U_GREATER_THAN_EQUAL		174U
#define OP_S_GREATER_THAN_EQUAL		175U
#define OP_U_LESS_THAN			176U
#define OP_S_LESS_THAN			177U
#define OP_U_LESS_THAN_EQUAL		178U
#define OP_S_LESS_THAN_EQUAL		179U
#define OP_F_ORD_EQUAL			180U
#define OP_F_UNORD_GREATER_THAN_EQUAL	191U
#define OP_SHIFT_RIGHT_LOGICAL		194U
#define OP_SHIFT_RIGHT_ARITHMETIC	195U
#define OP_SHIFT_LEFT_LOGICAL		196U
#define OP_BITWISE_OR			197U
#define OP_BITWISE_XOR			198U
#define OP_BITWISE_AND			199U
#define OP_NOT				200U
#define OP_PHI				245U
#define OP_LOOP_MERGE			246U
#define OP_SELECTION_MERGE		247U
#define OP_LABEL			248U
#define OP_BRANCH			249U
#define OP_BRANCH_CONDITIONAL		250U
#define OP_RETURN			253U
#define OP_RETURN_VALUE			254U

#define DECORATION_ARRAY_STRIDE		6U
#define DECORATION_MATRIX_STRIDE	7U
#define DECORATION_BUILT_IN		11U
#define DECORATION_NO_PERSPECTIVE	13U
#define DECORATION_FLAT			14U
#define DECORATION_LOCATION		30U
#define DECORATION_COMPONENT		31U
#define DECORATION_BINDING		33U
#define DECORATION_DESCRIPTOR_SET	34U
#define DECORATION_OFFSET		35U

#define BUILT_IN_POSITION		0U
#define BUILT_IN_NUM_WORKGROUPS		24U
#define BUILT_IN_WORKGROUP_ID		26U
#define BUILT_IN_LOCAL_INVOCATION_ID	27U
#define BUILT_IN_GLOBAL_INVOCATION_ID	28U
#define BUILT_IN_LOCAL_INVOCATION_INDEX	29U

#define STORAGE_UNIFORM_CONSTANT	0U
#define STORAGE_INPUT			1U
#define STORAGE_UNIFORM			2U
#define STORAGE_OUTPUT			3U
#define STORAGE_PRIVATE			6U
#define STORAGE_FUNCTION		7U
#define STORAGE_PUSH_CONSTANT		9U
#define STORAGE_STORAGE_BUFFER		12U

#define MODEL_VERTEX			0U
#define MODEL_FRAGMENT			4U
#define MODEL_GL_COMPUTE		5U
#define MODE_LOCAL_SIZE			17U

#define GLSL_ROUND			1U
#define GLSL_F_ABS			4U
#define GLSL_F_SIGN			6U
#define GLSL_FLOOR			8U
#define GLSL_CEIL			9U
#define GLSL_FRACT			10U
#define GLSL_SIN			13U
#define GLSL_COS			14U
#define GLSL_POW			26U
#define GLSL_EXP2			29U
#define GLSL_LOG2			30U
#define GLSL_SQRT			31U
#define GLSL_INVERSE_SQRT		32U
#define GLSL_F_MIN			37U
#define GLSL_U_MIN			38U
#define GLSL_S_MIN			39U
#define GLSL_F_MAX			40U
#define GLSL_U_MAX			41U
#define GLSL_S_MAX			42U
#define GLSL_F_CLAMP			43U
#define GLSL_U_CLAMP			44U
#define GLSL_S_CLAMP			45U
#define GLSL_F_MIX			46U
#define GLSL_STEP			48U
#define GLSL_SMOOTH_STEP		49U
#define GLSL_FMA			50U
#define GLSL_LENGTH			66U
#define GLSL_DISTANCE			67U
#define GLSL_NORMALIZE			69U

/* What an id stands for. */
#define ID_NONE				0U
#define ID_TYPE				1U
#define ID_CONSTANT			2U
#define ID_VALUE			3U
#define ID_POINTER			4U
#define ID_FUNCTION			5U
#define ID_LABEL			6U
#define ID_EXTENSION			7U
#define ID_SAMPLED_IMAGE		8U

/* The shapes of a type. */
#define TYPE_VOID			0U
#define TYPE_BOOL			1U
#define TYPE_INT			2U
#define TYPE_FLOAT			3U
#define TYPE_VECTOR			4U
#define TYPE_MATRIX			5U
#define TYPE_ARRAY			6U
#define TYPE_RUNTIME_ARRAY		7U
#define TYPE_STRUCT			8U
#define TYPE_POINTER			9U
#define TYPE_FUNCTION			10U
#define TYPE_IMAGE			11U
#define TYPE_SAMPLED_IMAGE		12U
#define TYPE_SAMPLER			13U

/* The id flags the decorations and declarations set. */
#define FLAG_LOCATION			0x01U
#define FLAG_FLAT			0x02U
#define FLAG_BUILT_IN			0x04U
#define FLAG_STRIDE			0x08U
#define FLAG_ENTERED			0x10U
#define FLAG_INTERFACE			0x20U
#define FLAG_HAS_INDEX			0x40U
#define FLAG_LOOP			0x80U

/* The largest composite one result may be, in words. */
#define COMPOSITE_MAX			16U

/* The float words the reader writes as constants. */
#define FLOAT_ZERO			0x00000000U
#define FLOAT_ONE			0x3f800000U
#define FLOAT_HALF			0x3f000000U
#define FLOAT_TWO			0x40000000U
#define FLOAT_THREE			0x40400000U
#define FLOAT_SIGN			0x80000000U
#define TRUE_WORD			0xffffffffU

/*
 * The sine polynomial: the odd powers' coefficients of sin(y) for y in
 * [-pi, pi], highest first, and the turn the argument is reduced by.
 * The Taylor terms to y^15 err by less than 1e-6 over the range.
 */
static const uint32_t sine_coefficients[8] = {
	0xab573f9fU,	/* -1/15! */
	0x2f309231U,	/* 1/13! */
	0xb2d7322bU,	/* -1/11! */
	0x3638ef1dU,	/* 1/9! */
	0xb9500d01U,	/* -1/7! */
	0x3c088889U,	/* 1/5! */
	0xbe2aaaabU,	/* -1/3! */
	0x3f800000U	/* 1 */
};

static int reader_declarations(struct shader_spirv_reader *reader);
static int reader_declaration(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length, uint32_t at);
static int reader_type(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static int reader_constant(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static int reader_global_variable(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static void reader_decorate(struct shader_spirv_reader *reader, uint32_t target, uint32_t decoration, uint32_t value);
static int reader_layout_interface(struct shader_spirv_reader *reader);
static int reader_entry_prologue(struct shader_spirv_reader *reader);
static int reader_entry_epilogue(struct shader_spirv_reader *reader);
static int reader_function(struct shader_spirv_reader *reader, uint32_t function, uint32_t *result);
static int reader_region(struct shader_spirv_reader *reader, uint32_t start, uint32_t stop, uint32_t through_first, uint32_t *returned);
static int reader_loop(struct shader_spirv_reader *reader, uint32_t header);
static int reader_arm(struct shader_spirv_reader *reader, uint32_t from, uint32_t target, uint32_t merge);
static int reader_edge(struct shader_spirv_reader *reader, uint32_t from, uint32_t target);
static int reader_exit(struct shader_spirv_reader *reader, uint32_t target, uint32_t *exited);
static int reader_phi_moves(struct shader_spirv_reader *reader, uint32_t from, uint32_t target);
static int reader_block_body(struct shader_spirv_reader *reader, uint32_t label, uint32_t *terminator);
static int reader_instruction(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static int reader_access_chain(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static int reader_load(struct shader_spirv_reader *reader, uint32_t type, uint32_t result, uint32_t pointer);
static int reader_store(struct shader_spirv_reader *reader, uint32_t pointer, uint32_t object);
static int reader_memory_address(struct shader_spirv_reader *reader, const struct shader_spirv_id *pointer, struct powervr_ir_operand address[3]);
static int reader_arithmetic(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length, uint32_t *handled);
static int reader_composite(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length, uint32_t *handled);
static int reader_extended(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static int reader_sample(struct shader_spirv_reader *reader, const uint32_t *words, uint32_t length);
static uint32_t scalar_count(struct shader_spirv_reader *reader, uint32_t type);
static uint32_t byte_size(struct shader_spirv_reader *reader, uint32_t type);
static int find_block(struct shader_spirv_reader *reader, uint32_t label, uint32_t *at);
static uint32_t new_value(struct shader_spirv_reader *reader, uint32_t width);
static void emit(struct shader_spirv_reader *reader, uint32_t opcode, uint32_t destination, const struct powervr_ir_operand *a, const struct powervr_ir_operand *b, const struct powervr_ir_operand *c, uint32_t immediate, uint32_t immediate_2);
static struct powervr_ir_operand emit_value(struct shader_spirv_reader *reader, uint32_t opcode, const struct powervr_ir_operand *a, const struct powervr_ir_operand *b, const struct powervr_ir_operand *c);
static struct powervr_ir_operand constant_operand(uint32_t bits);
static struct powervr_ir_operand value_operand(uint32_t value, uint32_t component);
static struct powervr_ir_operand plain(struct shader_spirv_reader *reader, struct powervr_ir_operand operand);
static struct powervr_ir_operand float_negate(struct powervr_ir_operand operand);
static struct powervr_ir_operand float_absolute(struct powervr_ir_operand operand);
static struct powervr_ir_operand shared_word(struct shader_spirv_reader *reader, uint32_t request, uint32_t word);
static int request_add(struct shader_spirv_reader *reader, uint32_t kind, uint32_t part, uint32_t set, uint32_t binding, uint32_t words, uint32_t *index);
static int result_begin(struct shader_spirv_reader *reader, uint32_t result, uint32_t type, uint32_t count, struct powervr_ir_operand **operands);
static int id_operands(struct shader_spirv_reader *reader, uint32_t id, struct powervr_ir_operand **operands, uint32_t *count);
static int operand_word(struct shader_spirv_reader *reader, uint32_t id, uint32_t component, struct powervr_ir_operand *operand);
static int composite_offset(struct shader_spirv_reader *reader, uint32_t type, const uint32_t *indices, uint32_t count, uint32_t *offset, uint32_t *member_type);
static struct powervr_ir_operand sine(struct shader_spirv_reader *reader, struct powervr_ir_operand x, uint32_t quarter);

/*
 * Reports how many bytes of working memory the compiler needs.
 */
size_t
powervr_shader_workspace_size(void)
{
	size_t size;

	/* The workspace holds every table of the reader and the back end. */
	size = sizeof(struct powervr_shader_workspace);

	/* Succeeded: the size. */
	return size;
}

/*
 * Reads a SPIR-V module's entry point of the given stage into the
 * intermediate form and fills in the interface part of the program.
 *
 * The program's tables (requests, inputs, varyings, workgroup) are
 * filled; the code and register counts are the back end's.  On success
 * the shader is the workspace's.
 */
int
powervr_shader_read(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	struct powervr_shader_workspace *workspace,
	struct powervr_shader_program *program,
	struct powervr_ir_shader **shader)
{
	struct shader_spirv_reader *reader;
	uint32_t result;
	uint8_t *bytes;
	size_t index;
	int error;

	/* Refuses missing arguments and a stage the compiler does not take. */
	if (words == NULL || workspace == NULL || program == NULL || shader == NULL)
		return EINVAL;
	if (stage > POWERVR_SHADER_FRAGMENT)
		return EINVAL;

	/* Refuses a module too short for its header or with the wrong magic number. */
	if (count < SPIRV_HEADER_WORDS || words[0] != SPIRV_MAGIC)
		return EINVAL;
	if (words[3] > SHADER_SPIRV_IDS)
		return ENOTSUP;

	/* Clears the reader, the shader and the program. */
	bytes = (uint8_t *)&workspace->reader;
	for (index = 0; index < sizeof(workspace->reader); index++)
		bytes[index] = 0;
	bytes = (uint8_t *)program;
	for (index = 0; index < sizeof(*program); index++)
		bytes[index] = 0;
	workspace->shader.instruction_count = 0;
	workspace->shader.value_count = 1;

	/* Ties the reader to the module, the shader and the program. */
	reader = &workspace->reader;
	reader->words = words;
	reader->count = count;
	reader->stage = stage;
	reader->bound = words[3];
	reader->shader = &workspace->shader;
	reader->program = program;
	reader->push_request = POWERVR_SHADER_REQUESTS;
	program->stage = stage;
	program->workgroup[0] = 1;
	program->workgroup[1] = 1;
	program->workgroup[2] = 1;

	/* The first pass: declarations, types, constants, variables, blocks. */
	error = reader_declarations(reader);
	if (error != 0)
		return error;

	/* Refuses a module without an entry point of the stage. */
	if (reader->entry == 0)
		return ENOTSUP;

	/* Places the interface: vertex inputs, varyings and requests known up front. */
	error = reader_layout_interface(reader);
	if (error != 0)
		return error;

	/* Reads the inputs into values before the entry point's first block. */
	error = reader_entry_prologue(reader);
	if (error != 0)
		return error;

	/* The second pass: the entry point's blocks, calls inlined. */
	error = reader_function(reader, reader->entry, &result);
	if (error != 0)
		return error;

	/* Hands the outputs over after the last block. */
	error = reader_entry_epilogue(reader);
	if (error != 0)
		return error;

	/* Sizes the push constant request to the words the shader read. */
	if (reader->push_request < POWERVR_SHADER_REQUESTS)
		program->requests[reader->push_request].words = (uint8_t)reader->push_words;

	/* Packs the requests into shared registers in table order. */
	program->shareds = 0;
	for (index = 0; index < program->request_count; index++) {
		program->requests[index].shared = (uint16_t)program->shareds;
		program->shareds += program->requests[index].words;
	}

	/* Succeeded: the shader. */
	*shader = reader->shader;
	return 0;
}

/*
 * Reads every declaration of the module and notes where each function
 * and block starts.
 */
static int
reader_declarations(
	struct shader_spirv_reader *reader)
{
	uint32_t at;
	uint32_t length;
	uint32_t opcode;
	int error;

	/* Walks the instructions after the header. */
	at = SPIRV_HEADER_WORDS;
	while (at < reader->count) {
		/* Each instruction's first word holds its length and opcode. */
		length = reader->words[at] >> 16;
		opcode = reader->words[at] & 0xffffU;
		if (length == 0 || at + length > reader->count)
			return EINVAL;

		/* Reads the declaration, or notes the function or block. */
		error = reader_declaration(reader, &reader->words[at], length, at);
		if (error != 0)
			return error;

		(void)opcode;
		at += length;
	}

	/* Succeeded: the declarations are read. */
	return 0;
}

/* Reads one instruction of the first pass. */
static int
reader_declaration(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length,
	uint32_t at)
{
	struct shader_spirv_member_decoration *decoration;
	struct shader_spirv_id *id;
	uint32_t opcode;
	uint32_t model;
	int error;

	/* Dispatches on the opcode. */
	opcode = words[0] & 0xffffU;
	error = 0;
	switch (opcode) {
	case OP_EXT_INST_IMPORT:
		/* Only GLSL.std.450 is known; its name starts "GLSL". */
		if (length < 3 || words[1] >= reader->bound)
			return EINVAL;
		reader->ids[words[1]].kind = ID_EXTENSION;
		if (words[2] == 0x4c534c47U)
			reader->glsl = words[1];
		break;
	case OP_ENTRY_POINT:
		/* Keeps the entry point of the stage asked for. */
		if (length < 4)
			return EINVAL;
		model = words[1];
		if ((reader->stage == POWERVR_SHADER_COMPUTE && model == MODEL_GL_COMPUTE) ||
		    (reader->stage == POWERVR_SHADER_VERTEX && model == MODEL_VERTEX) ||
		    (reader->stage == POWERVR_SHADER_FRAGMENT && model == MODEL_FRAGMENT)) {
			if (reader->entry == 0)
				reader->entry = words[2];
		}
		break;
	case OP_EXECUTION_MODE:
		/* The workgroup's size. */
		if (length >= 6 && words[1] == reader->entry && words[2] == MODE_LOCAL_SIZE) {
			reader->program->workgroup[0] = words[3];
			reader->program->workgroup[1] = words[4];
			reader->program->workgroup[2] = words[5];
		}
		break;
	case OP_DECORATE:
		/* A decoration of an id, with its one value where it has one. */
		if (length < 3 || words[1] >= reader->bound)
			return EINVAL;
		reader_decorate(reader, words[1], words[2], length > 3 ? words[3] : 0);
		break;
	case OP_MEMBER_DECORATE:
		/* Kept until the struct is read. */
		if (length < 4)
			return EINVAL;
		if (reader->member_decoration_count >= SHADER_SPIRV_MEMBER_DECORATIONS)
			return ENOTSUP;
		decoration = &reader->member_decorations[reader->member_decoration_count++];
		decoration->structure = words[1];
		decoration->member = words[2];
		decoration->decoration = words[3];
		decoration->value = length > 4 ? words[4] : 0;
		break;
	case OP_TYPE_VOID:
	case OP_TYPE_BOOL:
	case OP_TYPE_INT:
	case OP_TYPE_FLOAT:
	case OP_TYPE_VECTOR:
	case OP_TYPE_MATRIX:
	case OP_TYPE_IMAGE:
	case OP_TYPE_SAMPLER:
	case OP_TYPE_SAMPLED_IMAGE:
	case OP_TYPE_ARRAY:
	case OP_TYPE_RUNTIME_ARRAY:
	case OP_TYPE_STRUCT:
	case OP_TYPE_POINTER:
	case OP_TYPE_FUNCTION:
		error = reader_type(reader, words, length);
		break;
	case OP_CONSTANT_TRUE:
	case OP_CONSTANT_FALSE:
	case OP_CONSTANT:
	case OP_CONSTANT_COMPOSITE:
	case OP_CONSTANT_NULL:
	case OP_SPEC_CONSTANT_TRUE:
	case OP_SPEC_CONSTANT_FALSE:
	case OP_SPEC_CONSTANT:
	case OP_SPEC_CONSTANT_COMPOSITE:
	case OP_UNDEF:
		/* A function-local undefined value is read in the second pass too, harmlessly. */
		error = reader_constant(reader, words, length);
		break;
	case OP_VARIABLE:
		/* Global variables now; function variables when their block is walked. */
		if (length < 4)
			return EINVAL;
		if (words[3] != STORAGE_FUNCTION)
			error = reader_global_variable(reader, words, length);
		break;
	case OP_FUNCTION:
		/* Where the function starts. */
		if (length < 5 || words[2] >= reader->bound)
			return EINVAL;
		id = &reader->ids[words[2]];
		id->kind = ID_FUNCTION;
		id->word = at;
		break;
	case OP_LABEL:
		/* Where the block starts. */
		if (length < 2 || words[1] >= reader->bound)
			return EINVAL;
		id = &reader->ids[words[1]];
		id->kind = ID_LABEL;
		id->word = at;
		reader->current_label = words[1];
		break;
	case OP_SELECTION_MERGE:
		/* The current block's merge. */
		if (length < 3 || reader->current_label == 0)
			return EINVAL;
		reader->ids[reader->current_label].merge = words[1];
		break;
	case OP_LOOP_MERGE:
		/* The current block is a loop header with a merge and a continue target. */
		if (length < 4 || reader->current_label == 0)
			return EINVAL;
		reader->ids[reader->current_label].merge = words[1];
		reader->ids[reader->current_label].continue_target = words[2];
		reader->ids[reader->current_label].flags |= FLAG_LOOP;
		break;
	default:
		break;
	}

	/* Reports what the declaration came to. */
	return error;
}

/* Notes one decoration of an id. */
static void
reader_decorate(
	struct shader_spirv_reader *reader,
	uint32_t target,
	uint32_t decoration,
	uint32_t value)
{
	struct shader_spirv_id *id;

	/* Records the decorations the compiler uses; the others do not change the code. */
	id = &reader->ids[target];
	switch (decoration) {
	case DECORATION_ARRAY_STRIDE:
		id->stride = value;
		id->flags |= FLAG_STRIDE;
		break;
	case DECORATION_BUILT_IN:
		id->builtin = value;
		id->flags |= FLAG_BUILT_IN;
		break;
	case DECORATION_FLAT:
		id->flags |= FLAG_FLAT;
		break;
	case DECORATION_LOCATION:
		id->location = value;
		id->flags |= FLAG_LOCATION;
		break;
	case DECORATION_COMPONENT:
		id->component = value;
		break;
	case DECORATION_BINDING:
		id->binding = value;
		break;
	case DECORATION_DESCRIPTOR_SET:
		id->set = value;
		break;
	default:
		break;
	}
}

/* Reads one type declaration. */
static int
reader_type(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct shader_spirv_member_decoration *decoration;
	struct shader_spirv_member *member;
	struct shader_spirv_id *id;
	uint32_t opcode;
	uint32_t index;
	uint32_t scalars;
	uint32_t member_index;

	/* The type's id. */
	opcode = words[0] & 0xffffU;
	if (length < 2 || words[1] >= reader->bound)
		return EINVAL;
	id = &reader->ids[words[1]];
	id->kind = ID_TYPE;

	/* Fills in the shape of each kind of type. */
	switch (opcode) {
	case OP_TYPE_VOID:
		id->type_kind = TYPE_VOID;
		break;
	case OP_TYPE_BOOL:
		id->type_kind = TYPE_BOOL;
		id->scalars = 1;
		break;
	case OP_TYPE_INT:
		/* Only 32-bit integers. */
		if (length < 4 || words[2] != 32)
			return ENOTSUP;
		id->type_kind = TYPE_INT;
		id->is_signed = (uint8_t)words[3];
		id->scalars = 1;
		break;
	case OP_TYPE_FLOAT:
		/* Only 32-bit floats. */
		if (length < 3 || words[2] != 32)
			return ENOTSUP;
		id->type_kind = TYPE_FLOAT;
		id->scalars = 1;
		break;
	case OP_TYPE_VECTOR:
	case OP_TYPE_MATRIX:
		/* A vector of scalars, or a matrix of column vectors. */
		if (length < 4 || words[2] >= reader->bound)
			return EINVAL;
		id->type_kind = opcode == OP_TYPE_VECTOR ? TYPE_VECTOR : TYPE_MATRIX;
		id->type = words[2];
		id->count = words[3];
		id->scalars = reader->ids[words[2]].scalars * words[3];
		break;
	case OP_TYPE_IMAGE:
		id->type_kind = TYPE_IMAGE;
		break;
	case OP_TYPE_SAMPLER:
		id->type_kind = TYPE_SAMPLER;
		break;
	case OP_TYPE_SAMPLED_IMAGE:
		id->type_kind = TYPE_SAMPLED_IMAGE;
		break;
	case OP_TYPE_ARRAY:
		/* An array's length is a constant id. */
		if (length < 4 || words[2] >= reader->bound || words[3] >= reader->bound)
			return EINVAL;
		if (reader->ids[words[3]].kind != ID_CONSTANT)
			return ENOTSUP;
		id->type_kind = TYPE_ARRAY;
		id->type = words[2];
		id->count = reader->operands[reader->ids[words[3]].first].constant;
		id->scalars = reader->ids[words[2]].scalars * id->count;
		break;
	case OP_TYPE_RUNTIME_ARRAY:
		if (length < 3 || words[2] >= reader->bound)
			return EINVAL;
		id->type_kind = TYPE_RUNTIME_ARRAY;
		id->type = words[2];
		break;
	case OP_TYPE_STRUCT:
		/* The members, with the member decorations kept so far. */
		id->type_kind = TYPE_STRUCT;
		id->count = length - 2;
		id->first = reader->member_count;
		if (reader->member_count + id->count > SHADER_SPIRV_MEMBERS)
			return ENOTSUP;
		scalars = 0;
		for (member_index = 0; member_index < id->count; member_index++) {
			member = &reader->members[reader->member_count++];
			member->type = words[2 + member_index];
			if (member->type >= reader->bound)
				return EINVAL;
			member->scalar_offset = scalars;
			scalars += reader->ids[member->type].scalars;
		}
		id->scalars = scalars;
		for (index = 0; index < reader->member_decoration_count; index++) {
			decoration = &reader->member_decorations[index];
			if (decoration->structure != words[1] || decoration->member >= id->count)
				continue;
			member = &reader->members[id->first + decoration->member];
			if (decoration->decoration == DECORATION_OFFSET) {
				member->offset = decoration->value;
			} else if (decoration->decoration == DECORATION_BUILT_IN) {
				member->builtin = decoration->value;
				member->has_builtin = 1;
			} else if (decoration->decoration == DECORATION_MATRIX_STRIDE) {
				reader->ids[member->type].stride = decoration->value;
			}
		}
		break;
	case OP_TYPE_POINTER:
		if (length < 4 || words[3] >= reader->bound)
			return EINVAL;
		id->type_kind = TYPE_POINTER;
		id->storage = words[2];
		id->type = words[3];
		break;
	case OP_TYPE_FUNCTION:
		id->type_kind = TYPE_FUNCTION;
		break;
	default:
		return ENOTSUP;
	}

	/* Succeeded: the type is known. */
	return 0;
}

/* Reads one constant (or undefined value) as its scalar words. */
static int
reader_constant(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct powervr_ir_operand *operands;
	struct powervr_ir_operand *part;
	uint32_t opcode;
	uint32_t count;
	uint32_t parts;
	uint32_t index;
	uint32_t at;
	int error;

	/* The result's type and id, and its words. */
	opcode = words[0] & 0xffffU;
	if (length < 3 || words[1] >= reader->bound || words[2] >= reader->bound)
		return EINVAL;
	count = reader->ids[words[1]].scalars;
	if (count == 0 || count > COMPOSITE_MAX)
		return ENOTSUP;
	error = result_begin(reader, words[2], words[1], count, &operands);
	if (error != 0)
		return error;
	reader->ids[words[2]].kind = ID_CONSTANT;

	/* Fills the words: each kind of constant its own way. */
	if (opcode == OP_CONSTANT || opcode == OP_SPEC_CONSTANT) {
		if (length < 4)
			return EINVAL;
		operands[0] = constant_operand(words[3]);
	} else if (opcode == OP_CONSTANT_TRUE || opcode == OP_SPEC_CONSTANT_TRUE) {
		operands[0] = constant_operand(TRUE_WORD);
	} else if (opcode == OP_CONSTANT_COMPOSITE || opcode == OP_SPEC_CONSTANT_COMPOSITE) {
		/* The constituents' words, in order. */
		at = 0;
		for (index = 3; index < length; index++) {
			if (words[index] >= reader->bound)
				return EINVAL;
			error = id_operands(reader, words[index], &part, &parts);
			if (error != 0)
				return error;
			if (at + parts > count)
				return EINVAL;
			while (parts-- > 0)
				operands[at++] = *part++;
		}
		if (at != count)
			return EINVAL;
	} else {
		/* False, null and undefined are all zero words. */
		for (index = 0; index < count; index++)
			operands[index] = constant_operand(0);
	}

	/* Succeeded: the constant. */
	return 0;
}

/* Reads one global variable declaration. */
static int
reader_global_variable(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct shader_spirv_id *id;
	struct shader_spirv_id *pointer_type;

	/* The variable is a pointer of its type into itself; a global's initializer is outside the subset. */
	if (length > 4)
		return ENOTSUP;
	if (words[1] >= reader->bound || words[2] >= reader->bound)
		return EINVAL;
	pointer_type = &reader->ids[words[1]];
	if (pointer_type->type_kind != TYPE_POINTER)
		return EINVAL;
	id = &reader->ids[words[2]];
	id->kind = ID_POINTER;
	id->storage = words[3];
	id->type = pointer_type->type;
	id->variable = words[2];
	id->offset = 0;

	/* Refuses storage the compiler does not place. */
	switch (id->storage) {
	case STORAGE_UNIFORM_CONSTANT:
	case STORAGE_INPUT:
	case STORAGE_UNIFORM:
	case STORAGE_OUTPUT:
	case STORAGE_PRIVATE:
	case STORAGE_PUSH_CONSTANT:
	case STORAGE_STORAGE_BUFFER:
		break;
	default:
		return ENOTSUP;
	}

	/* Succeeded: the variable is known. */
	return 0;
}

/*
 * Places the interface variables: each register-class variable gets
 * values for its words, vertex inputs their registers, varyings their
 * output words or coefficient sets.
 */
static int
reader_layout_interface(
	struct shader_spirv_reader *reader)
{
	struct powervr_shader_program *program;
	struct shader_spirv_id *id;
	uint32_t location;
	uint32_t best;
	uint32_t index;
	uint32_t words;
	uint32_t next_input;
	uint32_t next_output;
	uint32_t next_coefficient;
	uint32_t any_smooth;
	uint32_t done[SHADER_SPIRV_IDS / 32U];
	uint32_t entry;

	/* Gives every register-class global its words as values. */
	program = reader->program;
	for (index = 1; index < reader->bound; index++) {
		id = &reader->ids[index];
		if (id->kind != ID_POINTER || id->variable != index)
			continue;
		if (id->storage != STORAGE_INPUT && id->storage != STORAGE_OUTPUT && id->storage != STORAGE_PRIVATE)
			continue;
		words = scalar_count(reader, id->type);
		if (words == 0 || words > COMPOSITE_MAX * 2U)
			return ENOTSUP;
		id->first = new_value(reader, 1);
		for (location = 1; location < words; location++)
			(void)new_value(reader, 1);
		if (reader->error != 0)
			return reader->error;
	}

	/* Does a fragment shader iterate anything with perspective? */
	any_smooth = 0;
	for (index = 1; index < reader->bound; index++) {
		id = &reader->ids[index];
		if (id->kind == ID_POINTER &&
		    id->variable == index &&
		    id->storage == STORAGE_INPUT &&
		    (id->flags & FLAG_LOCATION) != 0 &&
		    (id->flags & FLAG_FLAT) == 0)
			any_smooth = 1;
	}

	/* The w coefficients come first when a varying needs them. */
	next_coefficient = 0;
	if (reader->stage == POWERVR_SHADER_FRAGMENT && any_smooth != 0) {
		program->uses_w = 1;
		program->w_coefficient = 0;
		next_coefficient = 4;
	}

	/* Places located inputs and outputs in location order. */
	for (index = 0; index < SHADER_SPIRV_IDS / 32U; index++)
		done[index] = 0;
	next_input = 0;
	next_output = 4;
	for (;;) {
		/* Finds the unplaced located interface variable of the lowest location. */
		best = 0;
		for (index = 1; index < reader->bound; index++) {
			id = &reader->ids[index];
			if (id->kind != ID_POINTER || id->variable != index || (id->flags & FLAG_LOCATION) == 0)
				continue;
			if (id->storage != STORAGE_INPUT && id->storage != STORAGE_OUTPUT)
				continue;
			if ((done[index / 32U] & (1U << (index % 32U))) != 0)
				continue;
			if (best == 0 || id->location < reader->ids[best].location)
				best = index;
		}
		if (best == 0)
			break;

		/* Places it by stage and direction. */
		done[best / 32U] |= 1U << (best % 32U);
		id = &reader->ids[best];
		words = scalar_count(reader, id->type);
		entry = 0;
		if (id->storage == STORAGE_INPUT && reader->stage == POWERVR_SHADER_VERTEX) {
			if (program->input_count >= POWERVR_SHADER_INPUTS)
				return ENOTSUP;
			entry = program->input_count++;
			program->inputs[entry].location = id->location;
			program->inputs[entry].components = words;
			program->inputs[entry].reg = next_input;
			id->interface = next_input;
			next_input += words;
		} else if (id->storage == STORAGE_OUTPUT && reader->stage == POWERVR_SHADER_VERTEX) {
			if (program->varying_count >= POWERVR_SHADER_VARYINGS)
				return ENOTSUP;
			entry = program->varying_count++;
			program->varyings[entry].location = id->location;
			program->varyings[entry].components = words;
			program->varyings[entry].flat = (id->flags & FLAG_FLAT) != 0;
			program->varyings[entry].reg = next_output;
			id->interface = next_output;
			next_output += words;
		} else if (id->storage == STORAGE_INPUT && reader->stage == POWERVR_SHADER_FRAGMENT) {
			if (program->varying_count >= POWERVR_SHADER_VARYINGS)
				return ENOTSUP;
			entry = program->varying_count++;
			program->varyings[entry].location = id->location;
			program->varyings[entry].components = words;
			program->varyings[entry].flat = (id->flags & FLAG_FLAT) != 0;
			program->varyings[entry].reg = next_coefficient;
			id->interface = next_coefficient;
			next_coefficient += words * 4U;
		} else if (id->storage == STORAGE_OUTPUT && reader->stage == POWERVR_SHADER_FRAGMENT) {
			if (program->output_count >= POWERVR_SHADER_OUTPUTS || words > 4)
				return ENOTSUP;
			entry = program->output_count++;
			program->outputs[entry].location = id->location;
			id->interface = id->location * 4U;
		} else {
			/* A located compute variable is not a thing. */
			return ENOTSUP;
		}
	}

	/* Records the register counts of the interface. */
	program->vertex_inputs = next_input;
	program->vertex_outputs = next_output;
	if (reader->stage != POWERVR_SHADER_VERTEX)
		program->vertex_outputs = 0;
	program->coefficients = next_coefficient;

	/* Succeeded: the interface is placed. */
	return 0;
}

/*
 * Reads the stage's inputs into the input variables' values before the
 * first block, so every later read is a plain copy.
 */
static int
reader_entry_prologue(
	struct shader_spirv_reader *reader)
{
	struct powervr_ir_operand number;
	struct powervr_ir_operand group;
	struct powervr_ir_operand size;
	struct powervr_ir_operand word;
	struct shader_spirv_id *id;
	uint32_t index;
	uint32_t component;
	uint32_t words;
	uint32_t request;
	uint32_t builtin;
	int error;

	/* Reads each input variable's words. */
	for (index = 1; index < reader->bound; index++) {
		id = &reader->ids[index];
		if (id->kind != ID_POINTER || id->variable != index || id->storage != STORAGE_INPUT)
			continue;
		words = scalar_count(reader, id->type);

		/* A located input: a vertex input register or an iterated varying. */
		if ((id->flags & FLAG_LOCATION) != 0) {
			for (component = 0; component < words; component++) {
				if (reader->stage == POWERVR_SHADER_VERTEX) {
					emit(reader, POWERVR_IR_INPUT, id->first + component, NULL, NULL, NULL, id->interface + component, 0);
				} else {
					emit(reader, POWERVR_IR_ITERATE, id->first + component, NULL, NULL, NULL, id->interface + component * 4U, 0);
					if ((id->flags & FLAG_FLAT) != 0)
						reader->shader->instructions[reader->shader->instruction_count - 1U].flags = POWERVR_IR_FLAT;
				}
			}
			continue;
		}

		/* Refuses an input that is neither located nor a known built-in. */
		if ((id->flags & FLAG_BUILT_IN) == 0)
			return ENOTSUP;
		builtin = id->builtin;
		if (reader->stage != POWERVR_SHADER_COMPUTE)
			return ENOTSUP;

		/* The compute built-ins, built from the instance number and the workgroup. */
		number = emit_value(reader, POWERVR_IR_INSTANCE, NULL, NULL, NULL);
		if (builtin == BUILT_IN_LOCAL_INVOCATION_ID || builtin == BUILT_IN_LOCAL_INVOCATION_INDEX) {
			emit(reader, POWERVR_IR_MOVE, id->first, &number, NULL, NULL, 0, 0);
			for (component = 1; component < words; component++) {
				word = constant_operand(0);
				emit(reader, POWERVR_IR_MOVE, id->first + component, &word, NULL, NULL, 0, 0);
			}
		} else if (builtin == BUILT_IN_WORKGROUP_ID || builtin == BUILT_IN_NUM_WORKGROUPS || builtin == BUILT_IN_GLOBAL_INVOCATION_ID) {
			/* The workgroup id or count, three words in shared registers. */
			error = request_add(reader, builtin == BUILT_IN_NUM_WORKGROUPS ? POWERVR_SHADER_REQUEST_NUM_WORKGROUPS : POWERVR_SHADER_REQUEST_WORKGROUP_ID, 0, 0, 0, 3, &request);
			if (error != 0)
				return error;
			for (component = 0; component < words && component < 3; component++) {
				group = shared_word(reader, request, component);
				if (builtin != BUILT_IN_GLOBAL_INVOCATION_ID) {
					emit(reader, POWERVR_IR_MOVE, id->first + component, &group, NULL, NULL, 0, 0);
				} else if (component == 0) {
					/* x is the group times the workgroup's width plus the instance. */
					size = constant_operand(reader->program->workgroup[0]);
					emit(reader, POWERVR_IR_INTEGER_MULTIPLY, id->first, &group, &size, NULL, 0, 0);
					word = value_operand(id->first, 0);
					emit(reader, POWERVR_IR_INTEGER_ADD, id->first, &word, &number, NULL, 0, 0);
				} else {
					/* y and z are the group's, the workgroup being one deep in each. */
					emit(reader, POWERVR_IR_MOVE, id->first + component, &group, NULL, NULL, 0, 0);
				}
			}
		} else {
			return ENOTSUP;
		}
	}

	/* Clears the output variables, so an unwritten word is zero rather than stale. */
	for (index = 1; index < reader->bound; index++) {
		id = &reader->ids[index];
		if (id->kind != ID_POINTER || id->variable != index)
			continue;
		if (id->storage != STORAGE_OUTPUT && id->storage != STORAGE_PRIVATE)
			continue;
		words = scalar_count(reader, id->type);
		for (component = 0; component < words; component++) {
			word = constant_operand(0);
			emit(reader, POWERVR_IR_MOVE, id->first + component, &word, NULL, NULL, 0, 0);
		}
	}

	/* Refuses a workgroup the core's task cannot hold as one row. */
	if (reader->stage == POWERVR_SHADER_COMPUTE) {
		if (reader->program->workgroup[1] != 1 || reader->program->workgroup[2] != 1)
			return ENOTSUP;
	}

	/* Reports what the reads came to. */
	return reader->error;
}

/*
 * Hands the output variables' words over after the last block: the
 * vertex position and varyings to their output words, the fragment
 * colours to their outputs.
 */
static int
reader_entry_epilogue(
	struct shader_spirv_reader *reader)
{
	struct powervr_ir_operand word;
	struct shader_spirv_member *member;
	struct shader_spirv_id *type;
	struct shader_spirv_id *id;
	uint32_t index;
	uint32_t member_index;
	uint32_t component;
	uint32_t words;

	/* Each output variable. */
	for (index = 1; index < reader->bound; index++) {
		id = &reader->ids[index];
		if (id->kind != ID_POINTER || id->variable != index || id->storage != STORAGE_OUTPUT)
			continue;
		words = scalar_count(reader, id->type);

		/* A located output: a varying's or a colour's words. */
		if ((id->flags & FLAG_LOCATION) != 0) {
			for (component = 0; component < words; component++) {
				word = value_operand(id->first + component, 0);
				emit(reader, POWERVR_IR_OUTPUT, 0, &word, NULL, NULL, id->interface + component, 0);
			}
			continue;
		}

		/* A built-in position, alone or in the per-vertex block. */
		if ((id->flags & FLAG_BUILT_IN) != 0) {
			if (id->builtin != BUILT_IN_POSITION || reader->stage != POWERVR_SHADER_VERTEX)
				continue;
			for (component = 0; component < 4; component++) {
				word = value_operand(id->first + component, 0);
				emit(reader, POWERVR_IR_OUTPUT, 0, &word, NULL, NULL, component, 0);
			}
			continue;
		}

		/* The per-vertex block: its position member; point size and clip distances are dropped. */
		type = &reader->ids[id->type];
		if (type->type_kind != TYPE_STRUCT)
			return ENOTSUP;
		for (member_index = 0; member_index < type->count; member_index++) {
			member = &reader->members[type->first + member_index];
			if (member->has_builtin == 0 || member->builtin != BUILT_IN_POSITION)
				continue;
			if (reader->stage != POWERVR_SHADER_VERTEX)
				return ENOTSUP;
			for (component = 0; component < 4; component++) {
				word = value_operand(id->first + member->scalar_offset + component, 0);
				emit(reader, POWERVR_IR_OUTPUT, 0, &word, NULL, NULL, component, 0);
			}
		}
	}

	/* Reports what the hand-over came to. */
	return reader->error;
}

/*
 * Inlines one function: its parameters are already bound; its blocks
 * are walked from the first; its return value lands in result.
 */
static int
reader_function(
	struct shader_spirv_reader *reader,
	uint32_t function,
	uint32_t *result)
{
	uint32_t at;
	uint32_t length;
	uint32_t opcode;
	uint32_t first_label;
	uint32_t returned;
	uint32_t saved_loops;
	int error;

	/* Refuses calls nested deeper than the reader allows. */
	if (reader->calls >= SHADER_SPIRV_CALLS)
		return ENOTSUP;
	if (function >= reader->bound || reader->ids[function].kind != ID_FUNCTION)
		return EINVAL;

	/* Finds the first block past the function and its parameters. */
	at = reader->ids[function].word;
	first_label = 0;
	while (at < reader->count) {
		length = reader->words[at] >> 16;
		opcode = reader->words[at] & 0xffffU;
		if (length == 0)
			return EINVAL;
		if (opcode == OP_LABEL) {
			first_label = reader->words[at + 1];
			break;
		}
		at += length;
	}
	if (first_label == 0)
		return EINVAL;

	/* Walks the function's blocks; its loops are its own. */
	reader->calls++;
	saved_loops = reader->loop_count;
	returned = 0;
	error = reader_region(reader, first_label, 0, 0, &returned);
	reader->loop_count = saved_loops;
	reader->calls--;
	if (error != 0)
		return error;

	/* Refuses a function that does not end by returning. */
	if (returned == 0)
		return ENOTSUP;

	/* Succeeded: the function's result id, if it returns a value. */
	*result = reader->returned;
	return 0;
}

/*
 * Walks blocks in a straight line from start until a branch reaches
 * stop.  A loop header is walked as its whole loop; a selection as its
 * if; a branch to the innermost loop's merge or continue target that is
 * not the stop is a break or continue.  Through_first walks the first
 * block even when it is the stop (a loop whose header continues itself).
 */
static int
reader_region(
	struct shader_spirv_reader *reader,
	uint32_t start,
	uint32_t stop,
	uint32_t through_first,
	uint32_t *returned)
{
	const uint32_t *words;
	struct powervr_ir_operand condition;
	struct shader_spirv_id *label;
	uint32_t block;
	uint32_t at;
	uint32_t length;
	uint32_t opcode;
	uint32_t merge;
	uint32_t exited_true;
	uint32_t exited_false;
	uint32_t first;
	int error;

	/* Each block in turn. */
	block = start;
	first = through_first;
	for (;;) {
		/* The region ends where a branch reaches its stop. */
		if (block == stop && first == 0)
			return 0;
		first = 0;
		if (block >= reader->bound || reader->ids[block].kind != ID_LABEL)
			return EINVAL;
		label = &reader->ids[block];

		/* A loop header not yet entered is its whole loop, after which the merge follows. */
		if ((label->flags & FLAG_LOOP) != 0 && (label->flags & FLAG_ENTERED) == 0) {
			error = reader_loop(reader, block);
			if (error != 0)
				return error;
			block = label->merge;
			continue;
		}

		/* The block's instructions up to its terminator. */
		error = reader_block_body(reader, block, &at);
		if (error != 0)
			return error;
		words = &reader->words[at];
		length = words[0] >> 16;
		opcode = words[0] & 0xffffU;

		/* The terminator decides what follows. */
		if (opcode == OP_BRANCH) {
			/* An edge to the stop ends the region; an exit ends it too. */
			if (length < 2)
				return EINVAL;
			error = reader_phi_moves(reader, block, words[1]);
			if (error != 0)
				return error;
			if (words[1] == stop)
				return 0;
			error = reader_exit(reader, words[1], &exited_true);
			if (error != 0)
				return error;
			if (exited_true != 0)
				return 0;
			block = words[1];
		} else if (opcode == OP_BRANCH_CONDITIONAL) {
			/* The condition word. */
			if (length < 4)
				return EINVAL;
			error = operand_word(reader, words[1], 0, &condition);
			if (error != 0)
				return error;
			merge = label->merge;
			if ((label->flags & FLAG_LOOP) != 0)
				merge = 0;

			if (merge != 0) {
				/* A selection: an if over the two arms, then the merge. */
				emit(reader, POWERVR_IR_IF, 0, &condition, NULL, NULL, 0, 0);
				error = reader_arm(reader, block, words[2], merge);
				if (error != 0)
					return error;
				emit(reader, POWERVR_IR_ELSE, 0, NULL, NULL, NULL, 0, 0);
				error = reader_arm(reader, block, words[3], merge);
				if (error != 0)
					return error;
				emit(reader, POWERVR_IR_END_IF, 0, NULL, NULL, NULL, 0, 0);
				if (merge == stop)
					return reader->error;
				block = merge;
			} else {
				/* A loop's conditional exit: the exiting arm in an if, the other one goes on. */
				if (reader->loop_count == 0)
					return ENOTSUP;
				exited_true = 0;
				exited_false = 0;
				if (words[2] == reader->loops[reader->loop_count - 1U].merge ||
				    words[2] == reader->loops[reader->loop_count - 1U].continue_target)
					exited_true = 1;
				if (words[3] == reader->loops[reader->loop_count - 1U].merge ||
				    words[3] == reader->loops[reader->loop_count - 1U].continue_target)
					exited_false = 1;
				if (exited_true == 0 && exited_false == 0)
					return ENOTSUP;
				if (exited_true != 0 && exited_false != 0) {
					emit(reader, POWERVR_IR_IF, 0, &condition, NULL, NULL, 0, 0);
					error = reader_edge(reader, block, words[2]);
					if (error != 0)
						return error;
					emit(reader, POWERVR_IR_ELSE, 0, NULL, NULL, NULL, 0, 0);
					error = reader_edge(reader, block, words[3]);
					if (error != 0)
						return error;
					emit(reader, POWERVR_IR_END_IF, 0, NULL, NULL, NULL, 0, 0);
					return reader->error;
				}
				emit(reader, POWERVR_IR_IF, 0, &condition, NULL, NULL, 0, 0);
				if (exited_true != 0) {
					error = reader_edge(reader, block, words[2]);
					if (error != 0)
						return error;
					emit(reader, POWERVR_IR_END_IF, 0, NULL, NULL, NULL, 0, 0);
					error = reader_phi_moves(reader, block, words[3]);
					if (error != 0)
						return error;
					block = words[3];
				} else {
					emit(reader, POWERVR_IR_ELSE, 0, NULL, NULL, NULL, 0, 0);
					error = reader_edge(reader, block, words[3]);
					if (error != 0)
						return error;
					emit(reader, POWERVR_IR_END_IF, 0, NULL, NULL, NULL, 0, 0);
					error = reader_phi_moves(reader, block, words[2]);
					if (error != 0)
						return error;
					block = words[2];
				}
				if (block == stop)
					return reader->error;
			}
		} else if (opcode == OP_RETURN || opcode == OP_RETURN_VALUE) {
			/* A return only ends the function from its outermost level. */
			if (reader->loop_count != 0 || stop != 0)
				return ENOTSUP;
			reader->returned = 0;
			if (opcode == OP_RETURN_VALUE) {
				if (length < 2)
					return EINVAL;
				reader->returned = words[1];
			}
			*returned = 1;
			return reader->error;
		} else {
			/* Switches, kills and unreachable blocks are outside the subset. */
			return ENOTSUP;
		}

		/* Stops on an emission error. */
		if (reader->error != 0)
			return reader->error;
	}
}

/*
 * Walks one loop from its header: the body to the continue target, the
 * continue construct back to the header, between the loop markers.
 */
static int
reader_loop(
	struct shader_spirv_reader *reader,
	uint32_t header)
{
	struct shader_spirv_loop *loop;
	struct shader_spirv_id *label;
	uint32_t returned;
	int error;

	/* Refuses loops nested deeper than the reader allows. */
	if (reader->loop_count >= SHADER_SPIRV_LOOPS)
		return ENOTSUP;
	label = &reader->ids[header];

	/* Opens the loop. */
	emit(reader, POWERVR_IR_LOOP, 0, NULL, NULL, NULL, 0, 0);
	loop = &reader->loops[reader->loop_count++];
	loop->header = header;
	loop->merge = label->merge;
	loop->continue_target = label->continue_target;
	loop->ifs = 0;
	label->flags |= FLAG_ENTERED;

	/* The body, then the continue construct back to the header. */
	returned = 0;
	if (loop->continue_target == header) {
		error = reader_region(reader, header, header, 1, &returned);
	} else {
		error = reader_region(reader, header, loop->continue_target, 0, &returned);
		if (error == 0) {
			/* The continued lanes come back where the continue construct starts. */
			emit(reader, POWERVR_IR_LOOP_CONTINUE, 0, NULL, NULL, NULL, 0, 0);
			error = reader_region(reader, loop->continue_target, header, 0, &returned);
		}
	}
	label->flags &= (uint8_t)~FLAG_ENTERED;
	reader->loop_count--;
	if (error != 0)
		return error;

	/* Closes the loop. */
	emit(reader, POWERVR_IR_END_LOOP, 0, NULL, NULL, NULL, 0, 0);
	return reader->error;
}

/* Walks one arm of a selection from its first block to the merge. */
static int
reader_arm(
	struct shader_spirv_reader *reader,
	uint32_t from,
	uint32_t target,
	uint32_t merge)
{
	uint32_t exited;
	uint32_t returned;
	int error;

	/* An arm straight to the merge only moves the merge's phis. */
	if (target == merge)
		return reader_phi_moves(reader, from, target);

	/* An arm that leaves the loop. */
	error = reader_phi_moves(reader, from, target);
	if (error != 0)
		return error;
	error = reader_exit(reader, target, &exited);
	if (error != 0)
		return error;
	if (exited != 0)
		return 0;

	/* Otherwise the blocks up to the merge. */
	returned = 0;
	return reader_region(reader, target, merge, 0, &returned);
}

/* Takes an edge that must leave the innermost loop: its phis, then the break or continue. */
static int
reader_edge(
	struct shader_spirv_reader *reader,
	uint32_t from,
	uint32_t target)
{
	uint32_t exited;
	int error;

	/* The target's phis on this edge. */
	error = reader_phi_moves(reader, from, target);
	if (error != 0)
		return error;

	/* The exit itself. */
	error = reader_exit(reader, target, &exited);
	if (error != 0)
		return error;
	if (exited == 0)
		return ENOTSUP;

	/* Succeeded: the edge is taken. */
	return 0;
}

/* Writes a break or continue when a branch target leaves the innermost loop. */
static int
reader_exit(
	struct shader_spirv_reader *reader,
	uint32_t target,
	uint32_t *exited)
{
	struct shader_spirv_loop *loop;

	/* Outside a loop, nothing exits. */
	*exited = 0;
	if (reader->loop_count == 0)
		return 0;
	loop = &reader->loops[reader->loop_count - 1U];

	/* The merge is a break, the continue target a continue. */
	if (target == loop->merge) {
		emit(reader, POWERVR_IR_BREAK, 0, NULL, NULL, NULL, 0, 0);
		*exited = 1;
	} else if (target == loop->continue_target && target != loop->header) {
		emit(reader, POWERVR_IR_CONTINUE, 0, NULL, NULL, NULL, 0, 0);
		*exited = 1;
	}

	/* Reports what the exit came to. */
	return reader->error;
}

/*
 * Moves the incoming values of target's phis on the edge from a block,
 * through copies so phis that read each other see the old values.
 */
static int
reader_phi_moves(
	struct shader_spirv_reader *reader,
	uint32_t from,
	uint32_t target)
{
	struct powervr_ir_operand staged[COMPOSITE_MAX * 4U];
	struct powervr_ir_operand *operands;
	struct powervr_ir_operand *incoming;
	uint32_t staged_count;
	uint32_t moved;
	uint32_t pass;
	uint32_t at;
	uint32_t length;
	uint32_t pair;
	uint32_t count;
	uint32_t phi_count;
	uint32_t word;
	int error;

	/* Two passes over the target's leading phis: stage the values, then move them in. */
	staged_count = 0;
	moved = 0;
	for (pass = 0; pass < 2; pass++) {
		error = find_block(reader, target, &at);
		if (error != 0)
			return error;
		at += reader->words[at] >> 16;

		/* Each phi at the block's top. */
		while (at < reader->count && (reader->words[at] & 0xffffU) == OP_PHI) {
			length = reader->words[at] >> 16;
			if (length < 3 || at + length > reader->count || reader->words[at + 2] >= reader->bound)
				return EINVAL;

			/* The phi's own values, made on first sight. */
			if (reader->ids[reader->words[at + 2]].kind != ID_VALUE) {
				count = scalar_count(reader, reader->words[at + 1]);
				if (count == 0 || count > COMPOSITE_MAX)
					return ENOTSUP;
				error = result_begin(reader, reader->words[at + 2], reader->words[at + 1], count, &operands);
				if (error != 0)
					return error;
				for (word = 0; word < count; word++)
					operands[word] = value_operand(new_value(reader, 1), 0);
			}
			error = id_operands(reader, reader->words[at + 2], &operands, &phi_count);
			if (error != 0)
				return error;

			/* The incoming value on this edge. */
			for (pair = 3; pair + 1 < length; pair += 2) {
				if (reader->words[at + pair + 1] != from)
					continue;
				error = id_operands(reader, reader->words[at + pair], &incoming, &count);
				if (error != 0)
					return error;
				if (count != phi_count || staged_count + count > COMPOSITE_MAX * 4U)
					return ENOTSUP;
				for (word = 0; word < count; word++) {
					if (pass == 0) {
						staged[staged_count++] = emit_value(reader, POWERVR_IR_MOVE, &incoming[word], NULL, NULL);
					} else {
						emit(reader, POWERVR_IR_MOVE, operands[word].value, &staged[moved++], NULL, NULL, 0, 0);
					}
				}
				break;
			}
			at += length;
		}
	}

	/* Reports what the moves came to. */
	return reader->error;
}

/*
 * Reads one block's instructions up to its terminator, whose position
 * is reported.
 */
static int
reader_block_body(
	struct shader_spirv_reader *reader,
	uint32_t label,
	uint32_t *terminator)
{
	uint32_t at;
	uint32_t length;
	uint32_t opcode;
	int error;

	/* Starts after the label. */
	error = find_block(reader, label, &at);
	if (error != 0)
		return error;
	at += reader->words[at] >> 16;

	/* Each instruction until a branch or return. */
	while (at < reader->count) {
		length = reader->words[at] >> 16;
		opcode = reader->words[at] & 0xffffU;
		if (length == 0 || at + length > reader->count)
			return EINVAL;
		if (opcode == OP_BRANCH ||
		    opcode == OP_BRANCH_CONDITIONAL ||
		    opcode == OP_RETURN ||
		    opcode == OP_RETURN_VALUE ||
		    (opcode >= 251U && opcode <= 255U)) {
			*terminator = at;
			return 0;
		}
		error = reader_instruction(reader, &reader->words[at], length);
		if (error != 0)
			return error;
		at += length;
	}

	/* A block without a terminator. */
	return EINVAL;
}

/* Reads one instruction inside a block. */
static int
reader_instruction(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct powervr_ir_operand *operands;
	struct shader_spirv_id *function;
	struct shader_spirv_id *id;
	uint32_t opcode;
	uint32_t handled;
	uint32_t at;
	uint32_t parameter;
	uint32_t argument;
	uint32_t result;
	uint32_t words_count;
	int error;

	/* Dispatches on the opcode. */
	opcode = words[0] & 0xffffU;
	switch (opcode) {
	case 5U:	/* OpName */
	case 6U:	/* OpMemberName */
	case 8U:	/* OpLine */
	case 317U:	/* OpNoLine */
	case OP_PHI:
	case OP_SELECTION_MERGE:
	case OP_LOOP_MERGE:
		/* Phis are moved on their edges; merges were noted in the first pass. */
		return 0;
	case OP_UNDEF:
		return reader_constant(reader, words, length);
	case OP_VARIABLE:
		/* A function variable: values for its words, set to its initializer if it has one. */
		if (length < 4 || words[2] >= reader->bound)
			return EINVAL;
		if (words[3] != STORAGE_FUNCTION || words[1] >= reader->bound)
			return ENOTSUP;
		id = &reader->ids[words[2]];
		id->kind = ID_POINTER;
		id->storage = words[3];
		id->type = reader->ids[words[1]].type;
		id->variable = words[2];
		words_count = scalar_count(reader, id->type);
		if (words_count == 0 || words_count > COMPOSITE_MAX * 2U)
			return ENOTSUP;
		id->first = new_value(reader, 1);
		for (at = 1; at < words_count; at++)
			(void)new_value(reader, 1);
		if (length > 4) {
			error = id_operands(reader, words[4], &operands, &result);
			if (error != 0)
				return error;
			for (at = 0; at < result && at < words_count; at++)
				emit(reader, POWERVR_IR_MOVE, id->first + at, &operands[at], NULL, NULL, 0, 0);
		}
		return reader->error;
	case OP_LOAD:
		if (length < 4)
			return EINVAL;
		return reader_load(reader, words[1], words[2], words[3]);
	case OP_STORE:
		if (length < 3)
			return EINVAL;
		return reader_store(reader, words[1], words[2]);
	case OP_ACCESS_CHAIN:
	case OP_IN_BOUNDS_ACCESS_CHAIN:
		return reader_access_chain(reader, words, length);
	case OP_FUNCTION_CALL:
		/* Binds the parameters to the arguments, then inlines the body. */
		if (length < 4 || words[3] >= reader->bound)
			return EINVAL;
		function = &reader->ids[words[3]];
		if (function->kind != ID_FUNCTION)
			return EINVAL;
		at = function->word + (reader->words[function->word] >> 16);
		for (argument = 4; argument < length; argument++) {
			if ((reader->words[at] & 0xffffU) != OP_FUNCTION_PARAMETER)
				return EINVAL;
			parameter = reader->words[at + 2];
			if (parameter >= reader->bound || words[argument] >= reader->bound)
				return EINVAL;
			reader->ids[parameter] = reader->ids[words[argument]];
			at += reader->words[at] >> 16;
		}
		error = reader_function(reader, words[3], &result);
		if (error != 0)
			return error;
		if (result != 0) {
			if (words[2] >= reader->bound)
				return EINVAL;
			reader->ids[words[2]] = reader->ids[result];
		}
		return reader->error;
	case OP_EXT_INST:
		return reader_extended(reader, words, length);
	case OP_SAMPLED_IMAGE:
		/* The combined image's binding carries over. */
		if (length < 5 || words[2] >= reader->bound || words[3] >= reader->bound)
			return EINVAL;
		reader->ids[words[2]] = reader->ids[words[3]];
		return 0;
	case OP_IMAGE_SAMPLE_IMPLICIT_LOD:
		return reader_sample(reader, words, length);
	default:
		break;
	}

	/* Composite shuffling and arithmetic. */
	error = reader_composite(reader, words, length, &handled);
	if (error != 0 || handled != 0)
		return error;
	error = reader_arithmetic(reader, words, length, &handled);
	if (error != 0 || handled != 0)
		return error;

	/* Anything else is outside the subset. */
	return ENOTSUP;
}

/* Reads an access chain into a pointer with an offset and maybe an index. */
static int
reader_access_chain(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct powervr_ir_operand index;
	struct shader_spirv_member *member;
	struct shader_spirv_id *base;
	struct shader_spirv_id *type;
	struct shader_spirv_id pointer;
	uint32_t memory;
	uint32_t at;
	uint32_t current;
	uint32_t literal;
	uint32_t stride;
	int error;

	/* Starts from the base pointer. */
	if (length < 4 || words[2] >= reader->bound || words[3] >= reader->bound)
		return EINVAL;
	base = &reader->ids[words[3]];
	if (base->kind != ID_POINTER)
		return EINVAL;
	pointer = *base;
	current = base->type;
	memory = 0;
	if (pointer.storage == STORAGE_UNIFORM || pointer.storage == STORAGE_STORAGE_BUFFER || pointer.storage == STORAGE_PUSH_CONSTANT)
		memory = 1;

	/* Steps through each index. */
	for (at = 4; at < length; at++) {
		if (words[at] >= reader->bound || current >= reader->bound)
			return EINVAL;
		type = &reader->ids[current];
		error = operand_word(reader, words[at], 0, &index);
		if (error != 0)
			return error;
		literal = index.constant;

		if (type->type_kind == TYPE_STRUCT) {
			/* A member: a constant index. */
			if (index.kind != POWERVR_IR_OPERAND_CONSTANT || literal >= type->count)
				return EINVAL;
			member = &reader->members[type->first + literal];
			if (memory != 0) {
				pointer.offset += member->offset;
			} else {
				pointer.offset += member->scalar_offset;
			}
			current = member->type;
			continue;
		}

		/* An element of an array, a vector or a matrix. */
		if (type->type_kind != TYPE_ARRAY &&
		    type->type_kind != TYPE_RUNTIME_ARRAY &&
		    type->type_kind != TYPE_VECTOR &&
		    type->type_kind != TYPE_MATRIX)
			return ENOTSUP;
		if (memory != 0) {
			stride = type->stride;
			if (type->type_kind == TYPE_VECTOR)
				stride = 4;
			if (stride == 0)
				stride = byte_size(reader, type->type);
		} else {
			stride = scalar_count(reader, type->type);
		}

		if (index.kind == POWERVR_IR_OPERAND_CONSTANT) {
			pointer.offset += literal * stride;
		} else if (memory != 0 && (pointer.flags & FLAG_HAS_INDEX) == 0 && pointer.storage != STORAGE_PUSH_CONSTANT) {
			/* One run-time index into buffer memory. */
			pointer.index = plain(reader, index);
			pointer.index_stride = stride;
			pointer.flags |= FLAG_HAS_INDEX;
		} else {
			/* Run-time indexing of registers, or twice, is outside the subset. */
			return ENOTSUP;
		}
		current = type->type;
	}

	/* The result is the pointer to the last type reached. */
	pointer.type = current;
	reader->ids[words[2]] = pointer;
	reader->ids[words[2]].kind = ID_POINTER;
	return reader->error;
}

/* Reads the words a pointer names into a new result. */
static int
reader_load(
	struct shader_spirv_reader *reader,
	uint32_t type,
	uint32_t result,
	uint32_t pointer)
{
	struct powervr_ir_operand address[3];
	struct powervr_ir_operand offset;
	struct powervr_ir_operand word;
	struct powervr_ir_operand *operands;
	struct shader_spirv_id *source;
	uint32_t count;
	uint32_t index;
	uint32_t request;
	int error;

	/* The pointer and the words to read. */
	if (pointer >= reader->bound || result >= reader->bound || type >= reader->bound)
		return EINVAL;
	source = &reader->ids[pointer];
	if (source->kind != ID_POINTER)
		return EINVAL;

	/* A sampled image: its binding is all a load gives. */
	if (source->storage == STORAGE_UNIFORM_CONSTANT) {
		reader->ids[result].kind = ID_SAMPLED_IMAGE;
		reader->ids[result].set = reader->ids[source->variable].set;
		reader->ids[result].binding = reader->ids[source->variable].binding;
		return 0;
	}

	/* The result's words. */
	count = scalar_count(reader, type);
	if (count == 0 || count > COMPOSITE_MAX)
		return ENOTSUP;
	error = result_begin(reader, result, type, count, &operands);
	if (error != 0)
		return error;

	/* Fills them by where the pointer points. */
	switch (source->storage) {
	case STORAGE_INPUT:
	case STORAGE_OUTPUT:
	case STORAGE_PRIVATE:
	case STORAGE_FUNCTION:
		/* Copies of the variable's values, so later stores do not change the result. */
		for (index = 0; index < count; index++) {
			word = value_operand(reader->ids[source->variable].first + source->offset + index, 0);
			operands[index] = emit_value(reader, POWERVR_IR_MOVE, &word, NULL, NULL);
		}
		break;
	case STORAGE_PUSH_CONSTANT:
		/* Words of the push constant request. */
		if ((source->offset & 3U) != 0)
			return ENOTSUP;
		if (reader->push_request >= POWERVR_SHADER_REQUESTS) {
			error = request_add(reader, POWERVR_SHADER_REQUEST_PUSH_CONSTANTS, 0, 0, 0, 0, &request);
			if (error != 0)
				return error;
			reader->push_request = request;
		}
		for (index = 0; index < count; index++) {
			operands[index] = shared_word(reader, reader->push_request, source->offset / 4U + index);
			if (source->offset / 4U + index + 1U > reader->push_words)
				reader->push_words = source->offset / 4U + index + 1U;
		}
		break;
	case STORAGE_UNIFORM:
	case STORAGE_STORAGE_BUFFER:
		/* Loads from the buffer, word by word. */
		error = reader_memory_address(reader, source, address);
		if (error != 0)
			return error;
		for (index = 0; index < count; index++) {
			offset = address[2];
			if (index != 0) {
				word = constant_operand(index * 4U);
				offset = emit_value(reader, POWERVR_IR_INTEGER_ADD, &address[2], &word, NULL);
			}
			operands[index] = value_operand(new_value(reader, 1), 0);
			emit(reader, POWERVR_IR_LOAD, operands[index].value, &address[0], &address[1], &offset, 0, 0);
		}
		break;
	default:
		return ENOTSUP;
	}

	/* Reports what the load came to. */
	return reader->error;
}

/* Writes an object's words where a pointer names. */
static int
reader_store(
	struct shader_spirv_reader *reader,
	uint32_t pointer,
	uint32_t object)
{
	struct powervr_ir_operand address[3];
	struct powervr_ir_operand offset;
	struct powervr_ir_operand word;
	struct powervr_ir_operand *operands;
	struct powervr_ir_instruction *instruction;
	struct shader_spirv_id *target;
	uint32_t count;
	uint32_t index;
	int error;

	/* The pointer and the object's words. */
	if (pointer >= reader->bound)
		return EINVAL;
	target = &reader->ids[pointer];
	if (target->kind != ID_POINTER)
		return EINVAL;
	error = id_operands(reader, object, &operands, &count);
	if (error != 0)
		return error;

	/* Writes them by where the pointer points. */
	switch (target->storage) {
	case STORAGE_OUTPUT:
	case STORAGE_PRIVATE:
	case STORAGE_FUNCTION:
		/* Moves into the variable's values. */
		for (index = 0; index < count; index++)
			emit(reader, POWERVR_IR_MOVE, reader->ids[target->variable].first + target->offset + index, &operands[index], NULL, NULL, 0, 0);
		break;
	case STORAGE_UNIFORM:
	case STORAGE_STORAGE_BUFFER:
		/* Stores to the buffer, word by word. */
		error = reader_memory_address(reader, target, address);
		if (error != 0)
			return error;
		for (index = 0; index < count; index++) {
			offset = address[2];
			if (index != 0) {
				word = constant_operand(index * 4U);
				offset = emit_value(reader, POWERVR_IR_INTEGER_ADD, &address[2], &word, NULL);
			}
			word = plain(reader, operands[index]);
			emit(reader, POWERVR_IR_STORE, 0, &address[0], &address[1], &offset, 0, 0);
			instruction = &reader->shader->instructions[reader->shader->instruction_count - 1U];
			if (reader->error == 0)
				instruction->source[3] = word;
		}
		break;
	default:
		return ENOTSUP;
	}

	/* Reports what the store came to. */
	return reader->error;
}

/*
 * Works out a buffer pointer's address: the binding's address words
 * from its descriptor request, and the byte offset as an operand.
 */
static int
reader_memory_address(
	struct shader_spirv_reader *reader,
	const struct shader_spirv_id *pointer,
	struct powervr_ir_operand address[3])
{
	struct powervr_ir_operand stride;
	struct powervr_ir_operand scaled;
	struct powervr_ir_operand offset;
	const struct shader_spirv_id *variable;
	uint32_t request;
	int error;

	/* The binding's address words, asked for once per binding. */
	variable = &reader->ids[pointer->variable];
	error = request_add(reader, POWERVR_SHADER_REQUEST_DESCRIPTOR, POWERVR_SHADER_PART_BUFFER_ADDRESS, variable->set, variable->binding, 2, &request);
	if (error != 0)
		return error;
	address[0] = shared_word(reader, request, 0);
	address[1] = shared_word(reader, request, 1);

	/* The byte offset: the constant part, plus the index times its stride. */
	offset = constant_operand(pointer->offset);
	if ((pointer->flags & FLAG_HAS_INDEX) != 0) {
		stride = constant_operand(pointer->index_stride);
		scaled = emit_value(reader, POWERVR_IR_INTEGER_MULTIPLY, &pointer->index, &stride, NULL);
		offset = emit_value(reader, POWERVR_IR_INTEGER_ADD, &scaled, &offset, NULL);
	}
	address[2] = offset;

	/* Reports what the address came to. */
	return reader->error;
}

/*
 * Reads the composite-shaping instructions, which only rearrange words
 * and write no code.
 */
static int
reader_composite(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length,
	uint32_t *handled)
{
	struct powervr_ir_operand gathered[COMPOSITE_MAX * 2U];
	struct powervr_ir_operand *operands;
	struct powervr_ir_operand *part;
	struct powervr_ir_operand *second;
	uint32_t opcode;
	uint32_t count;
	uint32_t parts;
	uint32_t second_count;
	uint32_t at;
	uint32_t index;
	uint32_t offset;
	uint32_t member_type;
	uint32_t total;
	int error;

	/* Only the shaping opcodes are this function's. */
	opcode = words[0] & 0xffffU;
	*handled = 1;
	if (opcode != OP_COMPOSITE_CONSTRUCT &&
	    opcode != OP_COMPOSITE_EXTRACT &&
	    opcode != OP_COMPOSITE_INSERT &&
	    opcode != OP_VECTOR_SHUFFLE &&
	    opcode != OP_COPY_OBJECT &&
	    opcode != OP_BITCAST) {
		*handled = 0;
		return 0;
	}
	if (length < 4 || words[1] >= reader->bound || words[2] >= reader->bound)
		return EINVAL;

	/* Gathers the words the result is made from. */
	total = 0;
	if (opcode == OP_COMPOSITE_CONSTRUCT) {
		for (at = 3; at < length; at++) {
			error = id_operands(reader, words[at], &part, &parts);
			if (error != 0)
				return error;
			for (index = 0; index < parts && total < COMPOSITE_MAX * 2U; index++)
				gathered[total++] = part[index];
		}
	} else if (opcode == OP_COMPOSITE_EXTRACT) {
		error = id_operands(reader, words[3], &part, &parts);
		if (error != 0)
			return error;
		error = composite_offset(reader, reader->ids[words[3]].type, &words[4], length - 4, &offset, &member_type);
		if (error != 0)
			return error;
		count = scalar_count(reader, words[1]);
		if (offset + count > parts)
			return EINVAL;
		for (index = 0; index < count; index++)
			gathered[total++] = part[offset + index];
	} else if (opcode == OP_COMPOSITE_INSERT) {
		if (length < 5)
			return EINVAL;
		error = id_operands(reader, words[4], &part, &parts);
		if (error != 0)
			return error;
		for (index = 0; index < parts && total < COMPOSITE_MAX * 2U; index++)
			gathered[total++] = part[index];
		error = composite_offset(reader, reader->ids[words[4]].type, &words[5], length - 5, &offset, &member_type);
		if (error != 0)
			return error;
		error = id_operands(reader, words[3], &part, &parts);
		if (error != 0)
			return error;
		if (offset + parts > total)
			return EINVAL;
		for (index = 0; index < parts; index++)
			gathered[offset + index] = part[index];
	} else if (opcode == OP_VECTOR_SHUFFLE) {
		if (length < 5)
			return EINVAL;
		error = id_operands(reader, words[3], &part, &parts);
		if (error != 0)
			return error;
		error = id_operands(reader, words[4], &second, &second_count);
		if (error != 0)
			return error;
		for (at = 5; at < length && total < COMPOSITE_MAX * 2U; at++) {
			if (words[at] == 0xffffffffU) {
				gathered[total++] = constant_operand(0);
			} else if (words[at] < parts) {
				gathered[total++] = part[words[at]];
			} else if (words[at] - parts < second_count) {
				gathered[total++] = second[words[at] - parts];
			} else {
				return EINVAL;
			}
		}
	} else {
		/* A copy or a bitcast: the same words, a bitcast without float modifiers. */
		error = id_operands(reader, words[3], &part, &parts);
		if (error != 0)
			return error;
		for (index = 0; index < parts && total < COMPOSITE_MAX * 2U; index++) {
			gathered[total] = part[index];
			if (opcode == OP_BITCAST)
				gathered[total] = plain(reader, part[index]);
			total++;
		}
	}

	/* The result holds exactly its type's words. */
	count = scalar_count(reader, words[1]);
	if (count != total || count == 0 || count > COMPOSITE_MAX)
		return ENOTSUP;
	error = result_begin(reader, words[2], words[1], count, &operands);
	if (error != 0)
		return error;
	for (index = 0; index < count; index++)
		operands[index] = gathered[index];

	/* Reports what the shaping came to. */
	return reader->error;
}

/* Reads one arithmetic, comparison, logic or conversion instruction, word by word. */
static int
reader_arithmetic(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length,
	uint32_t *handled)
{
	struct powervr_ir_operand *result;
	struct powervr_ir_operand *a;
	struct powervr_ir_operand *b;
	struct powervr_ir_operand *c;
	struct powervr_ir_operand x;
	struct powervr_ir_operand y;
	struct powervr_ir_operand z;
	struct powervr_ir_operand sum;
	uint32_t opcode;
	uint32_t count;
	uint32_t a_count;
	uint32_t b_count;
	uint32_t c_count;
	uint32_t index;
	uint32_t row;
	uint32_t column;
	uint32_t columns;
	uint32_t ir;
	uint32_t swap;
	uint32_t invert;
	int error;

	/* The result and the sources every form has. */
	opcode = words[0] & 0xffffU;
	*handled = 1;
	if (length < 4 || words[1] >= reader->bound || words[2] >= reader->bound) {
		*handled = 0;
		return 0;
	}
	count = scalar_count(reader, words[1]);
	if (count == 0 || count > COMPOSITE_MAX) {
		*handled = 0;
		return 0;
	}
	error = id_operands(reader, words[3], &a, &a_count);
	if (error != 0) {
		*handled = 0;
		return 0;
	}
	b = NULL;
	b_count = 0;
	if (length > 4) {
		error = id_operands(reader, words[4], &b, &b_count);
		if (error != 0)
			b = NULL;
	}
	c = NULL;
	c_count = 0;
	if (length > 5) {
		error = id_operands(reader, words[5], &c, &c_count);
		if (error != 0)
			c = NULL;
	}

	/* The forms that are not word by word. */
	if (opcode == OP_DOT) {
		if (b == NULL || a_count != b_count || count != 1)
			return EINVAL;
		error = result_begin(reader, words[2], words[1], 1, &result);
		if (error != 0)
			return error;
		sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &a[0], &b[0], NULL);
		for (index = 1; index < a_count; index++)
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &a[index], &b[index], &sum);
		result[0] = sum;
		return reader->error;
	}
	if (opcode == OP_MATRIX_TIMES_VECTOR) {
		/* Each row: the sum of the columns' words in it times the vector's. */
		if (b == NULL || b_count == 0 || a_count != b_count * count)
			return ENOTSUP;
		error = result_begin(reader, words[2], words[1], count, &result);
		if (error != 0)
			return error;
		columns = b_count;
		for (row = 0; row < count; row++) {
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &a[row], &b[0], NULL);
			for (column = 1; column < columns; column++)
				sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &a[column * count + row], &b[column], &sum);
			result[row] = sum;
		}
		return reader->error;
	}

	/* Maps the opcode to the operation, with swapped or inverted forms. */
	swap = 0;
	invert = 0;
	ir = POWERVR_IR_OPCODE_COUNT;
	switch (opcode) {
	case OP_F_ADD:
		ir = POWERVR_IR_FLOAT_ADD;
		break;
	case OP_F_SUB:
		ir = POWERVR_IR_FLOAT_ADD;
		break;
	case OP_F_MUL:
		ir = POWERVR_IR_FLOAT_MULTIPLY;
		break;
	case OP_F_DIV:
		ir = POWERVR_IR_FLOAT_MULTIPLY;
		break;
	case OP_VECTOR_TIMES_SCALAR:
		ir = POWERVR_IR_FLOAT_MULTIPLY;
		break;
	case OP_MATRIX_TIMES_SCALAR:
		ir = POWERVR_IR_FLOAT_MULTIPLY;
		break;
	case OP_F_NEGATE:
		ir = POWERVR_IR_MOVE;
		break;
	case OP_I_ADD:
		ir = POWERVR_IR_INTEGER_ADD;
		break;
	case OP_I_SUB:
		ir = POWERVR_IR_INTEGER_ADD;
		break;
	case OP_I_MUL:
		ir = POWERVR_IR_INTEGER_MULTIPLY;
		break;
	case OP_S_NEGATE:
		ir = POWERVR_IR_INTEGER_MULTIPLY;
		break;
	case OP_BITWISE_AND:
		ir = POWERVR_IR_BIT_AND;
		break;
	case OP_BITWISE_OR:
		ir = POWERVR_IR_BIT_OR;
		break;
	case OP_BITWISE_XOR:
		ir = POWERVR_IR_BIT_XOR;
		break;
	case OP_NOT:
		ir = POWERVR_IR_BIT_XOR;
		break;
	case OP_LOGICAL_AND:
		ir = POWERVR_IR_BIT_AND;
		break;
	case OP_LOGICAL_OR:
		ir = POWERVR_IR_BIT_OR;
		break;
	case OP_LOGICAL_NOT:
		ir = POWERVR_IR_BIT_XOR;
		break;
	case OP_LOGICAL_NOT_EQUAL:
		ir = POWERVR_IR_BIT_XOR;
		break;
	case OP_LOGICAL_EQUAL:
		ir = POWERVR_IR_BIT_XOR;
		invert = 1;
		break;
	case OP_SHIFT_LEFT_LOGICAL:
		ir = POWERVR_IR_SHIFT_LEFT;
		break;
	case OP_SHIFT_RIGHT_LOGICAL:
		ir = POWERVR_IR_SHIFT_RIGHT;
		break;
	case OP_SHIFT_RIGHT_ARITHMETIC:
		ir = POWERVR_IR_SHIFT_RIGHT_SIGNED;
		break;
	case OP_I_EQUAL:
		ir = POWERVR_IR_INTEGER_EQUAL;
		break;
	case OP_I_NOT_EQUAL:
		ir = POWERVR_IR_INTEGER_NOT_EQUAL;
		break;
	case OP_U_LESS_THAN:
		ir = POWERVR_IR_UNSIGNED_LESS;
		break;
	case OP_U_GREATER_THAN_EQUAL:
		ir = POWERVR_IR_UNSIGNED_GREATER_EQUAL;
		break;
	case OP_U_GREATER_THAN:
		ir = POWERVR_IR_UNSIGNED_LESS;
		swap = 1;
		break;
	case OP_U_LESS_THAN_EQUAL:
		ir = POWERVR_IR_UNSIGNED_GREATER_EQUAL;
		swap = 1;
		break;
	case OP_S_LESS_THAN:
		ir = POWERVR_IR_SIGNED_LESS;
		break;
	case OP_S_GREATER_THAN_EQUAL:
		ir = POWERVR_IR_SIGNED_GREATER_EQUAL;
		break;
	case OP_S_GREATER_THAN:
		ir = POWERVR_IR_SIGNED_LESS;
		swap = 1;
		break;
	case OP_S_LESS_THAN_EQUAL:
		ir = POWERVR_IR_SIGNED_GREATER_EQUAL;
		swap = 1;
		break;
	case OP_SELECT:
		ir = POWERVR_IR_SELECT;
		break;
	case OP_CONVERT_F_TO_S:
		ir = POWERVR_IR_FLOAT_TO_SIGNED;
		break;
	case OP_CONVERT_F_TO_U:
		ir = POWERVR_IR_FLOAT_TO_UNSIGNED;
		break;
	case OP_CONVERT_S_TO_F:
		ir = POWERVR_IR_SIGNED_TO_FLOAT;
		break;
	case OP_CONVERT_U_TO_F:
		ir = POWERVR_IR_UNSIGNED_TO_FLOAT;
		break;
	default:
		/*
		 * The float comparisons, ordered and unordered alike (the
		 * subset does not tell NaNs apart): equal 180-181, not equal
		 * 182-183, less 184-185, greater 186-187, less or equal
		 * 188-189, greater or equal 190-191.
		 */
		if (opcode >= OP_F_ORD_EQUAL && opcode <= OP_F_UNORD_GREATER_THAN_EQUAL) {
			switch ((opcode - OP_F_ORD_EQUAL) / 2U) {
			case 0:
				ir = POWERVR_IR_FLOAT_EQUAL;
				break;
			case 1:
				ir = POWERVR_IR_FLOAT_NOT_EQUAL;
				break;
			case 2:
				ir = POWERVR_IR_FLOAT_LESS;
				break;
			case 3:
				ir = POWERVR_IR_FLOAT_LESS;
				swap = 1;
				break;
			case 4:
				ir = POWERVR_IR_FLOAT_GREATER_EQUAL;
				swap = 1;
				break;
			default:
				ir = POWERVR_IR_FLOAT_GREATER_EQUAL;
				break;
			}
		}
		break;
	}
	if (ir == POWERVR_IR_OPCODE_COUNT) {
		*handled = 0;
		return 0;
	}

	/* The result's words. */
	error = result_begin(reader, words[2], words[1], count, &result);
	if (error != 0)
		return error;

	/* Word by word, a scalar source spread over a vector result. */
	for (index = 0; index < count; index++) {
		if (index >= a_count && a_count != 1)
			return EINVAL;
		x = a[a_count == 1 ? 0 : index];
		if (b != NULL && b_count != 0) {
			y = b[b_count == 1 ? 0 : index];
		} else {
			y = constant_operand(0);
		}
		if (c != NULL && c_count != 0) {
			z = c[c_count == 1 ? 0 : index];
		} else {
			z = constant_operand(0);
		}

		/* The forms that adjust their sources. */
		switch (opcode) {
		case OP_F_SUB:
			y = float_negate(y);
			break;
		case OP_F_DIV:
			y = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL, &y, NULL, NULL);
			break;
		case OP_F_NEGATE:
			result[index] = float_negate(x);
			continue;
		case OP_I_SUB:
			x = plain(reader, x);
			y = plain(reader, y);
			z = constant_operand(TRUE_WORD);
			y = emit_value(reader, POWERVR_IR_INTEGER_MULTIPLY, &y, &z, NULL);
			break;
		case OP_S_NEGATE:
			y = constant_operand(TRUE_WORD);
			break;
		case OP_NOT:
		case OP_LOGICAL_NOT:
			y = constant_operand(TRUE_WORD);
			break;
		default:
			break;
		}

		/* Integer and bit forms read plain words. */
		if (ir != POWERVR_IR_FLOAT_ADD &&
		    ir != POWERVR_IR_FLOAT_MULTIPLY &&
		    ir != POWERVR_IR_FLOAT_LESS &&
		    ir != POWERVR_IR_FLOAT_GREATER_EQUAL &&
		    ir != POWERVR_IR_FLOAT_EQUAL &&
		    ir != POWERVR_IR_FLOAT_NOT_EQUAL &&
		    ir != POWERVR_IR_FLOAT_TO_SIGNED &&
		    ir != POWERVR_IR_FLOAT_TO_UNSIGNED) {
			x = plain(reader, x);
			y = plain(reader, y);
		}
		if (ir == POWERVR_IR_SELECT) {
			x = plain(reader, x);
			y = b[b_count == 1 ? 0 : index];
			z = c[c_count == 1 ? 0 : index];
			y = plain(reader, y);
			z = plain(reader, z);
		}

		/* The operation itself. */
		if (swap != 0) {
			result[index] = emit_value(reader, ir, &y, &x, NULL);
		} else {
			result[index] = emit_value(reader, ir, &x, &y, &z);
		}
		if (invert != 0) {
			y = constant_operand(TRUE_WORD);
			result[index] = emit_value(reader, POWERVR_IR_BIT_XOR, &result[index], &y, NULL);
		}
	}

	/* Reports what the arithmetic came to. */
	return reader->error;
}

/* Reads one GLSL.std.450 instruction, lowered to the intermediate form's operations. */
static int
reader_extended(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct powervr_ir_operand *result;
	struct powervr_ir_operand *a;
	struct powervr_ir_operand *b;
	struct powervr_ir_operand *c;
	struct powervr_ir_operand x;
	struct powervr_ir_operand y;
	struct powervr_ir_operand z;
	struct powervr_ir_operand t;
	struct powervr_ir_operand k;
	struct powervr_ir_operand sum;
	uint32_t instruction;
	uint32_t count;
	uint32_t a_count;
	uint32_t b_count;
	uint32_t c_count;
	uint32_t index;
	int error;

	/* Only GLSL.std.450 is known. */
	if (length < 6 || words[3] != reader->glsl || reader->glsl == 0)
		return ENOTSUP;
	instruction = words[4];
	count = scalar_count(reader, words[1]);
	if (count == 0 || count > COMPOSITE_MAX)
		return ENOTSUP;

	/* The sources. */
	error = id_operands(reader, words[5], &a, &a_count);
	if (error != 0)
		return error;
	b = NULL;
	c = NULL;
	b_count = 0;
	c_count = 0;
	if (length > 6) {
		error = id_operands(reader, words[6], &b, &b_count);
		if (error != 0)
			return error;
	}
	if (length > 7) {
		error = id_operands(reader, words[7], &c, &c_count);
		if (error != 0)
			return error;
	}
	error = result_begin(reader, words[2], words[1], count, &result);
	if (error != 0)
		return error;

	/* The reductions over a vector. */
	if (instruction == GLSL_LENGTH || instruction == GLSL_DISTANCE) {
		/* The root of the sum of squares (of the difference, for a distance). */
		sum = constant_operand(0);
		for (index = 0; index < a_count; index++) {
			x = a[index];
			if (instruction == GLSL_DISTANCE) {
				if (b == NULL || b_count != a_count)
					return EINVAL;
				y = float_negate(b[index]);
				x = emit_value(reader, POWERVR_IR_FLOAT_ADD, &x, &y, NULL);
			}
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &x, &x, &sum);
		}
		x = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL_ROOT, &sum, NULL, NULL);
		result[0] = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL, &x, NULL, NULL);
		return reader->error;
	}
	if (instruction == GLSL_NORMALIZE) {
		sum = constant_operand(0);
		for (index = 0; index < a_count; index++)
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &a[index], &a[index], &sum);
		t = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL_ROOT, &sum, NULL, NULL);
		for (index = 0; index < count; index++)
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &a[index], &t, NULL);
		return reader->error;
	}

	/* Word by word. */
	for (index = 0; index < count; index++) {
		x = a[a_count == 1 ? 0 : index];
		y = constant_operand(0);
		z = constant_operand(0);
		if (b != NULL)
			y = b[b_count == 1 ? 0 : index];
		if (c != NULL)
			z = c[c_count == 1 ? 0 : index];

		switch (instruction) {
		case GLSL_F_ABS:
			result[index] = float_absolute(x);
			break;
		case GLSL_FLOOR:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_FLOOR, &x, NULL, NULL);
			break;
		case GLSL_CEIL:
			/* The negated floor of the negated value. */
			x = float_negate(x);
			t = emit_value(reader, POWERVR_IR_FLOAT_FLOOR, &x, NULL, NULL);
			result[index] = float_negate(t);
			break;
		case GLSL_FRACT:
			t = emit_value(reader, POWERVR_IR_FLOAT_FLOOR, &x, NULL, NULL);
			t = float_negate(t);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_ADD, &x, &t, NULL);
			break;
		case GLSL_ROUND:
			/* The floor of the value plus a half. */
			k = constant_operand(FLOAT_HALF);
			t = emit_value(reader, POWERVR_IR_FLOAT_ADD, &x, &k, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_FLOOR, &t, NULL, NULL);
			break;
		case GLSL_F_SIGN:
			/* One, minus one or zero by two comparisons. */
			k = constant_operand(0);
			t = emit_value(reader, POWERVR_IR_FLOAT_LESS, &k, &x, NULL);
			y = constant_operand(FLOAT_ONE);
			z = constant_operand(0);
			t = emit_value(reader, POWERVR_IR_SELECT, &t, &y, &z);
			k = constant_operand(0);
			sum = emit_value(reader, POWERVR_IR_FLOAT_LESS, &x, &k, NULL);
			y = constant_operand(FLOAT_ONE | FLOAT_SIGN);
			result[index] = emit_value(reader, POWERVR_IR_SELECT, &sum, &y, &t);
			break;
		case GLSL_SIN:
			result[index] = sine(reader, x, 0);
			break;
		case GLSL_COS:
			result[index] = sine(reader, x, 1);
			break;
		case GLSL_EXP2:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_EXP2, &x, NULL, NULL);
			break;
		case GLSL_LOG2:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_LOG2, &x, NULL, NULL);
			break;
		case GLSL_POW:
			t = emit_value(reader, POWERVR_IR_FLOAT_LOG2, &x, NULL, NULL);
			t = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &t, &y, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_EXP2, &t, NULL, NULL);
			break;
		case GLSL_SQRT:
			/* The reciprocal of the reciprocal root, which is right at zero. */
			t = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL_ROOT, &x, NULL, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL, &t, NULL, NULL);
			break;
		case GLSL_INVERSE_SQRT:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL_ROOT, &x, NULL, NULL);
			break;
		case GLSL_F_MIN:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MINIMUM, &x, &y, NULL);
			break;
		case GLSL_F_MAX:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MAXIMUM, &x, &y, NULL);
			break;
		case GLSL_U_MIN:
			x = plain(reader, x);
			y = plain(reader, y);
			result[index] = emit_value(reader, POWERVR_IR_UNSIGNED_MINIMUM, &x, &y, NULL);
			break;
		case GLSL_U_MAX:
			x = plain(reader, x);
			y = plain(reader, y);
			result[index] = emit_value(reader, POWERVR_IR_UNSIGNED_MAXIMUM, &x, &y, NULL);
			break;
		case GLSL_S_MIN:
			x = plain(reader, x);
			y = plain(reader, y);
			result[index] = emit_value(reader, POWERVR_IR_SIGNED_MINIMUM, &x, &y, NULL);
			break;
		case GLSL_S_MAX:
			x = plain(reader, x);
			y = plain(reader, y);
			result[index] = emit_value(reader, POWERVR_IR_SIGNED_MAXIMUM, &x, &y, NULL);
			break;
		case GLSL_F_CLAMP:
			t = emit_value(reader, POWERVR_IR_FLOAT_MAXIMUM, &x, &y, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MINIMUM, &t, &z, NULL);
			break;
		case GLSL_U_CLAMP:
			x = plain(reader, x);
			y = plain(reader, y);
			z = plain(reader, z);
			t = emit_value(reader, POWERVR_IR_UNSIGNED_MAXIMUM, &x, &y, NULL);
			result[index] = emit_value(reader, POWERVR_IR_UNSIGNED_MINIMUM, &t, &z, NULL);
			break;
		case GLSL_S_CLAMP:
			x = plain(reader, x);
			y = plain(reader, y);
			z = plain(reader, z);
			t = emit_value(reader, POWERVR_IR_SIGNED_MAXIMUM, &x, &y, NULL);
			result[index] = emit_value(reader, POWERVR_IR_SIGNED_MINIMUM, &t, &z, NULL);
			break;
		case GLSL_F_MIX:
			/* x + (y - x) * z. */
			t = float_negate(x);
			t = emit_value(reader, POWERVR_IR_FLOAT_ADD, &y, &t, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &t, &z, &x);
			break;
		case GLSL_FMA:
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &x, &y, &z);
			break;
		case GLSL_STEP:
			/* Zero below the edge (x), one from it on. */
			t = emit_value(reader, POWERVR_IR_FLOAT_LESS, &y, &x, NULL);
			k = constant_operand(0);
			sum = constant_operand(FLOAT_ONE);
			result[index] = emit_value(reader, POWERVR_IR_SELECT, &t, &k, &sum);
			break;
		case GLSL_SMOOTH_STEP:
			/* t = clamp((z - x) / (y - x), 0, 1); t * t * (3 - 2 t). */
			t = float_negate(x);
			sum = emit_value(reader, POWERVR_IR_FLOAT_ADD, &y, &t, NULL);
			sum = emit_value(reader, POWERVR_IR_FLOAT_RECIPROCAL, &sum, NULL, NULL);
			t = emit_value(reader, POWERVR_IR_FLOAT_ADD, &z, &t, NULL);
			t = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &t, &sum, NULL);
			k = constant_operand(0);
			t = emit_value(reader, POWERVR_IR_FLOAT_MAXIMUM, &t, &k, NULL);
			k = constant_operand(FLOAT_ONE);
			t = emit_value(reader, POWERVR_IR_FLOAT_MINIMUM, &t, &k, NULL);
			k = constant_operand(FLOAT_TWO | FLOAT_SIGN);
			sum = constant_operand(FLOAT_THREE);
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &t, &k, &sum);
			sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &sum, &t, NULL);
			result[index] = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &sum, &t, NULL);
			break;
		default:
			return ENOTSUP;
		}
	}

	/* Reports what the lowering came to. */
	return reader->error;
}

/*
 * Works out sin(x), or cos(x) with quarter set, by reducing the angle
 * to a turn in [-1/2, 1/2) and summing the odd Taylor terms of
 * sin(2 pi t).
 */
static struct powervr_ir_operand
sine(
	struct shader_spirv_reader *reader,
	struct powervr_ir_operand x,
	uint32_t quarter)
{
	struct powervr_ir_operand turn;
	struct powervr_ir_operand k;
	struct powervr_ir_operand t;
	struct powervr_ir_operand y;
	struct powervr_ir_operand y2;
	struct powervr_ir_operand sum;
	uint32_t index;

	/*
	 * The turn: x / 2 pi, plus a quarter for a cosine and a half to
	 * centre it, less its floor, less the half again.
	 */
	k = constant_operand(0x3e22f983U);	/* 1 / 2 pi */
	turn = constant_operand(quarter != 0 ? 0x3f400000U : FLOAT_HALF);	/* 3/4 or 1/2 */
	t = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &x, &k, &turn);
	y = emit_value(reader, POWERVR_IR_FLOAT_FLOOR, &t, NULL, NULL);
	y = float_negate(y);
	t = emit_value(reader, POWERVR_IR_FLOAT_ADD, &t, &y, NULL);
	k = constant_operand(FLOAT_HALF | FLOAT_SIGN);
	t = emit_value(reader, POWERVR_IR_FLOAT_ADD, &t, &k, NULL);

	/* The angle y = 2 pi t in [-pi, pi) and its square. */
	k = constant_operand(0x40c90fdbU);	/* 2 pi */
	y = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &t, &k, NULL);
	y2 = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &y, &y, NULL);

	/* Horner's rule over y^2, highest term first, then times y. */
	sum = constant_operand(sine_coefficients[0]);
	for (index = 1; index < 8; index++) {
		k = constant_operand(sine_coefficients[index]);
		sum = emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY_ADD, &sum, &y2, &k);
	}
	return emit_value(reader, POWERVR_IR_FLOAT_MULTIPLY, &sum, &y, NULL);
}

/* Reads a sample of a combined image at two coordinates. */
static int
reader_sample(
	struct shader_spirv_reader *reader,
	const uint32_t *words,
	uint32_t length)
{
	struct powervr_ir_operand *coordinates;
	struct powervr_ir_operand *result;
	struct powervr_ir_operand u;
	struct powervr_ir_operand v;
	struct shader_spirv_id *image;
	uint32_t coordinate_count;
	uint32_t image_request;
	uint32_t sampler_request;
	uint32_t sampled;
	uint32_t index;
	int error;

	/* Only plain fragment samples of a two-dimensional image. */
	if (length != 5 || reader->stage != POWERVR_SHADER_FRAGMENT)
		return ENOTSUP;
	if (words[3] >= reader->bound || words[2] >= reader->bound)
		return EINVAL;
	image = &reader->ids[words[3]];
	if (image->kind != ID_SAMPLED_IMAGE)
		return ENOTSUP;
	error = id_operands(reader, words[4], &coordinates, &coordinate_count);
	if (error != 0)
		return error;
	if (coordinate_count != 2)
		return ENOTSUP;

	/* The image and sampler state words of the binding. */
	error = request_add(reader, POWERVR_SHADER_REQUEST_DESCRIPTOR, POWERVR_SHADER_PART_IMAGE_STATE, image->set, image->binding, POWERVR_SHADER_STATE_WORDS, &image_request);
	if (error != 0)
		return error;
	error = request_add(reader, POWERVR_SHADER_REQUEST_DESCRIPTOR, POWERVR_SHADER_PART_SAMPLER_STATE, image->set, image->binding, POWERVR_SHADER_STATE_WORDS, &sampler_request);
	if (error != 0)
		return error;

	/* The sample into a four-word value, then each word of it. */
	u = coordinates[0];
	v = coordinates[1];
	sampled = new_value(reader, 4);
	emit(reader, POWERVR_IR_SAMPLE, sampled, &u, &v, NULL, image_request, sampler_request);
	error = result_begin(reader, words[2], words[1], 4, &result);
	if (error != 0)
		return error;
	for (index = 0; index < 4; index++)
		result[index] = value_operand(sampled, index);

	/* Reports what the sample came to. */
	return reader->error;
}

/* Counts the 32-bit words a type flattens to. */
static uint32_t
scalar_count(
	struct shader_spirv_reader *reader,
	uint32_t type)
{
	/* An unknown id has no words. */
	if (type >= reader->bound)
		return 0;
	return reader->ids[type].scalars;
}

/* Works out a type's size in buffer memory, from its words and strides. */
static uint32_t
byte_size(
	struct shader_spirv_reader *reader,
	uint32_t type)
{
	struct shader_spirv_id *id;

	/* An array with a stride is its stride times its length; anything else four bytes a word. */
	if (type >= reader->bound)
		return 0;
	id = &reader->ids[type];
	if (id->type_kind == TYPE_ARRAY && id->stride != 0)
		return id->stride * id->count;
	return id->scalars * 4U;
}

/* Finds where a block's label is. */
static int
find_block(
	struct shader_spirv_reader *reader,
	uint32_t label,
	uint32_t *at)
{
	/* The first pass noted it. */
	if (label >= reader->bound || reader->ids[label].kind != ID_LABEL)
		return EINVAL;
	*at = reader->ids[label].word;
	return 0;
}

/* Makes a new value of a width, or notes that there is no room. */
static uint32_t
new_value(
	struct shader_spirv_reader *reader,
	uint32_t width)
{
	struct powervr_ir_shader *shader;
	uint32_t value;

	/* Refuses a shader with more values than the table holds. */
	shader = reader->shader;
	if (shader->value_count >= POWERVR_IR_VALUES) {
		if (reader->error == 0)
			reader->error = ENOTSUP;
		return 0;
	}

	/* The next value. */
	value = shader->value_count++;
	shader->values[value].width = (uint8_t)width;
	return value;
}

/* Appends one instruction, or notes that there is no room. */
static void
emit(
	struct shader_spirv_reader *reader,
	uint32_t opcode,
	uint32_t destination,
	const struct powervr_ir_operand *a,
	const struct powervr_ir_operand *b,
	const struct powervr_ir_operand *c,
	uint32_t immediate,
	uint32_t immediate_2)
{
	struct powervr_ir_instruction *instruction;
	struct powervr_ir_shader *shader;

	/* Refuses a shader with more instructions than the table holds. */
	shader = reader->shader;
	if (shader->instruction_count >= POWERVR_IR_INSTRUCTIONS) {
		if (reader->error == 0)
			reader->error = ENOTSUP;
		return;
	}

	/* Fills in the instruction. */
	instruction = &shader->instructions[shader->instruction_count++];
	instruction->opcode = (uint8_t)opcode;
	instruction->flags = 0;
	instruction->destination = (uint16_t)destination;
	instruction->source[0].kind = POWERVR_IR_OPERAND_NONE;
	instruction->source[1].kind = POWERVR_IR_OPERAND_NONE;
	instruction->source[2].kind = POWERVR_IR_OPERAND_NONE;
	instruction->source[3].kind = POWERVR_IR_OPERAND_NONE;
	if (a != NULL)
		instruction->source[0] = *a;
	if (b != NULL)
		instruction->source[1] = *b;
	if (c != NULL)
		instruction->source[2] = *c;
	instruction->immediate = immediate;
	instruction->immediate_2 = immediate_2;
}

/* Appends one instruction writing a new one-word value, and reports an operand reading it. */
static struct powervr_ir_operand
emit_value(
	struct shader_spirv_reader *reader,
	uint32_t opcode,
	const struct powervr_ir_operand *a,
	const struct powervr_ir_operand *b,
	const struct powervr_ir_operand *c)
{
	uint32_t value;

	/* A new value and the instruction that writes it. */
	value = new_value(reader, 1);
	emit(reader, opcode, value, a, b, c, 0, 0);
	return value_operand(value, 0);
}

/* Makes an operand of a constant word. */
static struct powervr_ir_operand
constant_operand(
	uint32_t bits)
{
	struct powervr_ir_operand operand;

	/* A constant with no modifiers. */
	operand.kind = POWERVR_IR_OPERAND_CONSTANT;
	operand.modifiers = 0;
	operand.component = 0;
	operand.value = 0;
	operand.constant = bits;
	return operand;
}

/* Makes an operand reading one word of a value. */
static struct powervr_ir_operand
value_operand(
	uint32_t value,
	uint32_t component)
{
	struct powervr_ir_operand operand;

	/* A value's word with no modifiers. */
	operand.kind = POWERVR_IR_OPERAND_VALUE;
	operand.modifiers = 0;
	operand.component = (uint8_t)component;
	operand.value = (uint16_t)value;
	operand.constant = 0;
	return operand;
}

/*
 * Makes an operand without float modifiers, for an integer or bit
 * operation: a constant has them applied, a value is copied through a
 * move that applies them.
 */
static struct powervr_ir_operand
plain(
	struct shader_spirv_reader *reader,
	struct powervr_ir_operand operand)
{
	struct powervr_ir_operand result;

	/* An operand without modifiers is already plain. */
	if (operand.modifiers == 0)
		return operand;

	/* A constant takes the modifiers into its bits. */
	if (operand.kind == POWERVR_IR_OPERAND_CONSTANT) {
		if ((operand.modifiers & POWERVR_IR_ABSOLUTE) != 0)
			operand.constant &= ~FLOAT_SIGN;
		if ((operand.modifiers & POWERVR_IR_NEGATE) != 0)
			operand.constant ^= FLOAT_SIGN;
		operand.modifiers = 0;
		return operand;
	}

	/* A value goes through a move. */
	result = emit_value(reader, POWERVR_IR_MOVE, &operand, NULL, NULL);
	return result;
}

/* Negates an operand as a float. */
static struct powervr_ir_operand
float_negate(
	struct powervr_ir_operand operand)
{
	/* A constant flips its sign; a value toggles its negate modifier. */
	if (operand.kind == POWERVR_IR_OPERAND_CONSTANT) {
		operand.constant ^= FLOAT_SIGN;
		return operand;
	}
	operand.modifiers ^= POWERVR_IR_NEGATE;
	return operand;
}

/* Takes the absolute value of an operand as a float. */
static struct powervr_ir_operand
float_absolute(
	struct powervr_ir_operand operand)
{
	/* A constant clears its sign; a value takes the absolute modifier, dropping any negate. */
	if (operand.kind == POWERVR_IR_OPERAND_CONSTANT) {
		operand.constant &= ~FLOAT_SIGN;
		return operand;
	}
	operand.modifiers = POWERVR_IR_ABSOLUTE;
	return operand;
}

/* Reads one word of a shared register request into a value. */
static struct powervr_ir_operand
shared_word(
	struct shader_spirv_reader *reader,
	uint32_t request,
	uint32_t word)
{
	uint32_t value;

	/* The word as a new value, read where it is used. */
	value = new_value(reader, 1);
	emit(reader, POWERVR_IR_SHARED, value, NULL, NULL, NULL, request, word);
	return value_operand(value, 0);
}

/*
 * Finds or adds a shared register request.  A request for the same
 * thing again is the same request.
 */
static int
request_add(
	struct shader_spirv_reader *reader,
	uint32_t kind,
	uint32_t part,
	uint32_t set,
	uint32_t binding,
	uint32_t words,
	uint32_t *index)
{
	struct powervr_shader_program *program;
	struct powervr_shader_request *request;
	uint32_t at;

	/* An existing request for the same thing. */
	program = reader->program;
	for (at = 0; at < program->request_count; at++) {
		request = &program->requests[at];
		if (request->kind == kind &&
		    request->part == part &&
		    request->set == set &&
		    request->binding == binding) {
			*index = at;
			return 0;
		}
	}

	/* A new one, if the table has room. */
	if (program->request_count >= POWERVR_SHADER_REQUESTS)
		return ENOTSUP;
	request = &program->requests[program->request_count];
	request->kind = (uint8_t)kind;
	request->part = (uint8_t)part;
	request->words = (uint8_t)words;
	request->set = set;
	request->binding = binding;
	request->array_index = 0;
	request->offset = 0;
	request->literal = 0;
	*index = program->request_count++;
	return 0;
}

/* Gives a result id room for its words in the operand pool. */
static int
result_begin(
	struct shader_spirv_reader *reader,
	uint32_t result,
	uint32_t type,
	uint32_t count,
	struct powervr_ir_operand **operands)
{
	struct shader_spirv_id *id;

	/* Refuses an id out of range or a pool without room. */
	if (result >= reader->bound)
		return EINVAL;
	if (reader->operand_count + count > SHADER_SPIRV_OPERANDS)
		return ENOTSUP;

	/* The result's words, at the pool's end. */
	id = &reader->ids[result];
	id->kind = ID_VALUE;
	id->type = type;
	id->first = reader->operand_count;
	id->count = count;
	reader->operand_count += count;
	*operands = &reader->operands[id->first];
	return 0;
}

/* Finds an id's words. */
static int
id_operands(
	struct shader_spirv_reader *reader,
	uint32_t id,
	struct powervr_ir_operand **operands,
	uint32_t *count)
{
	struct shader_spirv_id *entry;

	/* Only constants and computed results have words. */
	if (id >= reader->bound)
		return EINVAL;
	entry = &reader->ids[id];
	if (entry->kind != ID_VALUE && entry->kind != ID_CONSTANT)
		return EINVAL;
	*operands = &reader->operands[entry->first];
	*count = entry->count;
	return 0;
}

/* Finds one word of an id. */
static int
operand_word(
	struct shader_spirv_reader *reader,
	uint32_t id,
	uint32_t component,
	struct powervr_ir_operand *operand)
{
	struct powervr_ir_operand *operands;
	uint32_t count;
	int error;

	/* The id's words, and the one asked for. */
	error = id_operands(reader, id, &operands, &count);
	if (error != 0)
		return error;
	if (component >= count)
		return EINVAL;
	*operand = operands[component];
	return 0;
}

/* Works out the first word of a composite's part named by literal indices. */
static int
composite_offset(
	struct shader_spirv_reader *reader,
	uint32_t type,
	const uint32_t *indices,
	uint32_t count,
	uint32_t *offset,
	uint32_t *member_type)
{
	struct shader_spirv_member *member;
	struct shader_spirv_id *id;
	uint32_t at;

	/* Steps down through each index. */
	*offset = 0;
	for (at = 0; at < count; at++) {
		if (type >= reader->bound)
			return EINVAL;
		id = &reader->ids[type];
		if (id->type_kind == TYPE_STRUCT) {
			if (indices[at] >= id->count)
				return EINVAL;
			member = &reader->members[id->first + indices[at]];
			*offset += member->scalar_offset;
			type = member->type;
		} else if (id->type_kind == TYPE_VECTOR || id->type_kind == TYPE_MATRIX || id->type_kind == TYPE_ARRAY) {
			if (indices[at] >= id->count)
				return EINVAL;
			*offset += indices[at] * scalar_count(reader, id->type);
			type = id->type;
		} else {
			return EINVAL;
		}
	}

	/* Succeeded: the part's first word and its type. */
	*member_type = type;
	return 0;
}
