/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The nvrtx shader compiler for Turing (SM75), Ampere (SM86) and Ada
 * (SM89): the instruction set's facts, the encoder, the dependence
 * marking and the lowering from the shader IR (sm75.c).
 */

#ifndef DRIVERS_GPU_NVRTX_COMPILER_SM75_H
#define DRIVERS_GPU_NVRTX_COMPILER_SM75_H

#include "drivers/gpu/compiler/ir.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The facts of the Turing (SM75) instruction set the nvrtx shader compiler
 * uses (ws037-p008): where the fields of a 128-bit instruction lie, the
 * opcodes and the codes of their modifiers, the special registers, and the
 * constant buffer layout the compiler and the dispatch agree on.
 *
 * The constant buffer layout (NVRTX_SM75_CB_*) is zedBSD's own.
 */

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/headers/nvidia/classes/cla097sph.h, NVA097_SPHV3T1_* and
 * NVA097_SPHV3T2_* (lines 33-401, 403-638), and
 * src/nouveau/compiler/nak/sph.rs, ShaderProgramHeader::new (lines 94-111,
 * version 4 from SM73), set_sph_type to set_does_load_or_store (lines
 * 186-238), set_store_req_start and _end (lines 334-342), the map views
 * imap_* and omap_* (lines 114-185).
 */
/*
 * ==== The shader program header (version 4) ====
 *
 * 32 words before a graphics program's code: the type and version, what
 * the program does, and the maps of the attributes it reads and writes.
 */
#define NVRTX_SM75_SPH_WORDS		32U
#define NVRTX_SM75_SPH_BYTES		128U
#define NVRTX_SM75_SPH_TYPE_LOW		0U
#define NVRTX_SM75_SPH_TYPE_VTG		1U
#define NVRTX_SM75_SPH_TYPE_PS		2U
#define NVRTX_SM75_SPH_VERSION_LOW	5U
#define NVRTX_SM75_SPH_VERSION		4U
#define NVRTX_SM75_SPH_SHADER_TYPE_LOW	10U
#define NVRTX_SM75_SPH_VERTEX		1U
#define NVRTX_SM75_SPH_PIXEL		5U
#define NVRTX_SM75_SPH_MRT_ENABLE	14U
#define NVRTX_SM75_SPH_KILLS_PIXELS	15U
#define NVRTX_SM75_SPH_GLOBAL_STORE	16U
#define NVRTX_SM75_SPH_SASS_VERSION_LOW	17U
#define NVRTX_SM75_SPH_LOAD_OR_STORE	26U
#define NVRTX_SM75_SPH_STORE_REQ_START_LOW	140U
#define NVRTX_SM75_SPH_STORE_REQ_END_LOW	152U
#define NVRTX_SM75_SPH_IMAP_AB_LOW	160U
#define NVRTX_SM75_SPH_IMAP_GENERIC_LOW	192U
#define NVRTX_SM75_SPH_IMAP_C_VTG_LOW	336U
#define NVRTX_SM75_SPH_IMAP_C_PS_LOW	464U
#define NVRTX_SM75_SPH_OMAP_AB_LOW	400U
#define NVRTX_SM75_SPH_OMAP_GENERIC_LOW	432U
#define NVRTX_SM75_SPH_OMAP_TARGETS_LOW	576U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sph.rs, enum
 * PixelImap and its u8 values (lines 53-68).
 */
/* A fragment input's interpolation in the header's map: constant (flat), perspective, screen-linear. */
#define NVRTX_SM75_IMAP_CONSTANT	1U
#define NVRTX_SM75_IMAP_PERSPECTIVE	2U
#define NVRTX_SM75_IMAP_SCREEN_LINEAR	3U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs, is_turing,
 * is_ampere and is_ada (lines 9136-9148: Turing 73..79, Ampere 80..88, Ada
 * 89); the three models targeted are zedBSD's choice.
 */
/*
 * ==== The shader models (ws242-p002) ====
 *
 * The compiler targets Turing (SM75: RTX 20, GTX 16), Ampere (SM86: RTX 30)
 * and Ada (SM89: RTX 40).  The three share this file's encoding but for
 * the memory order of a global access (NVRTX_SM80_MEM_*), and Ampere and
 * Ada share one latency table that differs from Turing's
 * (sm75.c).
 */
#define NVRTX_SM_TURING			75U
#define NVRTX_SM_AMPERE			86U
#define NVRTX_SM_ADA			89U
#define NVRTX_SM_AMPERE_FIRST		80U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * encode_sm70_shader (lines 4148-4183): four 32-bit words to an instruction.
 */
/* An instruction is four 32-bit words, the first holding bit 0. */
#define NVRTX_SM75_INSTRUCTION_WORDS	4U
#define NVRTX_SM75_INSTRUCTION_BYTES	16U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70.rs, num_regs
 * and hw_reserved_gprs (lines 75-106),
 * src/nouveau/compiler/nak/sm70_encode.rs, zero_reg (lines 55-66), and
 * src/nouveau/compiler/nak/api.rs, the register count at least 4 (line 263).
 */
/* The general registers the compiler may assign (R0..R252), the zero register, and the two the hardware keeps. */
#define NVRTX_SM75_GPR_USABLE		253U
#define NVRTX_SM75_RZ			255U
#define NVRTX_SM75_GPR_RESERVED		2U
#define NVRTX_SM75_GPR_MINIMUM		4U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70.rs, num_regs
 * (RegFile::Pred, lines 75-99), and src/nouveau/compiler/nak/sm70_encode.rs,
 * set_pred_reg, set_pred_src_file and set_pred_src (lines 86-141).
 */
/* The predicates P0..P6, and PT, which is always true. */
#define NVRTX_SM75_P0			0U
#define NVRTX_SM75_PT			7U

/*
 * ==== The fields of an instruction (bit positions in the 128 bits) ====
 */

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_opcode (lines 68-70), set_pred (lines 197-208) and encode_alu_base
 * (lines 497-560, the 9-bit opcode and the form at 9..11).
 */
