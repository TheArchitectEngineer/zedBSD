#!/usr/bin/env python3
"""WS031 VK-2 base lowering: the existing host fixtures on the scalar IR.  usage: patch_vk_tests.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/plan/ws031/tests/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)
def cut(s, start, end, new):
    a = s.index(start); b = s.index(end, a)
    return s[:a] + new + s[b:]

# ---------------------------------------------------------------- spirv test
t = load("i915-vk-spirv-test.c")
t = cut(t, "/*\n * The vkdemo vertex shader (glslc -O0) keeps its intermediate values", "/* a minimal module:", """/*
 * The vkdemo vertex shader (glslc -O0, as shipped) keeps its intermediate values in
 * Function-storage variables and uses float constants, OpFNegate, component access and
 * 3/4-element composites.  It parses now; what the IR COMPUTES is checked by the lowering
 * fixture (i915-vk-lower-test.c), here only the interface and the shape of the stream.
 */
static void
test_vertex(void)
{
	uint32_t *code;
	size_t words;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_spirv_diag diag;
	int error;

	code = load_spv("cuboid.vert.spv", &words);
	error = i915_vk_spirv_parse_diag(code, words, I915_VK_STAGE_VERTEX, &ir, &diag);
	if (error != 0)
		printf("  vkdemo VS refused: opcode %u at word %u: %s\\n", diag.opcode, diag.word_offset, diag.reason);
	assert(error == 0 && ir != NULL);
	assert(ir->stage == I915_VK_STAGE_VERTEX);
	assert(ir->input_count == 2U && ir->output_count == 1U && ir->uniform_count == 0U);
	assert(ir->inputs[0].location == 0U && ir->inputs[0].components == 3U);
	assert(ir->inputs[1].location == 1U && ir->inputs[1].components == 2U);
	assert(ir->outputs[0].location == 0U && ir->outputs[0].components == 2U);
	assert(ir->push_bytes == 4U);                                   /* one float: animation.seconds */
	assert(count_op(ir, I915_VK_IR_SIN) == 2U && count_op(ir, I915_VK_IR_COS) == 2U);
	assert(count_op(ir, I915_VK_IR_FNEG) == 1U);                    /* -sy; the -1.6 is a constant */
	assert(count_op(ir, I915_VK_IR_LOAD_PUSH) == 2U);               /* seconds is read twice */
	assert(count_op(ir, I915_VK_IR_STORE_OUTPUT) == 6U);            /* gl_Position 4 + texture_coordinate 2 */
	i915_vk_spirv_free(ir);
	free(code);
}

""")
t = rep(t, "	/* OpFNegate %6 = -%7 */", "	/* OpFNegate %6 = -%7, where %7 is not a float value (nothing defines it): refused, not skipped */")
t = rep(t, "	test_vertex_is_refused_not_approximated();\n", "	test_vertex();\n")
save("i915-vk-spirv-test.c", t)

# ---------------------------------------------------------------- compile test
t = load("i915-vk-compile-test.c")
t = rep(t, "	ir.value_count = 16U;\n", "	ir.value_count = 16U;\n	ir.push_bytes = 8U;\n")
t = cut(t, "/* operations that used to come out as a DIFFERENT operation are refused", "int\nmain(void)", """/* -a is a MOV whose source is negated; a push constant is one float read as a scalar region */
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
	printf("  cuboid.vert.spv: %u IR instructions -> %u EU instructions, value registers r%u..r%u\\n",
		ir->instruction_count, binary->code_bytes / 16U, COMPILE_FIRST_VALUE_GRF, binary->grf_used - 1U);
	i915_vk_shader_binary_free(binary);
	i915_vk_spirv_free(ir);
	free(spv);
}

""")
t = rep(t, """	compile_shader("cuboid.frag.spv", I915_VK_STAGE_FRAGMENT, 0U);
	test_vertex_shader_does_not_reach_the_compiler();
""", """	compile_shader("cuboid.frag.spv", I915_VK_STAGE_FRAGMENT, 0U);
	compile_shader("cuboid.vert.spv", I915_VK_STAGE_VERTEX, 1U);
	test_vertex_shader_generates_eu();
	test_fneg_and_push_operands();
	test_register_lifetime();
""")
import os
NL, TAB = chr(10), chr(9)
snippet = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "eu_interp_snippet.c")).read()
t = rep(t, "int" + NL + "main(void)", snippet + "int" + NL + "main(void)")
t = rep(t, TAB + "test_vertex_shader_generates_eu();" + NL,
        TAB + "test_vertex_shader_generates_eu();" + NL + TAB + "test_vertex_shader_eu_computes_the_shader();" + NL)
t = rep(t, "#include <assert.h>" + NL, "#include <assert.h>" + NL + "#include <math.h>" + NL)
save("i915-vk-compile-test.c", t)

# ---------------------------------------------------------------- pipe test
t = load("i915-vk-pipe-test.c")
t = cut(t, "	/*\n	 * The real vkdemo vertex shader is NOT lowered by the kernel compiler yet", "		const uint64_t bad_vs_h", """	/*
	 * A VALID vertex shader with an instruction the kernel compiler does not lower: the shipped
	 * vkdemo shader with its first OpFMul (133) turned into OpFDiv (136, same operands).
	 * Creating a pipeline with it must fail as a whole -- a defined VkResult, no pipeline
	 * object, no code buffer left behind -- instead of producing a pipeline around a shader
	 * that is not the application's.
	 */
	{
""")
t = rep(t, """		unsigned live_before;

		encode_shader_module(vs, vs_words, bad_vs_h);
""", """		unsigned live_before;
		uint32_t *bad_vs = malloc(vs_words * sizeof(*bad_vs));
		size_t at;

		assert(bad_vs != NULL);
		memcpy(bad_vs, vs, vs_words * sizeof(*bad_vs));
		for (at = 5U; at < vs_words && (bad_vs[at] & 0xFFFFU) != 133U; at += bad_vs[at] >> 16)
			assert((bad_vs[at] >> 16) != 0U);
		assert(at < vs_words);
		bad_vs[at] = (bad_vs[at] & 0xFFFF0000U) | 136U;
		encode_shader_module(bad_vs, vs_words, bad_vs_h);
		free(bad_vs);
""")
t = rep(t, 'printf("i915 vk pipe: vkdemo vertex shader refused as VK_ERROR_FEATURE_NOT_PRESENT, nothing left behind\\n");',
        'printf("i915 vk pipe: vertex shader with an unlowered instruction refused as VK_ERROR_FEATURE_NOT_PRESENT, nothing left behind\\n");')
t = cut(t, "	/*\n	 * The wire format of a successful creation, with modules the compiler can lower", "	encode_shader_module(fs, fs_words, fs_h);", """	/* The wire format of a successful creation, with the shipped vkdemo shaders. */
	encode_shader_module(vs, vs_words, vs_h);
	assert(run_command(&session, reply, sizeof(reply)) == 24U);
""")
save("i915-vk-pipe-test.c", t)
