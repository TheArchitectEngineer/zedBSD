/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
 * Copyright © 2015 Intel Corporation
 * Copyright © 2020 Intel Corporation
 * Copyright © 2022 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * reformatted; values are unchanged.  Sources:
 *   drivers/gpu/drm/i915/gt/intel_workarounds.c
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h
 *   drivers/gpu/drm/i915/gt/intel_mocs.c
 *   drivers/gpu/drm/i915/gt/intel_gtt.h
 * Rewrites: _MMIO(x) -> (x); REG_BIT/REG_GENMASK/REG_FIELD_PREP are expanded
 * to their constant values; integer constants gain a U suffix.
 * Only the registers and fields the Alder Lake-P workaround, whitelist, MOCS
 * and PAT programming uses remain.
 */

#ifndef DRIVERS_GPU_I915_LINUX_I915_GT_WORKAROUNDS_INC
#define DRIVERS_GPU_I915_LINUX_I915_GT_WORKAROUNDS_INC

/* Multicast register steering (icl_wa_init_mcr). */
#define GEN8_MCR_SELECTOR			0x0fdcU
#define GEN11_MCR_SLICE_MASK			0x78000000U
#define GEN11_MCR_SUBSLICE_MASK			0x07000000U
#define GEN11_MCR_SLICE(slice)			((((uint32_t)(slice)) & 0xfU) << 27)
#define GEN11_MCR_SUBSLICE(subslice)		((((uint32_t)(subslice)) & 0x7U) << 24)

/* gen12_gt_workarounds_init: Wa_14011060649. */
#define VDBOX_CGCTL3F10(base)			((base) + 0x3f10U)
#define IECPUNIT_CLKGATE_DIS			(1U << 22)

/* gen12_gt_workarounds_init: Wa_14011059788 (a multicast register). */
#define GEN10_DFR_RATIO_EN_AND_CHICKEN		0x9550U
#define DFR_DISABLE				(1U << 9)

/* gen12_gt_workarounds_init: Wa_14015795083. */
#define GEN7_MISCCPCTL				0x9424U
#define GEN12_DOP_CLOCK_GATE_RENDER_ENABLE	(1U << 1)

/* rcs_engine_wa_init. */
#define GEN9_CS_DEBUG_MODE1			0x20ecU
#define FF_DOP_CLOCK_GATE_DISABLE		(1U << 1)
#define GEN8_ROW_CHICKEN2			0xe4f4U
#define GEN12_DISABLE_EARLY_READ		(1U << 14)
#define GEN12_PUSH_CONST_DEREF_HOLD_DIS		(1U << 8)
#define GEN7_FF_THREAD_MODE			0x20a0U
#define GEN12_FF_TESSELATION_DOP_GATE_DISABLE	(1U << 19)
#define GEN10_SAMPLER_MODE			0xe18cU
#define ENABLE_SMALLPL				(1U << 15)
#define GEN9_ROW_CHICKEN4			0xe48cU
#define GEN12_DISABLE_TDL_PUSH			(1U << 9)
#define RING_PSMI_CTL(base)			((base) + 0x50U)
#define GEN8_RC_SEMA_IDLE_MSG_DISABLE		(1U << 12)
#define GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE	(1U << 7)
#define GEN7_FF_SLICE_CS_CHICKEN1		0x20e0U
#define GEN9_FFSC_PERCTX_PREEMPT_CTRL		(1U << 14)

/* gen12_ctx_workarounds_init. */
#define GEN11_COMMON_SLICE_CHICKEN3		0x7304U
#define GEN12_DISABLE_CPS_AWARE_COLOR_PIPE	(1U << 9)
#define GEN8_CS_CHICKEN1			0x2580U
#define GEN9_PREEMPT_GPGPU_LEVEL_MASK		0x6U
#define GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL	0x2U
#define GEN12_FF_MODE2				0x6604U
#define FF_MODE2_GS_TIMER_224			(224U << 24)
#define FF_MODE2_TDS_TIMER_128			(4U << 16)
#define HIZ_CHICKEN				0x7018U
#define HZ_DEPTH_TEST_LE_GE_OPT_DISABLE		(1U << 13)
#define COMMON_SLICE_CHICKEN4			0x7300U
#define DISABLE_TDC_LOAD_BALANCING_CALC		(1U << 6)

