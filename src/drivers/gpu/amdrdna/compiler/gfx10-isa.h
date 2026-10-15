/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The facts of the RDNA1 (GFX10.1, gfx1010) instruction set the amdrdna
 * shader compiler uses (ws039-p007), and the program interface the compiler
 * and the driver agree on.
 *
 * The opcode numbers, the operand codes and the places of the fields in an
 * instruction word are data of the hardware.  Each block below names where
 * its values were read.  "Mesa" is Mesa 25.0.7 (tarball SHA-256
 * 592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6); the
 * values were also checked against LLVM's assembler (llvm-mc 19, -mcpu=gfx1010),
 * which the host test does again.  The program interface (AMDRDNA_GFX10_ABI_*)
 * is zedBSD's own.
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
 * Source: the RDNA1 ISA's scalar and vector source operand tables, as
 * Mesa src/amd/compiler/aco_ir.h (PhysReg, lines 422-445: vcc 106, m0 124,
 * sgpr_null 125, exec 126) and aco_assembler.cpp (emit_instruction, the literal
 * code 255) use them.
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
#define AMDRDNA_GFX10_HALF		240U
#define AMDRDNA_GFX10_MINUS_HALF	241U
#define AMDRDNA_GFX10_ONE		242U
#define AMDRDNA_GFX10_MINUS_ONE		243U
#define AMDRDNA_GFX10_TWO		244U
#define AMDRDNA_GFX10_MINUS_TWO		245U
#define AMDRDNA_GFX10_FOUR		246U
#define AMDRDNA_GFX10_MINUS_FOUR	247U
#define AMDRDNA_GFX10_INV_TWO_PI	248U
#define AMDRDNA_GFX10_LITERAL		255U
#define AMDRDNA_GFX10_VGPR_FIRST	256U
#define AMDRDNA_GFX10_VGPR_COUNT	256U

/* The code of SGPR n and of VGPR n. */
#define AMDRDNA_GFX10_S(n)		((unsigned)(n))
#define AMDRDNA_GFX10_V(n)		(AMDRDNA_GFX10_VGPR_FIRST + (unsigned)(n))

/*
 * The bits of the float inline constants (codes 240 .. 248).
 *
 * Source: the RDNA1 ISA's inline constant table; 1 / (2 pi) as Mesa
 * src/amd/compiler/aco_ir.h (Operand::c32, 0x3e22f983) gives it.
 */
#define AMDRDNA_GFX10_BITS_HALF		0x3f000000U
#define AMDRDNA_GFX10_BITS_ONE		0x3f800000U
#define AMDRDNA_GFX10_BITS_TWO		0x40000000U
#define AMDRDNA_GFX10_BITS_FOUR		0x40800000U
#define AMDRDNA_GFX10_BITS_INV_TWO_PI	0x3e22f983U

/*
 * ==== The encodings' fixed bits ====
 *
 * The high bits that mark each instruction format in its first word.
 *
 * Source: Mesa src/amd/compiler/aco_assembler.cpp, emit_instruction (the
 * GFX10 cases of each format), checked against llvm-mc.
 */
#define AMDRDNA_GFX10_ENC_SOP2		0x80000000U	/* bits 31:30 = 10 */
#define AMDRDNA_GFX10_ENC_SOPK		0xb0000000U	/* bits 31:28 = 1011 */
#define AMDRDNA_GFX10_ENC_SOP1		0xbe800000U	/* bits 31:23 = 101111101 */
#define AMDRDNA_GFX10_ENC_SOPC		0xbf000000U	/* bits 31:23 = 101111110 */
#define AMDRDNA_GFX10_ENC_SOPP		0xbf800000U	/* bits 31:23 = 101111111 */
#define AMDRDNA_GFX10_ENC_SMEM		0xf4000000U	/* bits 31:26 = 111101 */
#define AMDRDNA_GFX10_ENC_VOP1		0x7e000000U	/* bits 31:25 = 0111111 */
#define AMDRDNA_GFX10_ENC_VOPC		0x7c000000U	/* bits 31:25 = 0111110 */
#define AMDRDNA_GFX10_ENC_VOP2		0x00000000U	/* bit 31 = 0 */
#define AMDRDNA_GFX10_ENC_VOP3		0xd4000000U	/* bits 31:26 = 110101 */
#define AMDRDNA_GFX10_ENC_VINTRP	0xc8000000U	/* bits 31:26 = 110010 */
#define AMDRDNA_GFX10_ENC_DS		0xd8000000U	/* bits 31:26 = 110110 */
#define AMDRDNA_GFX10_ENC_MUBUF		0xe0000000U	/* bits 31:26 = 111000 */
#define AMDRDNA_GFX10_ENC_MIMG		0xf0000000U	/* bits 31:26 = 111100 */
#define AMDRDNA_GFX10_ENC_EXP		0xf8000000U	/* bits 31:26 = 111110 */

