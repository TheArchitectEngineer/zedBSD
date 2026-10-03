/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 * Copyright © 2023 Intel Corporation
 */

/*
 * Copyright © 2006-2019 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright © 2014 Intel Corporation
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
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
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
 * The display register and helper macros of the Linux display headers
 * that the modeset environment reads: the pipe and PLL offset helpers, the
 * display capability macros, the pipe and port names, the i915_reg.h
 * display registers (clock gating, data island packets and others), the
 * combo PHY, stream splitter, CX0 PHY, pipe color, backlight, DMC and
 * watermark registers, and the crtc mode flags.  Each macro is defined once:
 * a later extract's copy of a macro an earlier one already has (the same
 * text) is left out.
 *
 * The pipe and PLL offset helpers (Linux intel_display_reg_defs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_reg_defs.h
 * (sha256 a33d6ec765231b345dff39754227988263740254e06ad3e158f52876d81745d6) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The display capability macros (Linux intel_display_device.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_device.h
 * (sha256 cb7d717bb48eacf17756cb666dad7cf0e006611dcbb6358a0f33f2fed23e1e9f) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The pipe and port names and for_each_pipe() (Linux intel_display.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display.h
 * (sha256 dde8fd52907f640d35a5a490a85103a9430f1144d2a3b78c6a24041c884996e0) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The display registers of Linux i915_reg.h.
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The combo PHY registers (Linux intel_combo_phy_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_combo_phy_regs.h
 * (sha256 b5f41421ca2590dd705ace3198f8db19ce60768a2a76cba58feb43930601ecff) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The display stream splitter registers (Linux intel_vdsc_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_vdsc_regs.h
 * (sha256 a104c2392247301738c6a340bf67127c3bfd2f23d6227e760607a11b025f231d) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The CX0 PHY port buffer registers (Linux intel_cx0_phy_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h
 * (sha256 f81421088d8c61c7eb760177ae5752458985389a1a4c4a2f7b9e1026c7d934f7) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The crtc mode flags (Linux intel_display_types.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_types.h
 * (sha256 647773fcee0c549791a50b9c6a4cde175c11005497689e9d6d4bbd2db8d38db4) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The HDMI data island packet bits (Linux i915_reg.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The pipe gamma and CSC registers (Linux intel_color_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_color_regs.h
 * (sha256 f4b148d849385b5bf42b5f99d69b29be7fc55e248d3f42e40c8a7e4934a56c53) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The backlight PWM registers (Linux intel_backlight_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_backlight_regs.h
 * (sha256 fe411d1e4987cd0cb17c45b5ddd005efab1ee5c44ca0de23a70bc03dff7c98b7) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The pipe DMC registers (Linux intel_dmc_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dmc_regs.h
 * (sha256 5b4518fce358d21fbb78543da94c5dc440913470b4359e5c411cf63dcaed1184) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * PIPE_TO_DMC_ID() (Linux intel_dmc.c).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dmc.c
 * (sha256 58f778803f7b9dbff6a494183cc8c744e94096c31cd6a5aaa0790c301c42641d) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The watermark, DDB and MBUS registers (Linux skl_watermark_regs.h).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/skl_watermark_regs.h
 * (sha256 5e4dba2b538c68ea5139e1cee3b25ea9e5b6f0de14b2512e1924e5b773e3f058) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_MREG_H
#define DRIVERS_GPU_I915_INTEL_MREG_H

/* The pipe and PLL offset helpers (Linux intel_display_reg_defs.h). */

#define _PIPE(pipe, a, b)		_PICK_EVEN(pipe, a, b)
#define _PLL(pll, a, b)			_PICK_EVEN(pll, a, b)
#define _MMIO_PIPE(pipe, a, b)		_MMIO(_PIPE(pipe, a, b))
#define _MMIO_PLL(pll, a, b)		_MMIO(_PLL(pll, a, b))

/* The display capability macros (Linux intel_display_device.h). */

#define HAS_DDI(i915)			(DISPLAY_INFO(i915)->has_ddi)
#define HAS_HW_SAGV_WM(i915)		(DISPLAY_VER(i915) >= 13 && !IS_DGFX(i915))
#define HAS_MBUS_JOINING(i915)		(IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_MSO(i915)			(DISPLAY_VER(i915) >= 12)

/* The pipe and port names and for_each_pipe() (Linux intel_display.h). */

#define pipe_name(p) ((p) + 'A')
#define port_name(p) ((p) + 'A')
#define for_each_pipe(__dev_priv, __p) \
	for ((__p) = 0; (__p) < I915_MAX_PIPES; (__p)++) \
		for_each_if(DISPLAY_RUNTIME_INFO(__dev_priv)->pipe_mask & BIT(__p))

/* The display registers of Linux i915_reg.h. */

