/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2025 Intel Corporation
 * Copyright (C) 2014 Intel Corporation
 * Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 */

/*
 * The Gen12 EU instruction encoding (field bit positions and values) of
 * Mesa, for compiler/eu.c.
 *
 * zedBSD — transcribed hardware definitions (values only)
 *
 * SPDX-License-Identifier: MIT (the notice of the Mesa sources below)
 *
 * Source: Mesa main @ ab691a1cc7bcd264bec8f735deb2127861ad15ef (MIT):
 *   - src/intel/compiler/gen/xe.json            field bit positions of the "Xe" (Gen12) encoding
 *   - src/intel/compiler/gen/gen_encoding.cpp   operand sub-fields, the split of the SEND descriptors,
 *                                               the region / type / SWSB value encodings
 *   - src/intel/compiler/brw/brw_lower_scoreboard.cpp  what the SWSB annotation has to say
 * and, for the predication, flag, conditional-modifier and logic / rounding / comparison fields,
 * Mesa 25.0.7 (MIT; the release tarball, as Debian packages it):
 *   - src/intel/compiler/brw_eu_inst.h     (sha256 e9feb41a37628b8c117877d626c859083fd58802002246df4da5d28c2bf969f5)
 *                                          the Gen12 positions of cond_modifier, pred_control, pred_inv,
 *                                          flag_reg_nr, flag_subreg_nr and the source abs bits
 *   - src/intel/compiler/brw_eu.c          (sha256 4c608ce5e1c1aa2bd480339247ed864c1c1e7b79c1ded0db69e495cd08342d55)
 *                                          the Gen12 hardware opcodes of SEL, NOT, AND, OR, CMP, FRC and RNDD
 *   - src/intel/compiler/brw_eu_defines.h  (sha256 12a919edc56a75efe68afffa55ca42278a0cd489efddc8a0915919be756d7abc)
 *                                          the conditional modifiers, BRW_PREDICATE_NORMAL, BRW_ARF_FLAG and
 *                                          the LOG / EXP math selectors
 * and, for the integer and flow-control instructions (p014 stage E), the same release:
 *   - brw_eu.c                             the Gen12 hardware opcodes of XOR, SHR, SHL, ASR, RNDZ and WHILE
 *   - brw_eu_inst.h                        the Gen12 JIP of a branch: bits 127:96, with the src0 immediate
 *                                          bit set (brw_eu_inst_set_jip)
 *   - src/intel/compiler/brw_eu_emit.c     (sha256 7a359df7a0d4fc8085d050e510c34a074ebaccbf6ae826a009ac73875223853d)
 *                                          brw_WHILE: a null D destination, the JIP in bytes from the WHILE
 *                                          back to the first instruction of the loop (16 * (do - while))
 * and, for the integer division and the round-to-even of p014 stage E2, the same release:
 *   - brw_eu_defines.h                     BRW_MATH_FUNCTION_INT_DIV_QUOTIENT (12) and _REMAINDER (13)
 *   - brw_eu.c                             the Gen12 hardware opcode of RNDE (70)
 *   - brw_eu_emit.c                        gfx6_math(): INT DIV takes integer sources, a register or an
 *                                          immediate second source, and no source modifier
 *   - src/intel/compiler/brw_fs_nir.cpp    (sha256 2e6116f7818ad378a4ea4d0724b237cec7477940aaa0e19735b32a0c6a4920f8)
 *                                          idiv / udiv to SHADER_OPCODE_INT_QUOTIENT, umod / irem to
 *                                          SHADER_OPCODE_INT_REMAINDER (the hardware's signed remainder has the
 *                                          dividend's sign), imod from the remainder and the divisor's sign;
 *                                          fround_even to RNDE
 *   - src/intel/compiler/brw_nir.c         (sha256 7cb2e82e04b4cf81b76441eb9e71ede3835f16350171a04dc350a24a14fc32c2)
 *                                          nir_lower_idiv only from verx10 125 on: Gen12.0 divides in the math box
 *   - src/intel/compiler/brw_lower_simd_width.cpp (sha256 4ac59b1abd2c08c3f56a579c79636aedc43772b2156713bb1f2a25cb508a9885)
 *                                          integer division is SIMD8 at most
 * and, for spilling registers to scratch memory (p014 stage E3), the same release:
 *   - brw_eu_defines.h                     GFX7_SFID_DATAPORT_DATA_CACHE (10), the OWord block read (0) and
 *                                          write (8) message types, BRW_DATAPORT_OWORD_BLOCK_2_OWORDS (2) and
 *                                          GFX8_BTI_STATELESS_NON_COHERENT (253)
 *   - src/intel/compiler/brw_eu.h          (sha256 87a58fd1a719122d81539607f0fb72cf0483486ed8aa5540c387c217d337fc1b)
 *                                          brw_message_desc(): mlen in bits 28:25, rlen in 24:20, the header
 *                                          bit 19; brw_dp_desc(): the binding table index in 7:0, the message
 *                                          control in 13:8, the message type in 18:14
 *   - src/intel/compiler/brw_reg_allocate.cpp (sha256 6ba5ce7a8431c9e3aa0c583cf0386ba642022d5712517e181091f2c516df97e8)
 *                                          emit_spill() / emit_unspill() before LSC (verx10 < 125): a
 *                                          one-register header, the offset in OWords in its dword 2, a
 *                                          stateless non-coherent OWord block write (the data as the second
 *                                          payload run) or read of one register
 *   - src/intel/compiler/brw_generator.cpp (sha256 0c3f99afe06ae7b651d48ff4966d0f8dabcb744fa3513a7018ee51a1bea90d3b)
 *                                          generate_scratch_header(): the header cleared, then r0.3[3:0] (the
 *                                          per-thread scratch space) and r0.5[31:10] (the thread's scratch
 *                                          base) copied into its dwords 3 and 5
 * and checked against Mesa 25.0.7's brw_asm / brw_disasm (--gen=adl) for each new form, among them the
 * 16-bit strided source <16;8,2>:uw of the 32 x 16-bit multiply Mesa lowers a 32-bit integer multiply to
 * on Tiger Lake (has_integer_dword_mul false, brw_lower_integer_multiplication.cpp).
 * These are Intel hardware facts.  Only positions and values are transcribed; the encoder logic is
 * new.  Every emitter of compiler/eu.c is checked bit for bit against Mesa's assembler (gentool asm)
 * and disassembler by plan/ws031/tests/run-vk-gentool-test.sh; the earlier table (from the 23.1
 * brw_inst.h) had the float type, the region widths and the math selectors wrong and no SEND layout.
 *
 * Transcribed by hand from the files above; no generator produces this file.
 *
 * The Gen12 native instruction is four 32-bit words (128 bits); ranges are HI, LO inclusive.
 */