/* The opcode (12 bits), the guard predicate and its negation. */
#define NVRTX_SM75_OPCODE_LOW		0U
#define NVRTX_SM75_OPCODE_BITS		12U
#define NVRTX_SM75_ALU_OPCODE_BITS	9U
#define NVRTX_SM75_ALU_FORM_LOW		9U
#define NVRTX_SM75_ALU_FORM_BITS	3U
#define NVRTX_SM75_GUARD_LOW		12U
#define NVRTX_SM75_GUARD_NOT		15U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_dst (lines 210-216) and encode_alu_src0 (lines 419-431).
 */
/* The destination register and the first source register. */
#define NVRTX_SM75_DST_LOW		16U
#define NVRTX_SM75_SRC0_LOW		24U
#define NVRTX_SM75_REG_BITS		8U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * encode_alu_reg (lines 455-465) and encode_alu_base (lines 497-560).
 */
/* The second source: a register (32..39, abs 62, neg 63), a 32-bit immediate (32..63), or a constant buffer word. */
#define NVRTX_SM75_SRC1_LOW		32U
#define NVRTX_SM75_SRC1_ABS		62U
#define NVRTX_SM75_SRC1_NEG		63U
#define NVRTX_SM75_IMM_LOW		32U
#define NVRTX_SM75_IMM_BITS		32U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_src_cb (lines 179-195).
 */
/* A constant buffer operand: the byte offset (38..53) and the buffer (54..58); bit 91 clear for a bound buffer. */
#define NVRTX_SM75_CB_OFFSET_LOW	38U
#define NVRTX_SM75_CB_OFFSET_BITS	16U
#define NVRTX_SM75_CB_INDEX_LOW		54U
#define NVRTX_SM75_CB_INDEX_BITS	5U
#define NVRTX_SM75_CB_BINDLESS		91U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * encode_alu_src0 and encode_alu_src2 (lines 419-453).
 */
/* The first source's abs and neg, and the third source (64..71, abs 74, neg 75). */
#define NVRTX_SM75_SRC0_NEG		72U
#define NVRTX_SM75_SRC0_ABS		73U
#define NVRTX_SM75_SRC2_LOW		64U
#define NVRTX_SM75_SRC2_ABS		74U
#define NVRTX_SM75_SRC2_NEG		75U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * encode_alu_base, the form match (lines 516-558).
 */
/* The ALU forms: which of sources 1 and 2 is a register, an immediate or a constant buffer word. */
#define NVRTX_SM75_FORM_REG		1U
#define NVRTX_SM75_FORM_IMM_SRC2	2U
#define NVRTX_SM75_FORM_CB_SRC2		3U
#define NVRTX_SM75_FORM_IMM		4U
#define NVRTX_SM75_FORM_CB		5U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_pred_dst, set_pred_src_file and set_pred_src (lines 111-141), and the
 * predicate fields of OpFSetP (lines 883-913), OpISetP (lines 1596-1652),
 * OpSel (lines 2103-2140) and OpLop3 (lines 1794-1853).
 */
/* The predicate fields many ALU instructions have: two destinations and two sources with their negations. */
#define NVRTX_SM75_PDST0_LOW		81U
#define NVRTX_SM75_PDST1_LOW		84U
#define NVRTX_SM75_PSRC_A_LOW		87U
#define NVRTX_SM75_PSRC_A_NOT		90U
#define NVRTX_SM75_PSRC_B_LOW		77U
#define NVRTX_SM75_PSRC_B_NOT		80U
#define NVRTX_SM75_PSRC_C_LOW		68U
#define NVRTX_SM75_PSRC_C_NOT		71U
#define NVRTX_SM75_PRED_BITS		3U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_instr_deps (lines 242-251); src/nouveau/compiler/nak/sm70.rs,
 * max_instr_delay (lines 275-277);
 * src/nouveau/compiler/nak/calc_instr_deps.rs, BarAlloc, six scoreboards
 * (lines 186-195).
 */
/*
 * The scheduling control: the cycles before the next instruction issues,
 * the yield hint, the scoreboard the result releases (7: none), the one the
 * read of the sources releases (7: none), the scoreboards to wait for, and
 * the operand reuse cache.
 */
#define NVRTX_SM75_DELAY_LOW		105U
#define NVRTX_SM75_DELAY_BITS		4U
#define NVRTX_SM75_YIELD		109U
#define NVRTX_SM75_WRITE_BAR_LOW	110U
#define NVRTX_SM75_READ_BAR_LOW		113U
#define NVRTX_SM75_BAR_BITS		3U
#define NVRTX_SM75_WAIT_LOW		116U
#define NVRTX_SM75_WAIT_BITS		6U
#define NVRTX_SM75_REUSE_LOW		122U
#define NVRTX_SM75_BAR_NONE		7U
#define NVRTX_SM75_WAIT_ALL		0x3fU
#define NVRTX_SM75_DELAY_MAX		15U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs, the
 * encode of each operation: OpMov (lines 2029-2059), OpSel (2103-2140),
 * OpFMnMx (759-779), OpFSetP (883-913), OpISetP (1596-1652), OpIAdd3
 * (1350-1398), OpLop3 (1794-1853), OpShf (1871-1918), OpFMul (780-803),
 * OpFAdd (703-734), OpFFma (735-758), OpIMad (1499-1530), OpIMad64
 * (1531-1562), OpF2I (1969-1989), OpI2F (1990-2009), OpFRnd (2010-2028),
 * OpMuFu (950-974), OpLd (3039-3081), OpSt (3172-3211), OpNop (3753-3762),
 * OpS2R (3786-3802), OpBra (3635-3661), OpExit (3662-3677), OpLdc
 * (3082-3171), OpAtom (3275-3359), OpMemBar (3507-3528), OpBar (3689-3716),
 * OpBSSy (3609-3622), OpBSync (3623-3634), OpALd (3377-3396), OpASt
 * (3397-3415), OpIpa (3416-3456), OpTex (2499-2566), OpTld (2567-2641),
 * OpKill (3742-3752).
 */
/*
 * ==== Opcodes ====
 *
 * An ALU opcode is 9 bits beside its form; the others are 12 bits.
 */
