/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Gen12 video (MFX) commands of an H.264 VLD decode in the short format
 * driver interface: the command opcodes and packet lengths, and the dwords
 * and bits of the fields the decode programs.
 *
 * A video command's header dword is
 *   (Command Type 3 << 29) | (Pipeline 2 << 27) | (Media Command Opcode << 24) |
 *   (SubOpcode A << 21) | (SubOpcode B << 16) | (DWord Length = dwords - 2)
 * and the high sixteen bits are the opcode constants below.  A field at bit
 * b of the command is bit (b % 32) of dword (b / 32).  An address field
 * keeps the address's own bits where they are: a field from bit 6 of a
 * dword takes a 64-byte aligned address whose low dword is written as it
 * is.
 */

#ifndef DRIVERS_GPU_I915_INTEL_GENXML_VIDEO_H
#define DRIVERS_GPU_I915_INTEL_GENXML_VIDEO_H

/* Builds a video command header from an opcode and the packet's dword count. */
#define GEN12_VIDEO_HEADER(opcode, dwords)	(((uint32_t)(opcode) << 16) | ((dwords) - 2U))

/*
 * I915_CMD_MI_FLUSH_DW: MI Command Opcode 38, five dwords, DWord Length
 * 3.  Dword 0 bit 7 is Video Pipeline Cache Invalidate.  The other four
 * dwords (the post-sync address and data) stay zero: no post-sync write.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen110.xml (MI_FLUSH_DW).
 */
#define GEN12_VIDEO_MI_FLUSH_DW			0x13000003U
#define GEN12_VIDEO_MI_FLUSH_DW_DWORDS		5U
#define GEN12_VIDEO_MI_FLUSH_DW_INVALIDATE	(1U << 7)

/*
 * MI_FORCE_WAKEUP: MI Command Opcode 29, two dwords, DWord
 * Length 0.  Dword 1 bit 9 is MFX Power Well Control and bits 31:16 the
 * Mask Bits that let it be written (the mask written is 768).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen120.xml
 * (MI_FORCE_WAKEUP).
 */
#define GEN12_VIDEO_MI_FORCE_WAKEUP		0x0e800000U
#define GEN12_VIDEO_MI_FORCE_WAKEUP_DWORDS	2U
#define GEN12_VIDEO_FORCE_WAKEUP_MFX		(1U << 9)
#define GEN12_VIDEO_FORCE_WAKEUP_MASK_SHIFT	16U
#define GEN12_VIDEO_FORCE_WAKEUP_MASK		768U

/*
 * MFX_WAIT: one dword, Command Type 3, Command Subtype 1 (bits
 * 28:27), SubOpcode 0, DWord Length 0 (its bias is one).  Bit 8 is MFX Sync
 * Control Flag.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen75.xml (MFX_WAIT).
 */
#define GEN12_VIDEO_MFX_WAIT			0x68000000U
#define GEN12_VIDEO_MFX_WAIT_SYNC		(1U << 8)

/*
 * The opcodes of the MFX commands (the high sixteen bits of the header).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen120.xml and the files it
 * imports (the opcode fields of each command below).
 */
#define GEN12_VIDEO_MFX_PIPE_MODE_SELECT	0x7000U
#define GEN12_VIDEO_MFX_SURFACE_STATE		0x7001U
#define GEN12_VIDEO_MFX_PIPE_BUF_ADDR_STATE	0x7002U
#define GEN12_VIDEO_MFX_IND_OBJ_BASE_ADDR_STATE	0x7003U
#define GEN12_VIDEO_MFX_BSP_BUF_BASE_ADDR_STATE	0x7004U
#define GEN12_VIDEO_MFX_QM_STATE		0x7007U
#define GEN12_VIDEO_MFX_AVC_IMG_STATE		0x7100U
#define GEN12_VIDEO_MFX_AVC_DIRECTMODE_STATE	0x7102U
#define GEN12_VIDEO_MFD_AVC_PICID_STATE		0x7125U
#define GEN12_VIDEO_MFD_AVC_DPB_STATE		0x7126U
#define GEN12_VIDEO_MFD_AVC_SLICEADDR		0x7127U
#define GEN12_VIDEO_MFD_AVC_BSD_OBJECT		0x7128U

/*
 * The packet lengths of the MFX commands, in dwords.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen120.xml and the files it
 * imports (the "length" of each command below).
 */
