/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The command packets and the registers the amdrdna executor writes to
 * draw and dispatch on a GFX10 (RDNA1) graphics queue, and the writer of
 * an indirect buffer of packets.
 *
 * A register is named by its byte offset, as the packets that set it count
 * from the base of its space: the shader registers from 0xb000, the
 * context registers from 0x28000 and the user configuration registers from
 * 0x30000.  Every packet is the type-3 form: a header word with the body's
 * word count less one, the operation and the compute bit, then the body.
 */

#ifndef DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_GFX10_H
#define DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_GFX10_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/*
 * The packet header, the opcodes and the register spaces' first registers
 are the driver's (amdrdna-hw.h), which the driver's ring commands use
 * too.  What only the executor writes: the draw's generated-index source
 * (2), the end-of-pipe write's destination (memory) and interrupt (none,
 * after the write is confirmed) fields, and the place of an event's index.
 *
 * Values referenced: Mesa 25.0.7 src/amd/common/sid.h (EOP_DST_SEL_MEM,
 * EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EVENT_INDEX) and
 * src/amd/registers/gfx10.json (the enum VGT_DI_SOURCE_SELECT:
 * DI_SRC_SEL_AUTO_INDEX 2).
 */
#define AMDRDNA_PM4_DRAW_GENERATED_INDICES	2U
#define AMDRDNA_PM4_FENCE_TO_MEMORY		(0U << 16)
#define AMDRDNA_PM4_FENCE_INTERRUPT_AFTER_WRITE	(3U << 24)
#define AMDRDNA_PM4_EVENT_INDEX_SHIFT		8U

/*
 * The events the executor sends: the wait for the pixel and the compute
 * shaders, the flush of the colour units' metadata (index 0), and the
 * end-of-pipe flush and invalidation of the colour and depth caches with
 * a timestamp (index 5); and the field positions of the release's cache
 * actions (GLM, GLV, GL1, GL2 write back and invalidate, sequence).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json (the enum
 * VGT_EVENT_TYPE: CS_PARTIAL_FLUSH 7, PS_PARTIAL_FLUSH 16,
 * CACHE_FLUSH_AND_INV_TS_EVENT 20, FLUSH_AND_INV_CB_META 46),
 * src/amd/registers/pkt3.json (RELEASE_MEM_OP: EVENT_INDEX 11:8, GLM_WB 12,
 * GLM_INV 13, GLV_INV 14, GL1_INV 15, GL2_INV 20, GL2_WB 21, SEQ 23:22;
 * the enum GCR_SEQ: SEQ_FORWARD 1) and src/amd/vulkan/radv_cs.c
 * (gfx10_cs_emit_cache_flush: the partial flushes at index 4, the
 * timestamp events at index 5).
 */
#define AMDRDNA_PM4_EVENT_COMPUTE_DRAINED	7U
#define AMDRDNA_PM4_EVENT_PIXELS_DRAINED	16U
#define AMDRDNA_PM4_EVENT_CACHES_FLUSHED	20U
#define AMDRDNA_PM4_EVENT_COLOUR_META_FLUSHED	46U
#define AMDRDNA_PM4_EVENT_INDEX_DRAIN		4U
#define AMDRDNA_PM4_EVENT_INDEX_TIMESTAMP	5U
#define AMDRDNA_PM4_RELEASE_METADATA_WRITE_BACK	(1U << 12)
#define AMDRDNA_PM4_RELEASE_METADATA_INVALIDATE	(1U << 13)
#define AMDRDNA_PM4_RELEASE_VECTOR_INVALIDATE	(1U << 14)
#define AMDRDNA_PM4_RELEASE_LEVEL1_INVALIDATE	(1U << 15)
#define AMDRDNA_PM4_RELEASE_LEVEL2_INVALIDATE	(1U << 20)
#define AMDRDNA_PM4_RELEASE_LEVEL2_WRITE_BACK	(1U << 21)
#define AMDRDNA_PM4_RELEASE_IN_ORDER		(1U << 22)

/*
 * The cache actions of a memory acquire (its last word): the instruction
 * cache invalidated (bits 1:0 = 1), the metadata cache written back and
 * invalidated (4, 5), the scalar cache invalidated (7), the vector cache
 * (8) and the per-array cache (9) invalidated, the level-2 cache
 * invalidated (14) and written back (15).  Its fixed words (the whole
 * address space, the poll interval) are the driver's.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/pkt3.json (GCR_CNTL:
 * GLI_INV 1:0, GLM_WB 4, GLM_INV 5, GLK_INV 7, GLV_INV 8, GL1_INV 9,
 * GL2_INV 14, GL2_WB 15; the enum GCR_GLI_INV: GLI_ALL 1) and
 * src/amd/vulkan/radv_cs.c (gfx10_cs_emit_cache_flush: the acquire's
 * words 0, 0xffffffff, 0xffffff, 0, 0, 0xa, then the controls).
 */
