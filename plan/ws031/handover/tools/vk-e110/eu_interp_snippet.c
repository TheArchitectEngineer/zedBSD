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