#define GEN12_VIDEO_MFX_PIPE_MODE_SELECT_DWORDS		5U
#define GEN12_VIDEO_MFX_SURFACE_STATE_DWORDS		6U
#define GEN12_VIDEO_MFX_PIPE_BUF_ADDR_STATE_DWORDS	65U
#define GEN12_VIDEO_MFX_IND_OBJ_BASE_ADDR_STATE_DWORDS	26U
#define GEN12_VIDEO_MFX_BSP_BUF_BASE_ADDR_STATE_DWORDS	10U
#define GEN12_VIDEO_MFX_QM_STATE_DWORDS			18U
#define GEN12_VIDEO_MFX_AVC_IMG_STATE_DWORDS		21U
#define GEN12_VIDEO_MFX_AVC_DIRECTMODE_STATE_DWORDS	71U
#define GEN12_VIDEO_MFD_AVC_PICID_STATE_DWORDS		10U
#define GEN12_VIDEO_MFD_AVC_DPB_STATE_DWORDS		27U
#define GEN12_VIDEO_MFD_AVC_SLICEADDR_DWORDS		4U
#define GEN12_VIDEO_MFD_AVC_BSD_OBJECT_DWORDS		7U

/*
 * MFX_PIPE_MODE_SELECT, dword 1: Standard Select (3:0, AVC 2),
 * Codec Select (4, Decode 0), Pre Deblocking Output Enable (8), Post
 * Deblocking Output Enable (9), Decoder Mode Select (16:15, VLD 0) and
 * Decoder Short Format Mode (17, the short format driver interface 0).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen110.xml
 * (MFX_PIPE_MODE_SELECT).
 */
#define GEN12_VIDEO_MODE_STANDARD_AVC		2U
#define GEN12_VIDEO_MODE_POST_DEBLOCKING	(1U << 9)

/*
 * MFX_SURFACE_STATE.  Dword 1: Surface ID (3:0, the reference
 * picture 0).  Dword 2: Width (17:4) and Height (31:18), both less one.
 * Dword 3: Tile Walk (0, YMAJOR 1), Tiled Surface (1), Surface Pitch
 * (19:3, bytes less one), Interleave Chroma (27) and Surface Format
 * (31:28, PLANAR_420_8 4).  Dword 4: Y Offset for U(Cb) (14:0) and X Offset
 * (30:16).  Dword 5: Y Offset for V(Cr) (15:0) and X Offset (28:16).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen90.xml
 * (MFX_SURFACE_STATE).
 */
#define GEN12_VIDEO_SURFACE_WIDTH_SHIFT		4U
#define GEN12_VIDEO_SURFACE_HEIGHT_SHIFT	18U
#define GEN12_VIDEO_SURFACE_WALK_YMAJOR		(1U << 0)
#define GEN12_VIDEO_SURFACE_TILED		(1U << 1)
#define GEN12_VIDEO_SURFACE_PITCH_SHIFT		3U
#define GEN12_VIDEO_SURFACE_INTERLEAVE_CHROMA	(1U << 27)
#define GEN12_VIDEO_SURFACE_FORMAT_SHIFT	28U
#define GEN12_VIDEO_SURFACE_PLANAR_420_8	4U

/*
 * MFX_PIPE_BUF_ADDR_STATE: the dword each address starts at
 * (two dwords, the low and the high) and each attributes dword.  The
 * reference pictures are sixteen addresses of two dwords from dword 19,
 * with one attributes dword for all of them at 51.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen110.xml
 * (MFX_PIPE_BUF_ADDR_STATE).
 */
#define GEN12_VIDEO_BUF_PRE_DEBLOCKING		1U
#define GEN12_VIDEO_BUF_POST_DEBLOCKING		4U
#define GEN12_VIDEO_BUF_ORIGINAL_SOURCE		7U
#define GEN12_VIDEO_BUF_STREAM_OUT		10U
#define GEN12_VIDEO_BUF_INTRA_ROW_STORE		13U
#define GEN12_VIDEO_BUF_DEBLOCKING_ROW_STORE	16U
#define GEN12_VIDEO_BUF_REFERENCES		19U
#define GEN12_VIDEO_BUF_REFERENCE_ATTRIBUTES	51U
#define GEN12_VIDEO_BUF_MB_STATUS		52U
#define GEN12_VIDEO_BUF_MB_ILDB_STREAM_OUT	55U
#define GEN12_VIDEO_BUF_SECOND_MB_ILDB		58U
#define GEN12_VIDEO_BUF_COMPRESSION		61U
#define GEN12_VIDEO_BUF_SCALED_REFERENCE	62U

/*
 * MFX_IND_OBJ_BASE_ADDR_STATE: five objects of an address (two
 * dwords), an attributes dword and an upper bound (two dwords), from dword
 * 1 in steps of five; the first is the indirect bitstream object.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen80.xml
 * (MFX_IND_OBJ_BASE_ADDR_STATE).
 */
#define GEN12_VIDEO_IND_OBJECTS			5U
#define GEN12_VIDEO_IND_OBJECT_DWORDS		5U