#define GEN9_CLKGATE_DIS_0		_MMIO(0x46530)
#define   DARBF_GATING_DIS		REG_BIT(27)
#define   VIDEO_DIP_ENABLE_DRM_GLK	(1 << 28)
#define   VDIP_ENABLE_PPS		(1 << 24)
#define   VIDEO_DIP_ENABLE_VSC_HSW	(1 << 20)
#define   VIDEO_DIP_ENABLE_GCP_HSW	(1 << 16)
#define   VIDEO_DIP_ENABLE_AVI_HSW	(1 << 12)
#define   VIDEO_DIP_ENABLE_VS_HSW	(1 << 8)
#define   VIDEO_DIP_ENABLE_GMP_HSW	(1 << 4)
#define   VIDEO_DIP_ENABLE_SPD_HSW	(1 << 0)
#define _PIPEADSL		0x70000
#define   PIPEDSL_LINE_MASK	REG_GENMASK(19, 0)
#define PIPEDSL(pipe)		_MMIO_PIPE2(pipe, _PIPEADSL)
#define _PIPE_ARB_CTL_A			0x70028 /* icl+ */
#define PIPE_ARB_CTL(pipe)		_MMIO_PIPE2(pipe, _PIPE_ARB_CTL_A)
#define   PIPE_ARB_USE_PROG_SLOTS	REG_BIT(13)
#define _PIPE_MISC_A			0x70030
#define _PIPE_MISC_B			0x71030
#define   PIPE_MISC_YUV420_ENABLE		REG_BIT(27) /* glk+ */
#define   PIPE_MISC_YUV420_MODE_FULL_BLEND	REG_BIT(26) /* glk+ */
#define   PIPE_MISC_HDR_MODE_PRECISION		REG_BIT(23) /* icl+ */
#define   PIPE_MISC_PSR_MASK_SPRITE_ENABLE	REG_BIT(22) /* bdw */
#define   PIPE_MISC_OUTPUT_COLORSPACE_YUV	REG_BIT(11)
#define   PIPE_MISC_PIXEL_ROUNDING_TRUNC	REG_BIT(8) /* tgl+ */
#define   PIPE_MISC_BPC_MASK			REG_GENMASK(7, 5)
#define   PIPE_MISC_BPC_8			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 0)
#define   PIPE_MISC_BPC_10			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 1)
#define   PIPE_MISC_BPC_6			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 2)
#define   PIPE_MISC_BPC_12_ADLP			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 4) /* adlp+ */
#define   PIPE_MISC_DITHER_ENABLE		REG_BIT(4)
#define   PIPE_MISC_DITHER_TYPE_MASK		REG_GENMASK(3, 2)
#define   PIPE_MISC_DITHER_TYPE_SP		REG_FIELD_PREP(PIPE_MISC_DITHER_TYPE_MASK, 0)
#define PIPE_MISC(pipe)			_MMIO_PIPE(pipe, _PIPE_MISC_A, _PIPE_MISC_B)
#define _ICL_PIPE_A_STATUS			0x70058
#define ICL_PIPESTATUS(pipe)			_MMIO_PIPE2(pipe, _ICL_PIPE_A_STATUS)
#define   PIPE_STATUS_UNDERRUN				REG_BIT(31)
#define   PIPE_STATUS_SOFT_UNDERRUN_XELPD		REG_BIT(28)
#define   PIPE_STATUS_HARD_UNDERRUN_XELPD		REG_BIT(27)
#define   PIPE_STATUS_PORT_UNDERRUN_XELPD		REG_BIT(26)
#define _PIPEA_FRMCOUNT_G4X	0x70040
#define PIPE_FRMCOUNT_G4X(pipe) _MMIO_PIPE2(pipe, _PIPEA_FRMCOUNT_G4X)
#define GEN8_DE_PIPE_ISR(pipe) _MMIO(0x44400 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IMR(pipe) _MMIO(0x44404 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IIR(pipe) _MMIO(0x44408 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IER(pipe) _MMIO(0x4440c + (0x10 * (pipe)))
#define  GEN8_PIPE_FIFO_UNDERRUN	(1 << 31)
#define  XELPD_PIPE_SOFT_UNDERRUN	(1 << 22)
#define  XELPD_PIPE_HARD_UNDERRUN	(1 << 21)
#define  GEN8_PIPE_VBLANK		(1 << 0)
#define CHICKEN_PAR1_1		_MMIO(0x42080)
#define   KBL_ARB_FILL_SPARE_22		REG_BIT(22)
#define   FORCE_ARB_IDLE_PLANES		REG_BIT(14)
#define CHICKEN_MISC_2		_MMIO(0x42084)
#define   KBL_ARB_FILL_SPARE_14		REG_BIT(14)
#define   KBL_ARB_FILL_SPARE_13		REG_BIT(13)
#define _PIPEA_CHICKEN				0x70038
#define _PIPEB_CHICKEN				0x71038
#define PIPE_CHICKEN(pipe)			_MMIO_PIPE(pipe, _PIPEA_CHICKEN,\
							   _PIPEB_CHICKEN)
