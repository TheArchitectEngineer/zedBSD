/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright © 2006-2016 Intel Corporation
 * Copyright © 2021 Intel Corporation
 * Copyright © 2022 Intel Corporation
 * Copyright © 2023 Intel Corporation
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
 */

/*
 * The display 9 (Skylake, Kaby Lake, Coffee Lake, Comet Lake) registers
 * the Gen9 flip display (display/gen9-flip.c) reads and writes: the power
 * wells, the DC states, the clocks, the DPLL controls, the transcoders,
 * the pipes, the first universal plane, the pipe scalers, the eDP PSR
 * control and the display engine's pipe interrupts.
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * renamed (I915_SKL_*); the per-pipe and per-transcoder forms are written
 * as offsets from the pipe A or transcoder A register.  Values are
 * unchanged.  Sources (SHA-256 of the file in linux-6.8.12.tar.xz):
 *   drivers/gpu/drm/i915/i915_reg.h (lines 1319-1320, 1928-1959, 2535, 3025, 3584-3690,
 *     3975-3976, 4038-4043, 4279-4340, 5451-5462, 5574-5611, 5886-5942,
 *     6188-6196)
 *     efcb6385e32031ca3c5b97f7691c5ed1706c2a6709e3875ff78c2cf2eff0e626
 *   drivers/gpu/drm/i915/display/intel_backlight_regs.h (lines 65, 81-93)
 *     fe411d1e4987cd0cb17c45b5ddd005efab1ee5c44ca0de23a70bc03dff7c98b7
 *   drivers/gpu/drm/i915/display/intel_psr_regs.h (lines 24-27)
 *     0a207944317960d86ac7bd5ef076034bddad43c60c654746016a07a1625adf3b
 *   drivers/gpu/drm/i915/display/intel_dpll_mgr.c (lines 1824-1856, the
 *     port clock of a DPLL_CTRL1 link rate)
 *     6b339fd5ed331c7e0f35544821c64a249fabff09ababb2b6629f2f89bea02bb7
 *   drivers/gpu/drm/i915/display/intel_display_device.c (PIPE_EDP_OFFSET,
 *     TRANSCODER_EDP_OFFSET)
 *     c5f8524bf573978ccf971a32978aabc36a4965052703b71fc4cd45e7fc8edd47
 *   drivers/gpu/drm/i915/display/intel_fb.c (lines 790-793, the linear
 *     surface alignment of display 9)
 *     9255f7058c1fd775c3428582ac264fe72b4b66a80b571df3ef0c53cffa3361e4
 * i915_reg.h and intel_dpll_mgr.c carry the notice above; the others carry
 * SPDX-License-Identifier: MIT.
 */

#ifndef DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_H
#define DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_H

/* The pipes and the transcoders: A, B and C each drive their own pipe; EDP drives the pipe its input select names. */
#define I915_SKL_PIPES				3U
#define I915_SKL_TRANSCODERS			4U
#define I915_SKL_TRANSCODER_EDP			3U

/* The distance between two pipes' registers and between two transcoders' registers. */
#define I915_SKL_PIPE_STRIDE			0x1000U
#define I915_SKL_TRANSCODER_STRIDE		0x1000U

/* The power well state (HSW_PWR_WELL_CTL2, HSW_PWR_WELL_CTL_STATE of SKL_PW_CTL_IDX_PW_1 14 and _PW_2 15). */
#define I915_SKL_PWR_WELL_CTL2			0x45404U
#define I915_SKL_PW_1_STATE			(1U << 28)
#define I915_SKL_PW_2_STATE			(1U << 30)

/* The DC states the DMC may enter (DC_STATE_EN, DC_STATE_EN_UPTO_DC5 and _DC6). */
#define I915_SKL_DC_STATE_EN			0x45504U
#define I915_SKL_DC_STATE_MASK			0x3U

/* The CD clock and its PLL (CDCLK_CTL, LCPLL1_CTL). */
#define I915_SKL_CDCLK_CTL			0x46000U
#define I915_SKL_LCPLL1_CTL			0x46010U

/* The DPLL controls: six bits a DPLL in CTRL1, three bits a DDI port in CTRL2 (DPLL_CTRL1, DPLL_CTRL2). */
#define I915_SKL_DPLL_CTRL1			0x6c058U
#define I915_SKL_DPLL_CTRL1_BITS		6U
#define I915_SKL_DPLL_CTRL1_RATE_SHIFT		1U
#define I915_SKL_DPLL_CTRL1_RATE_MASK		0x7U
#define I915_SKL_DPLL_CTRL1_HDMI_SHIFT		5U
#define I915_SKL_DPLL_CTRL2			0x6c05cU
#define I915_SKL_DPLL_CTRL2_BITS		3U
#define I915_SKL_DPLL_CTRL2_SELECT_SHIFT	1U
#define I915_SKL_DPLL_CTRL2_SELECT_MASK		0x3U