/*
 * MFX_BSP_BUF_BASE_ADDR_STATE: the BSD/MPC row store at dword 1,
 * the MPR row store at dword 4 and the bitplane read buffer at dword 7,
 * each an address of two dwords and an attributes dword.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen90.xml
 * (MFX_BSP_BUF_BASE_ADDR_STATE).
 */
#define GEN12_VIDEO_BSP_BSD_MPC_ROW_STORE	1U
#define GEN12_VIDEO_BSP_MPR_ROW_STORE		4U
#define GEN12_VIDEO_BSP_BITPLANE		7U

/*
 * MFD_AVC_DPB_STATE.  Dword 1: Non-Existing Frame (bit i) and
 * Long Term Frame (bit 16 + i) of the sixteen DPB entries.  Dword 2: Used
 * for Reference (two bits from 2i, FRAME 3).  Dwords 3 to 10: the LTST
 * Frame Number List, sixteen bits each, two to a dword.  Dwords 11 to 26:
 * the view IDs and view orders, zero without MVC.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen90.xml
 * (MFD_AVC_DPB_STATE).
 */
#define GEN12_VIDEO_DPB_LONG_TERM_SHIFT		16U
#define GEN12_VIDEO_DPB_FRAME			3U
#define GEN12_VIDEO_DPB_FRAME_NUMBERS		3U

/*
 * MFD_AVC_PICID_STATE.  Dword 1 bit 0: PictureID Remapping
 * Disable (0, the 16-bit picture IDs are used).  Dwords 2 to 9: the
 * sixteen Picture IDs, sixteen bits each, two to a dword; 0xffff for an
 * entry no reference uses.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen75.xml
 * (MFD_AVC_PICID_STATE).
 */
#define GEN12_VIDEO_PICID_IDS			2U
#define GEN12_VIDEO_PICID_UNUSED		0xffffU

/*
 * MFX_AVC_IMG_STATE (Gen12 takes the 21-dword form, not the 14-dword form
 * of Gen8).
 *   dword 1: Frame Size (15:0, macroblocks)
 *   dword 2: Frame Width (7:0) and Frame Height (23:16), macroblocks less one
 *   dword 3: Image Structure (9:8, a frame 0), Weighted BiPrediction IDC
 *            (11:10), Weighted Prediction Enable (12), First Chroma QP
 *            Offset (20:16) and Second Chroma QP Offset (28:24), both signed
 *   dword 4: Field Picture (0), MBAFF Mode (1), Frame MB Only (2), 8x8 IDCT
 *            Transform Mode (3), Direct 8x8 Inference (4), Constrained Intra
 *            Prediction (5), Non-Reference Picture (6), Entropy Coding Sync
 *            Enable (7, CABAC) and Chroma Format IDC (11:10)
 *   dword 5: Trellis Quantization Chroma Disable (27)
 *   dword 13: Initial QP Value (7:0, signed), Number of Active Reference
 *            Pictures from L0 (13:8) and L1 (21:16), Number of Reference
 *            Frames (28:24)
 *   dword 14: Pic Order Present (0), Delta Pic Order Always Zero (1), Pic
 *            Order Count Type (3:2), Redundant Pic Count Present (11),
 *            Deblocking Filter Control Present (15), Log2 Max Frame Number
 *            (23:16) and Log2 Max Pic Order Count LSB (31:24), both less four
 *   dword 15: Current Picture Frame Number (31:16)
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen110.xml
 * (MFX_AVC_IMG_STATE).
 */
#define GEN12_VIDEO_IMG_HEIGHT_SHIFT		16U
#define GEN12_VIDEO_IMG_WEIGHTED_BIPRED_SHIFT	10U
#define GEN12_VIDEO_IMG_WEIGHTED_PRED		(1U << 12)
#define GEN12_VIDEO_IMG_CHROMA_OFFSET_SHIFT	16U
#define GEN12_VIDEO_IMG_SECOND_CHROMA_SHIFT	24U
#define GEN12_VIDEO_IMG_QP_OFFSET_MASK		0x1fU
#define GEN12_VIDEO_IMG_FRAME_MB_ONLY		(1U << 2)
#define GEN12_VIDEO_IMG_TRANSFORM_8X8		(1U << 3)
#define GEN12_VIDEO_IMG_DIRECT_8X8		(1U << 4)
#define GEN12_VIDEO_IMG_CONSTRAINED_INTRA	(1U << 5)
#define GEN12_VIDEO_IMG_NON_REFERENCE		(1U << 6)
#define GEN12_VIDEO_IMG_CABAC			(1U << 7)
#define GEN12_VIDEO_IMG_CHROMA_FORMAT_SHIFT	10U
#define GEN12_VIDEO_IMG_TRELLIS_CHROMA_DISABLE	(1U << 27)
#define GEN12_VIDEO_IMG_QP_MASK			0xffU
#define GEN12_VIDEO_IMG_L0_SHIFT		8U
#define GEN12_VIDEO_IMG_L1_SHIFT		16U
#define GEN12_VIDEO_IMG_REFERENCES_SHIFT	24U
#define GEN12_VIDEO_IMG_PIC_ORDER_PRESENT	(1U << 0)
#define GEN12_VIDEO_IMG_DELTA_ALWAYS_ZERO	(1U << 1)
#define GEN12_VIDEO_IMG_POC_TYPE_SHIFT		2U
#define GEN12_VIDEO_IMG_REDUNDANT_PIC_COUNT	(1U << 11)
#define GEN12_VIDEO_IMG_DEBLOCKING_CONTROL	(1U << 15)
#define GEN12_VIDEO_IMG_LOG2_FRAME_NUM_SHIFT	16U
#define GEN12_VIDEO_IMG_LOG2_POC_LSB_SHIFT	24U
#define GEN12_VIDEO_IMG_FRAME_NUM_SHIFT		16U