#define   UNDERRUN_RECOVERY_DISABLE_ADLP	REG_BIT(30)
#define   UNDERRUN_RECOVERY_ENABLE_DG2		REG_BIT(30)
#define   PIXEL_ROUNDING_TRUNC_FB_PASSTHRU	REG_BIT(15)
#define   DG2_RENDER_CCSTAG_4_3_EN		REG_BIT(12)
#define   PER_PIXEL_ALPHA_BYPASS_EN		REG_BIT(7)
#define _HSW_VIDEO_DIP_CTL_A		0x60200
#define HSW_TVIDEO_DIP_CTL(trans)		_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_CTL_A)
#define SOUTH_CHICKEN1		_MMIO(0xc2000)
#define  ICP_SECOND_PPS_IO_SELECT	REG_BIT(2)
#define TRANS_CMTG_CHICKEN		_MMIO(0x6fa90)
#define  DISABLE_DPT_CLK_GATING		REG_BIT(1)
#define _DP_TP_CTL_A			0x64040
#define _DP_TP_CTL_B			0x64140
#define _TGL_DP_TP_CTL_A		0x60540
#define DP_TP_CTL(port) _MMIO_PORT(port, _DP_TP_CTL_A, _DP_TP_CTL_B)
#define TGL_DP_TP_CTL(tran) _MMIO_TRANS2((tran), _TGL_DP_TP_CTL_A)
#define  DP_TP_CTL_ENABLE			(1 << 31)
#define  DP_TP_CTL_FEC_ENABLE			(1 << 30)
#define  DP_TP_CTL_MODE_SST			(0 << 27)
#define  DP_TP_CTL_MODE_MST			(1 << 27)
#define  DP_TP_CTL_ENHANCED_FRAME_ENABLE	(1 << 18)
#define  DP_TP_CTL_LINK_TRAIN_MASK		(7 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT1		(0 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT2		(1 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT3		(4 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT4		(5 << 8)
#define  DP_TP_CTL_LINK_TRAIN_IDLE		(2 << 8)
#define  DP_TP_CTL_LINK_TRAIN_NORMAL		(3 << 8)
#define _DP_TP_STATUS_A			0x64044
#define _DP_TP_STATUS_B			0x64144
#define _TGL_DP_TP_STATUS_A		0x60544
#define DP_TP_STATUS(port) _MMIO_PORT(port, _DP_TP_STATUS_A, _DP_TP_STATUS_B)
#define TGL_DP_TP_STATUS(tran) _MMIO_TRANS2((tran), _TGL_DP_TP_STATUS_A)
#define  DP_TP_STATUS_IDLE_DONE			(1 << 25)
#define _TRANS_CLK_SEL_A		0x46140
#define _TRANS_CLK_SEL_B		0x46144
#define TRANS_CLK_SEL(tran) _MMIO_TRANS(tran, _TRANS_CLK_SEL_A, _TRANS_CLK_SEL_B)
#define  TRANS_CLK_SEL_DISABLED		(0x0 << 29)
#define  TRANS_CLK_SEL_PORT(x)		(((x) + 1) << 29)
#define  TGL_TRANS_CLK_SEL_DISABLED	(0x0 << 28)
#define  TGL_TRANS_CLK_SEL_PORT(x)	(((x) + 1) << 28)
#define ICL_DPCLKA_CFGCR0			_MMIO(0x164280)
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)	(1 << _PICK(phy, 10, 11, 24, 4, 5))
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy)	((phy) * 2)
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy)	(3 << ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll, phy)	((pll) << ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define _DPLL0_ENABLE		0x46010
#define _DPLL1_ENABLE		0x46014
#define _ADLS_DPLL3_ENABLE	0x46030
#define   PLL_ENABLE		REG_BIT(31)
#define   PLL_LOCK		REG_BIT(30)
#define   PLL_POWER_ENABLE	REG_BIT(27)
#define   PLL_POWER_STATE	REG_BIT(26)
#define ICL_DPLL_ENABLE(pll)	_MMIO(_PICK_EVEN_2RANGES(pll, 3,			\
							_DPLL0_ENABLE, _DPLL1_ENABLE,	\
							_ADLS_DPLL3_ENABLE, _ADLS_DPLL3_ENABLE))
#define _MG_PLL1_ENABLE		0x46030
#define _MG_PLL2_ENABLE		0x46034
#define MG_PLL_ENABLE(tc_port)	_MMIO_PORT((tc_port), _MG_PLL1_ENABLE, \
					   _MG_PLL2_ENABLE)
#define DG1_DPLL_ENABLE(pll)    _MMIO(_PICK_EVEN_2RANGES(pll, 2,			\
							_DPLL0_ENABLE, _DPLL1_ENABLE,	\
							_MG_PLL1_ENABLE, _MG_PLL2_ENABLE))
