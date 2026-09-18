/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the baseline shader compiler (p006). Parses the vkdemo
 * shaders, lowers them to Gen12 GEN code and checks the binary is non-empty,
 * one register per value, and carries the expected instruction kinds. Semantic
 * correctness on hardware is the big-bang test; this verifies the lowering.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fixture_live;

void *
kern_calloc(size_t count, size_t size)
{
	void *pointer = calloc(count, size);
	if (pointer != NULL)
		fixture_live++;
	return pointer;
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		fixture_live--;
	free(pointer);
}

#include "../../../src/drivers/gpu/i915/vk/spirv.c"
#include "../../../src/drivers/gpu/i915/vk/eu.c"
#include "../../../src/drivers/gpu/i915/vk/compile.c"

static uint32_t *
load_spv(const char *name, size_t *words)
{
	char path[512];
	FILE *file;
	long size;
	uint32_t *code;

	snprintf(path, sizeof(path), "%s/userland/base/vkdemo/shaders/%s", VK_REPO, name);
	file = fopen(path, "rb");
	assert(file != NULL);
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	code = malloc((size_t)size);
	assert(fread(code, 1, (size_t)size, file) == (size_t)size);
	fclose(file);
	*words = (size_t)size / 4U;
	return code;
}

/* Reports whether any instruction in the code carries the given hw opcode. */
static int
has_opcode(const struct i915_vk_shader_binary *binary, uint32_t opcode)
{
	uint32_t dwords;
	uint32_t index;

	dwords = binary->code_bytes / 4U;
	for (index = 0U; index + 4U <= dwords; index += 4U) {
		if ((binary->code[index] & 0x7FU) == opcode)
			return 1;
	}
	return 0;
}

static void
compile_shader(const char *name, enum i915_vk_stage stage, uint32_t expect_math)
{
	uint32_t *spv;
	size_t words;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;
	int error;

	struct i915_vk_spirv_diag diag;

	spv = load_spv(name, &words);
	error = i915_vk_spirv_parse_diag(spv, words, stage, &ir, &diag);
	if (error != 0)
		printf("  %s refused: opcode %u at word %u: %s\n", name, diag.opcode, diag.word_offset, diag.reason != NULL ? diag.reason : "-");
	assert(error == 0);

	error = i915_vk_compile(NULL, ir, &binary);
	assert(error == 0);

	/* The binary carries whole instructions, more than the payload registers. */
	assert(binary->code_bytes > 0U);
	assert((binary->code_bytes % (4U * 4U)) == 0U);
	assert(binary->stage == stage);
	assert(binary->grf_used > COMPILE_FIRST_VALUE_GRF);

	/* Every shader ends by sending its output and retiring the thread. */
	assert(has_opcode(binary, EU_OP_SEND));

	/* The vertex shader's rotation lowers to math instructions. */
	if (expect_math != 0U)
		assert(has_opcode(binary, EU_OP_MATH));

	i915_vk_shader_binary_free(binary);
	i915_vk_spirv_free(ir);
	free(spv);
}

/* bit `position` of a 128-bit EU instruction */
static unsigned
inst_bit(const uint32_t *inst, unsigned position)
{
	return (inst[position / 32U] >> (position % 32U)) & 1U;
}

static unsigned
inst_field(const uint32_t *inst, unsigned high, unsigned low)
{
	unsigned value = 0U, position;

	for (position = low; position <= high; position++)
		value |= inst_bit(inst, position) << (position - low);
	return value;
}

static struct i915_vk_shader_ir *
hand_ir(struct i915_vk_inst *insts, unsigned count)
{
	static struct i915_vk_shader_ir ir;

	memset(&ir, 0, sizeof(ir));
	ir.stage = I915_VK_STAGE_FRAGMENT;
	ir.instructions = insts;
	ir.instruction_count = count;
	ir.value_count = 16U;
	ir.push_bytes = 8U;
	return &ir;
}

/*
 * a - b is not a + b and not b - a.  The EU has no SUB used here: the lowering is an ADD whose
 * SECOND source carries the negate modifier.  5 - 2 = 3 and 2 - 5 = -3 differ exactly in which
 * operand is negated, so the check is on operand numbers and modifiers, not on an opcode name.
 */
