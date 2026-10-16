/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The facts of the RDNA1 (GFX10.1, gfx1010) instruction set the amdrdna
 * shader compiler uses, and the program interface the compiler and the
 * driver agree on.
 *
 * The program interface (AMDRDNA_GFX10_ABI_*) is zedBSD's own.
 */

#ifndef DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_ISA_H
#define DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_ISA_H

/*
 * ==== Operand codes ====
 *
 * One number names every scalar or vector operand: an SGPR, a special
 * scalar register, an inline constant, the literal that follows the
 * instruction, or (from 256) a VGPR.  An 8-bit field holds the low bits of
 * a VGPR's code and a 9-bit source field holds the whole code.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_ir.h
 * (the special registers m0, vcc, vcc_hi, sgpr_null, exec_lo, exec_hi,
 * lines 422-446; Operand::c32_or_c64, the inline integers 128 + v and
 * 192 - v and the literal 255, lines 586-616).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_SGPR_COUNT	106U
#define AMDRDNA_GFX10_VCC_LO		106U
#define AMDRDNA_GFX10_VCC_HI		107U
#define AMDRDNA_GFX10_M0		124U
#define AMDRDNA_GFX10_NULL		125U
#define AMDRDNA_GFX10_EXEC_LO		126U
#define AMDRDNA_GFX10_EXEC_HI		127U
#define AMDRDNA_GFX10_ZERO		128U
#define AMDRDNA_GFX10_INT_FIRST		128U
#define AMDRDNA_GFX10_INT_LAST		192U
#define AMDRDNA_GFX10_NEG_FIRST		193U
#define AMDRDNA_GFX10_NEG_LAST		208U
#define AMDRDNA_GFX10_LITERAL		255U
#define AMDRDNA_GFX10_VGPR_FIRST	256U
#define AMDRDNA_GFX10_VGPR_COUNT	256U

/* The code of SGPR n and of VGPR n. */
#define AMDRDNA_GFX10_S(n)		((unsigned)(n))
#define AMDRDNA_GFX10_V(n)		(AMDRDNA_GFX10_VGPR_FIRST + (unsigned)(n))

/*
 * The float inline constants: the operand code of each and the bits of
 * the float it stands for.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_ir.h
 * (Operand::c32_or_c64, codes 240 .. 247 for 0.5, -0.5, 1.0, -1.0, 2.0,
 * -2.0, 4.0, -4.0, lines 597-612; Operand::get_const, 1 / (2 pi) as code
 * 248 and bits 0x3e22f983, lines 660-664).
 */
#define AMDRDNA_GFX10_HALF		240U
#define AMDRDNA_GFX10_MINUS_HALF	241U
#define AMDRDNA_GFX10_ONE		242U
#define AMDRDNA_GFX10_MINUS_ONE		243U
#define AMDRDNA_GFX10_TWO		244U
#define AMDRDNA_GFX10_MINUS_TWO		245U
#define AMDRDNA_GFX10_FOUR		246U
#define AMDRDNA_GFX10_MINUS_FOUR	247U
#define AMDRDNA_GFX10_INV_TWO_PI	248U
#define AMDRDNA_GFX10_BITS_HALF		0x3f000000U
#define AMDRDNA_GFX10_BITS_ONE		0x3f800000U
#define AMDRDNA_GFX10_BITS_TWO		0x40000000U
#define AMDRDNA_GFX10_BITS_FOUR		0x40800000U
#define AMDRDNA_GFX10_BITS_INV_TWO_PI	0x3e22f983U

