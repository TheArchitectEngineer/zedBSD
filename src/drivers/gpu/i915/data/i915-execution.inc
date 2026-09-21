/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014 Intel Corporation
 * Copyright © 2019 Intel Corporation
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
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12, the
 * version the execution path was ported from and tested against.  Values are
 * unchanged.  Sources:
 *   drivers/gpu/drm/i915/gt/intel_gpu_commands.h
 *   drivers/gpu/drm/i915/gt/intel_engine_types.h
 *   drivers/gpu/drm/i915/gt/intel_engine.h
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *   drivers/gpu/drm/i915/gt/intel_lrc.h
 *   drivers/gpu/drm/i915/gt/intel_lrc.c
 *   drivers/gpu/drm/i915/gt/intel_lrc_reg.h
 *   drivers/gpu/drm/i915/pxp/intel_pxp_regs.h
 * Rewrites: _MMIO(x) -> (x); REG_BIT/BIT -> I915_INC_BIT; REG_GENMASK ->
 * I915_INC_GENMASK; PAGE_SIZE -> I915_INC_PAGE_SIZE; sizeof(u32)/sizeof(u64) ->
 * 4U/8U; integer shift bases gain a U suffix.
 * Only what the execution path uses and data/i915-commands.inc and
 * data/i915-regs.inc do not already hold is here; those two are included
 * first so this fragment can build on their definitions.
 */

#ifndef DRIVERS_GPU_I915_DATA_I915_EXECUTION_INC
#define DRIVERS_GPU_I915_DATA_I915_EXECUTION_INC

#include "i915-commands.inc"
#include "i915-regs.inc"

/* drivers/gpu/drm/i915/gt/intel_gpu_commands.h */
#define MI_SET_PREDICATE	MI_INSTR(0x01, 0)
#define MI_SET_PREDICATE_DISABLE	(0U << 0)
#define MI_SEMAPHORE_REGISTER_POLL	(1U << 16)
#define MI_LOAD_REGISTER_MEM_GEN8	MI_INSTR(0x29, 2)
#define MI_LOAD_REGISTER_REG	MI_INSTR(0x2A, 1)
#define MI_LRR_SOURCE_CS_MMIO	I915_INC_BIT(18)
#define MI_FLUSH_DW_CCS	(1U << 16)
#define MI_INVALIDATE_BSD	(1U << 7)

/* drivers/gpu/drm/i915/gt/intel_engine_types.h */
#define EMIT_INVALIDATE	I915_INC_BIT(0)
#define EMIT_FLUSH	I915_INC_BIT(1)
#define EMIT_BARRIER	(EMIT_INVALIDATE | EMIT_FLUSH)

/* drivers/gpu/drm/i915/gt/intel_engine.h */
#define I915_GEM_HWS_PREEMPT_ADDR	(I915_GEM_HWS_PREEMPT * 4U)
#define I915_GEM_HWS_MIGRATE	(0x42 * 4U)
#define I915_GEM_HWS_PXP	0x60
#define I915_GEM_HWS_PXP_ADDR	(I915_GEM_HWS_PXP * 4U)

/* drivers/gpu/drm/i915/gt/intel_engine_regs.h */
#define RING_CMD_BUF_CCTL(base)	((base) + 0x84)
#define RING_CTX_TIMESTAMP(base)	((base) + 0x3a8) /* gen8+ */
#define GEN8_RING_CS_GPR(base, n)	((base) + 0x600 + (n) * 8)

/* drivers/gpu/drm/i915/gt/intel_gt_regs.h */
#define GEN12_CS_DEBUG_MODE2	(0x20d8)
#define INSTRUCTION_STATE_CACHE_INVALIDATE	I915_INC_BIT(6)
#define GEN12_CCS_AUX_INV	(0x4208)
#define GEN12_VD0_AUX_INV	(0x4218)
#define GEN12_VE0_AUX_INV	(0x4238)
#define GEN12_BCS0_AUX_INV	(0x4248)
#define GEN12_VD2_AUX_INV	(0x4298)
#define AUX_INV	I915_INC_BIT(0)
#define MSG_IDLE_CS	(0x8000)
#define MSG_IDLE_VCS0	(0x8004)
#define MSG_IDLE_BCS	(0x800C)
#define MSG_IDLE_VECS0	(0x8010)
#define MSG_IDLE_VCS2	(0x80C0)
#define MSG_IDLE_FW_MASK	I915_INC_GENMASK(13, 9)
#define MSG_IDLE_FW_SHIFT	9
#define GEN9_PWRGT_DOMAIN_STATUS	(0xA2A0)
#define GEN8_RPCS_ENABLE	(1U << 31)
#define GEN8_RPCS_S_CNT_ENABLE	(1U << 18)
#define GEN11_RPCS_S_CNT_SHIFT	12
#define GEN11_RPCS_S_CNT_MASK	(0x3fU << GEN11_RPCS_S_CNT_SHIFT)

/* drivers/gpu/drm/i915/gt/intel_lrc.h (enum value) */
#define INTEL_LEGACY_64B_CONTEXT	3

/* drivers/gpu/drm/i915/gt/intel_lrc.c */
#define DG2_PREDICATE_RESULT_WA	(I915_INC_PAGE_SIZE - 8U)
#define DG2_PREDICATE_RESULT_BB	(2048)

/* drivers/gpu/drm/i915/gt/intel_lrc_reg.h */
#define GEN12_CTX_RCS_INDIRECT_CTX_OFFSET_DEFAULT	0xD

/* drivers/gpu/drm/i915/pxp/intel_pxp_regs.h */
#define GEN12_KCR_BASE	0x32000

#endif