#ifndef DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN12_H
#define DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN12_H

#define GEN12_EU_DWORDS			4U

/* Control fields. */
#define EU_OPCODE_HI			6
#define EU_OPCODE_LO			0
#define EU_SWSB_HI			15
#define EU_SWSB_LO			8
#define EU_EXEC_SIZE_HI			18
#define EU_EXEC_SIZE_LO			16
#define EU_NO_MASK_BIT			31
#define EU_SATURATE_BIT			34
#define EU_MATH_FUNCTION_HI		95
#define EU_MATH_FUNCTION_LO		92

/*
 * Predication and flags (brw_eu_inst.h, Gen12 column).  The conditional modifier shares bits 95:92
 * with the math function: a math instruction has no conditional modifier.
 */
#define EU_FLAG_SUBREG_NR_BIT		22
#define EU_FLAG_REG_NR_BIT		23
#define EU_PRED_CONTROL_HI		27
#define EU_PRED_CONTROL_LO		24
#define EU_PRED_INV_BIT			28
#define EU_COND_MODIFIER_HI		95
#define EU_COND_MODIFIER_LO		92

/* Destination (direct).  An operand is [file bit][subregister 5][register 8]. */
#define EU_DST_REG_TYPE_HI		39
#define EU_DST_REG_TYPE_LO		36
#define EU_DST_HSTRIDE_HI		49
#define EU_DST_HSTRIDE_LO		48
#define EU_DST_REG_FILE_BIT		50
#define EU_DST_SUBREG_HI		55
#define EU_DST_SUBREG_LO		51
#define EU_DST_REG_NR_HI		63
#define EU_DST_REG_NR_LO		56