/*
 * ==== The marks of the instruction forms ====
 *
 * The high bits that say which form the first word of an instruction
 * has: the scalar forms with two sources, a 16-bit constant, one source,
 * a comparison and a program-control constant; the scalar load; the
 * vector forms with one source, a comparison, two sources and the wide
 * (64-bit) form; interpolation; the shared memory, buffer and image
 * accesses; and the export.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_assembler.cpp
 * (emit_sop2_instruction 0b10 << 30, emit_sopk_instruction 0b1011 << 28,
 * emit_sop1_instruction, emit_sopc_instruction and emit_sopp_instruction
 * 0b101111101, 0b101111110, 0b101111111 << 23, emit_vop1_instruction
 * 0b0111111 << 25, emit_vopc_instruction 0b0111110 << 25,
 * emit_vintrp_instruction 0b110010 << 26, emit_ds_instruction,
 * emit_mubuf_instruction, emit_mimg_instruction, emit_exp_instruction,
 * emit_vop3_instruction and emit_smem_instruction, the GFX10 cases).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_FORM_SCALAR_TWO		0x80000000U
#define AMDRDNA_GFX10_FORM_SCALAR_CONSTANT	0xb0000000U
#define AMDRDNA_GFX10_FORM_SCALAR_ONE		0xbe800000U
#define AMDRDNA_GFX10_FORM_SCALAR_COMPARE	0xbf000000U
#define AMDRDNA_GFX10_FORM_CONTROL		0xbf800000U
#define AMDRDNA_GFX10_FORM_SCALAR_LOAD		0xf4000000U
#define AMDRDNA_GFX10_FORM_VECTOR_ONE		0x7e000000U
#define AMDRDNA_GFX10_FORM_VECTOR_COMPARE	0x7c000000U
#define AMDRDNA_GFX10_FORM_VECTOR_TWO		0x00000000U
#define AMDRDNA_GFX10_FORM_VECTOR_WIDE		0xd4000000U
#define AMDRDNA_GFX10_FORM_INTERPOLATE		0xc8000000U
#define AMDRDNA_GFX10_FORM_SHARED		0xd8000000U
#define AMDRDNA_GFX10_FORM_BUFFER		0xe0000000U
#define AMDRDNA_GFX10_FORM_IMAGE		0xf0000000U
#define AMDRDNA_GFX10_FORM_EXPORT		0xf8000000U

/*
 * A comparison, a two-source or a one-source vector operation written in
 * the wide form takes its opcode plus this offset.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_assembler.cpp
 * (emit_vop3_instruction, the GFX10 case: VOP2 + 0x100, VOP1 + 0x180,
 * VOPC + 0) and src/amd/compiler/README-ISA.md ("VOP1 instructions
 * encoded as VOP3").
 */
#define AMDRDNA_GFX10_WIDE_FROM_COMPARE	0x000U
#define AMDRDNA_GFX10_WIDE_FROM_TWO	0x100U
#define AMDRDNA_GFX10_WIDE_FROM_ONE	0x180U

/*
 * ==== Scalar opcodes ====
 *
 * The two-source scalar operations.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SOP2, lines 446-482).
 */
#define AMDRDNA_GFX10_S_ADD_U32		0x00U
#define AMDRDNA_GFX10_S_SUB_U32		0x01U
#define AMDRDNA_GFX10_S_ADD_I32		0x02U
#define AMDRDNA_GFX10_S_CSELECT_B32	0x0aU
#define AMDRDNA_GFX10_S_AND_B32		0x0eU
#define AMDRDNA_GFX10_S_OR_B32		0x10U
#define AMDRDNA_GFX10_S_XOR_B32		0x12U
#define AMDRDNA_GFX10_S_ANDN2_B32	0x14U
#define AMDRDNA_GFX10_S_LSHL_B32	0x1eU
#define AMDRDNA_GFX10_S_LSHR_B32	0x20U
#define AMDRDNA_GFX10_S_MUL_I32		0x26U

/*
 * The scalar operations with a 16-bit constant.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SOPK, lines 532-554).
 */
#define AMDRDNA_GFX10_S_MOVK_I32	0x00U
#define AMDRDNA_GFX10_S_WAITCNT_VSCNT	0x17U

/*
 * The one-source scalar operations.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SOP1, lines 567-622).
 */
#define AMDRDNA_GFX10_S_MOV_B32		0x03U
#define AMDRDNA_GFX10_S_NOT_B32		0x07U
#define AMDRDNA_GFX10_S_WQM_B32		0x09U
#define AMDRDNA_GFX10_S_AND_SAVEEXEC_B32 0x3cU

/*
 * The scalar comparisons.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SOPC, lines 677-684).
 */
#define AMDRDNA_GFX10_S_CMP_EQ_U32	0x06U
#define AMDRDNA_GFX10_S_CMP_LG_U32	0x07U
#define AMDRDNA_GFX10_S_BITCMP1_B32	0x0dU

