/*
 * zedBSD — transcribed hardware definitions (values only)
 *
 * Source: Mesa src/intel/genxml/gen120.xml and its imports (core Mesa, MIT;
 * values are Intel PRM hardware facts). SHA-256 in the audit.
 *
 * The 3D pipeline command header dword is
 *   (Command Type 3 << 29) | (Pipeline 3 << 27) | (Opcode << 24) |
 *   (Sub Opcode << 16) | (DWord Length = total dwords - 2)
 * The high sixteen bits of that dword are the opcode constants below, taken
 * from each instruction's Command Type / Pipeline / 3D Command Opcode /
 * 3D Command Sub Opcode default fields.  The assembly logic is new.
 */

#ifndef I915_VK_3DSTATE_GEN12_INC
#define I915_VK_3DSTATE_GEN12_INC

/* Command opcodes (high sixteen bits of the header dword). */
#define GEN12_CMD_PIPELINE_SELECT		0x6104U
#define GEN12_CMD_STATE_BASE_ADDRESS		0x6101U
#define GEN12_CMD_3DSTATE_VS			0x7810U
#define GEN12_CMD_3DSTATE_PS			0x7820U
#define GEN12_CMD_3DSTATE_PS_EXTRA		0x784FU
#define GEN12_CMD_3DSTATE_VERTEX_ELEMENTS	0x7809U
#define GEN12_CMD_3DPRIMITIVE			0x7B00U

/* Builds a command header from an opcode and the packet's dword count. */
#define GEN12_CMD_HEADER(opcode, dwords)	(((opcode) << 16) | ((dwords) - 2U))

/* Packet lengths (gen120.xml instruction "length"). */
#define GEN12_3DSTATE_VS_DWORDS			9U
#define GEN12_3DSTATE_PS_DWORDS			12U
#define GEN12_3DSTATE_VERTEX_ELEMENTS_DWORDS	3U
#define GEN12_3DPRIMITIVE_DWORDS		7U
#define GEN12_PIPELINE_SELECT_DWORDS		1U
#define GEN12_STATE_BASE_ADDRESS_DWORDS		22U

/* PIPELINE_SELECT: the 3D pipeline is selection value 0. */
#define GEN12_PIPELINE_SELECT_3D		0U

#endif /* I915_VK_3DSTATE_GEN12_INC */