#define AMDRDNA_PM4_ACQUIRE_INSTRUCTIONS	(1U << 0)
#define AMDRDNA_PM4_ACQUIRE_METADATA_WRITE_BACK	(1U << 4)
#define AMDRDNA_PM4_ACQUIRE_METADATA_INVALIDATE	(1U << 5)
#define AMDRDNA_PM4_ACQUIRE_SCALAR		(1U << 7)
#define AMDRDNA_PM4_ACQUIRE_VECTOR		(1U << 8)
#define AMDRDNA_PM4_ACQUIRE_LEVEL1		(1U << 9)
#define AMDRDNA_PM4_ACQUIRE_LEVEL2_INVALIDATE	(1U << 14)
#define AMDRDNA_PM4_ACQUIRE_LEVEL2_WRITE_BACK	(1U << 15)

/*
 * The shader registers (bytes from 0): of the pixel program its code
 * address (low, high), its two resource words, its first user SGPR, its
 * CU masks (two) and its request control; of the vertex program the same;
 * of the compute program its code address, resource words, workgroup
 * size, start, first user SGPR, resource limits, dispatch tunnel and the
 * CU masks of its four shader engines.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_SHADER_PGM_LO_PS 0xb020, SPI_SHADER_PGM_HI_PS 0xb024,
 * SPI_SHADER_PGM_RSRC1_PS 0xb028, SPI_SHADER_PGM_RSRC2_PS 0xb02c,
 * SPI_SHADER_USER_DATA_PS_0 0xb030, SPI_SHADER_PGM_RSRC3_PS 0xb01c,
 * SPI_SHADER_PGM_RSRC4_PS 0xb004, SPI_SHADER_REQ_CTRL_PS 0xb0c0,
 * SPI_SHADER_PGM_LO_VS 0xb120, SPI_SHADER_PGM_HI_VS 0xb124,
 * SPI_SHADER_PGM_RSRC1_VS 0xb128, SPI_SHADER_PGM_RSRC2_VS 0xb12c,
 * SPI_SHADER_USER_DATA_VS_0 0xb130, SPI_SHADER_PGM_RSRC3_VS 0xb118,
 * SPI_SHADER_PGM_RSRC4_VS 0xb104, SPI_SHADER_REQ_CTRL_VS 0xb1c0,
 * COMPUTE_PGM_LO 0xb830, COMPUTE_PGM_HI 0xb834, COMPUTE_PGM_RSRC1 0xb848,
 * COMPUTE_PGM_RSRC2 0xb84c, COMPUTE_PGM_RSRC3 0xb8a0,
 * COMPUTE_NUM_THREAD_X 0xb81c, COMPUTE_START_X 0xb810,
 * COMPUTE_USER_DATA_0 0xb900, COMPUTE_RESOURCE_LIMITS 0xb854,
 * COMPUTE_DISPATCH_TUNNEL 0xb9f4) and src/amd/registers/gfx9.json
 * (COMPUTE_STATIC_THREAD_MGMT_SE0 0xb858, SE1 0xb85c, SE2 0xb864, SE3
 * 0xb868, which src/amd/common/ac_cmdbuf.c sets on GFX10 too).
 */
#define AMDRDNA_SH_PIXEL_CODE_LOW		0xb020U
#define AMDRDNA_SH_PIXEL_CODE_HIGH		0xb024U
#define AMDRDNA_SH_PIXEL_RESOURCES		0xb028U
#define AMDRDNA_SH_PIXEL_INPUTS			0xb02cU
#define AMDRDNA_SH_PIXEL_USER_DATA		0xb030U
#define AMDRDNA_SH_PIXEL_UNITS			0xb01cU
#define AMDRDNA_SH_PIXEL_UNITS_HIGH		0xb004U
#define AMDRDNA_SH_PIXEL_REQUESTS		0xb0c0U
#define AMDRDNA_SH_VERTEX_CODE_LOW		0xb120U
#define AMDRDNA_SH_VERTEX_CODE_HIGH		0xb124U
#define AMDRDNA_SH_VERTEX_RESOURCES		0xb128U
#define AMDRDNA_SH_VERTEX_INPUTS		0xb12cU
#define AMDRDNA_SH_VERTEX_USER_DATA		0xb130U
#define AMDRDNA_SH_VERTEX_UNITS			0xb118U
#define AMDRDNA_SH_VERTEX_UNITS_HIGH		0xb104U
#define AMDRDNA_SH_VERTEX_REQUESTS		0xb1c0U
#define AMDRDNA_SH_KERNEL_CODE_LOW		0xb830U
#define AMDRDNA_SH_KERNEL_CODE_HIGH		0xb834U
#define AMDRDNA_SH_KERNEL_RESOURCES		0xb848U
#define AMDRDNA_SH_KERNEL_INPUTS		0xb84cU
#define AMDRDNA_SH_KERNEL_MORE_RESOURCES	0xb8a0U
#define AMDRDNA_SH_KERNEL_GROUP_SIZE		0xb81cU
#define AMDRDNA_SH_KERNEL_START			0xb810U
#define AMDRDNA_SH_KERNEL_USER_DATA		0xb900U
#define AMDRDNA_SH_KERNEL_LIMITS		0xb854U
#define AMDRDNA_SH_KERNEL_TUNNEL		0xb9f4U
#define AMDRDNA_SH_KERNEL_UNITS_ENGINE0		0xb858U
#define AMDRDNA_SH_KERNEL_UNITS_ENGINE1		0xb85cU
#define AMDRDNA_SH_KERNEL_UNITS_ENGINE2		0xb864U
#define AMDRDNA_SH_KERNEL_UNITS_ENGINE3		0xb868U