#define _ICL_DPLL0_CFGCR0		0x164000
#define _ICL_DPLL1_CFGCR0		0x164080
#define ICL_DPLL_CFGCR0(pll)		_MMIO_PLL(pll, _ICL_DPLL0_CFGCR0, \
						  _ICL_DPLL1_CFGCR0)
#define   DPLL_CFGCR0_DCO_FRACTION_MASK	(0x7fff << 10)
#define   DPLL_CFGCR0_DCO_FRACTION_SHIFT	(10)
#define   DPLL_CFGCR0_DCO_INTEGER_MASK	(0x3ff)
#define _ICL_DPLL0_CFGCR1		0x164004
#define _ICL_DPLL1_CFGCR1		0x164084
#define ICL_DPLL_CFGCR1(pll)		_MMIO_PLL(pll, _ICL_DPLL0_CFGCR1, \
						  _ICL_DPLL1_CFGCR1)
#define   DPLL_CFGCR1_QDIV_RATIO_MASK	(0xff << 10)
#define   DPLL_CFGCR1_QDIV_RATIO_SHIFT	(10)
#define   DPLL_CFGCR1_KDIV_MASK		(7 << 6)
#define   DPLL_CFGCR1_KDIV_1		(1 << 6)
#define   DPLL_CFGCR1_KDIV_2		(2 << 6)
#define   DPLL_CFGCR1_KDIV_3		(4 << 6)
#define   DPLL_CFGCR1_PDIV_MASK		(0xf << 2)
#define   DPLL_CFGCR1_PDIV_2		(1 << 2)
#define   DPLL_CFGCR1_PDIV_3		(2 << 2)
#define   DPLL_CFGCR1_PDIV_5		(4 << 2)
#define   DPLL_CFGCR1_PDIV_7		(8 << 2)
#define _TGL_DPLL0_CFGCR0		0x164284
#define _TGL_DPLL1_CFGCR0		0x16428C
#define _TGL_TBTPLL_CFGCR0		0x16429C
#define TGL_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _TGL_TBTPLL_CFGCR0, _TGL_TBTPLL_CFGCR0))
#define RKL_DPLL_CFGCR0(pll)		_MMIO_PLL(pll, _TGL_DPLL0_CFGCR0, \
						  _TGL_DPLL1_CFGCR0)
#define _TGL_DPLL0_DIV0					0x164B00
#define _TGL_DPLL1_DIV0					0x164C00
#define TGL_DPLL0_DIV0(pll)				_MMIO_PLL(pll, _TGL_DPLL0_DIV0, _TGL_DPLL1_DIV0)
#define _TGL_DPLL0_CFGCR1		0x164288
#define _TGL_DPLL1_CFGCR1		0x164290
#define _TGL_TBTPLL_CFGCR1		0x1642A0
#define TGL_DPLL_CFGCR1(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _TGL_TBTPLL_CFGCR1, _TGL_TBTPLL_CFGCR1))
#define RKL_DPLL_CFGCR1(pll)		_MMIO_PLL(pll, _TGL_DPLL0_CFGCR1, \
						  _TGL_DPLL1_CFGCR1)
#define _DG1_DPLL2_CFGCR0		0x16C284
#define _DG1_DPLL3_CFGCR0		0x16C28C
#define DG1_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _DG1_DPLL2_CFGCR0, _DG1_DPLL3_CFGCR0))
#define _DG1_DPLL2_CFGCR1               0x16C288
#define _DG1_DPLL3_CFGCR1               0x16C290
#define DG1_DPLL_CFGCR1(pll)            _MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _DG1_DPLL2_CFGCR1, _DG1_DPLL3_CFGCR1))
#define _ADLS_DPLL4_CFGCR0		0x164294
#define _ADLS_DPLL3_CFGCR0		0x1642C0
#define ADLS_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _ADLS_DPLL4_CFGCR0, _ADLS_DPLL3_CFGCR0))
#define _ADLS_DPLL4_CFGCR1		0x164298
#define _ADLS_DPLL3_CFGCR1		0x1642C4
#define ADLS_DPLL_CFGCR1(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _ADLS_DPLL4_CFGCR1, _ADLS_DPLL3_CFGCR1))
#define _WM_LINETIME_A		0x45270
#define _WM_LINETIME_B		0x45274
#define WM_LINETIME(pipe) _MMIO_PIPE(pipe, _WM_LINETIME_A, _WM_LINETIME_B)
#define  HSW_LINETIME_MASK	REG_GENMASK(8, 0)
#define  HSW_LINETIME(x)	REG_FIELD_PREP(HSW_LINETIME_MASK, (x))
#define  HSW_IPS_LINETIME_MASK	REG_GENMASK(24, 16)
#define  HSW_IPS_LINETIME(x)	REG_FIELD_PREP(HSW_IPS_LINETIME_MASK, (x))

