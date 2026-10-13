/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright (C) 2016 Intel Corporation
 * Copyright (c) 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * The Gen9 (Skylake, Kaby Lake, Coffee Lake, Comet Lake) forms of the 3D and
 * compute state where they differ from the Gen12 ones of genxml.h: packet
 * lengths, field positions, the URB packets, the MOCS index, thread limits,
 * the L3 partition, and the instructions of a thread that only ends itself.
 * Everything not here is the same on both and is taken from genxml.h.
 *
 * zedBSD — transcribed hardware definitions (values only)
 *
 * Source: Mesa 25.0.7 (Debian source package 25.0.7-2+deb13u1; MIT; values
 * are Intel PRM hardware facts).  SHA-256 of the files:
 *   src/intel/genxml/gen90.xml
 *     d86fb566b9292280e2d6a385107711fb7ee8008e5c54bb3ba70a2c0343262ddb
 *   src/intel/genxml/gen80.xml (imported by gen90.xml)
 *     2962677cf69dc947345fd88bd7010427900160eb7a7b076463e6e8d28772439d
 *   src/intel/dev/intel_device_info.c (GFX9_HW_INFO, the GT1 URB limits)
 *     1a3c7c6d87c4a60af5d1003096b4add0a34c4a02afc12b40a378cc060540afd8
 *   src/intel/common/intel_l3_config.c (chv_l3_configs, the SKL table)
 *     dc3dfa83159ffd5a5a56f9b03402b5f5b11336f9e1216e4b692b5f044f901a01
 *   src/intel/vulkan/genX_cmd_buffer.c (the PIPELINE_SELECT mask and the
 *   Gen9 workarounds around it)
 *     b4a05c278da5ca5cfc0efd9c752d5747107b3a2d471041ea17d45ba83c4b14d0
 *   src/intel/vulkan/genX_pipeline.c (MEDIA_VFE_STATE on Gen9)
 *     9bf244df1284531767529980909176d09b72db037b6fdaa7f5874d997ebf7545
 *   src/intel/isl/isl_emit_depth_stencil.c (the Gen9 depth and stencil
 *   buffers)
 *     d0a71883586e838971621169f7439a9d1ebddb919241cf5937620e5742a56778
 * The MOCS table index is Linux 6.8.12 drivers/gpu/drm/i915/gt/intel_mocs.c
 * (skl_mocs_table, MIT)
 *     49f2b6a2a47bcf26bf57eef51d5a125458378298602462b8ff9986a54a15ffe7
 * and the default of L3CNTLREG is the Skylake PRM (IHD-OS-SKL-Vol 7-05.16,
 * "L3CNTLREG - L3 Control Register"); CACHE_MODE_1 is anv's
 * (src/intel/vulkan/genX_init_state.c
 *     716543ab781499a3079174681a90943e9a28b2c1658c2f12dbe4f15df2bead4b).  The end-of-thread instructions were
 * assembled by Mesa 25.0.7's brw_asm for Skylake.  The assembly logic is new.
 */

#ifndef DRIVERS_GPU_I915_INTEL_GENXML_GEN9_H
#define DRIVERS_GPU_I915_INTEL_GENXML_GEN9_H

/* The graphics version a batch carries to select these forms. */
#define GEN9_GRAPHICS_VER			9U

/*
 * PIPELINE_SELECT: anv masks in only the selection bits (1:0) on Gen9 and
 * sets no media sampler DOP clock gate.
 */
#define GEN9_PIPELINE_SELECT_MASK		(0x3U << 8)

/* STATE_BASE_ADDRESS has no bindless sampler state base on Gen9: 19 dwords. */
#define GEN9_STATE_BASE_ADDRESS_DWORDS		19U

/*
 * The MOCS index of uncached memory: entry 0 of the Gen9 table
 * (skl_mocs_table, I915_MOCS_UNCACHED); index 2, write-back, is the same
 * entry on both versions.  A MOCS field holds the index shifted left by one.
 */
#define GEN9_MOCS_UNCACHED_INDEX		0U

/*
 * The URB of Gen9 is allocated per stage with 3DSTATE_URB_VS, _HS, _DS and
 * _GS (two dwords each): dword 1 holds the starting address in 8 KiB chunks
 * (31:25), the entry allocation size in 64-byte units less one (24:16) and
 * the number of entries (15:0).
 */
#define GEN9_CMD_3DSTATE_URB_VS			0x7830U
#define GEN9_CMD_3DSTATE_URB_HS			0x7831U
#define GEN9_CMD_3DSTATE_URB_DS			0x7832U
#define GEN9_CMD_3DSTATE_URB_GS			0x7833U
#define GEN9_3DSTATE_URB_DWORDS			2U
#define GEN9_URB_START_SHIFT			25U
#define GEN9_URB_ENTRY_SIZE_SHIFT		16U

/*
 * One allocation fits every Gen9 GT: the default L3 partition gives the URB
 * 48 ways, 192 KiB a slice on GT1 (two banks) and more on the others.  The
 * push constants take the first 32 KiB (chunks 0-3), the vertex stage the
 * next 16 chunks (128 KiB), split 10 and 6 with the geometry stage when the
 * draw has one; the hull and domain stages start after them with no entries.
 * The entries are capped at GT1's limits, the smallest of the family: 928
 * vertex entries (the GT1 vertex fetch bug) and 256 geometry entries.
 */
