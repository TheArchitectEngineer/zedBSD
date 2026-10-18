/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display 9 registers (Skylake, Kaby Lake, Coffee Lake and Comet
 * Lake) the xd display code reads and writes, with their fields.
 *
 * Display 9 shares many of the later registers' places; what is here is
 * either only on display 9 or laid out differently there.
 */

#ifndef DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_XD9_H
#define DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_XD9_H

/*
 * The CD clock's control and DPLL 0's control (CDCLK_CTL, LCPLL1_CTL):
 * the frequency select in CDCLK_CTL 27:26, DPLL 0's enable and lock.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (CDCLK_CTL, LCPLL1_CTL).
 */
#define I915_XD9_CDCLK_CTL			0x46000U
#define I915_XD9_CDCLK_SELECT_SHIFT		26U
#define I915_XD9_CDCLK_SELECT_MASK		0x3U
#define I915_XD9_LCPLL1_CTL			0x46010U
#define I915_XD9_LCPLL_ENABLE			(1U << 31)
#define I915_XD9_LCPLL_LOCK			(1U << 30)

/*
 * The DPLL controls (DPLL_CTRL1, DPLL_CTRL2).
 *
 * DPLL_CTRL1 has six bits for each of the DPLLs 0 to 3: the override that
 * lets the field rule the DPLL (bit 0), the link rate (bits 3:1) and the
 * HDMI mode (bit 5).  DPLL_CTRL2 has three bits for each DDI: the
 * override (bit 0) and the DPLL that clocks the DDI (bits 2:1), and a
 * clock off bit for each DDI from bit 15.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (DPLL_CTRL1, DPLL_CTRL2).
 */
#define I915_XD9_DPLL_CTRL1			0x6c058U
#define I915_XD9_DPLL_CTRL1_BITS		6U
#define I915_XD9_DPLL_CTRL1_OVERRIDE		(1U << 0)
#define I915_XD9_DPLL_CTRL1_RATE_SHIFT		1U
#define I915_XD9_DPLL_CTRL1_RATE_MASK		0x7U
#define I915_XD9_DPLL_CTRL1_HDMI		(1U << 5)
#define I915_XD9_DPLL_CTRL2			0x6c05cU

/*
 * The link rates of DPLL_CTRL1's rate field, named by the link clock in
 * MHz (half the port's bit rate in 10 Mbit/s): 2700, 1350, 810, 1620,
 * 1080 and 2160.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (DPLL_CTRL1, Link Rate).
 */
#define I915_XD9_LINK_RATE_2700			0U
#define I915_XD9_LINK_RATE_1350			1U
#define I915_XD9_LINK_RATE_810			2U
#define I915_XD9_LINK_RATE_1620			3U
#define I915_XD9_LINK_RATE_1080			4U
#define I915_XD9_LINK_RATE_2160			5U

/*
 * The configuration words of a DPLL in HDMI mode (DPLL_CFGCR1, DPLL_CFGCR2):
 * the frequency enable, the DCO fraction (24:9, in 1/0x8000 of the
 * reference) and integer (8:0, in reference clocks); the Q divider ratio
 * (15:8) and mode (7), the K divider (6:5), the P divider (4:2) and the
 * central frequency (1:0).
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (DPLL_CFGCR1, DPLL_CFGCR2).
 */
#define I915_XD9_CFGCR1_FREQ_ENABLE		(1U << 31)
#define I915_XD9_CFGCR1_FRACTION_SHIFT		9U
#define I915_XD9_CFGCR2_QDIV_RATIO_SHIFT	8U
#define I915_XD9_CFGCR2_QDIV_MODE		(1U << 7)
#define I915_XD9_CFGCR2_KDIV_SHIFT		5U
#define I915_XD9_CFGCR2_PDIV_SHIFT		2U
#define I915_XD9_CFGCR2_CENTRAL_9600		0U
#define I915_XD9_CFGCR2_CENTRAL_9000		1U
#define I915_XD9_CFGCR2_CENTRAL_8400		3U

/*
 * The GMBUS pins of the DDIs B, C and D on the Sunrise Point and Cannon
 * Point families, and the PCH's clock gating bit of the GMBUS unit (in
 * SOUTH_DSPCLK_GATE_D), which display 9 holds off while the GMBUS runs.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (GMBUS0 Pin Pair Select, SOUTH_DSPCLK_GATE_D); Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_gmbus.c (Display WA #0868).
 */
#define I915_XD9_GMBUS_PIN_DDI_C		4U
#define I915_XD9_GMBUS_PIN_DDI_B		5U
#define I915_XD9_GMBUS_PIN_DDI_D		6U
#define I915_XD9_GMBUS_UNIT_UNGATE		(1U << 31)

/*
 * The driver's power well request register (HSW_PWR_WELL_CTL2) and the
 * state bit of power well 1, and the eDP transcoder's DDI function
 * control with its enable.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (PWR_WELL_CTL2, TRANS_DDI_FUNC_CTL_EDP).
 */
#define I915_XD9_PWR_WELL_CTL2			0x45404U
#define I915_XD9_PW1_STATE			(1U << 28)
#define I915_XD9_TRANS_DDI_FUNC_CTL_EDP		0x6f400U
#define I915_XD9_TRANS_DDI_FUNC_ENABLE		(1U << 31)

#endif /* DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_XD9_H */