/* The combo PHY registers (Linux intel_combo_phy_regs.h). */

#define _ICL_COMBOPHY_A				0x162000
#define _ICL_COMBOPHY_B				0x6C000
#define _EHL_COMBOPHY_C				0x160000
#define _RKL_COMBOPHY_D				0x161000
#define _ADL_COMBOPHY_E				0x16B000
#define _ICL_COMBOPHY(phy)			_PICK(phy, _ICL_COMBOPHY_A, \
						      _ICL_COMBOPHY_B, \
						      _EHL_COMBOPHY_C, \
						      _RKL_COMBOPHY_D, \
						      _ADL_COMBOPHY_E)
#define _ICL_PORT_CL_DW(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 4 * (dw))
#define ICL_PORT_CL_DW5(phy)			_MMIO(_ICL_PORT_CL_DW(5, phy))
#define   SUS_CLOCK_CONFIG			(3 << 0)
#define ICL_PORT_CL_DW10(phy)			_MMIO(_ICL_PORT_CL_DW(10, phy))
#define  PWR_UP_ALL_LANES			(0x0 << 4)
#define  PWR_DOWN_LN_3_2_1			(0xe << 4)
#define  PWR_DOWN_LN_3_2			(0xc << 4)
#define  PWR_DOWN_LN_3				(0x8 << 4)
#define  PWR_DOWN_LN_2_1_0			(0x7 << 4)
#define  PWR_DOWN_LN_1_0			(0x3 << 4)
#define  PWR_DOWN_LN_3_1			(0xa << 4)
#define  PWR_DOWN_LN_3_1_0			(0xb << 4)
#define  PWR_DOWN_LN_MASK			(0xf << 4)
#define  EDP4K2K_MODE_OVRD_EN			(1 << 3)
#define  EDP4K2K_MODE_OVRD_OPTIMIZED		(1 << 2)
#define _ICL_PORT_PCS_GRP			0x600
#define _ICL_PORT_PCS_LN(ln)			(0x800 + (ln) * 0x100)
#define _ICL_PORT_PCS_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_PCS_GRP + 4 * (dw))
#define _ICL_PORT_PCS_DW_LN(dw, ln, phy)	 (_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_PCS_LN(ln) + 4 * (dw))
#define ICL_PORT_PCS_DW1_GRP(phy)		_MMIO(_ICL_PORT_PCS_DW_GRP(1, phy))
#define ICL_PORT_PCS_DW1_LN(ln, phy)		_MMIO(_ICL_PORT_PCS_DW_LN(1, ln, phy))
#define   COMMON_KEEPER_EN			(1 << 26)
#define _ICL_PORT_TX_GRP			0x680
#define _ICL_PORT_TX_LN(ln)			(0x880 + (ln) * 0x100)
#define _ICL_PORT_TX_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_TX_GRP + 4 * (dw))
#define _ICL_PORT_TX_DW_LN(dw, ln, phy) 	(_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_TX_LN(ln) + 4 * (dw))
#define ICL_PORT_TX_DW2_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(2, ln, phy))
#define   SWING_SEL_UPPER(x)			(((x) >> 3) << 15)
#define   SWING_SEL_UPPER_MASK			(1 << 15)
#define   SWING_SEL_LOWER(x)			(((x) & 0x7) << 11)
#define   SWING_SEL_LOWER_MASK			(0x7 << 11)
#define   RCOMP_SCALAR(x)			((x) << 0)
#define   RCOMP_SCALAR_MASK			(0xFF << 0)
#define ICL_PORT_TX_DW4_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(4, ln, phy))
#define   LOADGEN_SELECT			(1 << 31)
#define   POST_CURSOR_1(x)			((x) << 12)
#define   POST_CURSOR_1_MASK			(0x3F << 12)
#define   POST_CURSOR_2(x)			((x) << 6)
#define   POST_CURSOR_2_MASK			(0x3F << 6)
#define   CURSOR_COEFF(x)			((x) << 0)
#define   CURSOR_COEFF_MASK			(0x3F << 0)
#define ICL_PORT_TX_DW5_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(5, phy))
#define ICL_PORT_TX_DW5_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(5, ln, phy))
#define   TX_TRAINING_EN			(1 << 31)
#define   TAP2_DISABLE				(1 << 30)
#define   TAP3_DISABLE				(1 << 29)
#define   SCALING_MODE_SEL(x)			((x) << 18)
#define   SCALING_MODE_SEL_MASK			(0x7 << 18)
#define   RTERM_SELECT(x)			((x) << 3)
#define   RTERM_SELECT_MASK			(0x7 << 3)
#define ICL_PORT_TX_DW7_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(7, ln, phy))
#define   N_SCALAR(x)				((x) << 24)
#define   N_SCALAR_MASK				(0x7F << 24)

/* The display stream splitter registers (Linux intel_vdsc_regs.h). */