#define GEN9_URB_VS_START_CHUNK			4U
#define GEN9_URB_VS_CHUNKS			16U
#define GEN9_URB_VS_SPLIT_CHUNKS		10U
#define GEN9_URB_GS_CHUNKS			6U
#define GEN9_URB_UNUSED_START_CHUNK		20U
#define GEN9_URB_VS_ENTRIES			928U
#define GEN9_URB_GS_ENTRIES			256U

/*
 * 3DSTATE_VS dword 7: the Maximum Number of Threads less one is bits 31:23
 * (31:22 on Gen12), at most 336 threads (GFX9_HW_INFO max_vs_threads).
 */
#define GEN9_VS_MAX_THREADS_SHIFT		23U
#define GEN9_MAX_VS_THREADS			336U

/*
 * 3DSTATE_DEPTH_BUFFER: dword 1 the Surface Type (31:29), Depth Write
 * Enable (28), Stencil Write Enable (27), the Surface Format (20:18) and the
 * pitch less one (17:0); dword 4 the Height (31:18) and Width (17:4) less
 * one; dword 5 the MOCS (6:0), the Minimum Array Element (20:10) and the
 * Depth (31:21); dword 7 the QPitch (14:0) and the Render Target View
 * Extent (31:21).
 */
#define GEN9_DEPTH_STENCIL_WRITE_ENABLE		(1U << 27)
#define GEN9_DEPTH_FORMAT_SHIFT			18U
#define GEN9_DEPTH_WIDTH_SHIFT			4U
#define GEN9_DEPTH_HEIGHT_SHIFT			18U
#define GEN9_DEPTH_MIN_ARRAY_SHIFT		10U
#define GEN9_DEPTH_DEPTH_SHIFT			21U

/*
 * 3DSTATE_STENCIL_BUFFER is five dwords on Gen9: dword 1 Stencil Buffer
 * Enable (31), the MOCS (28:22) and the pitch less one (16:0); dwords 2-3
 * the address; dword 4 the QPitch (14:0).
 */
#define GEN9_3DSTATE_STENCIL_BUFFER_DWORDS	5U
#define GEN9_STENCIL_BUFFER_ENABLE		(1U << 31)
#define GEN9_STENCIL_MOCS_SHIFT			22U

/*
 * MEDIA_VFE_STATE dword 3 bit 7 Reset Gateway Timer, which anv sets below
 * Gen11; the Maximum Number of Threads is 56 a subslice (GFX9_HW_INFO
 * max_cs_threads) times the subslices, less one.
 */
#define GEN9_VFE_RESET_GATEWAY_TIMER		(1U << 7)
#define GEN9_VFE_THREADS_PER_SUBSLICE		56U

/*
 * L3CNTLREG (0x7034): SLM Enable (0), the URB (7:1), RO (17:11), DC (24:18)
 * and All (31:25) allocations, in ways.  Its default (SKL PRM) is the first
 * Skylake configuration Mesa validates, URB 48 and All 48 with no shared
 * local memory; a kernel with shared local memory needs the one with 32
 * ways of it, URB 16 and All 48.
 */
#define GEN9_REG_L3CNTLREG			0x7034U
#define GEN9_L3CNTLREG_DEFAULT			0x60000060U
#define GEN9_L3CNTLREG_SLM			0x60000021U

/*
 * CACHE_MODE_1 (0x7004, masked: bit n + 16 enables writing bit n), which anv
 * programs once on Gen9 (genX_init_state.c): Partial Resolve Disable In VC
 * (1), Float Blend Optimization Enable (4) and MSC RAW Hazard Avoidance (9).
 */
#define GEN9_REG_CACHE_MODE_1			0x7004U
#define GEN9_CACHE_MODE_1_VALUE \
	(((1U << 1) | (1U << 4) | (1U << 9)) << 16 | (1U << 1) | (1U << 4) | (1U << 9))

/*
 * The SKL PRM (PIPE_CONTROL) wants a null PIPE_CONTROL before one that
 * invalidates the VF cache, and a post-sync write on the invalidating one:
 * Post Sync Operation 1, Write Immediate Data (bits 15:14 of dword 1).
 */
#define GEN9_PIPE_CONTROL_WRITE_IMMEDIATE	(1U << 14)

/*
 * A thread that only ends itself on Gen9: a SIMD8 render target write of
 * g112-g115 to binding table entry 0 with end of thread, sent to the render
 * cache (SFID 5) as the 128-bit Gen9 instruction.  Mesa's disassembler
 * reads it as
 *   sendc(8) null<1>UW g112<8,8,1>UD 0x88031400
 *            render MsgDesc: RT write SIMD8 LastRT Surface = 0 mlen 4 rlen 0
 *            { align1 1Q EOT };
 */
#define GEN9_EOT_ONLY_DWORD0			0x05600032U
#define GEN9_EOT_ONLY_DWORD1			0x20000240U
#define GEN9_EOT_ONLY_DWORD2			0x068d0e00U
#define GEN9_EOT_ONLY_DWORD3			0x88031400U

#endif /* DRIVERS_GPU_I915_INTEL_GENXML_GEN9_H */