/*
 * The program control operations: the branches, the waits, the end.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SOPP, lines 726-761).
 */
#define AMDRDNA_GFX10_S_NOP		0x00U
#define AMDRDNA_GFX10_S_ENDPGM		0x01U
#define AMDRDNA_GFX10_S_BRANCH		0x02U
#define AMDRDNA_GFX10_S_CBRANCH_SCC0	0x04U
#define AMDRDNA_GFX10_S_CBRANCH_SCC1	0x05U
#define AMDRDNA_GFX10_S_CBRANCH_VCCZ	0x06U
#define AMDRDNA_GFX10_S_CBRANCH_VCCNZ	0x07U
#define AMDRDNA_GFX10_S_CBRANCH_EXECZ	0x08U
#define AMDRDNA_GFX10_S_CBRANCH_EXECNZ	0x09U
#define AMDRDNA_GFX10_S_BARRIER		0x0aU
#define AMDRDNA_GFX10_S_WAITCNT		0x0cU
#define AMDRDNA_GFX10_S_CODE_END	0x1fU
#define AMDRDNA_GFX10_S_WAITCNT_DEPCTR	0x23U

/*
 * ==== Scalar memory opcodes ====
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table SMEM, lines 787-803).
 */
#define AMDRDNA_GFX10_S_LOAD_DWORD	0x00U
#define AMDRDNA_GFX10_S_LOAD_DWORDX2	0x01U
#define AMDRDNA_GFX10_S_LOAD_DWORDX4	0x02U
#define AMDRDNA_GFX10_S_LOAD_DWORDX8	0x03U
#define AMDRDNA_GFX10_S_BUFFER_LOAD_DWORD 0x08U
#define AMDRDNA_GFX10_S_BUFFER_LOAD_DWORDX4 0x0aU

/*
 * ==== Vector ALU opcodes ====
 *
 * The two-source vector operations.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table VOP2, lines 896-977).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_V_CNDMASK_B32	0x01U
#define AMDRDNA_GFX10_V_ADD_F32		0x03U
#define AMDRDNA_GFX10_V_SUB_F32		0x04U
#define AMDRDNA_GFX10_V_MUL_F32		0x08U
#define AMDRDNA_GFX10_V_MIN_F32		0x0fU
#define AMDRDNA_GFX10_V_MAX_F32		0x10U
#define AMDRDNA_GFX10_V_MIN_I32		0x11U
#define AMDRDNA_GFX10_V_MAX_I32		0x12U
#define AMDRDNA_GFX10_V_MIN_U32		0x13U
#define AMDRDNA_GFX10_V_MAX_U32		0x14U
#define AMDRDNA_GFX10_V_LSHRREV_B32	0x16U
#define AMDRDNA_GFX10_V_ASHRREV_I32	0x18U
#define AMDRDNA_GFX10_V_LSHLREV_B32	0x1aU
#define AMDRDNA_GFX10_V_AND_B32		0x1bU
#define AMDRDNA_GFX10_V_OR_B32		0x1cU
#define AMDRDNA_GFX10_V_XOR_B32		0x1dU
#define AMDRDNA_GFX10_V_ADD_NC_U32	0x25U
#define AMDRDNA_GFX10_V_SUB_NC_U32	0x26U
#define AMDRDNA_GFX10_V_CVT_PKRTZ_F16_F32 0x2fU

/*
 * The one-source vector operations.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table VOP1, lines 984-1035).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_V_NOP		0x00U
#define AMDRDNA_GFX10_V_MOV_B32		0x01U
#define AMDRDNA_GFX10_V_READFIRSTLANE_B32 0x02U
#define AMDRDNA_GFX10_V_CVT_F32_I32	0x05U
#define AMDRDNA_GFX10_V_CVT_F32_U32	0x06U
#define AMDRDNA_GFX10_V_CVT_U32_F32	0x07U
#define AMDRDNA_GFX10_V_CVT_I32_F32	0x08U
#define AMDRDNA_GFX10_V_CVT_F16_F32	0x0aU
#define AMDRDNA_GFX10_V_CVT_F32_F16	0x0bU
#define AMDRDNA_GFX10_V_TRUNC_F32	0x21U
#define AMDRDNA_GFX10_V_RNDNE_F32	0x23U
#define AMDRDNA_GFX10_V_FLOOR_F32	0x24U
#define AMDRDNA_GFX10_V_EXP_F32		0x25U
#define AMDRDNA_GFX10_V_LOG_F32		0x27U
#define AMDRDNA_GFX10_V_RCP_F32		0x2aU
#define AMDRDNA_GFX10_V_RCP_IFLAG_F32	0x2bU
#define AMDRDNA_GFX10_V_RSQ_F32		0x2eU
#define AMDRDNA_GFX10_V_SQRT_F32	0x33U
#define AMDRDNA_GFX10_V_SIN_F32		0x35U
#define AMDRDNA_GFX10_V_COS_F32		0x36U
#define AMDRDNA_GFX10_V_NOT_B32		0x37U

/*
 * The vector comparisons: the condition 0 .. 15 plus 0x00 for f32, 0x80
 * for i32 and 0xc0 for u32.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the loop after VOPC_CLASS that builds the comparisons, lines
 * 1104-1162, and the gfx10 column of F32, I32 and U32).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_V_CMP_LT_F32	0x01U
#define AMDRDNA_GFX10_V_CMP_EQ_F32	0x02U
#define AMDRDNA_GFX10_V_CMP_GE_F32	0x06U
#define AMDRDNA_GFX10_V_CMP_NEQ_F32	0x0dU
#define AMDRDNA_GFX10_V_CMP_LT_I32	0x81U
#define AMDRDNA_GFX10_V_CMP_EQ_I32	0x82U
#define AMDRDNA_GFX10_V_CMP_NE_I32	0x85U
#define AMDRDNA_GFX10_V_CMP_GE_I32	0x86U
#define AMDRDNA_GFX10_V_CMP_LT_U32	0xc1U
#define AMDRDNA_GFX10_V_CMP_EQ_U32	0xc2U
#define AMDRDNA_GFX10_V_CMP_NE_U32	0xc5U
#define AMDRDNA_GFX10_V_CMP_GE_U32	0xc6U

/*
 * The operations that only have the wide form.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table VOP3, lines 1260-1370).
 * Checked against llvm-mc 19 (-mcpu=gfx1010).
 */