/*
 * The CU masks the programs may run on (all, and a wave limit of 63), the
 * pixel program's request control (soft grouping, four requests per CU),
 * the compute dispatch's start word (the shader, the start at 0, the order
 * mode and wave32), and the code address shifts.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_SHADER_PGM_RSRC3_PS: CU_EN 15:0, WAVE_LIMIT 21:16;
 * SPI_SHADER_PGM_RSRC4_PS: CU_EN; SPI_SHADER_REQ_CTRL_PS:
 * SOFT_GROUPING_EN 0, NUMBER_OF_REQUESTS_PER_CU 4:1;
 * COMPUTE_DISPATCH_INITIATOR: COMPUTE_SHADER_EN 0, FORCE_START_AT_000 2,
 * ORDER_MODE 6, CS_W32_EN 15; SPI_SHADER_PGM_HI_PS: MEM_BASE 7:0) and
 * src/amd/common/ac_cmdbuf.c (gfx10_init_graphics_preamble_state: the CU
 * masks, the wave limit 0x3f and the request control).
 */
#define AMDRDNA_SH_UNITS_ALL			(0xffffU | (0x3fU << 16))
#define AMDRDNA_SH_UNITS_HIGH_ALL		0xffffU
#define AMDRDNA_SH_PIXEL_REQUEST_CONTROL	(1U | (3U << 1))
#define AMDRDNA_SH_DISPATCH_START		(1U | (1U << 2) | (1U << 6) | (1U << 15))
#define AMDRDNA_SH_CODE_LOW_SHIFT		8U
#define AMDRDNA_SH_CODE_HIGH_SHIFT		40U

/*
 * The context registers (bytes from 0x28000 on): the colour target 0 (its
 * address, high address, view, layout, attributes, size and surface), the
 * colour control, write and export masks, the blend of target 0 and its
 * optimisation, the export down-conversion, the depth state, the
 * scissors, the viewport 0, the clip, the rasteriser, the vertex output
 * and the pixel input words, and the stage enable.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (CB_COLOR0_BASE 0x28c60, CB_COLOR0_BASE_EXT 0x28e40, CB_COLOR0_VIEW
 * 0x28c6c, CB_COLOR0_INFO 0x28c70, CB_COLOR0_ATTRIB 0x28c74,
 * CB_COLOR0_ATTRIB2 0x28ec0, CB_COLOR0_ATTRIB3 0x28ee0, CB_COLOR_CONTROL
 * 0x28808, CB_TARGET_MASK 0x28238, CB_SHADER_MASK 0x2823c,
 * CB_BLEND0_CONTROL 0x28780, SX_MRT0_BLEND_OPT 0x28760, SX_PS_DOWNCONVERT
 * 0x28754, SX_BLEND_OPT_EPSILON 0x28758, SX_BLEND_OPT_CONTROL 0x2875c,
 * DB_RENDER_CONTROL 0x28000, DB_Z_INFO 0x28040, DB_STENCIL_INFO 0x28044,
 * DB_DEPTH_CONTROL 0x28800, DB_SHADER_CONTROL 0x2880c,
 * PA_SC_WINDOW_OFFSET 0x28200, PA_SC_WINDOW_SCISSOR_TL 0x28204,
 * PA_SC_WINDOW_SCISSOR_BR 0x28208, PA_SC_CLIPRECT_RULE 0x2820c,
 * PA_SC_SCREEN_SCISSOR_TL 0x28030, PA_SC_SCREEN_SCISSOR_BR 0x28034,
 * PA_SC_GENERIC_SCISSOR_TL 0x28240, PA_SC_GENERIC_SCISSOR_BR 0x28244,
 * PA_SC_VPORT_SCISSOR_0_TL 0x28250, PA_SC_VPORT_SCISSOR_0_BR 0x28254,
 * PA_SC_VPORT_ZMIN_0 0x282d0, PA_SC_VPORT_ZMAX_0 0x282d4,
 * PA_CL_VPORT_XSCALE 0x2843c, PA_CL_CLIP_CNTL 0x28810, PA_SU_SC_MODE_CNTL
 * 0x28814, PA_CL_VTE_CNTL 0x28818, PA_CL_VS_OUT_CNTL 0x2881c,
 * PA_SU_VTX_CNTL 0x28be4, PA_CL_GB_VERT_CLIP_ADJ 0x28be8, PA_SC_MODE_CNTL_0
 * 0x28a48, PA_SC_AA_CONFIG 0x28be0, PA_SC_AA_MASK_X0Y0_X1Y0 0x28c38,
 * PA_SC_BINNER_CNTL_0 0x28c44, PA_SC_EDGERULE 0x28230, PA_SU_POINT_SIZE
 * 0x28a00, PA_SU_POINT_MINMAX 0x28a04, PA_SU_LINE_CNTL 0x28a08,
 * PA_SU_SMALL_PRIM_FILTER_CNTL 0x28830, SPI_PS_INPUT_CNTL_0 0x28644,
 * SPI_VS_OUT_CONFIG 0x286c4, SPI_PS_INPUT_ENA 0x286cc, SPI_PS_INPUT_ADDR
 * 0x286d0, SPI_INTERP_CONTROL_0 0x286d4, SPI_PS_IN_CONTROL 0x286d8,
 * SPI_BARYC_CNTL 0x286e0, SPI_SHADER_POS_FORMAT 0x2870c,
 * SPI_SHADER_Z_FORMAT 0x28710, SPI_SHADER_COL_FORMAT 0x28714,
 * VGT_SHADER_STAGES_EN 0x28b54, VGT_GS_MODE 0x28a40, VGT_PRIMITIVEID_EN
 * 0x28a84, VGT_REUSE_OFF 0x28ab4, VGT_VERTEX_REUSE_BLOCK_CNTL 0x28c58).
 */
