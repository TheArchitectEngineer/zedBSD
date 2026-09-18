/*
 * zedBSD WS031: structures and register-field macros extracted textually from the Linux v6.8.12 i915
 * reference (display/intel_display_types.h, display/intel_dpll_mgr.h, i915_reg.h: MIT permission
 * notices, Copyright Intel Corporation; the full notices are kept in intel_link_port.c and
 * intel_dpll_port.c) by tools/port_lcd_calc.py.  Do not edit by hand.
 */
#ifndef PARITY_LCD_REF_TYPES_H
#define PARITY_LCD_REF_TYPES_H

struct intel_link_m_n {
	u32 tu;
	u32 data_m;
	u32 data_n;
	u32 link_m;
	u32 link_n;
};

struct intel_dpll_hw_state {
	/* i9xx, pch plls */
	u32 dpll;
	u32 dpll_md;
	u32 fp0;
	u32 fp1;

	/* hsw, bdw */
	u32 wrpll;
	u32 spll;

	/* skl */
	/*
	 * DPLL_CTRL1 has 6 bits for each each this DPLL. We store those in
	 * lower part of ctrl1 and they get shifted into position when writing
	 * the register.  This allows us to easily compare the state to share
	 * the DPLL.
	 */
	u32 ctrl1;
	/* HDMI only, 0 when used for DP */
	u32 cfgcr1, cfgcr2;

	/* icl */
	u32 cfgcr0;

	/* tgl */
	u32 div0;

	/* bxt */
	u32 ebb0, ebb4, pll0, pll1, pll2, pll3, pll6, pll8, pll9, pll10, pcsdw12;

	/*
	 * ICL uses the following, already defined:
	 * u32 cfgcr0, cfgcr1;
	 */
	u32 mg_refclkin_ctl;
	u32 mg_clktop2_coreclkctl1;
	u32 mg_clktop2_hsclkctl;
	u32 mg_pll_div0;
	u32 mg_pll_div1;
	u32 mg_pll_lf;
	u32 mg_pll_frac_lock;
	u32 mg_pll_ssc;
	u32 mg_pll_bias;
	u32 mg_pll_tdc_coldst_bias;
	u32 mg_pll_bias_mask;
	u32 mg_pll_tdc_coldst_bias_mask;
};

#define  TU_SIZE_MASK		REG_GENMASK(30, 25)
#define  TU_SIZE(x)		REG_FIELD_PREP(TU_SIZE_MASK, (x) - 1) /* default size 64 */
#define  DATA_LINK_M_N_MASK	REG_GENMASK(23, 0)
#define  DATA_LINK_N_MAX	(0x800000)
#define   DPLL_CFGCR0_DCO_FRACTION(x)	((x) << 10)
#define   DPLL_CFGCR1_QDIV_RATIO(x)	((x) << 10)
#define   DPLL_CFGCR1_QDIV_MODE(x)	((x) << 9)
#define   DPLL_CFGCR1_KDIV(x)		((x) << 6)
#define   DPLL_CFGCR1_PDIV(x)		((x) << 2)
#define   DPLL_CFGCR1_CENTRAL_FREQ_8400	(3 << 0)
#define   TGL_DPLL_CFGCR1_CFSELOVRD_NORMAL_XTAL	(0 << 0)
#define   TGL_DPLL0_DIV0_AFC_STARTUP_MASK		REG_GENMASK(27, 25)
#define   TGL_DPLL0_DIV0_AFC_STARTUP(val)		REG_FIELD_PREP(TGL_DPLL0_DIV0_AFC_STARTUP_MASK, (val))

#endif /* PARITY_LCD_REF_TYPES_H */
