/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The reference of host-dkl-test.c: Linux v6.8.12's own text of the Type-C
 * PLL calculation (icl_mg_pll_find_divisors(), icl_calc_mg_pll_state(),
 * icl_ddi_mg_pll_get_freq() of intel_dpll_mgr.c and the register fields
 * of intel_dkl_phy_regs.h and intel_mg_phy_regs.h), taken out unchanged
 * by host-dkl.sh into dkl-linux.inc and compiled here against the few
 * Linux types and helpers it reads.  It shares no header with the zedBSD
 * display, so the two calculations meet only through the plain words of
 * the functions below.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host-dkl-linux.h"

/* The Linux integer names and helpers the text uses. */
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define EINVAL 22
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define fallthrough __attribute__((fallthrough))
#define MISSING_CASE(x) ((void)(x))
#define REG_BIT(n) ((u32)1u << (n))
#define REG_GENMASK(h, l) ((u32)((0xffffffffu >> (31 - (h))) & (0xffffffffu << (l))))
#define REG_FIELD_PREP(mask, val) ((u32)((((u32)(val)) << __builtin_ctz(mask)) & (mask)))
#define DIV_ROUND_UP_ULL(ll, d) (((u64)(ll) + (d) - 1) / (d))
#define do_div(n, base) ((n) = (n) / (base))
#define div_u64(dividend, divisor) ((dividend) / (divisor))
#define mul_u32_u32(a, b) ((u64)(a) * (u64)(b))

/* The PLL words the text fills (intel_dpll_mgr.h, the MG / DKL part). */
struct intel_dpll_hw_state {
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

/* The device as the text reads it: its display version, reference clock and VBT AFC override. */
struct drm_device {
	int unused;
};
struct drm_i915_private {
	struct drm_device drm;
	int display_ver;
	struct {
		struct {
			struct {
				int nssc;
			} ref_clks;
		} dpll;
		struct {
			bool override_afc_startup;
			u8 override_afc_startup_val;
		} vbt;
	} display;
};
struct intel_shared_dpll;

/* The crtc state as the text reads it: the crtc's device, the link clock and the output kinds. */
struct drm_crtc {
	struct drm_device *dev;
};
struct intel_crtc_state {
	struct {
		struct drm_crtc *crtc;
	} uapi;
	int port_clock;
	unsigned output_types;
};

/* The output kind and the device accessors of the text. */
#define INTEL_OUTPUT_HDMI 6
#define to_i915(dev) ((struct drm_i915_private *)(dev))
#define DISPLAY_VER(i915) ((i915)->display_ver)
#define intel_crtc_has_type(crtc_state, type) (((crtc_state)->output_types & (1u << (type))) != 0)

static void linux_words_out(const struct intel_dpll_hw_state *state, struct host_dkl_words *words);
static void linux_words_in(const struct host_dkl_words *words, struct intel_dpll_hw_state *state);

/*
 * The Linux text, compiled as the kernel compiles it: its int / u32
 * comparisons and its unused PLL argument are Linux's own.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "dkl-linux.inc"
#pragma GCC diagnostic pop

/* Copies the words the text filled out. */
static void
linux_words_out(
	const struct intel_dpll_hw_state *state,
	struct host_dkl_words *words)
{
	/* One word per register. */
	words->refclkin_ctl = state->mg_refclkin_ctl;
	words->clktop2_coreclkctl1 = state->mg_clktop2_coreclkctl1;
	words->clktop2_hsclkctl = state->mg_clktop2_hsclkctl;
	words->div0 = state->mg_pll_div0;
	words->div1 = state->mg_pll_div1;
	words->ssc = state->mg_pll_ssc;
	words->bias = state->mg_pll_bias;
	words->tdc_coldst_bias = state->mg_pll_tdc_coldst_bias;
}

/* Copies plain words into the text's PLL state. */
static void
linux_words_in(
	const struct host_dkl_words *words,
	struct intel_dpll_hw_state *state)
{
	/* One word per register. */
	state->mg_refclkin_ctl = words->refclkin_ctl;
	state->mg_clktop2_coreclkctl1 = words->clktop2_coreclkctl1;
	state->mg_clktop2_hsclkctl = words->clktop2_hsclkctl;
	state->mg_pll_div0 = words->div0;
	state->mg_pll_div1 = words->div1;
	state->mg_pll_ssc = words->ssc;
	state->mg_pll_bias = words->bias;
	state->mg_pll_tdc_coldst_bias = words->tdc_coldst_bias;
}

/*
 * Computes the DKL PLL words of a link clock with Linux's text on display
 * version 13: 0, or Linux's -EINVAL.
 */
int
host_dkl_linux_calc(
	int port_clock,
	int is_hdmi,
	int ref_nssc,
	struct host_dkl_words *words)
{
	struct drm_i915_private i915 = { { 0 }, 13, { { { 0 } }, { false, 0 } } };
	struct drm_crtc crtc;
	struct intel_crtc_state crtc_state;
	struct intel_dpll_hw_state state = { 0 };
	int calc_result;

	/* The device of the reference clock and the crtc state of the link clock. */
	i915.display.dpll.ref_clks.nssc = ref_nssc;
	crtc.dev = &i915.drm;
	crtc_state.uapi.crtc = &crtc;
	crtc_state.port_clock = port_clock;
	crtc_state.output_types = 0;
	if (is_hdmi)
		crtc_state.output_types = 1u << INTEL_OUTPUT_HDMI;

	/* Runs the text. */
	calc_result = icl_calc_mg_pll_state(&crtc_state, &state);
	if (calc_result != 0)
		return calc_result;

	/* Succeeded: the words. */
	linux_words_out(&state, words);
	return 0;
}

/* Computes the link clock of DKL PLL words with Linux's text on display version 13. */
int
host_dkl_linux_freq(
	int ref_nssc,
	const struct host_dkl_words *words)
{
	struct drm_i915_private i915 = { { 0 }, 13, { { { 0 } }, { false, 0 } } };
	struct intel_dpll_hw_state state = { 0 };
	int freq;

	/* The device of the reference clock and the words. */
	i915.display.dpll.ref_clks.nssc = ref_nssc;
	linux_words_in(words, &state);

	/* Runs the text. */
	freq = icl_ddi_mg_pll_get_freq(&i915, NULL, &state);

	/* Succeeded: the link clock. */
	return freq;
}