#define AMDRDNA_GFX10_V_MAD_U32_U24	0x143U
#define AMDRDNA_GFX10_V_FMA_F32		0x14bU
#define AMDRDNA_GFX10_V_MUL_LO_U32	0x169U
#define AMDRDNA_GFX10_V_MUL_HI_U32	0x16aU
#define AMDRDNA_GFX10_V_PACK_B32_F16	0x311U

/*
 * The modifier bits of a wide vector instruction: one negate and one
 * absolute value bit per source.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_assembler.cpp
 * (emit_vop3_instruction: abs in bits 10:8 of the first word, neg in
 * bits 31:29 of the second, both one bit per source from the lowest).
 */
#define AMDRDNA_GFX10_WIDE_NEG(n)	(1U << (n))
#define AMDRDNA_GFX10_WIDE_ABS(n)	(1U << (3 + (n)))

/*
 * ==== Interpolation opcodes ====
 *
 * The parameter code of v_interp_mov_f32 that reads the attribute at the
 * provoking vertex.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table VINTRP, lines 1233-1235) and
 * src/amd/compiler/aco_assembler.cpp (emit_vintrp_instruction: the
 * parameter in the low bits of v_interp_mov_f32).
 * Checked against llvm-mc 19, which writes the parameter 2 as "p0".
 */
#define AMDRDNA_GFX10_V_INTERP_P1_F32	0x00U
#define AMDRDNA_GFX10_V_INTERP_P2_F32	0x01U
#define AMDRDNA_GFX10_V_INTERP_MOV_F32	0x02U
#define AMDRDNA_GFX10_INTERP_P0		2U

/*
 * ==== Local data share opcodes ====
 *
 * A compare-and-store takes the value compared in data0 and the value
 * stored in data1.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table DS, lines 1477-1511) and
 * src/amd/compiler/aco_instruction_selection.cpp (visit_shared_atomic:
 * the compare before the data).
 */
