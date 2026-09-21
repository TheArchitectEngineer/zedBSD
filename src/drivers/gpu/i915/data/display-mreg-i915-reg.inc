/* Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
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
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_i915_reg.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_I915_REG_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_I915_REG_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_I915_REG_INC

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

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_I915_REG_INC */
