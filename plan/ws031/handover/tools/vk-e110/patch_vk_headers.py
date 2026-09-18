#!/usr/bin/env python3
"""WS031 VK-2 base lowering: scalar IR in spirv.h, scalar-region operand in eu.{h,c}.  usage: patch_vk_headers.py <repo root>"""
import sys, os
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/src/drivers/gpu/i915/vk/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)

h = load("spirv.h")
h = rep(h, """ * SPIR-V parser producing a baseline, straight-line IR the compiler lowers
 * one instruction at a time.  Contract for p004; see external-design.md
 * section 4.4.  The IR types here are the record the compiler reads.
 */""", """ * SPIR-V parser producing a baseline, straight-line IR the compiler lowers
 * one instruction at a time.  Contract for p004; see external-design.md
 * section 4.4.  The IR types here are the record the compiler reads.
 *
 * The IR is SCALAR: every value is one 32-bit float.  A SPIR-V vector is a
 * group of scalars, and the operations that only rearrange components
 * (construct, extract, shuffle, component access, local store / load) leave
 * no instruction behind -- the parser resolves them to the scalars involved.
 */""")
h = rep(h, """	I915_VK_IR_NOP = 0,
	I915_VK_IR_LOAD_INPUT,
	I915_VK_IR_STORE_OUTPUT,
	I915_VK_IR_LOAD_PUSH,
	I915_VK_IR_FADD,
	I915_VK_IR_FSUB,
	I915_VK_IR_FMUL,
	I915_VK_IR_FMAD,
	I915_VK_IR_DOT,
	I915_VK_IR_RSQ,
	I915_VK_IR_SIN,
	I915_VK_IR_COS,
	I915_VK_IR_COMPOSE,
	I915_VK_IR_EXTRACT,
	I915_VK_IR_SAMPLE,
	I915_VK_IR_OP_COUNT
};
""", """	I915_VK_IR_NOP = 0,
	I915_VK_IR_CONST,		/* dst = the float whose bits are `immediate` */
	I915_VK_IR_LOAD_INPUT,		/* dst = input `location`, component `component` */
	I915_VK_IR_STORE_OUTPUT,	/* output `location` (or POSITION), component `component` = src[0] */
	I915_VK_IR_LOAD_PUSH,		/* dst = the push-constant float at byte offset `immediate` */
	I915_VK_IR_FADD,		/* dst = src[0] + src[1] */
	I915_VK_IR_FSUB,		/* dst = src[0] - src[1] */
	I915_VK_IR_FMUL,		/* dst = src[0] * src[1] */
	I915_VK_IR_FNEG,		/* dst = -src[0] */
	I915_VK_IR_RSQ,
	I915_VK_IR_SIN,
	I915_VK_IR_COS,
	I915_VK_IR_SAMPLE,		/* dst .. dst + 3 = texture(set `location`, binding `immediate`) at (src[0], src[1]) */
	I915_VK_IR_OP_COUNT
};

/* STORE_OUTPUT `location` of the Position builtin (not a user location). */
#define I915_VK_IR_LOCATION_POSITION 0xFFFFFFFFU
""")
h = rep(h, """/* One SSA instruction; sources name earlier values. */
struct i915_vk_inst {
	enum i915_vk_ir_op op;
	uint32_t dst;
	uint32_t src[4];
	uint32_t immediate;
	uint32_t swizzle;
};
""", """/* One SSA instruction on scalar values; sources name earlier values. */
struct i915_vk_inst {
	enum i915_vk_ir_op op;
	uint32_t dst;
	uint32_t src[4];
	uint32_t immediate;
	uint32_t location;
	uint32_t component;
};
""")
h = rep(h, TAB + "uint32_t value_count;" + NL + "};", TAB + "uint32_t value_count;" + NL +
        TAB + "uint32_t push_bytes;" + TAB + "/* bytes of push constants the shader reads */" + NL + "};")
save("spirv.h", h)

e = load("eu.h")
e = rep(e, """struct i915_vk_eu_reg
i915_vk_eu_imm_f(
	uint32_t bits);
""", """/*
 * One float at byte `subnr` of a register, replicated to every channel (region <0;1,0>):
 * how a value that is the same for the whole dispatch, such as a push constant, is read.
 */
struct i915_vk_eu_reg
i915_vk_eu_grf_scalar(
	uint32_t nr,
	uint32_t subnr);

struct i915_vk_eu_reg
i915_vk_eu_imm_f(
	uint32_t bits);
""")
save("eu.h", e)

c = load("eu.c")
c = rep(c, """/* The same register read negated. */
struct i915_vk_eu_reg
i915_vk_eu_negate(""", """/* Names one float of a general register, replicated to every channel. */
struct i915_vk_eu_reg
i915_vk_eu_grf_scalar(
	uint32_t nr,
	uint32_t subnr)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.subnr = subnr;
	reg.type = EU_TYPE_F;
	reg.vstride = EU_VSTRIDE_0;
	reg.width = EU_WIDTH_1;
	reg.hstride = EU_HSTRIDE_0;
	return reg;
}

/* The same register read negated. */
struct i915_vk_eu_reg
i915_vk_eu_negate(""")
c = rep(c, """	/* The three-source align1 operand layout differs and is completed on hardware. */
	(void)src0;
	(void)src1;
	(void)src2;
""", """	/*
	 * The three-source operand layout is not encoded.  An instruction without its operands
	 * is a different instruction, so the buffer is poisoned: no caller can ship it by accident.
	 */
	(void)src0;
	(void)src1;
	(void)src2;
	buffer->error = 1;
""")
save("eu.c", c)