static void
test_fsub_is_add_with_second_source_negated(void)
{
	struct i915_vk_inst insts[3];
	struct i915_vk_shader_binary *binary;
	const uint32_t *inst;
	unsigned src0_nr, src1_nr;
	int error;

	/* two loads first, so %1 and %2 own known registers whatever order the operands are visited in */
	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_LOAD_INPUT; insts[0].dst = 1U;
	insts[1].op = I915_VK_IR_LOAD_INPUT; insts[1].dst = 2U;
	insts[2].op = I915_VK_IR_FSUB;
	insts[2].dst = 3U; insts[2].src[0] = 1U; insts[2].src[1] = 2U;      /* %3 = %1 - %2 */
	error = i915_vk_compile(NULL, hand_ir(insts, 3U), &binary);
	assert(error == 0);
	inst = binary->code + 2U * 4U;
	assert((inst[0] & 0x7FU) == EU_OP_ADD);
	src0_nr = inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO);
	src1_nr = inst_field(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO);
	/* %1 -> first value register, %2 -> the next: the minuend is source 0, the subtrahend source 1 */
	assert(src0_nr == COMPILE_FIRST_VALUE_GRF && src1_nr == COMPILE_FIRST_VALUE_GRF + 1U);
	assert(inst_bit(inst, EU_SRC0_NEGATE_BIT) == 0U);                  /* the minuend is read as is */
	assert(inst_bit(inst, EU_SRC1_NEGATE_BIT) == 1U);                  /* the subtrahend is negated */
	i915_vk_shader_binary_free(binary);

	/* and an ADD stays an ADD with no modifier */
	insts[2].op = I915_VK_IR_FADD;
	error = i915_vk_compile(NULL, hand_ir(insts, 3U), &binary);
	assert(error == 0);
	inst = binary->code + 2U * 4U;
	assert((inst[0] & 0x7FU) == EU_OP_ADD);
	assert(inst_bit(inst, EU_SRC0_NEGATE_BIT) == 0U && inst_bit(inst, EU_SRC1_NEGATE_BIT) == 0U);
	i915_vk_shader_binary_free(binary);
}