#define NVRTX_SM75_OP_MOV		0x002U
#define NVRTX_SM75_OP_SEL		0x007U
#define NVRTX_SM75_OP_FMNMX		0x009U
#define NVRTX_SM75_OP_FSETP		0x00bU
#define NVRTX_SM75_OP_ISETP		0x00cU
#define NVRTX_SM75_OP_IADD3		0x010U
#define NVRTX_SM75_OP_LOP3		0x012U
#define NVRTX_SM75_OP_SHF		0x019U
#define NVRTX_SM75_OP_FMUL		0x020U
#define NVRTX_SM75_OP_FADD		0x021U
#define NVRTX_SM75_OP_FFMA		0x023U
#define NVRTX_SM75_OP_IMAD		0x024U
#define NVRTX_SM75_OP_IMAD_WIDE		0x025U
#define NVRTX_SM75_OP_F2I		0x105U
#define NVRTX_SM75_OP_I2F		0x106U
#define NVRTX_SM75_OP_FRND		0x107U
#define NVRTX_SM75_OP_MUFU		0x108U
#define NVRTX_SM75_OP_LDG		0x381U
#define NVRTX_SM75_OP_STG		0x386U
#define NVRTX_SM75_OP_NOP		0x918U
#define NVRTX_SM75_OP_S2R		0x919U
#define NVRTX_SM75_OP_BRA		0x947U
#define NVRTX_SM75_OP_EXIT		0x94dU
#define NVRTX_SM75_OP_LDC		0xb82U
#define NVRTX_SM75_OP_ATOMG		0x3a8U
#define NVRTX_SM75_OP_ATOMG_CAS		0x3a9U
#define NVRTX_SM75_OP_ATOMS		0x38cU
#define NVRTX_SM75_OP_ATOMS_CAS		0x38dU
#define NVRTX_SM75_OP_LDS		0x984U
#define NVRTX_SM75_OP_STS		0x388U
#define NVRTX_SM75_OP_MEMBAR		0x992U
#define NVRTX_SM75_OP_BAR		0xb1dU
#define NVRTX_SM75_OP_BSSY		0x945U
#define NVRTX_SM75_OP_BSYNC		0x941U
#define NVRTX_SM75_OP_ALD		0x321U
#define NVRTX_SM75_OP_AST		0x322U
#define NVRTX_SM75_OP_IPA		0x326U
#define NVRTX_SM75_OP_TEX		0xb60U
#define NVRTX_SM75_OP_TLD		0xb66U
#define NVRTX_SM75_OP_KILL		0x95bU

/*
 * ==== The modifiers of the instructions ====
 */

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpMov (lines 2029-2059).
 */
/* MOV: the lanes of a quad written (all four). */
#define NVRTX_SM75_MOV_LANES_LOW	72U
#define NVRTX_SM75_MOV_LANES_ALL	0xfU

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_rnd_mode (lines 633-644) and the ftz bit of OpFAdd, OpFMul and OpFFma
 * (lines 703-803).
 */
/* The float rounding (78..79): to nearest even, toward -inf, toward +inf, toward zero; flush to zero (80). */
#define NVRTX_SM75_ROUND_LOW		78U
#define NVRTX_SM75_ROUND_NEAREST	0U
#define NVRTX_SM75_ROUND_DOWN		1U
#define NVRTX_SM75_ROUND_UP		2U
#define NVRTX_SM75_ROUND_ZERO		3U
#define NVRTX_SM75_FTZ			80U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpFMul (lines 780-803).
 */
/* FMUL: the post-divide field holds 4 (none). */
#define NVRTX_SM75_FMUL_PDIV_LOW	84U
#define NVRTX_SM75_FMUL_PDIV_NONE	4U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_float_cmp_op (lines 805-826), set_pred_set_op (lines 828-838) and
 * set_int_cmp_op (lines 840-855).
 */
/* The comparisons of FSETP (76..79) and ISETP (76..78), and how the result combines with the accumulator (74..75). */
#define NVRTX_SM75_CMP_LOW		76U
#define NVRTX_SM75_FCMP_LT		0x1U
#define NVRTX_SM75_FCMP_EQ		0x2U
#define NVRTX_SM75_FCMP_GE		0x6U
#define NVRTX_SM75_FCMP_UNORD_NE	0xdU
#define NVRTX_SM75_ICMP_LT		1U
#define NVRTX_SM75_ICMP_EQ		2U
#define NVRTX_SM75_ICMP_NE		5U
#define NVRTX_SM75_ICMP_GE		6U
#define NVRTX_SM75_SETOP_LOW		74U
#define NVRTX_SM75_SETOP_AND		0U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpISetP (lines 1596-1652) and OpIMad (lines 1499-1530).
 */
/* ISETP and IMAD: bit 73 set for a signed comparison or product. */
#define NVRTX_SM75_SIGNED		73U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpLop3 (lines 1794-1853).
 */
/* LOP3: the truth table (72..79) over sources a (0xf0), b (0xcc) and c (0xaa). */
#define NVRTX_SM75_LUT_LOW		72U
#define NVRTX_SM75_LUT_A		0xf0U
#define NVRTX_SM75_LUT_B		0xccU
#define NVRTX_SM75_LUT_C		0xaaU

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpShf (lines 1871-1918).
 */
/* SHF: the data type (73..74), the wrap of the shift count (75), right (76), the high half (80). */
#define NVRTX_SM75_SHF_TYPE_LOW		73U
#define NVRTX_SM75_SHF_TYPE_I32		2U
#define NVRTX_SM75_SHF_TYPE_U32		3U
#define NVRTX_SM75_SHF_WRAP		75U
#define NVRTX_SM75_SHF_RIGHT		76U
#define NVRTX_SM75_SHF_HIGH		80U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpMuFu (lines 950-974).
 */
/* MUFU: the function (74..79). */
#define NVRTX_SM75_MUFU_LOW		74U
#define NVRTX_SM75_MUFU_COS		0U
#define NVRTX_SM75_MUFU_SIN		1U
#define NVRTX_SM75_MUFU_EX2		2U
#define NVRTX_SM75_MUFU_LG2		3U
#define NVRTX_SM75_MUFU_RCP		4U
#define NVRTX_SM75_MUFU_RSQ		5U
#define NVRTX_SM75_MUFU_SQRT		8U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpF2I and OpI2F (lines 1969-2009).
 */