/*
 * A VOPC, VOP2 or VOP1 operation written in the VOP3 encoding takes its
 * opcode plus this offset.
 *
 * Source: Mesa src/amd/compiler/aco_assembler.cpp, emit_instruction
 * (lines 1053 and 1058: VOP2 + 0x100, VOP1 + 0x180) and README-ISA.md
 * ("VOP1 instructions encoded as VOP3": 0x180).
 */
#define AMDRDNA_GFX10_VOP3_FROM_VOPC	0x000U
#define AMDRDNA_GFX10_VOP3_FROM_VOP2	0x100U
#define AMDRDNA_GFX10_VOP3_FROM_VOP1	0x180U

/*
 * The DPP control word of a VOP1 or VOP2 whose src0 is the code 250, and
 * the row and bank masks that enable every lane.
 *
 * Source: the RDNA1 ISA's DPP table, as Mesa src/amd/compiler/aco_ir.h
 * (dpp_ctrl quad_perm 0x000 .. 0x0ff) and llvm-mc give it.
 */
#define AMDRDNA_GFX10_DPP_SRC		250U
#define AMDRDNA_GFX10_DPP_ALL_ROWS	0xff000000U

/*
 * ==== Scalar opcodes (GFX10 numbers) ====
 *
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the tables SOP2, SOPK,
 * SOP1, SOPC and SOPP (the gfx10 column).
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

#define AMDRDNA_GFX10_S_MOVK_I32	0x00U
#define AMDRDNA_GFX10_S_WAITCNT_VSCNT	0x17U

#define AMDRDNA_GFX10_S_MOV_B32		0x03U
#define AMDRDNA_GFX10_S_NOT_B32		0x07U
#define AMDRDNA_GFX10_S_WQM_B32		0x09U
#define AMDRDNA_GFX10_S_AND_SAVEEXEC_B32 0x3cU

#define AMDRDNA_GFX10_S_CMP_EQ_U32	0x06U
#define AMDRDNA_GFX10_S_CMP_LG_U32	0x07U
#define AMDRDNA_GFX10_S_BITCMP1_B32	0x0dU

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
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the table SMEM (gfx10).
 */
#define AMDRDNA_GFX10_S_LOAD_DWORD	0x00U
#define AMDRDNA_GFX10_S_LOAD_DWORDX2	0x01U
#define AMDRDNA_GFX10_S_LOAD_DWORDX4	0x02U
#define AMDRDNA_GFX10_S_LOAD_DWORDX8	0x03U
#define AMDRDNA_GFX10_S_BUFFER_LOAD_DWORD 0x08U

/*
 * ==== Vector ALU opcodes ====
 *
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the tables VOP2, VOP1 and
 * VOP3 (gfx10).  The VOPC numbers are the RDNA1 ISA's comparison table
 * (f32 0x00 .. 0x0f, i32 0x80 .. 0x87, u32 0xc0 .. 0xc7), which
 * aco_opcodes.py builds in its VOPC loop; llvm-mc agrees.
 */
#define AMDRDNA_GFX10_V_CNDMASK_B32	0x01U
#define AMDRDNA_GFX10_V_ADD_F32		0x03U
#define AMDRDNA_GFX10_V_SUB_F32		0x04U
#define AMDRDNA_GFX10_V_MUL_F32		0x08U
#define AMDRDNA_GFX10_V_MIN_F32		0x0fU
#define AMDRDNA_GFX10_V_MAX_F32		0x10U
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

#define AMDRDNA_GFX10_V_NOP		0x00U
#define AMDRDNA_GFX10_V_MOV_B32		0x01U
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

#define AMDRDNA_GFX10_V_FMA_F32		0x14bU
#define AMDRDNA_GFX10_V_MUL_LO_U32	0x169U
#define AMDRDNA_GFX10_V_MUL_HI_U32	0x16aU
#define AMDRDNA_GFX10_V_PACK_B32_F16	0x311U

/*
 * The modifier bits of a VOP3 instruction: one negate and one absolute
 * value bit per source, and the clamp bit.
 *
 * Source: Mesa src/amd/compiler/aco_assembler.cpp, emit_instruction (VOP3:
 * abs 10:8, clamp 15 of the first word; neg 31:29 of the second).
 */