/* Source 0.  An immediate is said by its own bit; the operand field then stays clear. */
#define EU_SRC0_REG_TYPE_HI		43
#define EU_SRC0_REG_TYPE_LO		40
#define EU_SRC0_ABS_BIT			44
#define EU_SRC0_NEGATE_BIT		45
#define EU_SRC0_IS_IMM_BIT		46
#define EU_SRC0_HSTRIDE_HI		65
#define EU_SRC0_HSTRIDE_LO		64
#define EU_SRC0_REG_FILE_BIT		66
#define EU_SRC0_SUBREG_HI		71
#define EU_SRC0_SUBREG_LO		67
#define EU_SRC0_REG_NR_HI		79
#define EU_SRC0_REG_NR_LO		72
#define EU_SRC0_WIDTH_HI		83
#define EU_SRC0_WIDTH_LO		81
#define EU_SRC0_VSTRIDE_HI		87
#define EU_SRC0_VSTRIDE_LO		84

/* Source 1. */
#define EU_SRC1_IS_IMM_BIT		47
#define EU_SRC1_REG_TYPE_HI		91
#define EU_SRC1_REG_TYPE_LO		88
#define EU_SRC1_HSTRIDE_HI		97
#define EU_SRC1_HSTRIDE_LO		96
#define EU_SRC1_REG_FILE_BIT		98
#define EU_SRC1_SUBREG_HI		103
#define EU_SRC1_SUBREG_LO		99
#define EU_SRC1_REG_NR_HI		111
#define EU_SRC1_REG_NR_LO		104
#define EU_SRC1_WIDTH_HI		115
#define EU_SRC1_WIDTH_LO		113
#define EU_SRC1_VSTRIDE_HI		119
#define EU_SRC1_VSTRIDE_LO		116
#define EU_SRC1_ABS_BIT			120
#define EU_SRC1_NEGATE_BIT		121

/* A 32-bit immediate occupies the last word. */
#define EU_IMM32_HI			127
#define EU_IMM32_LO			96

/*
 * SEND / SENDC (gen_encoding.cpp GEN_FORMAT_SEND).  The operands carry a file bit and a register
 * number and no subregister, type or region; the two 32-bit descriptors are scattered:
 *   desc[31:30] -> 123:122   desc[29:25] -> 71:67   desc[24:20] -> 55:51
 *   desc[19:11] -> 121:113   desc[10:0]  -> 91:81
 *   ex_desc[31:28] -> 127:124  [27:26] -> 97:96  [25:24] -> 65:64  [23:11] -> 47:35  [10:6] -> 103:99
 */
#define EU_SEND_EOT_BIT			34
#define EU_SEND_SFID_HI			95
#define EU_SEND_SFID_LO			92

/* Hardware opcodes. */
#define EU_OP_SYNC			1U
#define EU_OP_SEND			49U
#define EU_OP_SENDC			50U
#define EU_OP_MATH			56U
#define EU_OP_ADD			64U
#define EU_OP_MUL			65U
#define EU_OP_FRC			67U
#define EU_OP_RNDD			69U
#define EU_OP_MAD			91U
#define EU_OP_NOP			96U
#define EU_OP_MOV			97U
#define EU_OP_SEL			98U
#define EU_OP_NOT			100U
#define EU_OP_AND			101U
#define EU_OP_OR			102U
#define EU_OP_CMP			112U
#define EU_OP_XOR			103U
#define EU_OP_SHR			104U
#define EU_OP_SHL			105U
#define EU_OP_ASR			108U
#define EU_OP_RNDZ			71U
#define EU_OP_RNDE			70U
#define EU_OP_WHILE			39U

/* A branch's jump target: bytes from the branch instruction, in the last word. */
#define EU_JIP_HI			127
#define EU_JIP_LO			96

/* Conditional modifiers (BRW_CONDITIONAL_*): what a CMP tests, or which source a SEL keeps. */
#define EU_COND_Z			1U
#define EU_COND_NZ			2U
#define EU_COND_G			3U
#define EU_COND_GE			4U
#define EU_COND_L			5U
#define EU_COND_LE			6U

/* The predicate that enables a channel whose bit in the named flag subregister is set. */
#define EU_PREDICATE_NORMAL		1U

/* The architecture register number of flag register f0; f1 is the next one. */
#define EU_ARF_FLAG			0x30U