#define  SPLITTER_ENABLE			(1 << 31)
#define  OVERLAP_PIXELS_MASK			(0xf << 16)
#define  OVERLAP_PIXELS(pixels)			((pixels) << 16)
#define _ICL_PIPE_DSS_CTL1_PB			0x78200
#define _ICL_PIPE_DSS_CTL1_PC			0x78400
#define ICL_PIPE_DSS_CTL1(pipe)			_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_PIPE_DSS_CTL1_PB, \
							   _ICL_PIPE_DSS_CTL1_PC)
#define  SPLITTER_CONFIGURATION_MASK		REG_GENMASK(26, 25)
#define  SPLITTER_CONFIGURATION_2_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 0)
#define  SPLITTER_CONFIGURATION_4_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 1)

/* The CX0 PHY port buffer registers (Linux intel_cx0_phy_regs.h). */

#define _XELPDP_PORT_BUF_CTL1_LN0_A			0x64004
#define _XELPDP_PORT_BUF_CTL1_LN0_B			0x64104
#define _XELPDP_PORT_BUF_CTL1_LN0_USBC1			0x16F200
#define _XELPDP_PORT_BUF_CTL1_LN0_USBC2			0x16F400
#define XELPDP_PORT_BUF_CTL1(port)			_MMIO(_PICK_EVEN_2RANGES(port, PORT_TC1, \
										 _XELPDP_PORT_BUF_CTL1_LN0_A, \
										 _XELPDP_PORT_BUF_CTL1_LN0_B, \
										 _XELPDP_PORT_BUF_CTL1_LN0_USBC1, \
										 _XELPDP_PORT_BUF_CTL1_LN0_USBC2))
#define   XELPDP_PORT_BUF_IO_SELECT_TBT			REG_BIT(11)
#define   XELPDP_PORT_BUF_PHY_IDLE			REG_BIT(7)

/* The crtc mode flags (Linux intel_display_types.h). */

#define I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP (1<<1)
#define I915_MODE_FLAG_VRR (1<<6)

/* The HDMI data island packet bits (Linux i915_reg.h). */



/* The pipe gamma and CSC registers (Linux intel_color_regs.h). */

#define _GAMMA_MODE_A		0x4a480
#define _GAMMA_MODE_B		0x4ac80
#define GAMMA_MODE(pipe) _MMIO_PIPE(pipe, _GAMMA_MODE_A, _GAMMA_MODE_B)
#define  PRE_CSC_GAMMA_ENABLE			REG_BIT(31) /* icl+ */
#define  POST_CSC_GAMMA_ENABLE			REG_BIT(30) /* icl+ */
#define  GAMMA_MODE_MODE_MASK			REG_GENMASK(1, 0)
#define  GAMMA_MODE_MODE_8BIT			REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 0)
#define  GAMMA_MODE_MODE_10BIT			REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 1)
#define  GAMMA_MODE_MODE_12BIT_MULTI_SEG	REG_FIELD_PREP(GAMMA_MODE_MODE_MASK, 3) /* icl-tgl */
#define _PIPE_A_CSC_MODE	0x49028
#define  ICL_CSC_ENABLE			(1 << 31) /* icl+ */
#define  ICL_OUTPUT_CSC_ENABLE		(1 << 30) /* icl+ */
#define _PIPE_B_CSC_MODE	0x49128
#define PIPE_CSC_MODE(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_MODE, _PIPE_B_CSC_MODE)
#define _SKL_BOTTOM_COLOR_A		0x70034
#define _SKL_BOTTOM_COLOR_B		0x71034
#define SKL_BOTTOM_COLOR(pipe)		_MMIO_PIPE(pipe, _SKL_BOTTOM_COLOR_A, _SKL_BOTTOM_COLOR_B)

/* The backlight PWM registers (Linux intel_backlight_regs.h). */

#define _BXT_BLC_PWM_CTL1			0xC8250
#define   BXT_BLC_PWM_ENABLE			(1 << 31)
#define   BXT_BLC_PWM_POLARITY			(1 << 29)
#define _BXT_BLC_PWM_FREQ1			0xC8254
#define _BXT_BLC_PWM_DUTY1			0xC8258
#define _BXT_BLC_PWM_CTL2			0xC8350
#define _BXT_BLC_PWM_FREQ2			0xC8354
#define _BXT_BLC_PWM_DUTY2			0xC8358
#define BXT_BLC_PWM_CTL(controller)    _MMIO_PIPE(controller,		\
					_BXT_BLC_PWM_CTL1, _BXT_BLC_PWM_CTL2)
#define BXT_BLC_PWM_FREQ(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_FREQ1, _BXT_BLC_PWM_FREQ2)
#define BXT_BLC_PWM_DUTY(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_DUTY1, _BXT_BLC_PWM_DUTY2)

/* The pipe DMC registers (Linux intel_dmc_regs.h). */