/* The conversions: F2I signed (72), I2F signed (74), the result's size (75..76) and the source's (84..85) as log2 of bytes. */
#define NVRTX_SM75_F2I_SIGNED		72U
#define NVRTX_SM75_I2F_SIGNED		74U
#define NVRTX_SM75_CVT_DST_SIZE_LOW	75U
#define NVRTX_SM75_CVT_SRC_SIZE_LOW	84U
#define NVRTX_SM75_CVT_SIZE_32		2U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpS2R (lines 3786-3802), and src/nouveau/compiler/nak_private.h,
 * NAK_SV_TID_* and NAK_SV_CTAID_* (lines 115-122).
 */
/* The special registers S2R reads (72..79): the thread's place in its group and the group's place in the grid. */
#define NVRTX_SM75_S2R_INDEX_LOW	72U
#define NVRTX_SM75_SR_TID_X		0x21U
#define NVRTX_SM75_SR_TID_Y		0x22U
#define NVRTX_SM75_SR_TID_Z		0x23U
#define NVRTX_SM75_SR_CTAID_X		0x25U
#define NVRTX_SM75_SR_CTAID_Y		0x26U
#define NVRTX_SM75_SR_CTAID_Z		0x27U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_mem_order (lines 2854-2891, the SM75 branch 2855-2878),
 * set_eviction_priority (lines 2893-2905), set_mem_type (lines 2907-2921),
 * set_mem_access (lines 2923-2934), OpLd and OpSt (lines 3039-3081,
 * 3172-3211).
 */
/*
 * The memory accesses: a 64-bit address (72), the size (73..75: 4 for 32
 * bits, 5 for 64), the scope (77..78: 2 the GPU) and the order (79..80: 2
 * strong) before SM80, the eviction priority (84..86: 1 normal), and the
 * immediate offset of a global access (40..63).
 */
#define NVRTX_SM75_MEM_A64		72U
#define NVRTX_SM75_MEM_TYPE_LOW		73U
#define NVRTX_SM75_MEM_B32		4U
#define NVRTX_SM75_MEM_B64		5U
#define NVRTX_SM75_MEM_SCOPE_LOW	77U
#define NVRTX_SM75_MEM_SCOPE_GPU	2U
#define NVRTX_SM75_MEM_ORDER_LOW	79U
#define NVRTX_SM75_MEM_ORDER_STRONG	2U
#define NVRTX_SM75_MEM_EVICT_LOW	84U
#define NVRTX_SM75_MEM_EVICT_NORMAL	1U
#define NVRTX_SM75_MEM_OFFSET_LOW	40U
#define NVRTX_SM75_MEM_OFFSET_BITS	24U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_mem_order, the SM80 branch (lines 2879-2890).
 */
/* From SM80 on, the scope and the order are one field (77..80): 7 is strong at the GPU's scope. */
#define NVRTX_SM80_MEM_ORDER_LOW	77U
#define NVRTX_SM80_MEM_ORDER_BITS	4U
#define NVRTX_SM80_MEM_STRONG_GPU	0x7U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpLdc (lines 3082-3171) and OpSt (lines 3172-3211).
 */
/* LDC: the mode (78..79, 0 indexed) and the data register a store reads (32..39). */
#define NVRTX_SM75_LDC_MODE_LOW		78U
#define NVRTX_SM75_STORE_DATA_LOW	32U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * set_atom_op (lines 3213-3229), set_atom_type (lines 3231-3272) and OpAtom
 * (lines 3275-3359).
 */
/*
 * The atomics: the operation (87..90), the type (73..75: 0 unsigned, 1
 * signed 32 bits), and the compare and exchange's second data register
 * (64..71, the compared value at 32..39).
 */
#define NVRTX_SM75_ATOM_OP_LOW		87U
#define NVRTX_SM75_ATOM_ADD		0U
#define NVRTX_SM75_ATOM_MIN		1U
#define NVRTX_SM75_ATOM_MAX		2U
#define NVRTX_SM75_ATOM_AND		5U
#define NVRTX_SM75_ATOM_OR		6U
#define NVRTX_SM75_ATOM_XOR		7U
#define NVRTX_SM75_ATOM_EXCH		8U
#define NVRTX_SM75_ATOM_TYPE_LOW	73U
#define NVRTX_SM75_ATOM_U32		0U
#define NVRTX_SM75_ATOM_I32		1U
#define NVRTX_SM75_CAS_DATA_LOW		64U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpMemBar (lines 3507-3528).
 */
/* MEMBAR: the scope (76..78): 0 the workgroup, 2 the GPU. */
#define NVRTX_SM75_MEMBAR_SCOPE_LOW	76U
#define NVRTX_SM75_MEMBAR_CTA		0U
#define NVRTX_SM75_MEMBAR_GPU		2U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpBSSy and OpBSync (lines 3609-3634) and set_rel_offset (lines 3541-3544).
 */
/* BSSY and BSYNC: the convergence barrier (16..19), BSSY's offset to its BSYNC in words from the next instruction (34..63). */
#define NVRTX_SM75_CONVERGE_BAR_LOW	16U
#define NVRTX_SM75_CONVERGE_BARS	16U
#define NVRTX_SM75_BSSY_OFFSET_LOW	34U
#define NVRTX_SM75_BSSY_OFFSET_BITS	30U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpALd and OpASt (lines 3377-3415).
 */
/*
 * The attribute loads and stores of the vertex stage: ALD's vertex (32..39)
 * and AST's (64..71), the offset register (24..31, RZ), the attribute
 * address (40..49) and the components less one (74..75); AST's data
 * (32..39).
 */
#define NVRTX_SM75_ATTR_ADDRESS_LOW	40U
#define NVRTX_SM75_ATTR_ADDRESS_BITS	10U
#define NVRTX_SM75_ATTR_COMPONENTS_LOW	74U
#define NVRTX_SM75_AST_VERTEX_LOW	64U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpIpa (lines 3416-3456).
 */
