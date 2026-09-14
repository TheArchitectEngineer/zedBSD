/*
 * zedBSD — transcribed hardware definitions (values only)
 *
 * Source: Mesa tag mesa-23.1.0 (MIT), which carries the Gen12 EU instruction
 * encoding in table form (later Mesa refactored it):
 *   - src/intel/compiler/brw_inst.h   field bit positions (the "12+" columns).
 *   - src/intel/compiler/brw_eu.c     opcode_descs[] hardware opcode values.
 *   - src/intel/compiler/brw_reg_type.c  register/immediate hw type values.
 * These are Intel PRM hardware facts. SHA-256 recorded in the audit.
 *
 * Only positions and values are transcribed; the encoder logic is new.  The
 * Gen12 native instruction is four 32-bit words (128 bits).  Bit ranges are
 * given as HI, LO inclusive.  Fields the driver does not yet set stay zero and
 * are refined during the on-hardware bring-up (big-bang test).
 */

#ifndef I915_VK_EU_ENCODING_GEN12_INC
#define I915_VK_EU_ENCODING_GEN12_INC

#define GEN12_EU_DWORDS			4U

/* Control fields. */
#define EU_OPCODE_HI			6
#define EU_OPCODE_LO			0
#define EU_SWSB_HI			15
#define EU_SWSB_LO			8
#define EU_EXEC_SIZE_HI			18
#define EU_EXEC_SIZE_LO			16
#define EU_QTR_CONTROL_HI		21
#define EU_QTR_CONTROL_LO		20
#define EU_SATURATE_BIT			34
#define EU_MATH_FUNCTION_HI		95
#define EU_MATH_FUNCTION_LO		92

/* Destination (direct, align1). */
#define EU_DST_REG_FILE_BIT		50
#define EU_DST_REG_TYPE_HI		39
#define EU_DST_REG_TYPE_LO		36
#define EU_DST_HSTRIDE_HI		49
#define EU_DST_HSTRIDE_LO		48
#define EU_DST_SUBREG_HI		55
#define EU_DST_SUBREG_LO		51
#define EU_DST_REG_NR_HI		63
#define EU_DST_REG_NR_LO		56

/* Source 0 (direct, align1). Reg file is two discontiguous bits; the FI table
   lists them high then low, and the high bit is the same bit as is_imm. */
#define EU_SRC0_IS_IMM_BIT		46
#define EU_SRC0_REG_FILE_HI_BIT		46
#define EU_SRC0_REG_FILE_LO_BIT		66
#define EU_SRC0_REG_TYPE_HI		43
#define EU_SRC0_REG_TYPE_LO		40
#define EU_SRC0_SUBREG_HI		71
#define EU_SRC0_SUBREG_LO		67
#define EU_SRC0_REG_NR_HI		79
#define EU_SRC0_REG_NR_LO		72
#define EU_SRC0_HSTRIDE_HI		65
#define EU_SRC0_HSTRIDE_LO		64
#define EU_SRC0_WIDTH_HI		83
#define EU_SRC0_WIDTH_LO		81
#define EU_SRC0_VSTRIDE_HI		87
#define EU_SRC0_VSTRIDE_LO		84

/* Source 1 (direct, align1). Reg file high bit is the same bit as is_imm. */
#define EU_SRC1_IS_IMM_BIT		47
#define EU_SRC1_REG_FILE_HI_BIT		47
#define EU_SRC1_REG_FILE_LO_BIT		98
#define EU_SRC1_REG_TYPE_HI		91
#define EU_SRC1_REG_TYPE_LO		88
#define EU_SRC1_SUBREG_HI		103
#define EU_SRC1_SUBREG_LO		99
#define EU_SRC1_REG_NR_HI		111
#define EU_SRC1_REG_NR_LO		104
#define EU_SRC1_HSTRIDE_HI		97
#define EU_SRC1_HSTRIDE_LO		96
#define EU_SRC1_WIDTH_HI		115
#define EU_SRC1_WIDTH_LO		113
#define EU_SRC1_VSTRIDE_HI		119
#define EU_SRC1_VSTRIDE_LO		116

/* A 32-bit immediate occupies the last word (bits 127:96). */
#define EU_IMM32_HI			127
#define EU_IMM32_LO			96

/* Hardware opcode values (Gen12, opcode_descs[]). */
#define EU_OP_MOV			97U
#define EU_OP_ADD			64U
#define EU_OP_MUL			65U
#define EU_OP_MATH			56U
#define EU_OP_MAD			91U
#define EU_OP_SEND			49U
#define EU_OP_NOP			96U

/* Register file values. */
#define EU_FILE_ARF			0U
#define EU_FILE_GRF			1U
#define EU_FILE_IMM			3U

/* Register/immediate hardware type for 32-bit float (Gfx11+). */
#define EU_TYPE_F			9U

/* Exec size encoding: the field holds log2 of the channel count. */
#define EU_EXEC_SIZE_8			3U

/* Horizontal stride encoding: the field holds log2 of the stride, plus one. */
#define EU_HSTRIDE_1			1U
#define EU_VSTRIDE_8			4U
#define EU_WIDTH_8			4U
#define EU_VSTRIDE_0			0U
#define EU_WIDTH_1			0U
#define EU_HSTRIDE_0			0U

/* Math function selectors (brw_eu_defines.h). */
#define EU_MATH_INV			1U
#define EU_MATH_RSQ			2U
#define EU_MATH_SQRT			3U
#define EU_MATH_SIN			5U
#define EU_MATH_COS			6U

#endif /* I915_VK_EU_ENCODING_GEN12_INC */
