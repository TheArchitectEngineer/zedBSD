/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2022 Intel Corporation
 */

/*
 * Copyright © 2008-2015 Intel Corporation
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
 * The Type-C PHY registers the modeset environment reads: the Dekel (DKL)
 * PHY's PLL, transmitter and DP mode registers, the MG PHY fields the DKL
 * PLL shares, the Type-C PLL and Thunderbolt PLL enable registers, and the
 * DDI clock select of a Type-C port.
 *
 * The DKL PHY registers (Linux intel_dkl_phy_regs.h).  Linux names a DKL
 * register by a structure that carries its MMIO window address and its
 * bank index; this environment names it by its PHY address, the offset the
 * Linux _DKL_* constants give, whose bits 15..12 are the bank and whose low
 * 12 bits are the offset in the port's 4 KiB window (display/dkl-phy.c
 * decodes it).  A lane register's PHY address is the lane 0 offset plus the
 * lane times the distance to the lane 1 offset, as _DKL_REG_LN() computes.
 * zedBSD WS051: definitions taken from the Linux v6.8.12 reference
 * drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h (sha256
 * c60cdded1a2c9e3ee85d85e87b0d2628f621913c7d7e08a82f2b98fda9785a8f).
 *
 * The MG PHY fields (Linux intel_mg_phy_regs.h, sha256
 * 11f21b245fb21098da65272fbe3bbf85834ef0f02f88c3161ff086d1dd86eba9): the
 * refclk mux, the CLKTOP2 dividers and the DP mode lanes, which the DKL
 * PHY's registers keep at the same bit positions.
 *
 * The enable and clock select registers (Linux i915_reg.h, sha256
 * efcb6385e32031ca3c5b97f7691c5ed1706c2a6709e3875ff78c2cf2eff0e626).
 *
 * The first notice above is that of intel_dkl_phy_regs.h, the second that
 * of i915_reg.h; intel_mg_phy_regs.h carries the first one's licence.
 */

#ifndef DRIVERS_GPU_I915_INTEL_DKL_H
#define DRIVERS_GPU_I915_INTEL_DKL_H

/* The Type-C ports of the display (Linux intel_display.h). */
enum tc_port {
	TC_PORT_NONE = -1,
	TC_PORT_1 = 0,
	TC_PORT_2,
	TC_PORT_3,
	TC_PORT_4,
	TC_PORT_5,
	TC_PORT_6,
	I915_MAX_TC_PORTS
};

/* ---- the DKL PHY: PHY addresses (bank in bits 15..12) and fields ---- */

/* The PCS of a lane: its core soft reset. */
#define _DKL_PCS_DW5_LN0				0x0014
#define _DKL_PCS_DW5_LN1				0x1014
#define DKL_PCS_DW5(ln)					(_DKL_PCS_DW5_LN0 + (ln) * (_DKL_PCS_DW5_LN1 - _DKL_PCS_DW5_LN0))
#define   DKL_PCS_DW5_CORE_SOFTRESET			REG_BIT(11)

/* The PLL's feedback divider and loop filter. */
#define DKL_PLL_DIV0					0x2200
#define   DKL_PLL_DIV0_AFC_STARTUP_MASK			REG_GENMASK(27, 25)
#define   DKL_PLL_DIV0_AFC_STARTUP(val)			REG_FIELD_PREP(DKL_PLL_DIV0_AFC_STARTUP_MASK, (val))
#define   DKL_PLL_DIV0_INTEG_COEFF(x)			((x) << 16)
#define   DKL_PLL_DIV0_INTEG_COEFF_MASK			(0x1F << 16)
#define   DKL_PLL_DIV0_PROP_COEFF(x)			((x) << 12)
#define   DKL_PLL_DIV0_PROP_COEFF_MASK			(0xF << 12)
#define   DKL_PLL_DIV0_FBPREDIV_SHIFT			(8)
#define   DKL_PLL_DIV0_FBPREDIV(x)			((x) << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBPREDIV_MASK			(0xF << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBDIV_INT(x)			((x) << 0)
#define   DKL_PLL_DIV0_FBDIV_INT_MASK			(0xFF << 0)
#define   DKL_PLL_DIV0_MASK				(DKL_PLL_DIV0_INTEG_COEFF_MASK | \
							 DKL_PLL_DIV0_PROP_COEFF_MASK | \
							 DKL_PLL_DIV0_FBPREDIV_MASK | \
							 DKL_PLL_DIV0_FBDIV_INT_MASK)