#define AMDRDNA_CX_TARGET_ADDRESS		0x28c60U
#define AMDRDNA_CX_TARGET_ADDRESS_HIGH		0x28e40U
#define AMDRDNA_CX_TARGET_VIEW			0x28c6cU
#define AMDRDNA_CX_TARGET_LAYOUT		0x28c70U
#define AMDRDNA_CX_TARGET_ATTRIBUTES		0x28c74U
#define AMDRDNA_CX_TARGET_SIZE			0x28ec0U
#define AMDRDNA_CX_TARGET_SURFACE		0x28ee0U
#define AMDRDNA_CX_COLOUR_CONTROL		0x28808U
#define AMDRDNA_CX_COLOUR_WRITE_MASK		0x28238U
#define AMDRDNA_CX_COLOUR_EXPORT_MASK		0x2823cU
#define AMDRDNA_CX_BLEND			0x28780U
#define AMDRDNA_CX_BLEND_SHORTCUT		0x28760U
#define AMDRDNA_CX_EXPORT_CONVERSION		0x28754U
#define AMDRDNA_CX_BLEND_SHORTCUT_EPSILON	0x28758U
#define AMDRDNA_CX_BLEND_SHORTCUT_CONTROL	0x2875cU
#define AMDRDNA_CX_DEPTH_RENDER			0x28000U
#define AMDRDNA_CX_DEPTH_LAYOUT			0x28040U
#define AMDRDNA_CX_STENCIL_LAYOUT		0x28044U
#define AMDRDNA_CX_DEPTH_CONTROL		0x28800U
#define AMDRDNA_CX_DEPTH_SHADER_CONTROL		0x2880cU
#define AMDRDNA_CX_WINDOW_OFFSET		0x28200U
#define AMDRDNA_CX_WINDOW_SCISSOR		0x28204U
#define AMDRDNA_CX_CLIP_RECTANGLE_RULE		0x2820cU
#define AMDRDNA_CX_SCREEN_SCISSOR		0x28030U
#define AMDRDNA_CX_GENERIC_SCISSOR		0x28240U
#define AMDRDNA_CX_VIEWPORT_SCISSOR		0x28250U
#define AMDRDNA_CX_VIEWPORT_DEPTH_RANGE		0x282d0U
#define AMDRDNA_CX_VIEWPORT_TRANSFORM		0x2843cU
#define AMDRDNA_CX_CLIP_CONTROL			0x28810U
#define AMDRDNA_CX_RASTER_MODE			0x28814U
#define AMDRDNA_CX_VIEWPORT_ENABLE		0x28818U
#define AMDRDNA_CX_VERTEX_OUTPUT_CONTROL	0x2881cU
#define AMDRDNA_CX_VERTEX_ROUNDING		0x28be4U
#define AMDRDNA_CX_GUARD_BAND			0x28be8U
#define AMDRDNA_CX_SCAN_MODE			0x28a48U
#define AMDRDNA_CX_SAMPLE_CONFIG		0x28be0U
#define AMDRDNA_CX_SAMPLE_MASK			0x28c38U
#define AMDRDNA_CX_BINNER_CONTROL		0x28c44U
#define AMDRDNA_CX_EDGE_RULE			0x28230U
#define AMDRDNA_CX_POINT_SIZE			0x28a00U
#define AMDRDNA_CX_POINT_LIMITS			0x28a04U
#define AMDRDNA_CX_LINE_WIDTH			0x28a08U
#define AMDRDNA_CX_SMALL_PRIMITIVE_FILTER	0x28830U
#define AMDRDNA_CX_ATTRIBUTE_CONTROL		0x28644U
#define AMDRDNA_CX_VERTEX_EXPORT_CONFIG		0x286c4U
#define AMDRDNA_CX_PIXEL_INPUT_ENABLE		0x286ccU
#define AMDRDNA_CX_PIXEL_INPUT_ADDRESS		0x286d0U
#define AMDRDNA_CX_INTERPOLATION_CONTROL	0x286d4U
#define AMDRDNA_CX_PIXEL_INPUT_CONTROL		0x286d8U
#define AMDRDNA_CX_BARYCENTRIC_CONTROL		0x286e0U
#define AMDRDNA_CX_POSITION_FORMATS		0x2870cU
#define AMDRDNA_CX_DEPTH_EXPORT_FORMAT		0x28710U
#define AMDRDNA_CX_COLOUR_EXPORT_FORMATS	0x28714U
#define AMDRDNA_CX_STAGES			0x28b54U
#define AMDRDNA_CX_GEOMETRY_MODE		0x28a40U
#define AMDRDNA_CX_PRIMITIVE_ID_ENABLE		0x28a84U
#define AMDRDNA_CX_REUSE_OFF			0x28ab4U
#define AMDRDNA_CX_VERTEX_REUSE			0x28c58U