#define AMDRDNA_GFX10_VOP3_NEG(n)	(1U << (n))
#define AMDRDNA_GFX10_VOP3_ABS(n)	(1U << (3 + (n)))

/*
 * ==== Interpolation opcodes ====
 *
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the table VINTRP (gfx10);
 * the parameter code of v_interp_mov_f32's P0 is the ISA's (2), which
 * llvm-mc writes for "p0".
 */
#define AMDRDNA_GFX10_V_INTERP_P1_F32	0x00U
#define AMDRDNA_GFX10_V_INTERP_P2_F32	0x01U
#define AMDRDNA_GFX10_V_INTERP_MOV_F32	0x02U
#define AMDRDNA_GFX10_INTERP_P0		2U

/*
 * ==== Local data share opcodes ====
 *
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the table DS (gfx10).  A
 * compare-and-store takes the value compared in data0 and the value stored
 * in data1 (aco_instruction_selection.cpp, visit_shared_atomic: operand 1
 * the compare, operand 2 the data).
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
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the table MUBUF (gfx10).
 * A compare-and-swap takes the value stored in vdata and the value
 * compared in vdata + 1, and returns the old value in vdata
 * (aco_instruction_selection.cpp, visit_image_atomic: the vector of the
 * data then the compare).
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
 * Source: Mesa src/amd/compiler/aco_opcodes.py, the table MIMG (gfx10);
 * the dimension code 1 (2D) is the ISA's, which llvm-mc writes for
 * dim:SQ_RSRC_IMG_2D.
 */
#define AMDRDNA_GFX10_IMAGE_LOAD_MIP	0x01U
#define AMDRDNA_GFX10_IMAGE_SAMPLE	0x20U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_L	0x24U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_B	0x25U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_LZ	0x27U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_O	0x30U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_L_O	0x34U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_B_O	0x35U
#define AMDRDNA_GFX10_IMAGE_SAMPLE_LZ_O	0x37U
#define AMDRDNA_GFX10_DIM_2D		1U

/*
 * ==== Export targets ====
 *
 * Source: Mesa src/amd/common/sid.h (V_008DFC_SQ_EXP_MRT 0, MRTZ 8,
 * NULL 9, POS 12, PARAM 32), checked against llvm-mc.
 */
#define AMDRDNA_GFX10_EXP_MRT		0U
#define AMDRDNA_GFX10_EXP_NULL		9U
#define AMDRDNA_GFX10_EXP_POS		12U
#define AMDRDNA_GFX10_EXP_PARAM		32U

/*
 * ==== The wait counters ====
 *
 * s_waitcnt's 16-bit immediate on GFX10: vmcnt in 3:0 with its high bits
 * in 15:14, expcnt in 6:4, lgkmcnt in 13:8; the value that waits for none
 * of a counter is its largest.  Stores are counted apart, by
 * s_waitcnt_vscnt.
 *
 * Source: Mesa src/amd/compiler/aco_insert_waitcnt.cpp and aco_ir.cpp
 * (wait_imm::pack, line 1177, the GFX10 layout), checked against llvm-mc.
 */
#define AMDRDNA_GFX10_WAIT_NONE		0xff7fU	/* vmcnt 63, expcnt 7, lgkmcnt 63 */
#define AMDRDNA_GFX10_WAIT_VM_MASK	0xc00fU
#define AMDRDNA_GFX10_WAIT_EXP_MASK	0x0070U
#define AMDRDNA_GFX10_WAIT_LGKM_MASK	0x3f00U

/*
 * The immediate of s_waitcnt_depctr that waits until the vector memory
 * instructions have read their scalar operands, the one VMEMtoScalarWrite
 * needs.
 *
 * Source: Mesa src/amd/compiler/aco_insert_NOPs.cpp,
 * handle_instruction_gfx10 (0xffe3).
 */
#define AMDRDNA_GFX10_DEPCTR_VM_VSRC	0xffe3U

/*
 * A branch whose offset is this value misbehaves on GFX10; one s_nop after
 * the branch moves the target.
 *
 * Source: Mesa src/amd/compiler/aco_assembler.cpp, fix_branches_gfx10.
 */
#define AMDRDNA_GFX10_BAD_BRANCH_OFFSET	0x3fU

/*
 * The words the instruction prefetcher may read past the end of a program:
 * three cache lines of 16 words.
 *
 * Source: Mesa src/amd/common/ac_binary.c,
 * ac_align_shader_binary_for_prefetch (GFX10: prefetch_distance 3).
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