/*
 * MFX_QM_STATE.  Dword 1: the matrix (1:0): 4x4 intra 0, 4x4
 * inter 1, 8x8 intra 2, 8x8 inter 3.  Dwords 2 to 17: the Forward Quantizer
 * Matrix, 64 bytes in raster order, four to a dword from the low byte (a
 * 4x4 matrix is the Y, Cb and Cr lists one after the other).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen90.xml (MFX_QM_STATE).
 */
#define GEN12_VIDEO_QM_4X4_INTRA		0U
#define GEN12_VIDEO_QM_4X4_INTER		1U
#define GEN12_VIDEO_QM_8X8_INTRA		2U
#define GEN12_VIDEO_QM_8X8_INTER		3U
#define GEN12_VIDEO_QM_BYTES			64U

/*
 * MFX_AVC_DIRECTMODE_STATE: sixteen Direct MV Buffer addresses
 * of two dwords from dword 1, their attributes at dword 33, the Direct MV
 * Buffer (Write) address at 34 and its attributes at 36, and the POC List
 * from dword 37: the top and the bottom field order counts of the sixteen
 * entries, then the current picture's (entries 32 and 33).
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen80.xml
 * (MFX_AVC_DIRECTMODE_STATE).
 */
#define GEN12_VIDEO_DIRECT_BUFFERS		1U
#define GEN12_VIDEO_DIRECT_ATTRIBUTES		33U
#define GEN12_VIDEO_DIRECT_WRITE		34U
#define GEN12_VIDEO_DIRECT_WRITE_ATTRIBUTES	36U
#define GEN12_VIDEO_DIRECT_POC_LIST		37U
#define GEN12_VIDEO_DIRECT_POC_ENTRIES		34U

/*
 * MFD_AVC_SLICEADDR and MFD_AVC_BSD_OBJECT: dword 1 the
 * Indirect BSD Data Length (bytes), dword 2 the Indirect BSD Data Start
 * Address (28:0, bytes from the indirect bitstream object's address).
 * The BSD object's inline data are dwords 3 to 5:
 *   dword 4: Last Slice (3), Fix Prev MB Skipped (7)
 *   dword 5: Intra Prediction Error Control (0), Intra 8x8/4x4 Prediction
 *            Error Concealment Control (1), I Slice Concealment Mode (31,
 *            intra concealment 1)
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen110.xml
 * (MFD_AVC_SLICEADDR, MFD_AVC_BSD_OBJECT).
 */
#define GEN12_VIDEO_BSD_START_MASK		0x1fffffffU
#define GEN12_VIDEO_BSD_LAST_SLICE		(1U << 3)
#define GEN12_VIDEO_BSD_FIX_PREV_MB_SKIPPED	(1U << 7)
#define GEN12_VIDEO_BSD_INTRA_ERROR_CONTROL	(1U << 0)
#define GEN12_VIDEO_BSD_INTRA_CONCEALMENT	(1U << 1)
#define GEN12_VIDEO_BSD_I_SLICE_CONCEALMENT	(1U << 31)

/*
 * MEMORYADDRESSATTRIBUTES: MOCS (6:0) is the low field of the
 * dword; the rest (arbitration, compression, cache select, tiled resource
 * mode) stays zero.
 *
 * Values referenced: Mesa 25.0.7 src/intel/genxml/gen90.xml
 * (MEMORYADDRESSATTRIBUTES).
 */
#define GEN12_VIDEO_ATTRIBUTES_MOCS_MASK	0x7fU

#endif /* DRIVERS_GPU_I915_INTEL_GENXML_VIDEO_H */
