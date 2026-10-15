/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: the known words of the amdrdna GFX10 encoder.
 *
 * Each case is an instruction record and the assembly text LLVM's
 * assembler reads for the same instruction.  The program prints one line
 * per case, "hex words<TAB>text"; amdrdna-compiler-host-test.sh assembles
 * the texts with llvm-mc (-mcpu=gfx1010) and compares the words.  It also
 * checks the register descriptions of a few instructions itself.
 */

#include "drivers/gpu/amdrdna/compiler/gfx10-encode.h"

#include <stdio.h>
#include <string.h>

/* The registers of the cases, by number. */
#define S(n)	AMDRDNA_GFX10_S(n)
#define V(n)	AMDRDNA_GFX10_V(n)

static struct amdrdna_gfx10_inst make(enum amdrdna_gfx10_kind kind, unsigned opcode, unsigned dst, unsigned src0, unsigned src1, unsigned src2);
static int emit(const char *text, const struct amdrdna_gfx10_inst *inst);
static int scalar_cases(void);
static int vector_cases(void);
static int memory_cases(void);
static int describe_cases(void);

/* Prints every case's words and text; 1 when a record could not be written or described as expected. */
int
main(void)
{
	int failed;

	/* Runs the three groups of known words and the register descriptions. */
	failed = 0;
	failed |= scalar_cases();
	failed |= vector_cases();
	failed |= memory_cases();
	failed |= describe_cases();

	/* Reports a refused record. */
	if (failed != 0)
		return 1;

	/* Succeeded: every record was written. */
	return 0;
}

/* Makes a record with the fields most instructions use. */
static struct amdrdna_gfx10_inst
make(
	enum amdrdna_gfx10_kind kind,
	unsigned opcode,
	unsigned dst,
	unsigned src0,
	unsigned src1,
	unsigned src2)
{
	struct amdrdna_gfx10_inst inst;

	/* Fills the common fields, the rest zero. */
	memset(&inst, 0, sizeof(inst));
	inst.kind = kind;
	inst.opcode = (uint16_t)opcode;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)src0;
	inst.src[1] = (uint16_t)src1;
	inst.src[2] = (uint16_t)src2;

	/* The record. */
	return inst;
}

/* Prints one case; 1 when the encoder refused the record. */
static int
emit(
	const char *text,
	const struct amdrdna_gfx10_inst *inst)
{
	uint32_t words[AMDRDNA_GFX10_MAX_WORDS];
	unsigned count;
	unsigned index;

	/* Writes the record. */
	count = amdrdna_gfx10_encode(inst, words);
	if (count == 0U) {
		printf("REFUSED\t%s\n", text);
		return 1;
	}

	/* Prints the words as llvm-mc's bytes, little-endian, then the text. */
	for (index = 0U; index < count; index++) {
		printf("%02x%02x%02x%02x",
		       words[index] & 0xffU,
		       (words[index] >> 8) & 0xffU,
		       (words[index] >> 16) & 0xffU,
		       (words[index] >> 24) & 0xffU);
	}
	printf("\t%s\n", text);

	/* Written. */
	return 0;
}