/* Register files as compiler/eu.c names them; the hardware has one bit (GRF or not) and the immediate bit. */
#define EU_FILE_ARF			0U
#define EU_FILE_GRF			1U
#define EU_FILE_IMM			3U

/* Register / immediate types: [float 8 | signed 4 | log2(bytes)]. */
#define EU_TYPE_UW			1U
#define EU_TYPE_UD			2U
#define EU_TYPE_D			6U
#define EU_TYPE_F			10U

/* Exec size: log2 of the channel count. */
#define EU_EXEC_SIZE_1			0U
#define EU_EXEC_SIZE_8			3U

/* Regions.  hstride 0,1,2,4 -> 0..3; width 1,2,4,8,16 -> 0..4; vstride 0,1,2,4,8,16 -> 0..5. */
#define EU_HSTRIDE_0			0U
#define EU_HSTRIDE_1			1U
#define EU_HSTRIDE_2			2U
#define EU_WIDTH_1			0U
#define EU_WIDTH_8			3U
#define EU_VSTRIDE_0			0U
#define EU_VSTRIDE_8			4U
#define EU_VSTRIDE_16			5U

/* Math function selectors. */
#define EU_MATH_INV			1U
#define EU_MATH_LOG			2U
#define EU_MATH_EXP			3U
#define EU_MATH_SQRT			4U
#define EU_MATH_RSQ			5U
#define EU_MATH_SIN			6U
#define EU_MATH_COS			7U
#define EU_MATH_INT_DIV_QUOTIENT	12U
#define EU_MATH_INT_DIV_REMAINDER	13U

/* Shared functions a SEND addresses. */
#define EU_SFID_SAMPLER			2U
#define EU_SFID_RENDER_CACHE		5U
#define EU_SFID_URB			6U
#define EU_SFID_DATA_CACHE		10U

/* The message descriptor: the payload and reply lengths in registers, and whether a header leads the payload. */
#define EU_DESC_MLEN_SHIFT		25
#define EU_DESC_RLEN_SHIFT		20
#define EU_DESC_HEADER_PRESENT		(1U << 19)

/* A data-port descriptor: the binding table index, the message control and the message type. */
#define EU_DP_CONTROL_SHIFT		8
#define EU_DP_TYPE_SHIFT		14

/* The OWord block messages of the data cache, and the block of two OWords (one SIMD8 register). */
#define EU_DP_OWORD_BLOCK_READ		0U
#define EU_DP_OWORD_BLOCK_WRITE		8U
#define EU_DP_OWORD_BLOCK_2_OWORDS	2U

/* The binding table index of stateless, non-coherent accesses: the scratch space. */
#define EU_BTI_STATELESS_NON_COHERENT	253U

/*
 * The scratch message header: the offset (in OWords) in dword 2; dwords 3
 * and 5 copy the per-thread scratch space (r0.3 bits 3:0) and the thread's
 * scratch base (r0.5 bits 31:10) out of the thread payload.
 */
#define EU_SCRATCH_HEADER_OFFSET_DWORD	2U
#define EU_SCRATCH_HEADER_SIZE_DWORD	3U
#define EU_SCRATCH_HEADER_BASE_DWORD	5U
#define EU_SCRATCH_SIZE_MASK		0x0000000fU
#define EU_SCRATCH_BASE_MASK		0xfffffc00U

/*
 * SWSB, the software scoreboard byte of Gen12.0 (gen_swsb_encode, brw_lower_scoreboard.cpp):
 *   regdist n                  = n          wait for the n-th in-order instruction before this one
 *   token set                  = 0x40 | id  an out-of-order instruction (MATH, SEND) names itself
 *   regdist n + token set      = 0x80 | n << 4 | id
 *   sync.nop on a token's dst  = 0x20 | id  wait until that instruction has written its destination
 *   sync.nop on a token's src  = 0x30 | id  wait until it has read its sources
 */
#define EU_SWSB_REGDIST(n)		((uint32_t)(n))
#define EU_SWSB_REGDIST_SET(n, id)	(0x80U | ((uint32_t)(n) << 4) | (uint32_t)(id))
#define EU_SWSB_SYNC_DST(id)		(0x20U | (uint32_t)(id))
#define EU_SWSB_SYNC_SRC(id)		(0x30U | (uint32_t)(id))

#endif /* DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN12_H */