/*
 * The user configuration registers (bytes from 0x30000 on): the primitive
 * type (set through the indexed setter with index 1), the geometry
 * engine's control and its index limits and offset, the instance base,
 * and the coherency start delay.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (VGT_PRIMITIVE_TYPE 0x30908, GE_CNTL 0x3096c, GE_MIN_VTX_INDX 0x30924,
 * GE_INDX_OFFSET 0x30928, GE_MAX_VTX_INDX 0x30964, VGT_INSTANCE_BASE_ID
 * 0x30968, GE_USER_VGPR_EN 0x30988, CP_COHER_START_DELAY 0x301ec) and
 * src/amd/vulkan/radv_cmd_buffer.c (the primitive type set with index 1).
 */
#define AMDRDNA_UC_PRIMITIVE_TYPE		0x30908U
#define AMDRDNA_UC_PRIMITIVE_TYPE_INDEX		1U
#define AMDRDNA_UC_GEOMETRY_CONTROL		0x3096cU
#define AMDRDNA_UC_INDEX_MINIMUM		0x30924U
#define AMDRDNA_UC_INDEX_OFFSET			0x30928U
#define AMDRDNA_UC_INDEX_MAXIMUM		0x30964U
#define AMDRDNA_UC_INSTANCE_BASE		0x30968U
#define AMDRDNA_UC_USER_VGPRS			0x30988U
#define AMDRDNA_UC_COHERENCY_DELAY		0x301ecU

/*
 * The colour target's fields: the layout's format in bits 6:2 (8, 8, 8, 8
 * is 10), number type in 10:8 (unorm 0, sRGB 6), component swap in 12:11
 * (standard 0, alternate 1), blend clamp 15 and simple float 17; the
 * size's height less one in 13:0 and width less one in 27:14; the
 * surface's resource type 2D (1) in 25:24 and resource level 1 in 29:27
 * (the linear swizzle mode is 0).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (CB_COLOR0_INFO: FORMAT, NUMBER_TYPE, COMP_SWAP, BLEND_CLAMP,
 * SIMPLE_FLOAT; CB_COLOR0_ATTRIB2: MIP0_HEIGHT, MIP0_WIDTH;
 * CB_COLOR0_ATTRIB3: COLOR_SW_MODE, RESOURCE_TYPE, RESOURCE_LEVEL; the
 * enums ColorFormat: COLOR_8_8_8_8 10, SurfaceNumber: NUMBER_UNORM 0,
 * NUMBER_SRGB 6, SurfaceSwap: SWAP_STD 0, SWAP_ALT 1).
 */
#define AMDRDNA_CX_TARGET_FORMAT_8888		(10U << 2)
#define AMDRDNA_CX_TARGET_NUMBER_UNORM		(0U << 8)
#define AMDRDNA_CX_TARGET_NUMBER_SRGB		(6U << 8)
#define AMDRDNA_CX_TARGET_SWAP_STANDARD		(0U << 11)
#define AMDRDNA_CX_TARGET_SWAP_ALTERNATE	(1U << 11)
#define AMDRDNA_CX_TARGET_BLEND_CLAMP		(1U << 15)
#define AMDRDNA_CX_TARGET_SIMPLE_FLOAT		(1U << 17)
#define AMDRDNA_CX_TARGET_WIDTH_SHIFT		14U
#define AMDRDNA_CX_TARGET_SURFACE_2D		((1U << 24) | (1U << 27))