/* The scalar, control and branch cases. */
static int
scalar_cases(void)
{
	struct amdrdna_gfx10_inst inst;
	int failed;

	failed = 0;

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_TWO, AMDRDNA_GFX10_S_AND_B32, AMDRDNA_GFX10_EXEC_LO, AMDRDNA_GFX10_EXEC_LO, AMDRDNA_GFX10_VCC_LO, 0U);
	failed |= emit("s_and_b32 exec_lo, exec_lo, vcc_lo", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_TWO, AMDRDNA_GFX10_S_ADD_U32, S(4), S(5), AMDRDNA_GFX10_LITERAL, 0U);
	inst.literal = 0x12345U;
	failed |= emit("s_add_u32 s4, s5, 0x12345", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_TWO, AMDRDNA_GFX10_S_ANDN2_B32, S(12), S(12), AMDRDNA_GFX10_EXEC_LO, 0U);
	failed |= emit("s_andn2_b32 s12, s12, exec_lo", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_TWO, AMDRDNA_GFX10_S_MUL_I32, S(7), S(2), AMDRDNA_GFX10_INT_FIRST + 64U, 0U);
	failed |= emit("s_mul_i32 s7, s2, 64", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_AND_SAVEEXEC_B32, S(10), AMDRDNA_GFX10_VCC_LO, 0U, 0U);
	failed |= emit("s_and_saveexec_b32 s10, vcc_lo", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_MOV_B32, AMDRDNA_GFX10_EXEC_LO, S(10), 0U, 0U);
	failed |= emit("s_mov_b32 exec_lo, s10", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_MOV_B32, AMDRDNA_GFX10_NULL, AMDRDNA_GFX10_ZERO, 0U, 0U);
	failed |= emit("s_mov_b32 null, 0", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_MOV_B32, AMDRDNA_GFX10_M0, S(2), 0U, 0U);
	failed |= emit("s_mov_b32 m0, s2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_WQM_B32, AMDRDNA_GFX10_EXEC_LO, S(20), 0U, 0U);
	failed |= emit("s_wqm_b32 exec_lo, s20", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_COMPARE, AMDRDNA_GFX10_S_CMP_EQ_U32, 0U, S(4), AMDRDNA_GFX10_ZERO, 0U);
	failed |= emit("s_cmp_eq_u32 s4, 0", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_CONSTANT, AMDRDNA_GFX10_S_MOVK_I32, S(4), 0U, 0U, 0U);
	inst.immediate = 0x1234U;
	failed |= emit("s_movk_i32 s4, 0x1234", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_CONSTANT, AMDRDNA_GFX10_S_WAITCNT_VSCNT, AMDRDNA_GFX10_NULL, 0U, 0U, 0U);
	failed |= emit("s_waitcnt_vscnt null, 0x0", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_WAITCNT, 0U, 0U, 0U, 0U);
	inst.immediate = AMDRDNA_GFX10_WAIT_NONE & ~(AMDRDNA_GFX10_WAIT_VECTOR_MASK | AMDRDNA_GFX10_WAIT_SCALAR_MASK);
	failed |= emit("s_waitcnt vmcnt(0) lgkmcnt(0)", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_WAITCNT, 0U, 0U, 0U, 0U);
	inst.immediate = AMDRDNA_GFX10_WAIT_NONE & ~AMDRDNA_GFX10_WAIT_EXPORT_MASK;
	failed |= emit("s_waitcnt expcnt(0)", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_WAITCNT_DEPCTR, 0U, 0U, 0U, 0U);
	inst.immediate = AMDRDNA_GFX10_DEPEND_VECTOR_MEMORY_READS;
	failed |= emit("s_waitcnt_depctr 0xffe3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_NOP, 0U, 0U, 0U, 0U);
	failed |= emit("s_nop 0", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_ENDPGM, 0U, 0U, 0U, 0U);
	failed |= emit("s_endpgm", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_CODE_END, 0U, 0U, 0U, 0U);
	failed |= emit("s_code_end", &inst);

	inst = make(AMDRDNA_GFX10_KIND_CONTROL, AMDRDNA_GFX10_S_BARRIER, 0U, 0U, 0U, 0U);
	failed |= emit("s_barrier", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BRANCH, AMDRDNA_GFX10_S_CBRANCH_EXECNZ, 0U, 0U, 0U, 0U);
	inst.immediate = 0xfffdU;
	failed |= emit("s_cbranch_execnz -3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BRANCH, AMDRDNA_GFX10_S_CBRANCH_EXECZ, 0U, 0U, 0U, 0U);
	inst.immediate = 7U;
	failed |= emit("s_cbranch_execz 7", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BRANCH, AMDRDNA_GFX10_S_BRANCH, 0U, 0U, 0U, 0U);
	inst.immediate = 0xfff6U;
	failed |= emit("s_branch -10", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BRANCH, AMDRDNA_GFX10_S_CBRANCH_SCC1, 0U, 0U, 0U, 0U);
	inst.immediate = 5U;
	failed |= emit("s_cbranch_scc1 5", &inst);

	return failed;
}

/* The vector ALU and interpolation cases, short and wide. */
static int
vector_cases(void)
{
	struct amdrdna_gfx10_inst inst;
	int failed;

	failed = 0;

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_F32, V(1), V(2), V(3), 0U);
	failed |= emit("v_add_f32_e32 v1, v2, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_F32, V(1), AMDRDNA_GFX10_ONE, V(2), 0U);
	failed |= emit("v_add_f32_e32 v1, 1.0, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_F32, V(1), V(3), S(2), 0U);
	failed |= emit("v_add_f32_e64 v1, v3, s2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_F32, V(1), V(2), V(3), 0U);
	inst.modifiers = (uint8_t)(AMDRDNA_GFX10_WIDE_NEG(0) | AMDRDNA_GFX10_WIDE_ABS(1));
	failed |= emit("v_sub_f32_e64 v1, -v2, |v3|", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MUL_F32, V(1), AMDRDNA_GFX10_INV_TWO_PI, V(2), 0U);
	failed |= emit("v_mul_f32_e32 v1, 0.15915494, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, V(1), AMDRDNA_GFX10_LITERAL, V(2), 0U);
	inst.literal = 0x80000000U;
	failed |= emit("v_xor_b32_e32 v1, 0x80000000, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, V(1), V(2), V(3), AMDRDNA_GFX10_VCC_LO);
	failed |= emit("v_cndmask_b32_e32 v1, v2, v3, vcc_lo", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, V(1), AMDRDNA_GFX10_ZERO, AMDRDNA_GFX10_NEG_FIRST, AMDRDNA_GFX10_VCC_LO);
	failed |= emit("v_cndmask_b32_e64 v1, 0, -1, vcc_lo", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, V(1), V(2), V(3), S(14));
	failed |= emit("v_cndmask_b32_e64 v1, v2, v3, s14", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_LSHLREV_B32, V(1), V(2), V(3), 0U);
	failed |= emit("v_lshlrev_b32_e32 v1, v2, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_LSHRREV_B32, V(1), AMDRDNA_GFX10_INT_FIRST + 16U, V(3), 0U);
	failed |= emit("v_lshrrev_b32_e32 v1, 16, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_NC_U32, V(10), V(11), V(12), 0U);
	failed |= emit("v_add_nc_u32_e32 v10, v11, v12", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, V(10), AMDRDNA_GFX10_ZERO, V(12), 0U);
	failed |= emit("v_sub_nc_u32_e32 v10, 0, v12", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MAX_I32, V(255), V(0), V(254), 0U);
	failed |= emit("v_max_i32_e32 v255, v0, v254", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, V(1), AMDRDNA_GFX10_LITERAL, 0U, 0U);
	inst.literal = 0x12345678U;
	failed |= emit("v_mov_b32_e32 v1, 0x12345678", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, V(1), S(5), 0U, 0U);
	failed |= emit("v_mov_b32_e32 v1, s5", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_READFIRSTLANE_B32, S(5), V(1), 0U, 0U);
	failed |= emit("v_readfirstlane_b32 s5, v1", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_F16_F32, V(1), V(2), 0U, 0U);
	failed |= emit("v_cvt_f16_f32_e32 v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_I32_F32, V(1), V(2), 0U, 0U);
	failed |= emit("v_cvt_i32_f32_e32 v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_RCP_IFLAG_F32, V(1), V(2), 0U, 0U);
	failed |= emit("v_rcp_iflag_f32_e32 v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_SIN_F32, V(1), V(2), 0U, 0U);
	failed |= emit("v_sin_f32_e32 v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_FLOOR_F32, V(1), V(2), 0U, 0U);
	inst.modifiers = (uint8_t)AMDRDNA_GFX10_WIDE_NEG(0);
	failed |= emit("v_floor_f32_e64 v1, -v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_NOT_B32, V(1), V(2), 0U, 0U);
	failed |= emit("v_not_b32_e32 v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_NE_U32, AMDRDNA_GFX10_VCC_LO, AMDRDNA_GFX10_ZERO, V(2), 0U);
	failed |= emit("v_cmp_ne_u32_e32 vcc_lo, 0, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_LT_F32, AMDRDNA_GFX10_VCC_LO, V(1), V(2), 0U);
	failed |= emit("v_cmp_lt_f32_e32 vcc_lo, v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_LT_F32, S(4), V(1), V(2), 0U);
	failed |= emit("v_cmp_lt_f32_e64 s4, v1, v2", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_GE_I32, AMDRDNA_GFX10_VCC_LO, V(1), S(3), 0U);
	failed |= emit("v_cmp_ge_i32_e64 vcc_lo, v1, s3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_NEQ_F32, AMDRDNA_GFX10_VCC_LO, V(7), V(8), 0U);
	failed |= emit("v_cmp_neq_f32_e32 vcc_lo, v7, v8", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_GE_U32, AMDRDNA_GFX10_VCC_LO, V(7), V(8), 0U);
	failed |= emit("v_cmp_ge_u32_e32 vcc_lo, v7, v8", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_FMA_F32, V(1), V(2), V(3), AMDRDNA_GFX10_LITERAL);
	inst.literal = 0x40490fdbU;
	failed |= emit("v_fma_f32 v1, v2, v3, 0x40490fdb", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_LO_U32, V(1), V(2), V(3), 0U);
	failed |= emit("v_mul_lo_u32 v1, v2, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_HI_U32, V(1), V(2), S(9), 0U);
	failed |= emit("v_mul_hi_u32 v1, v2, s9", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MAD_U32_U24, V(1), V(2), AMDRDNA_GFX10_INT_FIRST + 64U, V(3));
	failed |= emit("v_mad_u32_u24 v1, v2, 64, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_PACK_B32_F16, V(1), V(2), V(3), 0U);
	failed |= emit("v_pack_b32_f16 v1, v2, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_INTERPOLATE, AMDRDNA_GFX10_V_INTERP_P1_F32, V(2), V(0), 0U, 0U);
	inst.target = 3U;
	inst.channel = 1U;
	failed |= emit("v_interp_p1_f32_e32 v2, v0, attr3.y", &inst);

	inst = make(AMDRDNA_GFX10_KIND_INTERPOLATE, AMDRDNA_GFX10_V_INTERP_P2_F32, V(2), V(1), 0U, 0U);
	inst.target = 3U;
	inst.channel = 1U;
	failed |= emit("v_interp_p2_f32_e32 v2, v1, attr3.y", &inst);

	inst = make(AMDRDNA_GFX10_KIND_INTERPOLATE, AMDRDNA_GFX10_V_INTERP_MOV_F32, V(2), AMDRDNA_GFX10_INTERP_P0, 0U, 0U);
	inst.target = 31U;
	inst.channel = 3U;
	failed |= emit("v_interp_mov_f32_e32 v2, p0, attr31.w", &inst);

	return failed;
}

/* The scalar load, shared memory, buffer, image and export cases. */
static int
memory_cases(void)
{
	struct amdrdna_gfx10_inst inst;
	int failed;

	failed = 0;

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_LOAD, AMDRDNA_GFX10_S_LOAD_DWORDX4, S(4), S(0), 0U, 0U);
	inst.immediate = 0x10U;
	inst.count = 4U;
	failed |= emit("s_load_dwordx4 s[4:7], s[0:1], 0x10", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_LOAD, AMDRDNA_GFX10_S_LOAD_DWORD, S(20), S(0), 0U, 0U);
	inst.immediate = 0x104U;
	inst.count = 1U;
	failed |= emit("s_load_dword s20, s[0:1], 0x104", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_LOAD, AMDRDNA_GFX10_S_LOAD_DWORDX8, S(16), S(0), 0U, 0U);
	inst.immediate = 0x40U;
	inst.count = 8U;
	failed |= emit("s_load_dwordx8 s[16:23], s[0:1], 0x40", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SCALAR_LOAD, AMDRDNA_GFX10_S_BUFFER_LOAD_DWORD, S(20), S(8), 0U, 0U);
	inst.immediate = 0x10U;
	inst.count = 1U;
	failed |= emit("s_buffer_load_dword s20, s[8:11], 0x10", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SHARED, AMDRDNA_GFX10_DS_READ_B32, V(1), V(2), 0U, 0U);
	inst.immediate = 4U;
	failed |= emit("ds_read_b32 v1, v2 offset:4", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SHARED, AMDRDNA_GFX10_DS_WRITE_B32, 0U, V(1), V(2), 0U);
	inst.immediate = 8U;
	failed |= emit("ds_write_b32 v1, v2 offset:8", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SHARED, AMDRDNA_GFX10_DS_ADD_RTN_U32, V(1), V(2), V(3), 0U);
	failed |= emit("ds_add_rtn_u32 v1, v2, v3", &inst);

	inst = make(AMDRDNA_GFX10_KIND_SHARED, AMDRDNA_GFX10_DS_CMPST_RTN_B32, V(1), V(2), V(3), V(4));
	failed |= emit("ds_cmpst_rtn_b32 v1, v2, v3, v4", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_LOAD_DWORD, V(1), V(2), S(4), AMDRDNA_GFX10_ZERO);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS;
	inst.count = 1U;
	failed |= emit("buffer_load_dword v1, v2, s[4:7], 0 offen", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_LOAD_DWORD, V(1), V(2), S(4), S(9));
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS | AMDRDNA_GFX10_FLAG_GLOBALLY_COHERENT;
	inst.immediate = 12U;
	inst.count = 1U;
	failed |= emit("buffer_load_dword v1, v2, s[4:7], s9 offen offset:12 glc", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_STORE_DWORD, 0U, V(2), S(4), AMDRDNA_GFX10_ZERO);
	inst.src[3] = (uint16_t)V(1);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS;
	failed |= emit("buffer_store_dword v1, v2, s[4:7], 0 offen", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_ATOMIC_UMAX, V(1), V(2), S(8), AMDRDNA_GFX10_ZERO);
	inst.src[3] = (uint16_t)V(1);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS | AMDRDNA_GFX10_FLAG_GLOBALLY_COHERENT;
	failed |= emit("buffer_atomic_umax v1, v2, s[8:11], 0 offen glc", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_ATOMIC_ADD, 0U, V(2), S(8), AMDRDNA_GFX10_ZERO);
	inst.src[3] = (uint16_t)V(1);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS;
	failed |= emit("buffer_atomic_add v1, v2, s[8:11], 0 offen", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_ATOMIC_CMPSWAP, V(2), V(4), S(8), AMDRDNA_GFX10_ZERO);
	inst.src[3] = (uint16_t)V(2);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS | AMDRDNA_GFX10_FLAG_GLOBALLY_COHERENT;
	failed |= emit("buffer_atomic_cmpswap v[2:3], v4, s[8:11], 0 offen glc", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_LOAD_FORMAT_XYZW, V(4), V(2), S(8), AMDRDNA_GFX10_ZERO);
	inst.flags = AMDRDNA_GFX10_FLAG_INDEX_ADDRESS;
	inst.count = 4U;
	failed |= emit("buffer_load_format_xyzw v[4:7], v2, s[8:11], 0 idxen", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_GL0_INV, 0U, 0U, 0U, 0U);
	failed |= emit("buffer_gl0_inv", &inst);

	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_GL1_INV, 0U, 0U, 0U, 0U);
	failed |= emit("buffer_gl1_inv", &inst);

	inst = make(AMDRDNA_GFX10_KIND_IMAGE, AMDRDNA_GFX10_IMAGE_SAMPLE, V(0), V(4), S(8), S(16));
	inst.mask = 0xfU;
	inst.count = 2U;
	failed |= emit("image_sample v[0:3], v[4:5], s[8:15], s[16:19] dmask:0xf dim:SQ_RSRC_IMG_2D", &inst);

	inst = make(AMDRDNA_GFX10_KIND_IMAGE, AMDRDNA_GFX10_IMAGE_SAMPLE_LZ, V(0), V(4), S(8), S(16));
	inst.mask = 0xfU;
	inst.count = 2U;
	failed |= emit("image_sample_lz v[0:3], v[4:5], s[8:15], s[16:19] dmask:0xf dim:SQ_RSRC_IMG_2D", &inst);

	inst = make(AMDRDNA_GFX10_KIND_IMAGE, AMDRDNA_GFX10_IMAGE_SAMPLE_L_O, V(0), V(4), S(8), S(16));
	inst.mask = 0xfU;
	inst.count = 4U;
	failed |= emit("image_sample_l_o v[0:3], v[4:7], s[8:15], s[16:19] dmask:0xf dim:SQ_RSRC_IMG_2D", &inst);

	inst = make(AMDRDNA_GFX10_KIND_IMAGE, AMDRDNA_GFX10_IMAGE_LOAD_MIP, V(0), V(4), S(8), AMDRDNA_GFX10_NULL);
	inst.mask = 0xfU;
	inst.count = 3U;
	inst.flags = AMDRDNA_GFX10_FLAG_UNNORMALIZED;
	failed |= emit("image_load_mip v[0:3], v[4:6], s[8:15] dmask:0xf dim:SQ_RSRC_IMG_2D unorm", &inst);

	inst = make(AMDRDNA_GFX10_KIND_EXPORT, 0U, 0U, V(0), V(1), V(2));
	inst.src[3] = (uint16_t)V(3);
	inst.target = AMDRDNA_GFX10_EXPORT_COLOR;
	inst.mask = 0xfU;
	inst.flags = AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK;
	failed |= emit("exp mrt0 v0, v1, v2, v3 done vm", &inst);

	inst = make(AMDRDNA_GFX10_KIND_EXPORT, 0U, 0U, V(4), V(5), V(6));
	inst.src[3] = (uint16_t)V(7);
	inst.target = AMDRDNA_GFX10_EXPORT_POSITION;
	inst.mask = 0xfU;
	inst.flags = AMDRDNA_GFX10_FLAG_LAST;
	failed |= emit("exp pos0 v4, v5, v6, v7 done", &inst);

	inst = make(AMDRDNA_GFX10_KIND_EXPORT, 0U, 0U, V(9), 0U, 0U);
	inst.target = AMDRDNA_GFX10_EXPORT_PARAMETER + 3U;
	inst.mask = 1U;
	failed |= emit("exp param3 v9, off, off, off", &inst);

	inst = make(AMDRDNA_GFX10_KIND_EXPORT, 0U, 0U, 0U, 0U, 0U);
	inst.target = AMDRDNA_GFX10_EXPORT_NOTHING;
	inst.flags = AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK;
	failed |= emit("exp null off, off, off, off done vm", &inst);

	return failed;
}

/* Checks the registers a few instructions are described as reading and writing. */
static int
describe_cases(void)
{
	struct amdrdna_gfx10_inst inst;
	struct amdrdna_gfx10_access access;
	int failed;

	failed = 0;

	/* A short selection reads the lane mask, both sources and VCC_LO, and writes its VGPR. */
	inst = make(AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, V(1), V(2), V(3), AMDRDNA_GFX10_VCC_LO);
	amdrdna_gfx10_describe(&inst, &access);
	if (access.read_count != 4U || access.write_count != 1U || access.writes[0].first != V(1)) {
		printf("DESCRIBE\tv_cndmask_b32_e32: %u reads, %u writes\n", access.read_count, access.write_count);
		failed = 1;
	}

	/* A buffer load reads the lane mask, the address and four descriptor SGPRs, and its result arrives by the load counter. */
	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_LOAD_DWORD, V(1), V(2), S(4), AMDRDNA_GFX10_ZERO);
	inst.count = 1U;
	amdrdna_gfx10_describe(&inst, &access);
	if (access.read_count != 3U || access.reads[2].count != 4U ||
	    access.counter != AMDRDNA_GFX10_COUNTER_VECTOR_LOAD) {
		printf("DESCRIBE\tbuffer_load_dword: %u reads, counter %d\n", access.read_count, (int)access.counter);
		failed = 1;
	}

	/* A store has no result and is counted as a store. */
	inst = make(AMDRDNA_GFX10_KIND_BUFFER, AMDRDNA_GFX10_BUFFER_STORE_DWORD, 0U, V(2), S(4), AMDRDNA_GFX10_ZERO);
	inst.src[3] = (uint16_t)V(1);
	amdrdna_gfx10_describe(&inst, &access);
	if (access.write_count != 0U || access.counter != AMDRDNA_GFX10_COUNTER_VECTOR_STORE) {
		printf("DESCRIBE\tbuffer_store_dword: %u writes, counter %d\n", access.write_count, (int)access.counter);
		failed = 1;
	}

	/* An image sample of four channels writes four VGPRs. */
	inst = make(AMDRDNA_GFX10_KIND_IMAGE, AMDRDNA_GFX10_IMAGE_SAMPLE, V(0), V(4), S(8), S(16));
	inst.mask = 0xfU;
	inst.count = 2U;
	amdrdna_gfx10_describe(&inst, &access);
	if (access.write_count != 1U || access.writes[0].count != 4U) {
		printf("DESCRIBE\timage_sample: %u writes\n", access.write_count);
		failed = 1;
	}

	/* Narrowing the lanes reads and writes the lane mask. */
	inst = make(AMDRDNA_GFX10_KIND_SCALAR_ONE, AMDRDNA_GFX10_S_AND_SAVEEXEC_B32, S(10), AMDRDNA_GFX10_VCC_LO, 0U, 0U);
	amdrdna_gfx10_describe(&inst, &access);
	if (access.read_count != 2U || access.write_count != 2U) {
		printf("DESCRIBE\ts_and_saveexec_b32: %u reads, %u writes\n", access.read_count, access.write_count);
		failed = 1;
	}

	return failed;
}