/* The link rates of DPLL_CTRL1 (DPLL_CTRL1_LINK_RATE_*). */
#define I915_SKL_LINK_RATE_2700			0U
#define I915_SKL_LINK_RATE_1350			1U
#define I915_SKL_LINK_RATE_810			2U
#define I915_SKL_LINK_RATE_1620			3U
#define I915_SKL_LINK_RATE_1080			4U
#define I915_SKL_LINK_RATE_2160			5U

/* The transcoder's DDI function (TRANS_DDI_FUNC_CTL A 0x60400, EDP 0x6f400). */
#define I915_SKL_TRANS_DDI_FUNC_CTL_A		0x60400U
#define I915_SKL_TRANS_DDI_FUNC_CTL_EDP		0x6f400U
#define I915_SKL_TRANS_DDI_FUNC_ENABLE		(1U << 31)
#define I915_SKL_TRANS_DDI_PORT_SHIFT		28U
#define I915_SKL_TRANS_DDI_PORT_MASK		0x7U
#define I915_SKL_TRANS_DDI_MODE_SHIFT		24U
#define I915_SKL_TRANS_DDI_MODE_MASK		0x7U
#define I915_SKL_TRANS_DDI_MODE_DP_SST		2U
#define I915_SKL_TRANS_DDI_MODE_DP_MST		3U
#define I915_SKL_TRANS_DDI_EDP_INPUT_SHIFT	12U
#define I915_SKL_TRANS_DDI_EDP_INPUT_MASK	0x7U
#define I915_SKL_TRANS_DDI_EDP_INPUT_A_ON	0U
#define I915_SKL_TRANS_DDI_EDP_INPUT_A_ONOFF	4U
#define I915_SKL_TRANS_DDI_EDP_INPUT_B_ONOFF	5U
#define I915_SKL_TRANS_DDI_EDP_INPUT_C_ONOFF	6U

/* The transcoder configuration (TRANSCONF: A 0x70008, EDP 0x7f008), its enable and its state. */
#define I915_SKL_TRANSCONF_A			0x70008U
#define I915_SKL_TRANSCONF_EDP			0x7f008U
#define I915_SKL_TRANSCONF_ENABLE		(1U << 31)
#define I915_SKL_TRANSCONF_STATE		(1U << 30)

/* The transcoder's timing totals (TRANS_HTOTAL, TRANS_VTOTAL): total - 1 in 31:16, active - 1 in 15:0. */
#define I915_SKL_TRANS_HTOTAL_A			0x60000U
#define I915_SKL_TRANS_VTOTAL_A			0x6000cU
#define I915_SKL_TRANS_HTOTAL_EDP		0x6f000U
#define I915_SKL_TRANS_VTOTAL_EDP		0x6f00cU
#define I915_SKL_TIMING_TOTAL_SHIFT		16U
#define I915_SKL_TIMING_FIELD_MASK		0xffffU

/* The transcoder's DP link M and N (PIPE_LINK_M1, _N1), 23:0. */
#define I915_SKL_LINK_M1_A			0x60040U
#define I915_SKL_LINK_N1_A			0x60044U
#define I915_SKL_LINK_M1_EDP			0x6f040U
#define I915_SKL_LINK_N1_EDP			0x6f044U
#define I915_SKL_LINK_MN_MASK			0x00ffffffU

/* The eDP transcoder's PSR control (EDP_PSR_CTL of TRANSCODER_EDP) and its enable. */
#define I915_SKL_EDP_PSR_CTL			0x6f800U
#define I915_SKL_EDP_PSR_ENABLE			(1U << 31)

/* The frame buffer compression control of the first compressor (ILK_DPFC_CONTROL) and its enable. */
#define I915_SKL_DPFC_CONTROL			0x43208U
#define I915_SKL_DPFC_CTL_EN			(1U << 31)

/* The pipe's source size (PIPESRC): width - 1 in 31:16, height - 1 in 15:0. */
#define I915_SKL_PIPESRC_A			0x6001cU

/* The pipe's frame counter (PIPE_FRMCOUNT_G4X), which moves at every vblank start. */
#define I915_SKL_PIPE_FRMCOUNT_A		0x70040U

/* The first universal plane of pipe A (PLANE_*_1_A); another pipe's is I915_SKL_PIPE_STRIDE further. */
#define I915_SKL_PLANE_CTL_1_A			0x70180U
#define I915_SKL_PLANE_STRIDE_1_A		0x70188U
#define I915_SKL_PLANE_POS_1_A			0x7018cU
#define I915_SKL_PLANE_SIZE_1_A			0x70190U
#define I915_SKL_PLANE_SURF_1_A			0x7019cU
#define I915_SKL_PLANE_OFFSET_1_A		0x701a4U
#define I915_SKL_PLANE_SURFLIVE_1_A		0x701acU