/* IPA: the attribute's address in words (64..71), the location (76..77, 0 the pixel's centre) and the frequency (78..79: 0 pass, 1 constant). */
#define NVRTX_SM75_IPA_ADDRESS_LOW	64U
#define NVRTX_SM75_IPA_FREQUENCY_LOW	78U
#define NVRTX_SM75_IPA_PASS		0U
#define NVRTX_SM75_IPA_CONSTANT		1U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpTex (lines 2499-2566), set_tex_cb_ref (lines 2351-2357), set_tex_dim
 * (lines 2359-2373), set_tex_lod_mode (lines 2375-2390), set_tex_channel_mask
 * (lines 2460-2466) and set_eviction_priority (lines 2893-2905).
 */
/*
 * TEX with its handle in a constant buffer: the handle's word offset
 * (40..53) and buffer (54..58), the second destination pair (64..71, RZ for
 * none), the coordinates' first register (24..31) and the second source
 * (32..39, the level of detail), the dimension (61..63: 1 2D), the
 * channels (72..75), the level of detail mode (87..89: 0 implicit, 1 zero,
 * 2 bias, 3 explicit), the fault predicate (81..83) and the eviction
 * priority (84..86).
 */
#define NVRTX_SM75_TEX_HANDLE_LOW	40U
#define NVRTX_SM75_TEX_HANDLE_BITS	14U
#define NVRTX_SM75_TEX_BUFFER_LOW	54U
#define NVRTX_SM75_TEX_DST2_LOW		64U
#define NVRTX_SM75_TEX_DIM_LOW		61U
#define NVRTX_SM75_TEX_DIM_2D		1U
#define NVRTX_SM75_TEX_CHANNELS_LOW	72U
#define NVRTX_SM75_TEX_LOD_LOW		87U
#define NVRTX_SM75_TEX_LOD_AUTO		0U
#define NVRTX_SM75_TEX_LOD_ZERO		1U
#define NVRTX_SM75_TEX_LOD_BIAS		2U
#define NVRTX_SM75_TEX_LOD_EXPLICIT	3U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpTex and OpTld (lines 2499-2641, AOFFI at bit 76), and
 * src/nouveau/compiler/nak_nir_lower_tex.c, the offsets packed 4 bits a
 * component (lines 230-243).
 */
/*
 * A constant texel offset (bit 76, AOFFI): the register after the level of
 * detail (or the second source alone) holds u in bits 3..0, v in 7..4 and r
 * in 11..8, each a signed 4-bit number.  TLD (texel
 * fetch) has the TEX fields with an explicit level of detail mode.
 */
#define NVRTX_SM75_TEX_AOFFI		76U
#define NVRTX_SM75_TEX_OFFSET_BITS	4U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak_private.h,
 * NAK_ATTR_* (lines 45-77).
 */
/* The attribute addresses: the position, the point size, the first generic one (16 bytes each), the instance and the vertex numbers. */
#define NVRTX_SM75_ATTR_POINT_SIZE	0x06cU
#define NVRTX_SM75_ATTR_POSITION	0x070U
#define NVRTX_SM75_ATTR_POSITION_W	0x07cU
#define NVRTX_SM75_ATTR_GENERIC		0x080U
#define NVRTX_SM75_ATTR_GENERIC_END	0x280U
#define NVRTX_SM75_ATTR_INSTANCE_ID	0x2f8U
#define NVRTX_SM75_ATTR_VERTEX_ID	0x2fcU

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs,
 * OpBra (lines 3635-3661) and set_rel_offset (lines 3541-3545).
 */
/* BRA: the offset to the target in words from the next instruction (34..81). */
#define NVRTX_SM75_BRA_OFFSET_LOW	34U
#define NVRTX_SM75_BRA_OFFSET_BITS	48U

/* zedBSD's own. */
/*
 * ==== The constant buffer layout the compiler and the dispatch agree on (zedBSD's) ====
 *
 * Constant buffer 0 holds the push constants from byte 0, and from
 * NVRTX_SM75_CB_STORAGE one 16-byte entry per storage buffer the shader
 * uses: the buffer's GPU address (low word, high word) and its size in
 * bytes.  Constant buffers 1 to 7 are the uniform buffers, in the order the
 * shader's uniform list names them.
 */
#define NVRTX_SM75_CB_ROOT		0U
#define NVRTX_SM75_CB_PUSH		0x000U
#define NVRTX_SM75_CB_PUSH_BYTES	0x100U
#define NVRTX_SM75_CB_STORAGE		0x200U
#define NVRTX_SM75_CB_STORAGE_ENTRY	16U
#define NVRTX_SM75_CB_STORAGE_SIZE	8U
#define NVRTX_SM75_CB_STORAGE_MAX	32U
#define NVRTX_SM75_CB_UNIFORM_FIRST	1U
#define NVRTX_SM75_CB_UNIFORM_MAX	7U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_tex.c
 * (lines 155-160) and src/nouveau/vulkan (the image and sampler descriptors):
 * the handle's bits, header 19:0 and sampler 31:20.  The placement in
 * constant buffer 0 is zedBSD's own.
 */
/*
 * The texture handles: from NVRTX_SM75_CB_TEXTURES of constant buffer 0, one
 * word per sampled image the shader uses, in the order of its uniform list
 * (the dispatch writes the texture header index in bits 19:0 and the
 * sampler's in 31:20).
 */
#define NVRTX_SM75_CB_TEXTURES		0x400U
#define NVRTX_SM75_CB_TEXTURES_MAX	32U

/*
 * The encoder of Turing (SM75) instructions (sm75.c, ws037-p008).
 *
 * Each function fills one 128-bit instruction from operands that are
 * already physical registers, immediates or constant buffer words.  An
 * instruction starts with nvrtx_sm75_begin() (zero, guarded by PT, no
 * scoreboard), gets its operation from one encode function, and may then
 * get a guard predicate and its scheduling control.  Nothing here allocates
 * or fails: the caller checks its operands first.  The field positions are
 * in sm75.h.
 */