/* -a is a MOV whose source is negated; a push constant is one float read as a scalar region */
static void
test_fneg_and_push_operands(void)
{
	struct i915_vk_inst insts[3];
	struct i915_vk_shader_binary *binary;
	const uint32_t *inst;
	int error;

	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_LOAD_PUSH; insts[0].dst = 1U; insts[0].immediate = 4U;   /* the SECOND float */
	insts[1].op = I915_VK_IR_FNEG; insts[1].dst = 2U; insts[1].src[0] = 1U;
	insts[2].op = I915_VK_IR_STORE_OUTPUT; insts[2].src[0] = 2U; insts[2].location = 0U; insts[2].component = 3U;
	error = i915_vk_compile(NULL, hand_ir(insts, 3U), &binary);
	assert(error == 0);
	inst = binary->code;                                            /* MOV r16 <- r2.4<0;1,0> */
	assert((inst[0] & 0x7FU) == EU_OP_MOV);
	assert(inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == COMPILE_PAYLOAD_GRF);
	assert(inst_field(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO) == 4U);
	assert(inst_field(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO) == EU_VSTRIDE_0);
	assert(inst_field(inst, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO) == EU_WIDTH_1);
	assert(inst_field(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO) == EU_HSTRIDE_0);
	inst = binary->code + 4U;                                       /* MOV r17 <- -r16 */
	assert((inst[0] & 0x7FU) == EU_OP_MOV);
	assert(inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF);
	assert(inst_bit(inst, EU_SRC0_NEGATE_BIT) == 1U);
	assert(inst_field(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF + 1U);
	inst = binary->code + 8U;                                       /* MOV out slot 0 component 3 <- r17 */
	assert(inst_field(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == COMPILE_OUTPUT_GRF + 3U);
	assert(inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF + 1U);
	assert(inst_bit(inst, EU_SRC0_NEGATE_BIT) == 0U);
	i915_vk_shader_binary_free(binary);
}

/* a register is reused only after the LAST reader of its value */
static void
test_register_lifetime(void)
{
	struct i915_vk_inst insts[6];
	struct i915_vk_shader_binary *binary;
	const uint32_t *inst;
	int error;

	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_LOAD_INPUT; insts[0].dst = 1U;                                   /* r16, read at 2 and 4 */
	insts[1].op = I915_VK_IR_LOAD_INPUT; insts[1].dst = 2U; insts[1].component = 1U;          /* r17, last read at 2 */
	insts[2].op = I915_VK_IR_FMUL; insts[2].dst = 3U; insts[2].src[0] = 1U; insts[2].src[1] = 2U;  /* r18 */
	insts[3].op = I915_VK_IR_CONST; insts[3].dst = 4U; insts[3].immediate = 0x40000000U;      /* takes r17 (free), NOT r16 */
	insts[4].op = I915_VK_IR_FSUB; insts[4].dst = 5U; insts[4].src[0] = 1U; insts[4].src[1] = 4U;
	insts[5].op = I915_VK_IR_STORE_OUTPUT; insts[5].src[0] = 5U;
	error = i915_vk_compile(NULL, hand_ir(insts, 6U), &binary);
	assert(error == 0);
	inst = binary->code + 3U * 4U;
	assert(inst_field(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF + 1U);
	inst = binary->code + 4U * 4U;
	assert(inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF);       /* %1 still intact */
	assert(inst_field(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO) == COMPILE_FIRST_VALUE_GRF + 1U);
	assert(inst_bit(inst, EU_SRC1_NEGATE_BIT) == 1U);
	i915_vk_shader_binary_free(binary);
}

/* an unknown IR operation, a value read before its definition, a push constant outside the block: no binary */
static void
test_not_lowered_ir_is_refused(void)
{
	struct i915_vk_inst insts[1];
	struct i915_vk_shader_binary *binary;
	int error;

	memset(insts, 0, sizeof(insts));
	insts[0].op = (enum i915_vk_ir_op)0x7fff;
	insts[0].dst = 3U; insts[0].src[0] = 1U; insts[0].src[1] = 2U;
	binary = (struct i915_vk_shader_binary *)1;
	error = i915_vk_compile(NULL, hand_ir(insts, 1U), &binary);
	assert(error == ENOTSUP && binary == NULL);

	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_FADD; insts[0].dst = 3U; insts[0].src[0] = 1U; insts[0].src[1] = 2U;
	error = i915_vk_compile(NULL, hand_ir(insts, 1U), &binary);
	assert(error == EINVAL && binary == NULL);

	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_LOAD_PUSH; insts[0].dst = 3U; insts[0].immediate = 8U;   /* push_bytes is 8 */
	error = i915_vk_compile(NULL, hand_ir(insts, 1U), &binary);
	assert(error == EINVAL && binary == NULL);

	/* more outputs than staging registers */
	memset(insts, 0, sizeof(insts));
	insts[0].op = I915_VK_IR_STORE_OUTPUT; insts[0].location = 4U;
	error = i915_vk_compile(NULL, hand_ir(insts, 1U), &binary);
	assert(error == ENOTSUP && binary == NULL);
}

/*
 * EU generation for the shipped vertex shader: one EU instruction per IR instruction plus the
 * terminator, the transcendentals as MATH.  This is the "EU generated" stage: it says nothing
 * about the payload / URB conventions, which are not verified on hardware.
 */
static void
test_vertex_shader_generates_eu(void)
{
	uint32_t *spv;
	size_t words;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;

	spv = load_spv("cuboid.vert.spv", &words);
	assert(i915_vk_spirv_parse(spv, words, I915_VK_STAGE_VERTEX, &ir) == 0);
	assert(i915_vk_compile(NULL, ir, &binary) == 0);
	assert(binary->code_bytes == (ir->instruction_count + 1U) * 16U);
	assert(binary->grf_used > COMPILE_FIRST_VALUE_GRF && binary->grf_used <= COMPILE_OUTPUT_GRF);
	printf("  cuboid.vert.spv: %u IR instructions -> %u EU instructions, value registers r%u..r%u\n",
		ir->instruction_count, binary->code_bytes / 16U, COMPILE_FIRST_VALUE_GRF, binary->grf_used - 1U);
	i915_vk_shader_binary_free(binary);
	i915_vk_spirv_free(ir);
	free(spv);
}

/*
 * A model of what the generated EU words compute, for the instructions this compiler emits:
 * 8 SIMD channels, a register = 8 floats (32 bytes), a <8;8,1> region reads channel c from
 * float c, a <0;1,0> region reads the one float at byte `subnr` for every channel, an
 * immediate is the same for every channel.  The model decodes the ENCODED words (bit fields
 * from the transcribed encoding table), so an operand in the wrong slot, a missing negate,
 * a register reused while its value is still needed, or a push constant read as a vector all
 * change the result.  It is a model of the instruction semantics only; it says nothing about
 * how the hardware fills the payload or consumes the staged outputs.
 */
struct eu_model {
	float grf[128][8];
};

static float
eu_model_source(const struct eu_model *m, const uint32_t *inst, int which, unsigned channel)
{
	unsigned file, nr, subnr, vstride, width, hstride, negate;
	float value;

	if (which == 0) {
		file = inst_bit(inst, EU_SRC0_REG_FILE_LO_BIT) | (inst_bit(inst, EU_SRC0_REG_FILE_HI_BIT) << 1);
		nr = inst_field(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO);
		subnr = inst_field(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO);
		vstride = inst_field(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO);
		width = inst_field(inst, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO);
		hstride = inst_field(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO);
		negate = inst_bit(inst, EU_SRC0_NEGATE_BIT);
		assert(inst_field(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO) == EU_TYPE_F);
	} else {
		file = inst_bit(inst, EU_SRC1_REG_FILE_LO_BIT) | (inst_bit(inst, EU_SRC1_REG_FILE_HI_BIT) << 1);
		nr = inst_field(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO);
		subnr = inst_field(inst, EU_SRC1_SUBREG_HI, EU_SRC1_SUBREG_LO);
		vstride = inst_field(inst, EU_SRC1_VSTRIDE_HI, EU_SRC1_VSTRIDE_LO);
		width = inst_field(inst, EU_SRC1_WIDTH_HI, EU_SRC1_WIDTH_LO);
		hstride = inst_field(inst, EU_SRC1_HSTRIDE_HI, EU_SRC1_HSTRIDE_LO);
		negate = inst_bit(inst, EU_SRC1_NEGATE_BIT);
		assert(inst_field(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO) == EU_TYPE_F);
	}
	if (file == EU_FILE_IMM) {
		memcpy(&value, &inst[3], sizeof(value));
		return value;
	}
	assert(file == EU_FILE_GRF && nr < 128U);
	if (vstride == EU_VSTRIDE_8 && width == EU_WIDTH_8 && hstride == EU_HSTRIDE_1) {
		assert(subnr == 0U);
		value = m->grf[nr][channel];
	} else {
		assert(vstride == EU_VSTRIDE_0 && width == EU_WIDTH_1 && hstride == EU_HSTRIDE_0 && (subnr % 4U) == 0U);
		value = m->grf[nr][subnr / 4U];
	}
	return negate != 0U ? -value : value;
}

/* Runs the words up to the terminating SEND; any other instruction is a test failure. */
static void
eu_model_run(struct eu_model *m, const struct i915_vk_shader_binary *binary)
{
	unsigned count = binary->code_bytes / 16U, index, channel;

	for (index = 0U; index < count; index++) {
		const uint32_t *inst = binary->code + index * 4U;
		unsigned opcode = inst_field(inst, EU_OPCODE_HI, EU_OPCODE_LO);
		unsigned dst = inst_field(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO);
		float result[8];

		if (opcode == EU_OP_SEND) {
			assert(index == count - 1U);            /* only the terminator in the shaders run here */
			return;
		}
		assert(inst_field(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO) == EU_EXEC_SIZE_8);
		assert(inst_bit(inst, EU_DST_REG_FILE_BIT) == 1U && dst < 128U);
		assert(inst_field(inst, EU_DST_SUBREG_HI, EU_DST_SUBREG_LO) == 0U);
		for (channel = 0U; channel < 8U; channel++) {
			float a = eu_model_source(m, inst, 0, channel);

			if (opcode == EU_OP_MOV) {
				result[channel] = a;
			} else if (opcode == EU_OP_ADD) {
				result[channel] = a + eu_model_source(m, inst, 1, channel);
			} else if (opcode == EU_OP_MUL) {
				result[channel] = a * eu_model_source(m, inst, 1, channel);
			} else if (opcode == EU_OP_MATH) {
				unsigned function = inst_field(inst, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO);

				assert(function == EU_MATH_SIN || function == EU_MATH_COS || function == EU_MATH_RSQ);
				result[channel] = function == EU_MATH_SIN ? sinf(a) : function == EU_MATH_COS ? cosf(a) : 1.0f / sqrtf(a);
			} else {
				assert(!"EU opcode the model does not know");
			}
		}
		memcpy(m->grf[dst], result, sizeof(result));
	}
	assert(!"no terminating SEND");
}

/*
 * The shipped vertex shader through BOTH stages (SPIR-V -> IR -> EU words), eight different
 * vertices in the eight channels at once, against the GLSL source evaluated here in C.
 */
static void
test_vertex_shader_eu_computes_the_shader(void)
{
	static const float vertices[8][5] = {
		{ 0.5f, -1.25f, 2.0f, 0.25f, 0.75f }, { -1.0f, 1.0f, -1.0f, 0.0f, 1.0f },
		{ 1.0f, 0.5f, 0.75f, 1.0f, 0.0f }, { -0.3f, -0.7f, 1.9f, 0.125f, 0.625f },
		{ 2.0f, 3.0f, 5.0f, 0.5f, 0.25f }, { -7.0f, 0.1f, 0.2f, 0.75f, 0.5f },
		{ 0.0f, 0.0f, 1.0f, 0.375f, 0.875f }, { 1.5f, -2.5f, -3.5f, 0.0625f, 0.9375f },
	};
	const float seconds = 1.7f;
	const unsigned in0 = COMPILE_PAYLOAD_GRF + 1U;          /* after one register of push constants */
	uint32_t *spv;
	size_t words;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;
	static struct eu_model m;
	unsigned c, k, r;

	spv = load_spv("cuboid.vert.spv", &words);
	assert(i915_vk_spirv_parse(spv, words, I915_VK_STAGE_VERTEX, &ir) == 0);
	assert(i915_vk_compile(NULL, ir, &binary) == 0);

	/* every register starts as junk that differs per channel, so an unset read cannot look right */
	for (r = 0U; r < 128U; r++)
		for (c = 0U; c < 8U; c++)
			m.grf[r][c] = -1000.0f - (float)(r * 8U + c);
	m.grf[COMPILE_PAYLOAD_GRF][0] = seconds;                /* the other seven floats of the push register stay junk */
	for (c = 0U; c < 8U; c++) {
		for (k = 0U; k < 3U; k++)
			m.grf[in0 + k][c] = vertices[c][k];
		m.grf[in0 + 4U][c] = vertices[c][3];
		m.grf[in0 + 5U][c] = vertices[c][4];
	}
	eu_model_run(&m, binary);

	for (c = 0U; c < 8U; c++) {
		const float *p = vertices[c];
		float angle_x = 0.30f + 0.43f * seconds, angle_y = 0.40f + 0.70f * seconds;
		float sx = sinf(angle_x), cx = cosf(angle_x), sy = sinf(angle_y), cy = cosf(angle_y);
		float rx = p[0], ry = cx * p[1] - sx * p[2], rz = sx * p[1] + cx * p[2];
		float vx = cy * rx + sy * rz, vy = ry, vz = -sy * rx + cy * rz + 3.0f;
		float want[6];

		want[0] = 1.2f * vx;
		want[1] = -1.6f * vy;
		want[2] = (10.0f / 9.9f) * vz - (1.0f / 9.9f);
		want[3] = vz;
		want[4] = p[3];
		want[5] = p[4];
		for (k = 0U; k < 6U; k++) {
			float got = m.grf[COMPILE_OUTPUT_GRF + k][c];
			float scale = fabsf(want[k]) > 1.0f ? fabsf(want[k]) : 1.0f;

			if (fabsf(got - want[k]) > 2e-6f * scale) {
				printf("  channel %u staged output %u = %.9g want %.9g\n", c, k, (double)got, (double)want[k]);
				assert(!"EU model result differs from the GLSL source");
			}
		}
	}
	printf("  cuboid.vert.spv: the generated EU words compute gl_Position / texture_coordinate of the GLSL source for 8 vertices (instruction-semantics model, not hardware)\n");
	i915_vk_shader_binary_free(binary);
	i915_vk_spirv_free(ir);
	free(spv);
}

int
main(void)
{
	compile_shader("cuboid.frag.spv", I915_VK_STAGE_FRAGMENT, 0U);
	compile_shader("cuboid.vert.spv", I915_VK_STAGE_VERTEX, 1U);
	test_vertex_shader_generates_eu();
	test_vertex_shader_eu_computes_the_shader();
	test_fneg_and_push_operands();
	test_register_lifetime();
	test_fsub_is_add_with_second_source_negated();
	test_not_lowered_ir_is_refused();
	assert(fixture_live == 0U);
	printf("i915 vk compile host test PASS\n");
	return 0;
}