/* The PLL's current reference trim and TDC target count. */
#define DKL_PLL_DIV1					0x2204
#define   DKL_PLL_DIV1_IREF_TRIM(x)			((x) << 16)
#define   DKL_PLL_DIV1_IREF_TRIM_MASK			(0x1F << 16)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT(x)		((x) << 0)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT_MASK		(0xFF << 0)

/* The PLL's spread spectrum. */
#define DKL_PLL_SSC					0x2210
#define   DKL_PLL_SSC_IREF_NDIV_RATIO(x)		((x) << 29)
#define   DKL_PLL_SSC_IREF_NDIV_RATIO_MASK		(0x7 << 29)
#define   DKL_PLL_SSC_STEP_LEN(x)			((x) << 16)
#define   DKL_PLL_SSC_STEP_LEN_MASK			(0xFF << 16)
#define   DKL_PLL_SSC_STEP_NUM(x)			((x) << 11)
#define   DKL_PLL_SSC_STEP_NUM_MASK			(0x7 << 11)
#define   DKL_PLL_SSC_EN				(1 << 9)

/* The PLL's fractional feedback divider. */
#define DKL_PLL_BIAS					0x2214
#define   DKL_PLL_BIAS_FRAC_EN_H			(1 << 30)
#define   DKL_PLL_BIAS_FBDIV_SHIFT			(8)
#define   DKL_PLL_BIAS_FBDIV_FRAC(x)			((x) << DKL_PLL_BIAS_FBDIV_SHIFT)
#define   DKL_PLL_BIAS_FBDIV_FRAC_MASK			(0x3FFFFF << DKL_PLL_BIAS_FBDIV_SHIFT)

/* The PLL's SSC step size and feed-forward gain. */
#define DKL_PLL_TDC_COLDST_BIAS				0x2218
#define   DKL_PLL_TDC_SSC_STEP_SIZE(x)			((x) << 8)
#define   DKL_PLL_TDC_SSC_STEP_SIZE_MASK		(0xFF << 8)
#define   DKL_PLL_TDC_FEED_FWD_GAIN(x)			((x) << 0)
#define   DKL_PLL_TDC_FEED_FWD_GAIN_MASK		(0xFF << 0)

/* The PLL's reference clock input and its high-speed and core clock dividers. */
#define DKL_REFCLKIN_CTL				0x212C
#define DKL_CLKTOP2_HSCLKCTL				0x20D4
#define DKL_CLKTOP2_CORECLKCTL1				0x20D8

/* A lane's transmitter equalisation: preshoot, de-emphasis and voltage swing. */
#define _DKL_TX_DPCNTL0_LN0				0x02C0
#define _DKL_TX_DPCNTL0_LN1				0x12C0
#define DKL_TX_DPCNTL0(ln)				(_DKL_TX_DPCNTL0_LN0 + (ln) * (_DKL_TX_DPCNTL0_LN1 - _DKL_TX_DPCNTL0_LN0))
#define  DKL_TX_PRESHOOT_COEFF(x)			((x) << 13)
#define  DKL_TX_PRESHOOT_COEFF_MASK			(0x1f << 13)
#define  DKL_TX_DE_EMPHASIS_COEFF(x)			((x) << 8)
#define  DKL_TX_DE_EMPAHSIS_COEFF_MASK			(0x1f << 8)
#define  DKL_TX_VSWING_CONTROL(x)			((x) << 0)
#define  DKL_TX_VSWING_CONTROL_MASK			(0x7 << 0)
#define _DKL_TX_DPCNTL1_LN0				0x02C4
#define _DKL_TX_DPCNTL1_LN1				0x12C4
#define DKL_TX_DPCNTL1(ln)				(_DKL_TX_DPCNTL1_LN0 + (ln) * (_DKL_TX_DPCNTL1_LN1 - _DKL_TX_DPCNTL1_LN0))

/* A lane's transmitter mode: 20-bit mode and the load generator select. */
#define _DKL_TX_DPCNTL2_LN0				0x02C8
#define _DKL_TX_DPCNTL2_LN1				0x12C8
#define DKL_TX_DPCNTL2(ln)				(_DKL_TX_DPCNTL2_LN0 + (ln) * (_DKL_TX_DPCNTL2_LN1 - _DKL_TX_DPCNTL2_LN0))
#define  DKL_TX_DP20BITMODE				REG_BIT(2)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK	REG_GENMASK(4, 3)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK, (val))
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK	REG_GENMASK(6, 5)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK, (val))