/* What an ALU source operand is. */
#define NVRTX_SM75_SRC_NONE	0
#define NVRTX_SM75_SRC_REG	1
#define NVRTX_SM75_SRC_IMM	2
#define NVRTX_SM75_SRC_CB	3

/*
 * One instruction: four words, the first holding bits 0..31.
 *
 * The caller owns it; it is copied into the program's code when complete.
 */
struct nvrtx_sm75_word {
	uint32_t w[4];
};

/*
 * One source operand of an ALU instruction.
 *
 * value is the register number (RZ for zero), the immediate's bits, or the
 * byte offset in constant buffer cb_index.  neg and abs are the source
 * modifiers the operand's slot has.
 */
struct nvrtx_sm75_src {
	int kind;
	uint32_t value;
	uint32_t cb_index;
	int neg;
	int abs;
};

/*
 * ==== Fields ====
 */

/* Writes a field of up to 64 bits at a bit position. */
void nvrtx_sm75_set(struct nvrtx_sm75_word *word, unsigned low, unsigned bits, uint64_t value);

/* Reads a field of up to 64 bits at a bit position. */
uint64_t nvrtx_sm75_get(const struct nvrtx_sm75_word *word, unsigned low, unsigned bits);

/* Starts an instruction: all bits zero, guarded by PT, no scoreboard. */
void nvrtx_sm75_begin(struct nvrtx_sm75_word *word);

/* Guards an instruction by a predicate, or its negation. */
void nvrtx_sm75_guard(struct nvrtx_sm75_word *word, unsigned predicate, int negate);

/* Sets the scheduling control: the delay, the yield, the scoreboards written and read (7: none), and the ones waited for. */
void nvrtx_sm75_control(struct nvrtx_sm75_word *word, unsigned delay, int yield, unsigned write_bar, unsigned read_bar, unsigned wait_mask);

/* Makes a register, an immediate or a constant buffer source. */
struct nvrtx_sm75_src nvrtx_sm75_reg(uint32_t reg);
struct nvrtx_sm75_src nvrtx_sm75_imm(uint32_t bits);
struct nvrtx_sm75_src nvrtx_sm75_cb(uint32_t index, uint32_t offset);

/*
 * ==== Instructions ====
 */

/* The ALU form shared by most arithmetic: dst, then sources 0, 1 and 2 placed by the form they make. */
void nvrtx_sm75_alu(struct nvrtx_sm75_word *word, uint32_t opcode, uint32_t dst, const struct nvrtx_sm75_src *src0, const struct nvrtx_sm75_src *src1, const struct nvrtx_sm75_src *src2);

/* MOV dst, src. */
void nvrtx_sm75_mov(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *src);

/* FADD, FMUL and FFMA (round to nearest, denormals kept). */
void nvrtx_sm75_fadd(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b);
void nvrtx_sm75_fmul(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b);
void nvrtx_sm75_ffma(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, const struct nvrtx_sm75_src *c);

/* FMNMX: the smaller of a and b when min, else the larger. */
void nvrtx_sm75_fmnmx(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, int min);

/* FSETP and ISETP: a predicate from a comparison (NVRTX_SM75_FCMP_*, NVRTX_SM75_ICMP_*), ANDed with PT. */
void nvrtx_sm75_fsetp(struct nvrtx_sm75_word *word, uint32_t predicate, uint32_t compare, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b);
void nvrtx_sm75_isetp(struct nvrtx_sm75_word *word, uint32_t predicate, uint32_t compare, int is_signed, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b);

/* IADD3 dst = a + b + c (no carries). */
void nvrtx_sm75_iadd3(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, const struct nvrtx_sm75_src *c);

/* IMAD dst = a * b + c, the low 32 bits; IMAD.WIDE.U32 the 64-bit dst pair = a * b + the c pair. */
void nvrtx_sm75_imad(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, const struct nvrtx_sm75_src *c, int is_signed);
void nvrtx_sm75_imad_wide(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, const struct nvrtx_sm75_src *c);

/* LOP3 dst = lut(a, b, c). */
void nvrtx_sm75_lop3(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, const struct nvrtx_sm75_src *c, uint32_t lut);

/* SHF: a 32-bit shift of value by count, left or right (logical or arithmetic), the count taken modulo 32. */
void nvrtx_sm75_shift(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t value, const struct nvrtx_sm75_src *count, int right, int arithmetic);

/* SEL dst = predicate (or its negation) ? a : b. */
void nvrtx_sm75_sel(struct nvrtx_sm75_word *word, uint32_t dst, const struct nvrtx_sm75_src *a, const struct nvrtx_sm75_src *b, uint32_t predicate, int negate);

/* MUFU dst = function(src) (NVRTX_SM75_MUFU_*). */
void nvrtx_sm75_mufu(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t function, uint32_t src);

/* I2F, F2I and FRND between 32-bit values, with a rounding (NVRTX_SM75_ROUND_*). */
void nvrtx_sm75_i2f(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t src, int is_signed);
void nvrtx_sm75_f2i(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t src, int is_signed);
void nvrtx_sm75_frnd(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t src, uint32_t rounding);

/* S2R dst = a special register (NVRTX_SM75_SR_*). */
void nvrtx_sm75_s2r(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t special);

/* LDC dst = the 32- or 64-bit word at offset in constant buffer index (a 64-bit result fills an even register pair). */
void nvrtx_sm75_ldc(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t index, uint32_t offset, int wide);

/* LDG.E and STG.E of a 32-bit word at the 64-bit address in a register pair. */
void nvrtx_sm75_ldg(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t address);
void nvrtx_sm75_stg(struct nvrtx_sm75_word *word, uint32_t address, uint32_t data);

/*
 * Sets a global access's memory order (strong at the GPU's scope) in the
 * form of a shader model: SM75's scope and order fields, or from SM80 on
 * their one field.  The global encoders above write SM75's form.
 */
void nvrtx_sm75_memory_order(struct nvrtx_sm75_word *word, uint32_t sm);

/* LDS and STS of a 32-bit word at a byte offset of the workgroup's shared memory in a register. */
void nvrtx_sm75_lds(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t offset);
void nvrtx_sm75_sts(struct nvrtx_sm75_word *word, uint32_t offset, uint32_t data);

