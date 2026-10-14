/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2022 Collabora, Ltd.
 * Copyright © 2024 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * The facts of the Turing (SM75) instruction set the nvrtx shader compiler
 * uses (ws037-p008): where the fields of a 128-bit instruction lie, the
 * opcodes and the codes of their modifiers, the special registers, and the
 * constant buffer layout the compiler and the dispatch agree on.
 *
 * Modified for zedBSD: the values are excerpted by hand from Mesa 25.3.6
 * (src/nouveau/compiler, SPDX MIT) and renamed (NVRTX_SM75_*); the code that
 * uses them is zedBSD's own.  Values are unchanged.  Sources (SHA-256):
 *   src/nouveau/compiler/nak/sm70_encode.rs (© 2022 Collabora)
 *     111b841c9661f46729b52072ef070b72fc1bbbe5b304651c43aaf40e297b6695
 *   src/nouveau/compiler/nak/sm70.rs (register files, the largest delay)
 *   src/nouveau/compiler/nak/calc_instr_deps.rs (the serial dependences)
 *     8c92e065aa043dcb259cb0bc0ef7436ec692b9b5d10321969fd5adc3d6178695
 *   src/nouveau/compiler/nak/sm75_instr_latencies.rs (the latencies)
 *     419b8b1d25fb16ddc8ae726768b15e593d3b84f467cc6bb59101eb39bf764878
 *   src/nouveau/compiler/nak_private.h (the special register numbers, the attribute addresses)
 *     d2b1a0e4b0b38ee9cc01ed5b7dd6ab38b8a7d8722b453a81e5383c98d884e61f
 *   src/nouveau/compiler/nak/sph.rs (the shader program header's maps)
 *     e845a822a7a82822e0c07fd84cacce413146debeeaeb7484dc48deffc492c368
 *   src/nouveau/compiler/nak/from_nir.rs (a fragment program's header always maps the position's w)
 *     734f35b552f4225b885aef708863ad016ee034bd7289788ab92d63b607fff11a
 *   src/nouveau/headers/nvidia/classes/cla097sph.h (the header's fields, NVIDIA's MIT notice)
 *     a804dc66816ec4bd5a49be6c6745f7aa2cda31881e48472625abe4a691f0c1a3
 *   src/nouveau/compiler/nak_nir_lower_fs_inputs.c (SM70's interpolation: pass, then times 1 / w)
 * The constant buffer layout (NVRTX_SM75_CB_*) is zedBSD's own.
 */

#ifndef DRIVERS_GPU_NVRTX_COMPILER_SM75_ISA_H
#define DRIVERS_GPU_NVRTX_COMPILER_SM75_ISA_H

/*
 * ==== The shader program header (version 4, NVIDIA's cla097sph.h) ====
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

/* A fragment input's interpolation in the header's map: constant (flat), perspective, screen-linear. */
#define NVRTX_SM75_IMAP_CONSTANT	1U
#define NVRTX_SM75_IMAP_PERSPECTIVE	2U
#define NVRTX_SM75_IMAP_SCREEN_LINEAR	3U

/* An instruction is four 32-bit words, the first holding bit 0. */
#define NVRTX_SM75_INSTRUCTION_WORDS	4U
#define NVRTX_SM75_INSTRUCTION_BYTES	16U

/* The general registers the compiler may assign (R0..R252), the zero register, and the two the hardware keeps. */
#define NVRTX_SM75_GPR_USABLE		253U
#define NVRTX_SM75_RZ			255U
#define NVRTX_SM75_GPR_RESERVED		2U
#define NVRTX_SM75_GPR_MINIMUM		4U

/* The predicates P0..P6, and PT, which is always true. */
#define NVRTX_SM75_P0			0U
#define NVRTX_SM75_PT			7U

/*
 * ==== The fields of an instruction (bit positions in the 128 bits) ====
 */

/* The opcode (12 bits), the guard predicate and its negation. */
#define NVRTX_SM75_OPCODE_LOW		0U
#define NVRTX_SM75_OPCODE_BITS		12U
#define NVRTX_SM75_ALU_OPCODE_BITS	9U
#define NVRTX_SM75_ALU_FORM_LOW		9U
#define NVRTX_SM75_ALU_FORM_BITS	3U
#define NVRTX_SM75_GUARD_LOW		12U
#define NVRTX_SM75_GUARD_NOT		15U

/* The destination register and the first source register. */
#define NVRTX_SM75_DST_LOW		16U
#define NVRTX_SM75_SRC0_LOW		24U
#define NVRTX_SM75_REG_BITS		8U

/* The second source: a register (32..39, abs 62, neg 63), a 32-bit immediate (32..63), or a constant buffer word. */
#define NVRTX_SM75_SRC1_LOW		32U
#define NVRTX_SM75_SRC1_ABS		62U
#define NVRTX_SM75_SRC1_NEG		63U
#define NVRTX_SM75_IMM_LOW		32U
#define NVRTX_SM75_IMM_BITS		32U

/* A constant buffer operand: the byte offset (38..53) and the buffer (54..58); bit 91 clear for a bound buffer. */
#define NVRTX_SM75_CB_OFFSET_LOW	38U
#define NVRTX_SM75_CB_OFFSET_BITS	16U
#define NVRTX_SM75_CB_INDEX_LOW		54U
#define NVRTX_SM75_CB_INDEX_BITS	5U
#define NVRTX_SM75_CB_BINDLESS		91U

/* The first source's abs and neg, and the third source (64..71, abs 74, neg 75). */
#define NVRTX_SM75_SRC0_NEG		72U
#define NVRTX_SM75_SRC0_ABS		73U
#define NVRTX_SM75_SRC2_LOW		64U
#define NVRTX_SM75_SRC2_ABS		74U
#define NVRTX_SM75_SRC2_NEG		75U

/* The ALU forms: which of sources 1 and 2 is a register, an immediate or a constant buffer word. */
#define NVRTX_SM75_FORM_REG		1U
#define NVRTX_SM75_FORM_IMM_SRC2	2U
#define NVRTX_SM75_FORM_CB_SRC2		3U
#define NVRTX_SM75_FORM_IMM		4U
#define NVRTX_SM75_FORM_CB		5U

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
#define NVRTX_SM75_OP_KILL		0x95bU

/*
 * ==== The modifiers of the instructions ====
 */

/* MOV: the lanes of a quad written (all four). */
#define NVRTX_SM75_MOV_LANES_LOW	72U
#define NVRTX_SM75_MOV_LANES_ALL	0xfU

/* The float rounding (78..79): to nearest even, toward -inf, toward +inf, toward zero; flush to zero (80). */
#define NVRTX_SM75_ROUND_LOW		78U
#define NVRTX_SM75_ROUND_NEAREST	0U
#define NVRTX_SM75_ROUND_DOWN		1U
#define NVRTX_SM75_ROUND_UP		2U
#define NVRTX_SM75_ROUND_ZERO		3U
#define NVRTX_SM75_FTZ			80U

/* FMUL: the post-divide field holds 4 (none). */
#define NVRTX_SM75_FMUL_PDIV_LOW	84U
#define NVRTX_SM75_FMUL_PDIV_NONE	4U

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

/* ISETP and IMAD: bit 73 set for a signed comparison or product. */
#define NVRTX_SM75_SIGNED		73U

/* LOP3: the truth table (72..79) over sources a (0xf0), b (0xcc) and c (0xaa). */
#define NVRTX_SM75_LUT_LOW		72U
#define NVRTX_SM75_LUT_A		0xf0U
#define NVRTX_SM75_LUT_B		0xccU
#define NVRTX_SM75_LUT_C		0xaaU

/* SHF: the data type (73..74), the wrap of the shift count (75), right (76), the high half (80). */
#define NVRTX_SM75_SHF_TYPE_LOW		73U
#define NVRTX_SM75_SHF_TYPE_I32		2U
#define NVRTX_SM75_SHF_TYPE_U32		3U
#define NVRTX_SM75_SHF_WRAP		75U
#define NVRTX_SM75_SHF_RIGHT		76U
#define NVRTX_SM75_SHF_HIGH		80U

/* MUFU: the function (74..79). */
#define NVRTX_SM75_MUFU_LOW		74U
#define NVRTX_SM75_MUFU_COS		0U
#define NVRTX_SM75_MUFU_SIN		1U
#define NVRTX_SM75_MUFU_EX2		2U
#define NVRTX_SM75_MUFU_LG2		3U
#define NVRTX_SM75_MUFU_RCP		4U
#define NVRTX_SM75_MUFU_RSQ		5U
#define NVRTX_SM75_MUFU_SQRT		8U

/* The conversions: F2I signed (72), I2F signed (74), the result's size (75..76) and the source's (84..85) as log2 of bytes. */
#define NVRTX_SM75_F2I_SIGNED		72U
#define NVRTX_SM75_I2F_SIGNED		74U
#define NVRTX_SM75_CVT_DST_SIZE_LOW	75U
#define NVRTX_SM75_CVT_SRC_SIZE_LOW	84U
#define NVRTX_SM75_CVT_SIZE_32		2U

/* The special registers S2R reads (72..79): the thread's place in its group and the group's place in the grid. */
#define NVRTX_SM75_S2R_INDEX_LOW	72U
#define NVRTX_SM75_SR_TID_X		0x21U
#define NVRTX_SM75_SR_TID_Y		0x22U
#define NVRTX_SM75_SR_TID_Z		0x23U
#define NVRTX_SM75_SR_CTAID_X		0x25U
#define NVRTX_SM75_SR_CTAID_Y		0x26U
#define NVRTX_SM75_SR_CTAID_Z		0x27U

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

/* LDC: the mode (78..79, 0 indexed) and the data register a store reads (32..39). */
#define NVRTX_SM75_LDC_MODE_LOW		78U
#define NVRTX_SM75_STORE_DATA_LOW	32U

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

/* MEMBAR: the scope (76..78): 0 the workgroup, 2 the GPU. */
#define NVRTX_SM75_MEMBAR_SCOPE_LOW	76U
#define NVRTX_SM75_MEMBAR_CTA		0U
#define NVRTX_SM75_MEMBAR_GPU		2U

/* BSSY and BSYNC: the convergence barrier (16..19), BSSY's offset to its BSYNC in words from the next instruction (34..63). */
#define NVRTX_SM75_CONVERGE_BAR_LOW	16U
#define NVRTX_SM75_CONVERGE_BARS	16U
#define NVRTX_SM75_BSSY_OFFSET_LOW	34U
#define NVRTX_SM75_BSSY_OFFSET_BITS	30U

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

/* IPA: the attribute's address in words (64..71), the location (76..77, 0 the pixel's centre) and the frequency (78..79: 0 pass, 1 constant). */
#define NVRTX_SM75_IPA_ADDRESS_LOW	64U
#define NVRTX_SM75_IPA_FREQUENCY_LOW	78U
#define NVRTX_SM75_IPA_PASS		0U
#define NVRTX_SM75_IPA_CONSTANT		1U

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

/* The attribute addresses (NAK's NAK_ATTR_*): the position, the point size, the first generic one (16 bytes each), the instance and the vertex numbers. */
#define NVRTX_SM75_ATTR_POINT_SIZE	0x06cU
#define NVRTX_SM75_ATTR_POSITION	0x070U
#define NVRTX_SM75_ATTR_POSITION_W	0x07cU
#define NVRTX_SM75_ATTR_GENERIC		0x080U
#define NVRTX_SM75_ATTR_GENERIC_END	0x280U
#define NVRTX_SM75_ATTR_INSTANCE_ID	0x2f8U
#define NVRTX_SM75_ATTR_VERTEX_ID	0x2fcU

/* BRA: the offset to the target in words from the next instruction (34..81). */
#define NVRTX_SM75_BRA_OFFSET_LOW	34U
#define NVRTX_SM75_BRA_OFFSET_BITS	48U

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
 * The texture handles: from NVRTX_SM75_CB_TEXTURES of constant buffer 0, one
 * word per sampled image the shader uses, in the order of its uniform list
 * (the dispatch writes the texture header index in bits 19:0 and the
 * sampler's in 31:20, as NVK's descriptors on Turing).
 */
#define NVRTX_SM75_CB_TEXTURES		0x400U
#define NVRTX_SM75_CB_TEXTURES_MAX	32U

#endif /* DRIVERS_GPU_NVRTX_COMPILER_SM75_ISA_H */