/*
 * The colour control (normal mode 1 in 6:4, the copy raster operation 0xcc
 * in 23:16); the blend's fields (colour source 4:0, function 7:5,
 * destination 12:8, alpha source 20:16, function 23:21, destination 28:24,
 * separate alpha 29, enable 30); its factors and functions; the blend
 * shortcut that is off for a target that does not blend (6 in both
 * function fields, 10:8 and 26:24) or that blends (0 there, no shortcut).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (CB_COLOR_CONTROL: MODE, ROP3; the enum CBMode: CB_NORMAL 1;
 * CB_BLEND0_CONTROL and the enums BlendOp: BLEND_ZERO 0, BLEND_ONE 1,
 * BLEND_SRC_COLOR 2, BLEND_ONE_MINUS_SRC_COLOR 3, BLEND_SRC_ALPHA 4,
 * BLEND_ONE_MINUS_SRC_ALPHA 5, BLEND_DST_ALPHA 6, BLEND_ONE_MINUS_DST_ALPHA
 * 7, BLEND_DST_COLOR 8, BLEND_ONE_MINUS_DST_COLOR 9,
 * BLEND_SRC_ALPHA_SATURATE 10, BLEND_CONSTANT_COLOR 13,
 * BLEND_ONE_MINUS_CONSTANT_COLOR 14, BLEND_SRC1_COLOR 15,
 * BLEND_INV_SRC1_COLOR 16, BLEND_SRC1_ALPHA 17, BLEND_INV_SRC1_ALPHA 18,
 * BLEND_CONSTANT_ALPHA 19, BLEND_ONE_MINUS_CONSTANT_ALPHA 20, CombFunc:
 * COMB_DST_PLUS_SRC 0, COMB_SRC_MINUS_DST 1, COMB_MIN_DST_SRC 2,
 * COMB_MAX_DST_SRC 3, COMB_DST_MINUS_SRC 4; SX_MRT0_BLEND_OPT: COLOR_COMB_FCN,
 * ALPHA_COMB_FCN; the enum SX_OPT_COMB_FCN: OPT_COMB_NONE 0,
 * OPT_COMB_BLEND_DISABLED 6) and src/amd/vulkan/radv_cmd_buffer.c
 * (radv_emit_color_blend: the shortcut off without blending, none with
 * it as for a dual-source blend).
 */
#define AMDRDNA_CX_COLOUR_CONTROL_NORMAL	((1U << 4) | (0xccU << 16))
#define AMDRDNA_CX_BLEND_DESTINATION_SHIFT	8U
#define AMDRDNA_CX_BLEND_FUNCTION_SHIFT		5U
#define AMDRDNA_CX_BLEND_ALPHA_SHIFT		16U
#define AMDRDNA_CX_BLEND_SEPARATE_ALPHA		(1U << 29)
#define AMDRDNA_CX_BLEND_ENABLE			(1U << 30)
#define AMDRDNA_BLEND_ZERO			0U
#define AMDRDNA_BLEND_ONE			1U
#define AMDRDNA_BLEND_SOURCE_COLOUR		2U
#define AMDRDNA_BLEND_ONE_MINUS_SOURCE_COLOUR	3U
#define AMDRDNA_BLEND_SOURCE_ALPHA		4U
#define AMDRDNA_BLEND_ONE_MINUS_SOURCE_ALPHA	5U
#define AMDRDNA_BLEND_TARGET_ALPHA		6U
#define AMDRDNA_BLEND_ONE_MINUS_TARGET_ALPHA	7U
#define AMDRDNA_BLEND_TARGET_COLOUR		8U
#define AMDRDNA_BLEND_ONE_MINUS_TARGET_COLOUR	9U
#define AMDRDNA_BLEND_SOURCE_ALPHA_SATURATED	10U
#define AMDRDNA_BLEND_CONSTANT_COLOUR		13U
#define AMDRDNA_BLEND_ONE_MINUS_CONSTANT_COLOUR	14U
#define AMDRDNA_BLEND_SECOND_COLOUR		15U
#define AMDRDNA_BLEND_ONE_MINUS_SECOND_COLOUR	16U
#define AMDRDNA_BLEND_SECOND_ALPHA		17U
#define AMDRDNA_BLEND_ONE_MINUS_SECOND_ALPHA	18U
#define AMDRDNA_BLEND_CONSTANT_ALPHA		19U
#define AMDRDNA_BLEND_ONE_MINUS_CONSTANT_ALPHA	20U
#define AMDRDNA_BLEND_ADD			0U
#define AMDRDNA_BLEND_SUBTRACT			1U
#define AMDRDNA_BLEND_MINIMUM			2U
#define AMDRDNA_BLEND_MAXIMUM			3U
#define AMDRDNA_BLEND_REVERSE_SUBTRACT		4U
#define AMDRDNA_CX_BLEND_SHORTCUT_OFF		((6U << 8) | (6U << 24))
#define AMDRDNA_CX_BLEND_SHORTCUT_NONE		0U