#define AMDRDNA_GFX10_DS_WRITE_B32	0x0dU
#define AMDRDNA_GFX10_DS_ADD_RTN_U32	0x20U
#define AMDRDNA_GFX10_DS_SUB_RTN_U32	0x21U
#define AMDRDNA_GFX10_DS_MIN_RTN_I32	0x25U
#define AMDRDNA_GFX10_DS_MAX_RTN_I32	0x26U
#define AMDRDNA_GFX10_DS_MIN_RTN_U32	0x27U
#define AMDRDNA_GFX10_DS_MAX_RTN_U32	0x28U
#define AMDRDNA_GFX10_DS_AND_RTN_B32	0x29U
#define AMDRDNA_GFX10_DS_OR_RTN_B32	0x2aU
#define AMDRDNA_GFX10_DS_XOR_RTN_B32	0x2bU
#define AMDRDNA_GFX10_DS_WRXCHG_RTN_B32	0x2dU
#define AMDRDNA_GFX10_DS_CMPST_RTN_B32	0x30U
#define AMDRDNA_GFX10_DS_READ_B32	0x36U

/*
 * ==== Buffer opcodes ====
 *
 * A compare-and-swap takes the value stored in vdata and the value
 * compared in vdata + 1, and returns the old value in vdata.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table MUBUF, lines 1647-1722) and
 * src/amd/compiler/aco_instruction_selection.cpp (visit_image_atomic:
 * the data vector before the compare).
 */
#define AMDRDNA_GFX10_BUFFER_LOAD_FORMAT_XYZW 0x03U
#define AMDRDNA_GFX10_BUFFER_LOAD_DWORD	0x0cU
#define AMDRDNA_GFX10_BUFFER_STORE_DWORD 0x1cU
#define AMDRDNA_GFX10_BUFFER_ATOMIC_SWAP 0x30U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_CMPSWAP 0x31U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_ADD	0x32U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_SUB	0x33U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_SMIN 0x35U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_UMIN 0x36U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_SMAX 0x37U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_UMAX 0x38U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_AND	0x39U
#define AMDRDNA_GFX10_BUFFER_ATOMIC_OR	0x3aU
#define AMDRDNA_GFX10_BUFFER_ATOMIC_XOR	0x3bU
#define AMDRDNA_GFX10_BUFFER_GL0_INV	0x71U
#define AMDRDNA_GFX10_BUFFER_GL1_INV	0x72U

/*
 * ==== Image opcodes and the 2D dimension ====
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_opcodes.py
 * (the GFX10 numbers of the table MIMG, lines 1761-1824).
 * Checked against llvm-mc 19, which writes the dimension 1 as
 * "dim:SQ_RSRC_IMG_2D".
 */
#define AMDRDNA_GFX10_IMAGE_LOAD_MIP	0x01U
#define AMDRDNA_GFX10_IMAGE_GET_RESINFO	0x0eU
#define AMDRDNA_GFX10_IMAGE_SAMPLE	0x20U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_D	0x22U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_L	0x24U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_B	0x25U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_LZ	0x27U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C	0x28U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_D	0x2aU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_L	0x2cU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_B	0x2dU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_LZ	0x2fU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_O	0x30U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_D_O	0x32U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_L_O	0x34U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_B_O	0x35U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_LZ_O	0x37U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_O	0x38U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_D_O	0x3aU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_L_O	0x3cU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_B_O	0x3dU
#define AMDRDNA_GFX10_IMAGE_SAMPLE_C_LZ_O	0x3fU
#define AMDRDNA_GFX10_IMAGE_DIM_2D	1U

/*
 * ==== Export targets ====
 *
 * Colour target 0 (the others follow it), nothing, position 0 and
 * parameter 0.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10-rsrc.json
 * (the enum of the export targets, lines 169-174: 0, 9, 12 and 32).
 * Checked against llvm-mc 19.
 */
#define AMDRDNA_GFX10_EXPORT_COLOR	0U
#define AMDRDNA_GFX10_EXPORT_NOTHING	9U
#define AMDRDNA_GFX10_EXPORT_POSITION	12U
#define AMDRDNA_GFX10_EXPORT_PARAMETER	32U