#define _PIPEDMC_CONTROL_A		0x45250
#define _PIPEDMC_CONTROL_B		0x45254
#define PIPEDMC_CONTROL(pipe)		_MMIO_PIPE(pipe, \
						   _PIPEDMC_CONTROL_A, \
						   _PIPEDMC_CONTROL_B)
#define  PIPEDMC_ENABLE			REG_BIT(0)
#define MTL_PIPEDMC_CONTROL		_MMIO(0x45250)
#define  PIPEDMC_ENABLE_MTL(pipe)	REG_BIT(((pipe) - PIPE_A) * 4)

/* PIPE_TO_DMC_ID() (Linux intel_dmc.c). */

#define PIPE_TO_DMC_ID(pipe)		 (DMC_FW_PIPEA + ((pipe) - PIPE_A))

/* The watermark, DDB and MBUS registers (Linux skl_watermark_regs.h). */

#define _PIPEA_MBUS_DBOX_CTL			0x7003C
#define _PIPEB_MBUS_DBOX_CTL			0x7103C
#define PIPE_MBUS_DBOX_CTL(pipe)		_MMIO_PIPE(pipe, _PIPEA_MBUS_DBOX_CTL, \
							   _PIPEB_MBUS_DBOX_CTL)
#define MBUS_DBOX_B2B_TRANSACTIONS_MAX_MASK	REG_GENMASK(24, 20) /* tgl+ */
#define MBUS_DBOX_B2B_TRANSACTIONS_MAX(x)	REG_FIELD_PREP(MBUS_DBOX_B2B_TRANSACTIONS_MAX_MASK, x)
#define MBUS_DBOX_B2B_TRANSACTIONS_DELAY_MASK	REG_GENMASK(19, 17) /* tgl+ */
#define MBUS_DBOX_B2B_TRANSACTIONS_DELAY(x)	REG_FIELD_PREP(MBUS_DBOX_B2B_TRANSACTIONS_DELAY_MASK, x)
#define MBUS_DBOX_REGULATE_B2B_TRANSACTIONS_EN	REG_BIT(16) /* tgl+ */
#define MBUS_DBOX_BW_CREDIT_MASK		REG_GENMASK(15, 14)
#define MBUS_DBOX_BW_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, x)
#define MBUS_DBOX_BW_4CREDITS_MTL		REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, 0x2)
#define MBUS_DBOX_BW_8CREDITS_MTL		REG_FIELD_PREP(MBUS_DBOX_BW_CREDIT_MASK, 0x3)
#define MBUS_DBOX_B_CREDIT_MASK			REG_GENMASK(12, 8)
#define MBUS_DBOX_B_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_B_CREDIT_MASK, x)
#define MBUS_DBOX_I_CREDIT_MASK			REG_GENMASK(7, 5)
#define MBUS_DBOX_I_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_I_CREDIT_MASK, x)
#define MBUS_DBOX_A_CREDIT_MASK			REG_GENMASK(3, 0)
#define MBUS_DBOX_A_CREDIT(x)			REG_FIELD_PREP(MBUS_DBOX_A_CREDIT_MASK, x)
#define MBUS_CTL			_MMIO(0x4438C)
#define MBUS_JOIN			REG_BIT(31)
#define MBUS_HASHING_MODE_MASK		REG_BIT(30)
#define MBUS_HASHING_MODE_2x2		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 0)
#define MBUS_HASHING_MODE_1x4		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 1)
#define MBUS_JOIN_PIPE_SELECT_MASK	REG_GENMASK(28, 26)
#define MBUS_JOIN_PIPE_SELECT(pipe)	REG_FIELD_PREP(MBUS_JOIN_PIPE_SELECT_MASK, pipe)
#define MBUS_JOIN_PIPE_SELECT_NONE	MBUS_JOIN_PIPE_SELECT(7)
#define _CUR_WM_A_0		0x70140
#define _CUR_WM_B_0		0x71140
#define _CUR_WM_SAGV_A		0x70158
#define _CUR_WM_SAGV_B		0x71158
#define _CUR_WM_SAGV_TRANS_A	0x7015C
#define _CUR_WM_SAGV_TRANS_B	0x7115C
#define _CUR_WM_TRANS_A		0x70168
#define _CUR_WM_TRANS_B		0x71168
#define _PLANE_WM_1_A_0		0x70240
#define _PLANE_WM_1_B_0		0x71240
#define _PLANE_WM_2_A_0		0x70340
#define _PLANE_WM_2_B_0		0x71340
#define _PLANE_WM_SAGV_1_A	0x70258
#define _PLANE_WM_SAGV_1_B	0x71258
#define _PLANE_WM_SAGV_2_A	0x70358
#define _PLANE_WM_SAGV_2_B	0x71358
#define _PLANE_WM_SAGV_TRANS_1_A	0x7025C
#define _PLANE_WM_SAGV_TRANS_1_B	0x7125C
#define _PLANE_WM_SAGV_TRANS_2_A	0x7035C
#define _PLANE_WM_SAGV_TRANS_2_B	0x7135C
#define _PLANE_WM_TRANS_1_A	0x70268
#define _PLANE_WM_TRANS_1_B	0x71268
#define _PLANE_WM_TRANS_2_A	0x70368
#define _PLANE_WM_TRANS_2_B	0x71368
#define   PLANE_WM_EN		(1 << 31)
#define   PLANE_WM_IGNORE_LINES	(1 << 30)
#define   PLANE_WM_LINES_MASK	REG_GENMASK(26, 14)
#define   PLANE_WM_BLOCKS_MASK	REG_GENMASK(11, 0)
#define _CUR_WM_0(pipe) _PIPE(pipe, _CUR_WM_A_0, _CUR_WM_B_0)
#define CUR_WM(pipe, level) _MMIO(_CUR_WM_0(pipe) + ((4) * (level)))
#define CUR_WM_SAGV(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_A, _CUR_WM_SAGV_B)
#define CUR_WM_SAGV_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_TRANS_A, _CUR_WM_SAGV_TRANS_B)
#define CUR_WM_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_TRANS_A, _CUR_WM_TRANS_B)
#define _PLANE_WM_1(pipe) _PIPE(pipe, _PLANE_WM_1_A_0, _PLANE_WM_1_B_0)
#define _PLANE_WM_2(pipe) _PIPE(pipe, _PLANE_WM_2_A_0, _PLANE_WM_2_B_0)
#define _PLANE_WM_BASE(pipe, plane) \
	_PLANE(plane, _PLANE_WM_1(pipe), _PLANE_WM_2(pipe))
