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
 * The Gen9 EU instruction encoding (field bit positions and values) of
 * Mesa, for compiler/eu.c.
 *
 * zedBSD — transcribed hardware definitions (values only)
 *
 * SPDX-License-Identifier: MIT (the notice of the Mesa sources below)
 *
 * Source: Mesa 25.0.7 (MIT; the release tarball, sha256 592272df..., as plan/ws101/tests/host/mesa-tools.sh fetches it):
 *   - src/intel/compiler/brw_eu_inst.h     (sha256 e9feb41a37628b8c117877d626c859083fd58802002246df4da5d28c2bf969f5)
 *                                          the "9+" column of the native instruction: every control and operand
 *                                          field, the JIP (127:96) and UIP (95:64) of a branch, the SEND
 *                                          descriptor in 126:96 with the end-of-thread bit 127 and the SFID in
 *                                          27:24, and the SENDS operands and extended descriptor
 *                                          (brw_eu_inst_set_sends_ex_desc(): ex_desc[31:16] in 95:80, the
 *                                          second payload's length ex_desc[9:6] in 67:64)
 *   - src/intel/compiler/brw_eu.c          (sha256 4c608ce5e1c1aa2bd480339247ed864c1c1e7b79c1ded0db69e495cd08342d55)
 *                                          the hardware opcodes before Gfx12 (GFX_LT(GFX12) and GFX_ALL rows)
 *   - src/intel/compiler/brw_reg_type.c    (sha256 acb63f401add3384b15e7d6bb4d148b7b60325c2e618abf2a727856a3138731b)
 *                                          brw_type_encode() before Gfx11: the register and the immediate type
 *                                          encodings
 *   - src/intel/compiler/brw_eu_emit.c     (sha256 7a359df7a0d4fc8085d050e510c34a074ebaccbf6ae826a009ac73875223853d)
 *                                          brw_set_src0(): before Gfx12 an immediate source 0 smaller than
 *                                          64 bits sets source 1's file to ARF and its type to source 0's;
 *                                          brw_IF() / brw_ENDIF() / brw_WHILE(): an immediate D 0 as source 0
 *                                          before Gfx12; brw_WAIT(): a SIMD1 NoMask WAIT on n0;
 *                                          brw_send_indirect_message() / _split_message(): SEND with the
 *                                          descriptor as source 1's UD immediate, SENDS for a second payload
 *   - src/intel/compiler/brw_eu_defines.h  (sha256 12a919edc56a75efe68afffa55ca42278a0cd489efddc8a0915919be756d7abc)
 *                                          BRW_ARF_NOTIFICATION_COUNT (0x90)
 *   - src/intel/compiler/brw_generator.cpp (sha256 0c3f99afe06ae7b651d48ff4966d0f8dabcb744fa3513a7018ee51a1bea90d3b)
 *                                          generate_barrier(): before Gfx12 the gateway message, then WAIT;
 *                                          FS_OPCODE_SCHEDULING_FENCE: before Gfx12 a MOV of the fence's reply to
 *                                          the null register makes the thread wait for it
 *   - src/intel/compiler/brw_fs_nir.cpp    (sha256 2e6116f7818ad378a4ea4d0724b237cec7477940aaa0e19735b32a0c6a4920f8)
 *                                          emit_barrier(): the barrier ID mask 0x8f000000 on Gfx9; the memory
 *                                          fence before Gfx11: one data cache fence (binding table entry 0, the
 *                                          commit bit on Gfx9) covers shared memory too
 *   - src/intel/compiler/brw_fs_visitor.cpp (sha256 b20de8d3a514c81de03f2de615abcabfa99c1d84e4c5d6496491116dd67e64ec)
 *                                          emit_cs_terminate(): before Gfx11 the descriptor's bit 4, "do not
 *                                          dereference URB"
 * and checked against Mesa 25.0.7's brw_asm / brw_disasm (--gen=skl) for each form compiler/eu.c emits.
 * These are Intel hardware facts.  Only positions and values are transcribed; the encoder logic is new.
 *
 * Transcribed by hand from the files above; no generator produces this file.
 *
 * The Gen9 native instruction is four 32-bit words (128 bits), like Gen12's; ranges are HI, LO inclusive.
 * The Align1 access mode (bit 8 clear) is the only one compiler/eu.c emits.  Gen9 has no software
 * scoreboard: the hardware tracks register dependencies itself.
 */

#ifndef DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN9_H
#define DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN9_H

/* Control fields. */
#define GEN9_EU_OPCODE_HI		6
#define GEN9_EU_OPCODE_LO		0
#define GEN9_EU_PRED_CONTROL_HI		19
#define GEN9_EU_PRED_CONTROL_LO		16
#define GEN9_EU_PRED_INV_BIT		20
#define GEN9_EU_EXEC_SIZE_HI		23
#define GEN9_EU_EXEC_SIZE_LO		21

/* The conditional modifier, the math function and a SEND's shared function share bits 27:24. */
#define GEN9_EU_COND_MODIFIER_HI	27
#define GEN9_EU_COND_MODIFIER_LO	24
#define GEN9_EU_MATH_FUNCTION_HI	27
#define GEN9_EU_MATH_FUNCTION_LO	24
#define GEN9_EU_SEND_SFID_HI		27
#define GEN9_EU_SEND_SFID_LO		24

#define GEN9_EU_SATURATE_BIT		31
#define GEN9_EU_FLAG_SUBREG_NR_BIT	32
#define GEN9_EU_FLAG_REG_NR_BIT		33
#define GEN9_EU_NO_MASK_BIT		34

/* Destination (direct, Align1).  The register file has two bits. */
#define GEN9_EU_DST_REG_FILE_HI		36
#define GEN9_EU_DST_REG_FILE_LO		35
#define GEN9_EU_DST_REG_TYPE_HI		40
#define GEN9_EU_DST_REG_TYPE_LO		37
#define GEN9_EU_DST_SUBREG_HI		52
#define GEN9_EU_DST_SUBREG_LO		48
#define GEN9_EU_DST_REG_NR_HI		60
#define GEN9_EU_DST_REG_NR_LO		53
#define GEN9_EU_DST_HSTRIDE_HI		62
#define GEN9_EU_DST_HSTRIDE_LO		61

/* Source 0 (direct, Align1). */
#define GEN9_EU_SRC0_REG_FILE_HI	42
#define GEN9_EU_SRC0_REG_FILE_LO	41
#define GEN9_EU_SRC0_REG_TYPE_HI	46
#define GEN9_EU_SRC0_REG_TYPE_LO	43
#define GEN9_EU_SRC0_SUBREG_HI		68
#define GEN9_EU_SRC0_SUBREG_LO		64
#define GEN9_EU_SRC0_REG_NR_HI		76
#define GEN9_EU_SRC0_REG_NR_LO		69
#define GEN9_EU_SRC0_ABS_BIT		77
#define GEN9_EU_SRC0_NEGATE_BIT		78
#define GEN9_EU_SRC0_HSTRIDE_HI		81
#define GEN9_EU_SRC0_HSTRIDE_LO		80
#define GEN9_EU_SRC0_WIDTH_HI		84
#define GEN9_EU_SRC0_WIDTH_LO		82
#define GEN9_EU_SRC0_VSTRIDE_HI		88
#define GEN9_EU_SRC0_VSTRIDE_LO		85

/* Source 1 (direct, Align1). */
#define GEN9_EU_SRC1_REG_FILE_HI	90
#define GEN9_EU_SRC1_REG_FILE_LO	89
#define GEN9_EU_SRC1_REG_TYPE_HI	94
#define GEN9_EU_SRC1_REG_TYPE_LO	91
#define GEN9_EU_SRC1_SUBREG_HI		100
#define GEN9_EU_SRC1_SUBREG_LO		96
#define GEN9_EU_SRC1_REG_NR_HI		108
#define GEN9_EU_SRC1_REG_NR_LO		101
#define GEN9_EU_SRC1_ABS_BIT		109
#define GEN9_EU_SRC1_NEGATE_BIT		110
#define GEN9_EU_SRC1_HSTRIDE_HI		113
#define GEN9_EU_SRC1_HSTRIDE_LO		112
#define GEN9_EU_SRC1_WIDTH_HI		116
#define GEN9_EU_SRC1_WIDTH_LO		114
#define GEN9_EU_SRC1_VSTRIDE_HI		120
#define GEN9_EU_SRC1_VSTRIDE_LO		117

/* A 32-bit immediate occupies the last word, whichever source it is. */
#define GEN9_EU_IMM32_HI		127
#define GEN9_EU_IMM32_LO		96

/* A branch's jump target and second target: bytes from the branch, as on Gen12. */
#define GEN9_EU_JIP_HI			127
#define GEN9_EU_JIP_LO			96
#define GEN9_EU_UIP_HI			95
#define GEN9_EU_UIP_LO			64

/*
 * SEND / SENDC: the descriptor is source 1's UD immediate, of which bits
 * 126:96 carry descriptor bits 30:0 and bit 127 is the end-of-thread bit.
 */
#define GEN9_EU_SEND_DESC_HI		126
#define GEN9_EU_SEND_DESC_LO		96
#define GEN9_EU_SEND_EOT_BIT		127

/*
 * SENDS / SENDSC, a message of two payloads: a one-bit destination file, the
 * second payload's file and register, and the extended descriptor's bits
 * 31:16 and 9:6 (the length of the second payload); its other bits must be
 * zero.
 */
#define GEN9_EU_SENDS_DST_FILE_BIT	35
#define GEN9_EU_SENDS_SRC1_FILE_BIT	36
#define GEN9_EU_SENDS_SRC1_REG_NR_HI	51
#define GEN9_EU_SENDS_SRC1_REG_NR_LO	44
#define GEN9_EU_SENDS_EX_DESC_HIGH_HI	95
#define GEN9_EU_SENDS_EX_DESC_HIGH_LO	80
#define GEN9_EU_SENDS_EX_MLEN_HI	67
#define GEN9_EU_SENDS_EX_MLEN_LO	64
#define GEN9_EU_SENDS_EX_DESC_HIGH_SHIFT	16
#define GEN9_EU_SENDS_EX_MLEN_SHIFT	6
#define GEN9_EU_SENDS_EX_MLEN_MASK	0xfU
#define GEN9_EU_SENDS_EX_DESC_ZERO_MASK	0x0000fc3fU

/* Register files: two bits, the immediate one of them. */
#define GEN9_EU_FILE_ARF		0U
#define GEN9_EU_FILE_GRF		1U
#define GEN9_EU_FILE_IMM		3U

/* Hardware opcodes (before Gfx12). */
#define GEN9_EU_OP_MOV			1U
#define GEN9_EU_OP_SEL			2U
#define GEN9_EU_OP_NOT			4U
#define GEN9_EU_OP_AND			5U
#define GEN9_EU_OP_OR			6U
#define GEN9_EU_OP_XOR			7U
#define GEN9_EU_OP_SHR			8U
#define GEN9_EU_OP_SHL			9U
#define GEN9_EU_OP_ASR			12U
#define GEN9_EU_OP_CMP			16U
#define GEN9_EU_OP_IF			34U
#define GEN9_EU_OP_ENDIF		37U
#define GEN9_EU_OP_WHILE		39U
#define GEN9_EU_OP_WAIT			48U
#define GEN9_EU_OP_SEND			49U
#define GEN9_EU_OP_SENDC		50U
#define GEN9_EU_OP_SENDS		51U
#define GEN9_EU_OP_SENDSC		52U
#define GEN9_EU_OP_MATH			56U
#define GEN9_EU_OP_ADD			64U
#define GEN9_EU_OP_MUL			65U
#define GEN9_EU_OP_FRC			67U
#define GEN9_EU_OP_RNDD			69U
#define GEN9_EU_OP_RNDE			70U
#define GEN9_EU_OP_RNDZ			71U
#define GEN9_EU_OP_NOP			126U

/* Register types (brw_type_encode() before Gfx11, a register operand). */
#define GEN9_EU_TYPE_UD			0U
#define GEN9_EU_TYPE_D			1U
#define GEN9_EU_TYPE_UW			2U
#define GEN9_EU_TYPE_W			3U
#define GEN9_EU_TYPE_F			7U
#define GEN9_EU_TYPE_HF			10U

/* Immediate types that differ from the register ones: the vector V and the half float. */
#define GEN9_EU_IMM_TYPE_V		6U
#define GEN9_EU_IMM_TYPE_HF		11U

/*
 * The notification register n0 a WAIT waits on (BRW_ARF_NOTIFICATION_COUNT):
 * the gateway answers a thread's barrier message there.
 */
#define GEN9_EU_ARF_NOTIFICATION	0x90U

/*
 * The messages whose form differs before Gfx11 (compiler/compile*.c):
 * the "do not dereference URB" bit of the message that ends a compute
 * thread, and the barrier ID's mask in r0.2 on Gfx9.
 */
#define GEN9_EU_DESC_TERMINATE_NO_URB	(1U << 4)
#define GEN9_EU_BARRIER_ID_MASK		0x8f000000U

#endif /* DRIVERS_GPU_I915_INTEL_EU_ENCODING_GEN9_H */