/* An atomic on a 32-bit word: global (the address a register pair) or shared (the offset a register), returning the old value. */
void nvrtx_sm75_atomic(struct nvrtx_sm75_word *word, int shared, uint32_t dst, uint32_t address, uint32_t data, uint32_t operation, int is_signed);

/* A compare and exchange: where the word equals compare, data is written; the old value is returned. */
void nvrtx_sm75_atomic_cas(struct nvrtx_sm75_word *word, int shared, uint32_t dst, uint32_t address, uint32_t compare, uint32_t data);

/* MEMBAR of a scope (NVRTX_SM75_MEMBAR_*), and BAR.SYNC of barrier 0 for the whole workgroup. */
void nvrtx_sm75_membar(struct nvrtx_sm75_word *word, uint32_t scope);
void nvrtx_sm75_bar(struct nvrtx_sm75_word *word);

/* BSSY: the convergence barrier collects the threads at the BSYNC so many instructions after the next one; BSYNC waits for them. */
void nvrtx_sm75_bssy(struct nvrtx_sm75_word *word, uint32_t barrier, int32_t instructions);
void nvrtx_sm75_bsync(struct nvrtx_sm75_word *word, uint32_t barrier);

/* ALD dst = one word of the vertex's input attribute at an address; AST: one word of the output attribute. */
void nvrtx_sm75_ald(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t address);
void nvrtx_sm75_ast(struct nvrtx_sm75_word *word, uint32_t address, uint32_t data);

/* IPA dst = the fragment's interpolation of the attribute at an address: passed (pass) or the provoking vertex's (constant). */
void nvrtx_sm75_ipa(struct nvrtx_sm75_word *word, uint32_t dst, uint32_t address, uint32_t frequency);

/*
 * TEX.2D of the texture whose handle is the word at offset of constant
 * buffer index, at the coordinates in the pair from coordinates: the four
 * channels into the pairs first and second, with a level of detail mode and
 * its register (RZ for none).
 */
void nvrtx_sm75_tex(struct nvrtx_sm75_word *word, uint32_t first, uint32_t second, uint32_t coordinates, uint32_t lod, uint32_t lod_mode, uint32_t index, uint32_t offset);

/* Makes a TEX a TLD (a texel fetch at integer coordinates, an explicit level of detail mode). */
void nvrtx_sm75_tex_fetch(struct nvrtx_sm75_word *word);

/* Makes a TEX or a TLD take a constant texel offset from the register after its level of detail (AOFFI). */
void nvrtx_sm75_tex_offset(struct nvrtx_sm75_word *word);

/* KILL of the fragment (under the instruction's guard). */
void nvrtx_sm75_kill(struct nvrtx_sm75_word *word);

/* Makes a BRA conditional: it branches where the predicate (or its negation) holds. */
void nvrtx_sm75_bra_if(struct nvrtx_sm75_word *word, uint32_t predicate, int negate);

/* EXIT, NOP, and BRA by a signed number of instructions from the next one. */
void nvrtx_sm75_exit(struct nvrtx_sm75_word *word);
void nvrtx_sm75_nop(struct nvrtx_sm75_word *word);
void nvrtx_sm75_bra(struct nvrtx_sm75_word *word, int32_t instructions);

/*
 * The dependence marking of the nvrtx shader compiler (sm75.c,
 * ws037-p008 stage D).
 *
 * After the lowering has emitted a program, this pass sets every
 * instruction's scheduling control (sm75.h, NVRTX_SM75_DELAY_LOW ..):
 * a fixed-latency result holds its reader back only as many cycles as
 * the shader model's latency table says for that writer and reader (Turing's,
 * or the one Ampere and Ada share, ws242-p002), and a
 * variable-latency instruction releases a scoreboard of its own that only
 * the instructions that need its result (or its sources' registers) wait
 * for.  Branches, convergence barriers, barriers, discards, EXIT and the
 * targets of branches start a new block with every result complete.
 *
 * The pass reads the instructions back from their words: it knows the
 * operands of every instruction the lowering emits and refuses any other.
 */

/*
 * How an instruction's results become ready (Turing's latency classes).
 *
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs (the latency classes).
 */
#define NVRTX_SM75_CLASS_ALU		0U
#define NVRTX_SM75_CLASS_FMA		1U
#define NVRTX_SM75_CLASS_IMAD		2U
#define NVRTX_SM75_CLASS_IMAD_WIDE	3U
#define NVRTX_SM75_CLASS_DISPATCH	4U
#define NVRTX_SM75_CLASS_DECOUPLED	5U

/*
 * A read by a variable-latency instruction of the address unit (the global
 * and shared memory accesses and the attribute stores), which Ampere's and
 * Ada's table tells from the other variable-latency reads; only a read
 * has it, the instruction's class is NVRTX_SM75_CLASS_DECOUPLED.
 *
 * Value referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs (DecoupledAgu).
 */
#define NVRTX_SM75_CLASS_AGU		6U

/* The most registers and predicates one instruction reads and writes. */
#define NVRTX_SM75_OPERANDS_READS	8U
#define NVRTX_SM75_OPERANDS_WRITES	4U

/*
 * What one instruction reads and writes, as the dependence marking sees
 * it: the general registers (RZ left out) with, for each read, the class
 * the read counts as (an IMAD.WIDE's addend reads late); the guard
 * predicate and the predicate sources (PT left out); the predicate it
 * writes; its latency class; whether it ends a block; and the fewest
 * cycles before the next instruction may issue.
 */
struct nvrtx_sm75_operands {
	uint32_t reads[NVRTX_SM75_OPERANDS_READS];
	uint32_t read_class[NVRTX_SM75_OPERANDS_READS];
	uint32_t read_count;
	uint32_t writes[NVRTX_SM75_OPERANDS_WRITES];
	uint32_t write_count;
	uint32_t guard;
	uint32_t predicate_read;
	uint32_t predicate_write;
	uint32_t class_of;
	int boundary;
	uint32_t min_delay;
};