/*
 * The depth state of a pass without depth: the pixel program's depth
 * control with early depth then late (1 in 5:4), the kill enable (6) for a
 * program that discards, and the dual-quad disable (15); the depth and
 * stencil layouts invalid (0).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (DB_SHADER_CONTROL: Z_ORDER, KILL_ENABLE, DUAL_QUAD_DISABLE; the enum
 * ZOrder: EARLY_Z_THEN_LATE_Z 1; DB_Z_INFO: FORMAT; the enum ZFormat:
 * Z_INVALID 0).
 */
#define AMDRDNA_CX_DEPTH_SHADER_EARLY		(1U << 4)
#define AMDRDNA_CX_DEPTH_SHADER_KILL		(1U << 6)
#define AMDRDNA_CX_DEPTH_SHADER_NO_DUAL_QUAD	(1U << 15)

/*
 * The scissor words: x in 14:0, y in 30:16 (the screen scissor's 15:0 and
 * 31:16), and the window offset off (31); the edge rule and the clip
 * rectangle rule of a plain rasteriser; the viewport scissor enable (bit 1
 * of the scan mode); the clip control of a D3D clip space with linear
 * attribute clipping (19, 24); the viewport enables (5:0) with w as 1/w
 * (10); the vertex rounding (the pixel centre at 0.5 (0), to even (2 in
 * 2:1), 1/256 fixed point (5 in 5:3)); the guard band words of 1.0; the
 * rasteriser's cull bits (front 0, back 1) and its clockwise front face (2).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (PA_SC_VPORT_SCISSOR_0_TL: TL_X, TL_Y, WINDOW_OFFSET_DISABLE;
 * PA_SC_SCREEN_SCISSOR_TL; PA_SC_MODE_CNTL_0: VPORT_SCISSOR_ENABLE;
 * PA_CL_CLIP_CNTL: DX_CLIP_SPACE_DEF, DX_LINEAR_ATTR_CLIP_ENA;
 * PA_CL_VTE_CNTL: VPORT_*_ENA, VTX_W0_FMT; PA_SU_VTX_CNTL: PIX_CENTER,
 * ROUND_MODE, QUANT_MODE and their enums X_ROUND_TO_EVEN 2,
 * X_16_8_FIXED_POINT_1_256TH 5; PA_SU_SC_MODE_CNTL: CULL_FRONT,
 * CULL_BACK, FACE) and src/amd/vulkan/radv_queue.c (the edge rule
 * 0xaaaaaaaa, the vertex rounding and the viewport enables of the
 * preamble) and src/amd/vulkan/radv_cmd_buffer.c (the clip rectangle rule
 * 0xffff).
 */
#define AMDRDNA_CX_SCISSOR_Y_SHIFT		16U
#define AMDRDNA_CX_SCISSOR_NO_WINDOW_OFFSET	(1U << 31)
#define AMDRDNA_CX_EDGE_RULE_PLAIN		0xaaaaaaaaU
#define AMDRDNA_CX_CLIP_RECTANGLE_ALL		0xffffU
#define AMDRDNA_CX_SCAN_VIEWPORT_SCISSOR	(1U << 1)
#define AMDRDNA_CX_CLIP_D3D			((1U << 19) | (1U << 24))
#define AMDRDNA_CX_VIEWPORT_ALL			(0x3fU | (1U << 10))
#define AMDRDNA_CX_VERTEX_ROUNDING_PLAIN	(1U | (2U << 1) | (5U << 3))
#define AMDRDNA_CX_RASTER_CULL_FRONT		(1U << 0)
#define AMDRDNA_CX_RASTER_CULL_BACK		(1U << 1)
#define AMDRDNA_CX_RASTER_CLOCKWISE		(1U << 2)

/*
 * The point size of 1.0 (8 in each 12.4 half: height 15:0, width 31:16),
 * the point limits 0 to 4095.9375 (12.4), the line width 1.0 (8), the
 * small primitive filter on (bit 0), the sample configuration and mask of
 * one sample, the stages of a legacy vertex program in wave32 (two
 * primitive groups a wave in 18:15, wave32 23), and the geometry engine's
 * 128 primitives and 256 vertices a group (8:0, 17:9).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (PA_SU_POINT_SIZE, PA_SU_POINT_MINMAX, PA_SU_LINE_CNTL,
 * PA_SU_SMALL_PRIM_FILTER_CNTL, VGT_SHADER_STAGES_EN: MAX_PRIMGRP_IN_WAVE,
 * VS_W32_EN, GE_CNTL: PRIM_GRP_SIZE, VERT_GRP_SIZE),
 * src/amd/vulkan/radv_queue.c (radv_emit_graphics: the point size and
 * limits) and src/amd/vulkan/radv_cmd_buffer.c (the stages' two primitive
 * groups; gfx10_emit_ge_cntl: the group sizes 128 and 256 without a
 * geometry or tessellation program).
 */
#define AMDRDNA_CX_POINT_SIZE_ONE		((8U << 16) | 8U)
#define AMDRDNA_CX_POINT_LIMITS_ALL		(0xffffU << 16)
#define AMDRDNA_CX_LINE_WIDTH_ONE		8U
#define AMDRDNA_CX_SMALL_PRIMITIVE_FILTER_ON	1U
#define AMDRDNA_CX_SAMPLE_MASK_ALL		0xffffffffU
#define AMDRDNA_CX_STAGES_LEGACY_VERTEX		((2U << 15) | (1U << 23))
#define AMDRDNA_UC_GEOMETRY_GROUPS		(128U | (256U << 9))