#define PLANE_WM(pipe, plane, level) \
	_MMIO(_PLANE_WM_BASE(pipe, plane) + ((4) * (level)))
#define _PLANE_WM_SAGV_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_1_A, _PLANE_WM_SAGV_1_B)
#define _PLANE_WM_SAGV_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_2_A, _PLANE_WM_SAGV_2_B)
#define PLANE_WM_SAGV(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_1(pipe), _PLANE_WM_SAGV_2(pipe)))
#define _PLANE_WM_SAGV_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_1_A, _PLANE_WM_SAGV_TRANS_1_B)
#define _PLANE_WM_SAGV_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_2_A, _PLANE_WM_SAGV_TRANS_2_B)
#define PLANE_WM_SAGV_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_TRANS_1(pipe), _PLANE_WM_SAGV_TRANS_2(pipe)))
#define _PLANE_WM_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_1_A, _PLANE_WM_TRANS_1_B)
#define _PLANE_WM_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_2_A, _PLANE_WM_TRANS_2_B)
#define PLANE_WM_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_TRANS_1(pipe), _PLANE_WM_TRANS_2(pipe)))
#define _PLANE_BUF_CFG_1_B			0x7127c
#define _PLANE_BUF_CFG_2_B			0x7137c
#define _PLANE_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_1_A, _PLANE_BUF_CFG_1_B)
#define _PLANE_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_2_A, _PLANE_BUF_CFG_2_B)
#define PLANE_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_BUF_CFG_1(pipe), _PLANE_BUF_CFG_2(pipe))
#define _PLANE_NV12_BUF_CFG_1_B		0x71278
#define _PLANE_NV12_BUF_CFG_2_B		0x71378
#define _PLANE_NV12_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_1_A, _PLANE_NV12_BUF_CFG_1_B)
#define _PLANE_NV12_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_2_A, _PLANE_NV12_BUF_CFG_2_B)
#define PLANE_NV12_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_NV12_BUF_CFG_1(pipe), _PLANE_NV12_BUF_CFG_2(pipe))
#define _CUR_BUF_CFG_A				0x7017c
#define _CUR_BUF_CFG_B				0x7117c
#define CUR_BUF_CFG(pipe)	_MMIO_PIPE(pipe, _CUR_BUF_CFG_A, _CUR_BUF_CFG_B)
#define _DBUF_CTL_S0				0x45008
#define _DBUF_CTL_S1				0x44FE8
#define _DBUF_CTL_S2				0x44300
#define _DBUF_CTL_S3				0x44304
#define DBUF_CTL_S(slice)			_MMIO(_PICK(slice, \
							    _DBUF_CTL_S0, \
							    _DBUF_CTL_S1, \
							    _DBUF_CTL_S2, \
							    _DBUF_CTL_S3))
#define  DBUF_MIN_TRACKER_STATE_SERVICE_MASK	REG_GENMASK(18, 16) /* ADL-P+ */
#define  DBUF_MIN_TRACKER_STATE_SERVICE(x)		REG_FIELD_PREP(DBUF_MIN_TRACKER_STATE_SERVICE_MASK, x) /* ADL-P+ */

#endif /* DRIVERS_GPU_I915_INTEL_MREG_H */