/* The PLANE_CTL fields (pre-ICL). */
#define I915_SKL_PLANE_CTL_ENABLE		(1U << 31)
#define I915_SKL_PLANE_CTL_FORMAT_SHIFT		24U
#define I915_SKL_PLANE_CTL_FORMAT_MASK		0xfU
#define I915_SKL_PLANE_CTL_FORMAT_XRGB_8888	4U
#define I915_SKL_PLANE_CTL_ORDER_RGBX		(1U << 20)
#define I915_SKL_PLANE_CTL_TILED_SHIFT		10U
#define I915_SKL_PLANE_CTL_TILED_MASK		0x7U
#define I915_SKL_PLANE_CTL_TILED_LINEAR		0U
#define I915_SKL_PLANE_CTL_ASYNC_FLIP		(1U << 9)
#define I915_SKL_PLANE_CTL_ROTATE_MASK		0x3U

/* The PLANE_STRIDE of a linear surface counts 64-byte units in 11:0. */
#define I915_SKL_PLANE_STRIDE_MASK		0xfffU
#define I915_SKL_PLANE_STRIDE_UNIT		64U

/* The PLANE_SIZE fields: width - 1 in 15:0, height - 1 in 31:16. */
#define I915_SKL_PLANE_SIZE_HEIGHT_SHIFT	16U
#define I915_SKL_PLANE_SIZE_FIELD_MASK		0xffffU

/* The surface address bits of PLANE_SURF and PLANE_SURFLIVE, and a linear surface's alignment (256 KiB). */
#define I915_SKL_PLANE_SURF_ADDR_MASK		0xfffff000U
#define I915_SKL_LINEAR_ALIGNMENT		(256U * 1024U)

/* The two scalers of pipe A (PS_CTRL of scaler 1 and 2); another pipe's are 0x800 further. */
#define I915_SKL_PS_1A_CTRL			0x68180U
#define I915_SKL_PS_2A_CTRL			0x68280U
#define I915_SKL_PS_PIPE_STRIDE			0x800U
#define I915_SKL_PS_SCALER_EN			(1U << 31)

/* The display engine pipe interrupt bit of GEN8_MASTER_IRQ (GEN8_DE_PIPE_A_IRQ << pipe). */
#define I915_SKL_MASTER_DE_PIPE_A		(1U << 16)

/* The pipe interrupt registers (GEN8_DE_PIPE_ISR, _IMR, _IIR, _IER), 0x10 apart from pipe to pipe. */
#define I915_SKL_DE_PIPE_IMR_A			0x44404U
#define I915_SKL_DE_PIPE_IIR_A			0x44408U
#define I915_SKL_DE_PIPE_IER_A			0x4440cU
#define I915_SKL_DE_PIPE_IRQ_STRIDE		0x10U

/* The first plane's flip done (GEN9_PIPE_PLANE1_FLIP_DONE). */
#define I915_SKL_DE_PIPE_PLANE1_FLIP_DONE	(1U << 3)

/* The first plane's fault: the display engine could not read its surface (GEN9_PIPE_PLANE1_FAULT). */
#define I915_SKL_DE_PIPE_PLANE1_FAULT		(1U << 7)

/*
 * The south display's backlight PWM.  A Sunrise Point or Kaby Point PCH has
 * the LPT layout: CTL1 holds the enable and the polarity, CTL2 the period
 * (31:16) and the duty (15:0).  A Cannon Point or Comet Point PCH has the
 * Broxton layout with one controller: CTL1, then the period and the duty
 * each in a register of its own (BLC_PWM_PCH_CTL1, _CTL2, _BXT_BLC_PWM_*1).
 */
#define I915_SKL_BLC_PWM_PCH_CTL1		0xc8250U
#define I915_SKL_BLC_PWM_PCH_CTL2		0xc8254U
#define I915_SKL_BLM_PCH_PWM_ENABLE		(1U << 31)
#define I915_SKL_BLM_PCH_POLARITY		(1U << 29)
#define I915_SKL_BLC_PWM_PERIOD_SHIFT		16U
#define I915_SKL_BLC_PWM_DUTY_MASK		0xffffU
#define I915_CNP_BLC_PWM_CTL1			0xc8250U
#define I915_CNP_BLC_PWM_FREQ1			0xc8254U
#define I915_CNP_BLC_PWM_DUTY1			0xc8258U

#endif /* DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_H */