/*
 * The binner of GFX10 off: binning disabled with the new scan converter
 * (2 in 1:0), bins of 128 by 128 (2 in 6:4 and 9:7), no start of
 * primitive (18) and a flush on a binning transition (28).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (PA_SC_BINNER_CNTL_0: BINNING_MODE, BIN_SIZE_X_EXTEND, BIN_SIZE_Y_EXTEND,
 * DISABLE_START_OF_PRIM, FLUSH_ON_BINNING_TRANSITION; the enums
 * BinningMode: DISABLE_BINNING_USE_NEW_SC 2, BinSizeExtend:
 * BIN_SIZE_128_PIXELS 2) and src/amd/vulkan/radv_cmd_buffer.c
 * (radv_get_disabled_binning_state, the GFX10 form for targets of four
 * bytes a pixel).
 */
#define AMDRDNA_CX_BINNER_OFF			(2U | (2U << 4) | (2U << 7) | (1U << 18) | (1U << 28))

/*
 * The primitive types: a triangle list (4) and strip (6), a point list
 * (1), a line list (2) and strip (3).
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json (the enum
 * VGT_DI_PRIM_TYPE: DI_PT_POINTLIST 1, DI_PT_LINELIST 2, DI_PT_LINESTRIP 3,
 * DI_PT_TRILIST 4, DI_PT_TRISTRIP 6).
 */
#define AMDRDNA_PRIMITIVE_POINTS		1U
#define AMDRDNA_PRIMITIVE_LINES			2U
#define AMDRDNA_PRIMITIVE_LINE_STRIP		3U
#define AMDRDNA_PRIMITIVE_TRIANGLES		4U
#define AMDRDNA_PRIMITIVE_TRIANGLE_STRIP	6U

/*
 * An indirect buffer of packets being written: its words, how many are
 * written, and its capacity.  A packet that would pass the end is not
 * written and full is set; the writer is then used no more.
 */
struct amdrdna_vk_pm4 {
	uint32_t *words;
	uint32_t count;
	uint32_t capacity;
	bool full;
};

void amdrdna_vk_pm4_start(struct amdrdna_vk_pm4 *pm4, uint32_t *words, uint32_t capacity);
void amdrdna_vk_pm4_packet(struct amdrdna_vk_pm4 *pm4, uint32_t operation, uint32_t body_words, bool compute);
void amdrdna_vk_pm4_word(struct amdrdna_vk_pm4 *pm4, uint32_t word);
void amdrdna_vk_pm4_context_start(struct amdrdna_vk_pm4 *pm4);
void amdrdna_vk_pm4_set_context(struct amdrdna_vk_pm4 *pm4, uint32_t reg, uint32_t value);
void amdrdna_vk_pm4_set_context_run(struct amdrdna_vk_pm4 *pm4, uint32_t reg, const uint32_t *values, uint32_t count);
void amdrdna_vk_pm4_set_shader(struct amdrdna_vk_pm4 *pm4, uint32_t reg, uint32_t value);
void amdrdna_vk_pm4_set_shader_run(struct amdrdna_vk_pm4 *pm4, uint32_t reg, const uint32_t *values, uint32_t count);
void amdrdna_vk_pm4_set_user_config(struct amdrdna_vk_pm4 *pm4, uint32_t reg, uint32_t value);
void amdrdna_vk_pm4_set_user_config_indexed(struct amdrdna_vk_pm4 *pm4, uint32_t reg, uint32_t index, uint32_t value);
void amdrdna_vk_pm4_event(struct amdrdna_vk_pm4 *pm4, uint32_t event, uint32_t index);
void amdrdna_vk_pm4_event_fence(struct amdrdna_vk_pm4 *pm4, uint32_t event, uint32_t caches, uint64_t address, uint32_t value);
void amdrdna_vk_pm4_wait_value(struct amdrdna_vk_pm4 *pm4, uint64_t address, uint32_t value);
void amdrdna_vk_pm4_cache_acquire(struct amdrdna_vk_pm4 *pm4, uint32_t caches);
void amdrdna_vk_pm4_fetcher_sync(struct amdrdna_vk_pm4 *pm4);
void amdrdna_vk_pm4_draw(struct amdrdna_vk_pm4 *pm4, uint32_t vertex_count, uint32_t instance_count);
void amdrdna_vk_pm4_dispatch(struct amdrdna_vk_pm4 *pm4, uint32_t x, uint32_t y, uint32_t z);
void amdrdna_vk_pm4_pad(struct amdrdna_vk_pm4 *pm4, uint32_t multiple);

#endif /* DRIVERS_GPU_AMDRDNA_VK_AMDRDNA_VK_GFX10_H */