/* A lane's PMD suspend. */
#define _DKL_TX_PMD_LANE_SUS_LN0			0x0D00
#define _DKL_TX_PMD_LANE_SUS_LN1			0x1D00
#define DKL_TX_PMD_LANE_SUS(ln)				(_DKL_TX_PMD_LANE_SUS_LN0 + (ln) * (_DKL_TX_PMD_LANE_SUS_LN1 - _DKL_TX_PMD_LANE_SUS_LN0))

/* A lane's DP mode (its x1 / x2 configuration, fields of MG_DP_MODE below). */
#define _DKL_DP_MODE_LN0				0x00A0
#define _DKL_DP_MODE_LN1				0x10A0
#define DKL_DP_MODE(ln)					(_DKL_DP_MODE_LN0 + (ln) * (_DKL_DP_MODE_LN1 - _DKL_DP_MODE_LN0))

/* ---- the MG PHY fields the DKL registers share ---- */

/* MG_DP_MODE / DKL_DP_MODE: the lane pair's DP width. */
#define   MG_DP_MODE_CFG_DP_X2_MODE			(1 << 7)
#define   MG_DP_MODE_CFG_DP_X1_MODE			(1 << 6)

/* MG_REFCLKIN_CTL / DKL_REFCLKIN_CTL. */
#define   MG_REFCLKIN_CTL_OD_2_MUX(x)			((x) << 8)
#define   MG_REFCLKIN_CTL_OD_2_MUX_MASK			(0x7 << 8)

/* MG_CLKTOP2_CORECLKCTL1 / DKL_CLKTOP2_CORECLKCTL1. */
#define   MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO(x)		((x) << 8)
#define   MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO_MASK	(0xff << 8)

/* MG_CLKTOP2_HSCLKCTL / DKL_CLKTOP2_HSCLKCTL. */
#define   MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL(x)		((x) << 16)
#define   MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL_MASK	(0x1 << 16)
#define   MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL(x)	((x) << 14)
#define   MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL_MASK	(0x3 << 14)
#define   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_MASK		(0x3 << 12)
#define   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_2		(0 << 12)
#define   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_3		(1 << 12)
#define   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_5		(2 << 12)
#define   MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_7		(3 << 12)
#define   MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO(x)		((x) << 8)
#define   MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_SHIFT		8
#define   MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_MASK		(0xf << 8)

/* ---- the PLL enable and DDI clock select registers (i915_reg.h) ---- */

/* The Thunderbolt PLL's enable (its bits are those of every PLL enable). */
#define TBT_PLL_ENABLE			_MMIO(0x46020)

/* Alder Lake-P's Type-C PLL enables: TC1 at 0x46038, then every 8 bytes. */
#define PORTTC1_PLL_ENABLE	0x46038
#define PORTTC2_PLL_ENABLE	0x46040
#define ADLP_PORTTC_PLL_ENABLE(tc_port)		_MMIO_PORT((tc_port), \
							    PORTTC1_PLL_ENABLE, \
							    PORTTC2_PLL_ENABLE)

/* The DDI clock select of a Type-C port: the port's MG / DKL PLL or one of the TBT PLL's rates. */
#define _PORT_CLK_SEL_A			0x46100
#define _PORT_CLK_SEL_B			0x46104
#define PORT_CLK_SEL(port) _MMIO_PORT(port, _PORT_CLK_SEL_A, _PORT_CLK_SEL_B)
#define DDI_CLK_SEL(port)		PORT_CLK_SEL(port)
#define  DDI_CLK_SEL_MASK		REG_GENMASK(31, 28)
#define  DDI_CLK_SEL_NONE		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0x0)
#define  DDI_CLK_SEL_MG			REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0x8)
#define  DDI_CLK_SEL_TBT_162		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xC)
#define  DDI_CLK_SEL_TBT_270		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xD)
#define  DDI_CLK_SEL_TBT_540		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xE)
#define  DDI_CLK_SEL_TBT_810		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xF)

/* The clock gate of a Type-C port in ICL_DPCLKA_CFGCR0: TC1..TC3 at bits 12..14, TC4 and up from bit 21. */
#define  ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port)	(1 << ((tc_port) < TC_PORT_4 ? \
						       (tc_port) + 12 : \
						       (tc_port) - TC_PORT_4 + 21))

#endif /* DRIVERS_GPU_I915_INTEL_DKL_H */