/* Reads an instruction's operands from its four words; nonzero for an instruction the pass does not know. */
int nvrtx_sm75_operands(const uint32_t *words, struct nvrtx_sm75_operands *operands);

/*
 * Reorders the instructions inside each block of a program for a shader
 * model (NVRTX_SM_*) so that the long-latency ones issue early; nonzero,
 * with the instruction and a reason, when one is not known or there is no
 * memory.  Branches, convergence and barriers, NOPs and the blocks' bounds
 * keep their places, so every branch offset stays valid.
 */
int nvrtx_sm75_reorder(uint32_t *code, uint32_t count, uint32_t sm, uint32_t *instruction, const char **reason);

/*
 * Sets the scheduling control of a program's instructions for a shader
 * model (NVRTX_SM_*); nonzero, with the instruction and a reason, when one
 * is not known or a delay would not fit its field.
 */
int nvrtx_sm75_schedule(uint32_t *code, uint32_t count, uint32_t sm, uint32_t *instruction, const char **reason);

/*
 * The nvrtx shader compiler: SPIR-V to Turing (SM75) code (sm75.c,
 * ws037-p008).
 *
 * The shared SPIR-V frontend makes the scalar IR (drivers/gpu/compiler/);
 * this backend chooses SM75 instructions for it, assigns registers, marks
 * the dependences, and encodes 128-bit instructions.  The result says what
 * the dispatch needs: the general registers to reserve, the shared memory,
 * the workgroup's size, and which buffer goes where in the constant
 * buffers (sm75.h, NVRTX_SM75_CB_*).
 *
 * Stages A and B compile compute shaders: arithmetic, conversions, the
 * special functions, storage and shared memory with their atomics,
 * workgroup barriers and fences, integer division, and loops.  Stage E
 * adds vertex and fragment programs with their shader program header:
 * attributes, interpolated inputs, colour outputs, 2D texture samples and
 * discards.  Geometry, the other texture messages, derivatives and
 * spilling are refused with a reason.
 *
 * The same program is compiled for Turing (SM75, RTX 20 and GTX 16),
 * Ampere (SM86, RTX 30) or Ada (SM89, RTX 40), ws242-p002: the
 * instructions chosen are the same, a global access's memory order field
 * and the latencies the dependence marking waits differ.  The functions
 * without a shader model compile for Turing.
 */

/*
 * The (set, binding) of one buffer the shader uses, and for a uniform
 * buffer the bytes it reads (from 0).
 */
struct nvrtx_sm75_binding {
	uint32_t set;
	uint32_t binding;
	uint32_t size;
};

/*
 * One compiled compute program.
 *
 * Made by nvrtx_sm75_compile() and owned by the caller, who copies the code
 * into GPU memory and frees it with nvrtx_sm75_binary_free().
 *
 * storage[k] is the buffer whose address and size the dispatch writes at
 * NVRTX_SM75_CB_STORAGE + 16 * k of constant buffer 0; uniforms[j] is the
 * buffer the dispatch binds as constant buffer 1 + j.
 */
struct nvrtx_sm75_binary {
	/* The code: 128-bit instructions, four words each, padded to a whole 128-byte line. */
	uint32_t *code;
	uint32_t code_words;

	/* The shader model the code is for (NVRTX_SM_*). */
	uint32_t shader_model;

	/* The general registers the program needs, the hardware's two included (the QMD's register count). */
	uint32_t gpr_count;

	/* The workgroup's size, its shared memory in bytes, and the workgroup barriers it uses (the QMD's barrier count). */
	uint32_t local_size[3];
	uint32_t shared_bytes;
	uint32_t barrier_count;

	/* The push constant bytes the program reads (at offset 0 of constant buffer 0). */
	uint32_t push_bytes;

	/* The storage buffers and the uniform buffers, as above. */
	struct nvrtx_sm75_binding storage[NVRTX_SM75_CB_STORAGE_MAX];
	uint32_t storage_count;
	struct nvrtx_sm75_binding uniforms[NVRTX_SM75_CB_UNIFORM_MAX];
	uint32_t uniform_count;

	/* The sampled images: the k-th one's texture handle is the word at NVRTX_SM75_CB_TEXTURES + 4 * k of constant buffer 0. */
	struct nvrtx_sm75_binding textures[NVRTX_SM75_CB_TEXTURES_MAX];
	uint32_t texture_count;

	/*
	 * A vertex or fragment program's shader program header (version 4, 128
	 * bytes), which the program's address points at, the code following
	 * it; nonzero graphics says it is one (a compute program has none).
	 */
	uint32_t graphics;
	uint32_t sph[NVRTX_SM75_SPH_WORDS];
};

/*
 * Why a program was refused: the IR instruction (or SPIR-V word offset for
 * a refusal of the frontend) and an immutable reason.
 */
struct nvrtx_sm75_diagnostic {
	uint32_t instruction;
	const char *reason;
};

/* Compiles a SPIR-V compute module: 0 with the program, EINVAL or ENOTSUP with a reason, or ENOMEM. */
int nvrtx_sm75_compile(const uint32_t *words, size_t word_count, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a SPIR-V module's stage (compute, vertex or fragment), as nvrtx_sm75_compile(). */
int nvrtx_sm75_compile_stage(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a parsed compute program (the IR is only read). */
int nvrtx_sm75_compile_ir(const struct drv_gpu_shader_ir *ir, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a SPIR-V module's stage for a shader model (NVRTX_SM_TURING, _AMPERE or _ADA); ENOTSUP for another. */
int nvrtx_sm75_compile_stage_sm(const uint32_t *words, size_t word_count, enum drv_gpu_shader_stage stage, uint32_t sm, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Compiles a parsed program for a shader model, as nvrtx_sm75_compile_stage_sm(). */
int nvrtx_sm75_compile_ir_sm(const struct drv_gpu_shader_ir *ir, uint32_t sm, struct nvrtx_sm75_binary **binary, struct nvrtx_sm75_diagnostic *diagnostic);

/* Frees a compiled program. */
void nvrtx_sm75_binary_free(struct nvrtx_sm75_binary *binary);

#endif