/*
 * ==== The wait counters ====
 *
 * The 16-bit immediate of the counter wait: the vector memory count in
 * bits 3:0 with its high bits in 15:14, the export count in 6:4, the
 * local, scalar and message count in 13:8.  The value that waits for none
 * of a counter is its largest.  Stores are counted apart, by the store
 * counter wait.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_ir.cpp
 * (wait_imm::pack, the GFX10 case, lines 1185-1188).
 * Checked against llvm-mc 19.
 */
#define AMDRDNA_GFX10_WAIT_NONE		0xff7fU
#define AMDRDNA_GFX10_WAIT_VECTOR_MASK	0xc00fU
#define AMDRDNA_GFX10_WAIT_EXPORT_MASK	0x0070U
#define AMDRDNA_GFX10_WAIT_SCALAR_MASK	0x3f00U

/*
 * The immediates of the dependency wait that let the vector memory
 * instructions finish reading their scalar operands (needed before a
 * scalar instruction writes one of them), and that let a non-vector
 * instruction finish reading the lane mask (needed before a vector
 * instruction writes it).
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_insert_NOPs.cpp
 * (handle_instruction_gfx10: 0xffe3 for VMEMtoScalarWriteHazard, 0xfffe
 * for VcmpxExecWARHazard).
 */
#define AMDRDNA_GFX10_DEPEND_VECTOR_MEMORY_READS 0xffe3U
#define AMDRDNA_GFX10_DEPEND_LANE_MASK_READS	0xfffeU

/*
 * A branch whose distance (the words from the one after the branch to the
 * target) is this value misbehaves on GFX10; an idle instruction after the
 * branch moves the target.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_assembler.cpp
 * (fix_branches_gfx10, lines 1505-1524).
 */
#define AMDRDNA_GFX10_UNSAFE_BRANCH_DISTANCE 0x3fU

/*
 * The words the instruction prefetcher may read past the end of a program:
 * three cache lines of 16 words.
 *
 * Values referenced: Mesa 25.0.7 src/amd/common/ac_binary.c
 * (ac_align_shader_binary_for_prefetch: a distance of 3 lines of 64 bytes
 * on GFX10, lines 135-146).
 */
#define AMDRDNA_GFX10_PREFETCH_WORDS	48U

/*
 * ==== The program interface (zedBSD's own) ====
 *
 * Every program finds its resource table's address in s[0:1].  A vertex
 * program has the base vertex in s2, the first instance in s3 and the mask
 * of the locations fetched by instance in s4.  The hardware puts a compute
 * program's workgroup id x, y, z in the three SGPRs after the user SGPRs,
 * and a pixel program's primitive mask in the one after them.
 */
#define AMDRDNA_GFX10_ABI_TABLE		0U
#define AMDRDNA_GFX10_ABI_BASE_VERTEX	2U
#define AMDRDNA_GFX10_ABI_FIRST_INSTANCE 3U
#define AMDRDNA_GFX10_ABI_INSTANCE_MASK	4U
#define AMDRDNA_GFX10_ABI_USER_SGPRS	2U
#define AMDRDNA_GFX10_ABI_USER_SGPRS_VS	5U

/*
 * The parameter a vertex program exports the primitive number in when it
 * is asked to (a pixel program reads it flat from there), and the value
 * that names no attribute.
 */
#define AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER	31U
#define AMDRDNA_GFX10_NO_ATTRIBUTE		0xffffffffU

/* The bytes of one buffer descriptor (V#), image descriptor (T#) and sampler (S#). */
#define AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES	16U
#define AMDRDNA_GFX10_IMAGE_DESCRIPTOR_BYTES	32U
#define AMDRDNA_GFX10_SAMPLER_DESCRIPTOR_BYTES	16U

/* The most of each kind of resource one program binds, and the vertex input locations. */
#define AMDRDNA_GFX10_STORAGE_MAX	16U
#define AMDRDNA_GFX10_UNIFORM_MAX	8U
#define AMDRDNA_GFX10_TEXTURE_MAX	8U
#define AMDRDNA_GFX10_LOCATIONS		32U

#endif /* DRIVERS_GPU_AMDRDNA_COMPILER_GFX10_ISA_H */