/*
 * engine_fake_wa_init: the command streamer's default MOCS.  Both fields
 * take a MOCS value, which is the table index shifted left by one.
 */
#define RING_CMD_CCTL(base)			((base) + 0xc4U)
#define CMD_CCTL_MOCS_MASK			0x00003fffU
#define CMD_CCTL_MOCS_OVERRIDE(write, read) \
	(((((uint32_t)(write) << 1) << 7) & 0x3f80U) | (((uint32_t)(read) << 1) & 0x7fU))

/* gen12_ctx_gt_mocs_init: the copy engine's source and destination MOCS. */
#ifndef BLIT_CCTL
#define BLIT_CCTL(base)				((base) + 0x204U)
#endif
#define BLIT_CCTL_MASK				0x00007f7fU
#define BLIT_CCTL_MOCS(destination, source) \
	(((((uint32_t)(destination) << 1) << 8) & 0x7f00U) | (((uint32_t)(source) << 1) & 0x7fU))

/* tgl_whitelist_build and allow_read_ctx_timestamp. */
#define PS_INVOCATION_COUNT			0x2348U
#define GEN7_COMMON_SLICE_CHICKEN1		0x7010U
#define RING_FORCE_TO_NONPRIV_ACCESS_RD		(1U << 28)
#define RING_FORCE_TO_NONPRIV_RANGE_4		(1U << 0)
#ifndef RING_CTX_TIMESTAMP
#define RING_CTX_TIMESTAMP(base)		((base) + 0x3a8U)
#endif

/* intel_engine_apply_whitelist: the per-engine non-privileged slots. */
#define RING_FORCE_TO_NONPRIV(base, slot)	((base) + 0x4d0U + (unsigned)(slot) * 4U)
#define RING_NOPID(base)			((base) + 0x94U)
#define RING_MAX_NONPRIV_SLOTS			12U

/* intel_mocs.c: the global MOCS table and the L3 cache control pairs. */
#ifndef GEN12_GLOBAL_MOCS
#define GEN12_GLOBAL_MOCS(index)		(0x4000U + (unsigned)(index) * 4U)
#endif
#ifndef GEN9_LNCFCMOCS
#define GEN9_LNCFCMOCS(index)			(0xb020U + (unsigned)(index) * 4U)
#endif

/* intel_mocs.c: one MOCS control value (XXX_MOCS_0 - XXX_MOCS_63). */
#define _LE_CACHEABILITY(value)			((uint32_t)(value) << 0)
#define _LE_TGT_CACHE(value)			((uint32_t)(value) << 2)
#define LE_LRUM(value)				((uint32_t)(value) << 4)
#define LE_AOM(value)				((uint32_t)(value) << 6)
#define LE_RSC(value)				((uint32_t)(value) << 7)
#define LE_SCC(value)				((uint32_t)(value) << 8)
#define LE_SCF(value)				((uint32_t)(value) << 14)
#define LE_SSE(value)				((uint32_t)(value) << 17)
#define LE_1_UC					_LE_CACHEABILITY(1)
#define LE_3_WB					_LE_CACHEABILITY(3)
#define LE_TC_1_LLC				_LE_TGT_CACHE(1)

/* intel_mocs.c: one L3 cache control half-word (LNCFMOCS0 - LNCFMOCS31). */
#define _L3_CACHEABILITY(value)			((uint16_t)((value) << 4))
#define L3_1_UC					_L3_CACHEABILITY(1)
#define L3_3_WB					_L3_CACHEABILITY(3)

/* intel_mocs.c: GEN9_NUM_MOCS_ENTRIES, and the Gen12 table's special indices. */
#define GEN9_NUM_MOCS_ENTRIES			64U
#define GEN12_MOCS_UC_INDEX			3U
#define GEN12_MOCS_UNUSED_ENTRIES_INDEX		2U

/* intel_gtt.h: the Gen12 private PAT and its memory types. */
#define GEN12_PAT_INDEX(index)			(0x4800U + (unsigned)(index) * 4U)
#define GEN8_PPAT_WB				(3U << 0)
#define GEN8_PPAT_WT				(2U << 0)
#define GEN8_PPAT_WC				(1U << 0)
#define GEN8_PPAT_UC				(0U << 0)

#endif
