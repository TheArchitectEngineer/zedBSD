/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_cdclk.c),
 * which carries the following notice.
 *
 * Copyright © 2006-2017 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_dpll_mgr.c),
 * which carries the following notice.
 *
 * Copyright © 2006-2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * The display clocks: CDCLK and the shared DPLLs (see clock.h).
 *
 * The CDCLK half ports intel_cdclk.c's bxt_* path for display versions 12
 * and 13: intel_init_cdclk_hooks() selects the platform table, and
 * intel_cdclk_init_hw() reads the pre-OS state out (the DE PLL and
 * CDCLK_CTL), sanitizes it, and reprograms it only when it is not a legal
 * frequency / VCO pair for the platform -- with the PCODE prepare / notify
 * handshake around the PLL and CDCLK_CTL writes.  Alder Lake-P can crawl
 * the PLL; neither platform squashes.  The modeset's CDCLK check
 * (bxt_modeset_calc_cdclk() reduced to the one crtc) runs on the Linux text
 * of intel_cdclk.c.
 *
 * The DPLL half keeps the device's pool of PLLs that every screen's
 * modeset object shares (the Linux text of intel_dpll_mgr.c's references,
 * readout and sanitize) and the Type-C PLL's words; the combo PLLs' and
 * the Thunderbolt PLL's words, enable, disable and read, and the DDI clock
 * map, are the xd clock's (xd-clock.c), reached through a window on the
 * modeset's backend.
 */

#include "modeset-internal.h"
#include "takeover-internal.h"
#include "clock.h"
#include "xd-clock.h"
#include "xd.h"
#include <kern/kcrt.h>

#include "../mmio.h"
#include "../power.h"
#include "../sync.h"

#include <kern/klog.h>

#include <uapi/errno.h>

/*
 * One row of a platform's CDCLK table as the modeset text reads it
 * (intel_cdclk.c's struct intel_cdclk_vals).
 *
 * Instances are the rows of the constant tables below; the modeset
 * object's device view points at one of them.
 */
struct intel_cdclk_vals {
	u32 cdclk;
	u16 refclk;
	u16 waveform;
	u8 divider;	/* CD2X divider * 2 */
	u8 ratio;
};

/* The CDCLK tables of the modeset's check (Linux-derived, constant). */
#include "../intel/clock.h"

static void i915_cdclk_to_xd(const struct i915_cdclk_config *cfg, struct i915_xd_cdclk *state);
static void i915_cdclk_from_xd(const struct i915_xd_cdclk *state, struct i915_cdclk_config *cfg);
static void i915_cdclk_report(struct i915_cdclk_dev *cd, const struct i915_xd_cdclk_report *report);
static int i915_bxt_calc_cdclk(struct drm_i915_private *dev_priv, int min_cdclk);
static int i915_bxt_calc_cdclk_pll_vco(struct drm_i915_private *dev_priv, int cdclk);
static u8 i915_calc_voltage_level(int cdclk, int num_voltage_levels, const int voltage_level_max_cdclk[]);
static u8 i915_tgl_calc_voltage_level(int cdclk);
static int i915_pixel_rate_to_cdclk(const struct intel_crtc_state *crtc_state);
static int i915_planes_min_cdclk(struct i915_takeover_world *takeover, const struct intel_crtc_state *crtc_state);
static void i915_icl_pll_power_enable(struct drm_i915_private *i915, struct intel_shared_dpll *pll, i915_reg_t enable_reg);
static void i915_icl_pll_enable(struct drm_i915_private *i915, struct intel_shared_dpll *pll, i915_reg_t enable_reg);
static void i915_intel_enable_shared_dpll(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_enable_shared_dpll_locked(struct drm_i915_private *i915, struct intel_crtc *crtc, struct intel_shared_dpll *pll, unsigned int pipe_mask);
static void i915_intel_disable_shared_dpll(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_disable_shared_dpll_locked(struct drm_i915_private *i915, struct intel_crtc *crtc, struct intel_shared_dpll *pll, unsigned int pipe_mask);
static unsigned long i915_dpll_mask_all(struct drm_i915_private *i915);
static struct intel_shared_dpll *i915_find_shared_dpll(struct i915_lcd_world *world, struct intel_atomic_state *state, const struct intel_crtc *crtc, const struct intel_dpll_hw_state *pll_state, unsigned long dpll_mask);
static void i915_reference_shared_dpll_crtc(const struct intel_crtc *crtc, const struct intel_shared_dpll *pll, struct intel_shared_dpll_state *shared_dpll_state);
static void i915_reference_shared_dpll(struct i915_lcd_world *world, struct intel_atomic_state *state, const struct intel_crtc *crtc, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
static void i915_unreference_shared_dpll(struct i915_lcd_world *world, struct intel_atomic_state *state, const struct intel_crtc *crtc, const struct intel_shared_dpll *pll);
static void i915_readout_dpll_hw_state(struct i915_takeover_world *takeover, struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_sanitize_dpll_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_lcd_dpll_pool_init(struct i915_lcd_world *world, struct i915_lcd_modeset *ms);
static enum tc_port i915_icl_pll_id_to_tc_port(enum intel_dpll_id id);
static i915_reg_t i915_intel_tc_pll_enable_reg(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static u32 i915_mg_hsdiv_of_div1(int div1);
static int i915_icl_mg_pll_find_divisors(int clock_khz, bool is_dp, bool use_ssc, u32 *target_dco_khz, struct intel_dpll_hw_state *state, bool is_dkl);
static int i915_icl_calc_mg_pll_state(struct intel_crtc_state *crtc_state, struct intel_dpll_hw_state *pll_state);
static int i915_icl_ddi_mg_pll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
static int i915_icl_ddi_tbt_pll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
static bool i915_dkl_pll_get_hw_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll, struct intel_dpll_hw_state *hw_state);
static void i915_dkl_pll_write(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_mg_pll_enable(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static struct i915_xd_io *i915_dpll_window(struct drm_i915_private *i915);
static uint32_t i915_dpll_window_read(void *context, uint32_t offset);
static void i915_dpll_window_write(void *context, uint32_t offset, uint32_t value);
static void i915_dpll_window_wait(void *context, unsigned microseconds);
static const struct i915_xd_pll_desc *i915_dpll_xd_desc(const struct intel_shared_dpll *pll);
static int i915_dpll_afc(const struct drm_i915_private *i915);
static void i915_dpll_report(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, unsigned late);
static bool i915_combo_pll_get_hw_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll, struct intel_dpll_hw_state *hw_state);
static int i915_combo_pll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
static void i915_combo_pll_enable(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_port_pll_disable(struct drm_i915_private *i915, struct intel_shared_dpll *pll);

/*
 * The hooks of a combo PLL of these platforms: the xd clock's read,
 * link clock, enable and disable of the PLL the id names.  Constant and
 * shared by every PLL of every device.
 */
static const struct intel_shared_dpll_funcs i915_combo_pll_funcs = {
	.get_hw_state = i915_combo_pll_get_hw_state,
	.get_freq = i915_combo_pll_get_freq,
	.enable = i915_combo_pll_enable,
	.disable = i915_port_pll_disable,
};

/*
 * The hooks of a Type-C port's DKL PLL (the reference's dkl_pll_funcs,
 * TC PLL 1..4 of Alder Lake-P and Tiger Lake).  Constant and shared by
 * every Type-C PLL of every device.
 */
static const struct intel_shared_dpll_funcs i915_dkl_pll_funcs = {
	.get_hw_state = i915_dkl_pll_get_hw_state,
	.get_freq = i915_icl_ddi_mg_pll_get_freq,
	.enable = i915_mg_pll_enable,
	.disable = i915_port_pll_disable,
};

/*
 * The hooks of the Thunderbolt PLL.  The Type-C ports reserve it beside
 * their own PLL; it drives a port only in Thunderbolt-alt mode, which
 * this path does not drive.  It is read and switched as a combo PLL is
 * (the xd clock's operations); it has no single link clock.  Constant
 * and shared by every device.
 */
static const struct intel_shared_dpll_funcs i915_tbt_pll_funcs = {
	.get_hw_state = i915_combo_pll_get_hw_state,
	.get_freq = i915_icl_ddi_tbt_pll_get_freq,
	.enable = i915_combo_pll_enable,
	.disable = i915_port_pll_disable,
};

/*
 * The names of the Type-C ports' PLLs, by Type-C port (the reference's
 * dpll_info rows "TC PLL 1" .. "TC PLL 6").  Constant; a description
 * points at one of them for the device's lifetime.
 */
static const char *const i915_tc_pll_names[I915_MAX_TC_PORTS] = {
	"TC PLL 1",
	"TC PLL 2",
	"TC PLL 3",
	"TC PLL 4",
	"TC PLL 5",
	"TC PLL 6"
};

/*
 * Maps an Alder Lake-P PCI revision id to its display stepping (the
 * entries of adlp_revids[] this driver needs).
 */
int
drv_i915_adlp_display_step(
	uint8_t revid)
{
	/* Looks the revision up. */
	switch (revid) {
	case 0x0:
		return I915_STEP_A0;
	case 0x4:
		return I915_STEP_B0;
	case 0x8:
		return I915_STEP_C0;
	case 0xC:
		return I915_STEP_D0;
	default:
		break;
	}

	/* A revision the table does not list has no known stepping. */
	return I915_STEP_NONE;
}

/*
 * Prepares the CDCLK state of a display over its xd description: the
 * display version, and whether the PLL may crawl to a new ratio.
 */
void
drv_i915_init_cdclk_hooks(
	struct i915_cdclk_dev *cd,
	struct i915_xd *xd)
{
	/* Records the xd state the CD clock is read and set through. */
	cd->xd = xd;

	/* A display without a description has no CD clock to bring up. */
	if (xd == NULL || !xd->chosen)
		return;

	/* The display version and the PLL crawl of the platform; none squashes. */
	cd->display_ver = xd->choice.platform->display_ver;
	cd->has_cdclk_crawl = xd->choice.platform->cdclk_crawl;
	cd->has_cdclk_squash = 0;
}

/*
 * Reports the lowest CDCLK of the platform's table at or above a minimum
 * for the current reference clock (bxt_calc_cdclk()), or 0 when none is.
 */
int
drv_i915_bxt_calc_cdclk(
	struct i915_cdclk_dev *cd,
	int min_cdclk)
{
	uint32_t cdclk;

	/* Asks the xd table. */
	cdclk = drv_i915_xd_cdclk_lowest(cd->xd->choice.platform, cd->hw.ref, (uint32_t)min_cdclk);
	if (cdclk == 0u) {
		kern_logf("i915: cdclk: cannot satisfy min cdclk %d @ refclk %u\n", min_cdclk, cd->hw.ref);
		return 0;
	}

	/* Succeeded: the CDCLK. */
	return (int)cdclk;
}

/*
 * Reports the PLL VCO of a table CDCLK (bxt_calc_cdclk_pll_vco()): 0 for
 * the bypass frequency and for a frequency the table does not list.
 */
int
drv_i915_bxt_calc_cdclk_pll_vco(
	struct i915_cdclk_dev *cd,
	int cdclk)
{
	struct i915_xd_cdclk state;
	uint32_t vco;

	/* Asks the xd table on the current reference. */
	i915_cdclk_to_xd(&cd->hw, &state);
	vco = drv_i915_xd_cdclk_vco(cd->xd->choice.platform, &state, (uint32_t)cdclk);
	if (vco == 0u && (uint32_t)cdclk != cd->hw.bypass)
		kern_logf("i915: cdclk: cdclk %d not valid for refclk %u\n", cdclk, cd->hw.ref);

	/* Succeeded: the VCO. */
	return (int)vco;
}

/*
 * Reads the CDCLK state out of the hardware (bxt_get_cdclk()); the voltage
 * level is the one the frequency needs.
 */
void
drv_i915_bxt_get_cdclk(
	struct i915_cdclk_dev *cd,
	struct i915_cdclk_config *cfg)
{
	struct i915_xd_cdclk state;

	/* Reads it through the xd clock. */
	drv_i915_xd_cdclk_read(&cd->xd->io, cd->xd->choice.platform, &state);
	i915_cdclk_from_xd(&state, cfg);
}

/*
 * Reads the current CDCLK state into the device (intel_update_cdclk()).
 */
void
drv_i915_update_cdclk(
	struct i915_cdclk_dev *cd)
{
	/* The device's state is the hardware's. */
	drv_i915_bxt_get_cdclk(cd, &cd->hw);
}

/*
 * Programs a CDCLK state (bxt_set_cdclk()) through the xd clock: the
 * firmware's prepare, the PLL and CDCLK_CTL, the voltage notify and the
 * readout.  A refused prepare writes nothing; a refused notify leaves the
 * hardware changed and the state as it was.
 */
void
drv_i915_bxt_set_cdclk(
	struct i915_cdclk_dev *cd,
	const struct i915_cdclk_config *cfg)
{
	struct i915_xd_cdclk state;
	struct i915_xd_cdclk target;
	struct i915_xd_cdclk_report report;

	/* Sets it through the xd clock and keeps what it met. */
	kern_memset(&report, 0, sizeof(report));
	i915_cdclk_to_xd(&cd->hw, &state);
	i915_cdclk_to_xd(cfg, &target);
	drv_i915_xd_cdclk_set(&cd->xd->io, cd->xd->choice.platform, &state, &target, &report);
	i915_cdclk_from_xd(&state, &cd->hw);
	i915_cdclk_report(cd, &report);
}

/*
 * Brings the CDCLK up (intel_cdclk_init_hw()): keeps a legal state the
 * firmware left, and otherwise sets the table's lowest.
 */
void
drv_i915_cdclk_init_hw(
	struct i915_cdclk_dev *cd)
{
	struct i915_xd_cdclk state;
	struct i915_xd_cdclk_report report;

	/* Brings it up through the xd clock and keeps what it met. */
	drv_i915_xd_cdclk_init(&cd->xd->io, cd->xd->choice.platform, &state, &report);
	i915_cdclk_from_xd(&state, &cd->hw);
	i915_cdclk_report(cd, &report);
}

/*
 * Takes the CDCLK down for a suspend (intel_cdclk_uninit_hw()): the bypass
 * frequency with the PLL off.  The resume's drv_i915_cdclk_init_hw() finds
 * the PLL off and sets the CDCLK again.
 */
void
drv_i915_cdclk_uninit_hw(
	struct i915_cdclk_dev *cd)
{
	struct i915_xd_cdclk state;
	struct i915_xd_cdclk_report report;

	/* Takes it down through the xd clock and keeps what it met. */
	i915_cdclk_to_xd(&cd->hw, &state);
	drv_i915_xd_cdclk_uninit(&cd->xd->io, cd->xd->choice.platform, &state, &report);
	i915_cdclk_from_xd(&state, &cd->hw);
	i915_cdclk_report(cd, &report);
}

/*
 * Reports the highest CDCLK of the platform (intel_update_max_cdclk()).
 */
uint32_t
drv_i915_max_cdclk_freq(
	const struct i915_cdclk_dev *cd)
{
	struct i915_xd_cdclk state;
	uint32_t max;

	/* Asks the xd clock for the reference. */
	i915_cdclk_to_xd(&cd->hw, &state);
	max = drv_i915_xd_cdclk_max(&state);

	/* Succeeded: the highest CDCLK. */
	return max;
}

/*
 * Computes the CDCLK a crtc needs (intel_crtc_compute_min_cdclk()).
 *
 * The planes of the crtc are walked in the takeover registry, which the
 * Linux text reached through a file-scope accessor: they are found only
 * while the registry is live (NULL: no registry, no planes).
 */
int
drv_i915_crtc_compute_min_cdclk(
	struct i915_takeover_world *takeover,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	int min_cdclk;
	int planes_min;
	int display_ver;
	bool has_dp;
	bool has_dsi;

	/* Resolves the device of the crtc. */
	dev_priv = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);

	/* A crtc that is not enabled needs nothing. */
	if (!crtc_state->hw.enable)
		return 0;

	/* Starts from the pixel rate. */
	min_cdclk = i915_pixel_rate_to_cdclk(crtc_state);

	/* The pixel rate must not exceed 95% of the CDCLK with IPS on BDW. */
	if (IS_BROADWELL(dev_priv) && hsw_crtc_state_ips_capable(crtc_state))
		min_cdclk = DIV_ROUND_UP(min_cdclk * 100, 95);

	/*
	 * BSpec: "Do not use DisplayPort with CDCLK less than 432 MHz, audio
	 * enabled, port width x4, and link rate HBR2 (5.4 GHz), or else there
	 * may be audio corruption or screen corruption."  The GLK restriction
	 * is 316.8 MHz.
	 */
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);
	has_dp = intel_crtc_has_dp_encoder(crtc_state);
	if (has_dp &&
	    crtc_state->has_audio &&
	    crtc_state->port_clock >= 540000 &&
	    crtc_state->lane_count == 4) {
		if (display_ver == 10) {
			/* Display WA #1145: glk */
			min_cdclk = max(316800, min_cdclk);
		} else if (display_ver == 9 || IS_BROADWELL(dev_priv)) {
			/* Display WA #1144: skl,bxt */
			min_cdclk = max(432000, min_cdclk);
		}
	}

	/*
	 * BSpec: "The CD clock frequency must be at least twice the frequency
	 * of the Azalia BCLK", and BCLK is 96 MHz by default.
	 */
	if (crtc_state->has_audio && display_ver >= 9)
		min_cdclk = max(2 * 96000, min_cdclk);

	/*
	 * "For DP audio configuration, cdclk frequency shall be set to meet the
	 * following requirements: 270 MHz link -> 320 MHz or higher, 162 MHz
	 * link -> 200 MHz or higher."
	 */
	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    has_dp && crtc_state->has_audio)
		min_cdclk = max(crtc_state->port_clock, min_cdclk);

	/* On Valleyview some DSI panels lose (v|h)sync when the clock is lower than 320 MHz. */
	has_dsi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI);
	if (has_dsi && IS_VALLEYVIEW(dev_priv))
		min_cdclk = max(320000, min_cdclk);

	/*
	 * On Geminilake, once the CDCLK gets as low as 79.2 MHz the picture
	 * gets unstable, although the DSI PLL and DE PLL values are correct.
	 */
	if (has_dsi && IS_GEMINILAKE(dev_priv))
		min_cdclk = max(158400, min_cdclk);

	/* Accounts for the additional needs of the planes. */
	planes_min = i915_planes_min_cdclk(takeover, crtc_state);
	min_cdclk = max(planes_min, min_cdclk);

	/* DSC is not part of this path; reaching it is reported. */
	if (crtc_state->dsc.compression_enable)
		min_cdclk = max(min_cdclk, intel_vdsc_min_cdclk(crtc_state));

	/*
	 * HACK of the reference: on TGL / DG2 the lowest CDCLK computed from
	 * half the pixel rate can underrun, so the pixel rate itself is the
	 * minimum, clamped to the maximum CDCLK so 8K still works.
	 */
	if (I915_LCD_IS_TIGERLAKE(dev_priv) || IS_DG2(dev_priv)) {
		min_cdclk = max_t(int, min_cdclk,
				  min_t(int, crtc_state->pixel_rate,
					dev_priv->display.cdclk.max_cdclk_freq));
	}

	/* Succeeded: reports the crtc's CDCLK need. */
	return min_cdclk;
}

/*
 * Checks the modeset's CDCLK need against the state the normal
 * initialization left (bxt_modeset_calc_cdclk() for the one crtc, then
 * intel_cdclk_changed()).
 *
 * Nothing is programmed: when the required state equals the current one
 * the reference's pre / post plane updates leave the hardware alone, and
 * that is the only case the modeset accepts.  Returns 0, or I915_LCD_EINVAL
 * when the need cannot be met.
 */
int
drv_i915_lcd_ms_cdclk_check(
	struct i915_takeover_world *takeover,
	struct i915_lcd_modeset *ms)
{
	struct drm_i915_private *i915;
	const struct intel_crtc_state *cs;
	int min_cdclk;
	int cdclk;
	int vco;
	int level;
	int display_ver;
	int change_needed;

	/* Resolves the modeset object's device and crtc state. */
	i915 = &ms->i915;
	cs = &ms->crtc_state;

	/*
	 * intel_init_cdclk_hooks(): the table of this display -- Alder Lake-P
	 * past B0 uses adlp_cdclk_table, Tiger Lake icl_cdclk_table.  It must
	 * be the table the probe programmed the hardware from, or every mode
	 * looks like a CDCLK change.
	 */
	display_ver = drv_i915_lcd_display_ver();
	i915->display.cdclk.table = icl_cdclk_table;
	if (display_ver >= 13)
		i915->display.cdclk.table = adlp_cdclk_table;

	/* intel_compute_min_cdclk(): the crtc's need. */
	min_cdclk = drv_i915_crtc_compute_min_cdclk(takeover, cs);
	if (min_cdclk < 0)
		return -min_cdclk;

	/* And the bandwidth need (no forced minimum). */
	ms->cdclk.crtc_min = min_cdclk;
	ms->cdclk.bw_min = drv_i915_lcd_ms_bw_min_cdclk(ms);
	min_cdclk = max(ms->cdclk.bw_min, min_cdclk);
	if (min_cdclk > (int)i915->display.cdclk.max_cdclk_freq)
		return I915_LCD_EINVAL;

	/* bxt_modeset_calc_cdclk(): the frequency, its VCO and the voltage level. */
	cdclk = i915_bxt_calc_cdclk(i915, min_cdclk);
	vco = i915_bxt_calc_cdclk_pll_vco(i915, cdclk);
	level = max_t(int, cs->min_voltage_level, i915_tgl_calc_voltage_level(cdclk));
	if (cdclk == 0 || vco == 0)
		return I915_LCD_EINVAL;

	/* Records the required state. */
	ms->cdclk.min_cdclk = min_cdclk;
	ms->cdclk.cdclk = cdclk;
	ms->cdclk.vco = vco;
	ms->cdclk.voltage_level = level;

	/*
	 * intel_cdclk_changed(): intel_cdclk_needs_modeset() (the frequency,
	 * VCO or reference differ) or the voltage level differs.
	 */
	change_needed = 0;
	if (cdclk != (int)i915->display.cdclk.hw.cdclk)
		change_needed = 1;
	else if (vco != (int)i915->display.cdclk.hw.vco)
		change_needed = 1;
	else if (level != (int)i915->display.cdclk.hw.voltage_level)
		change_needed = 1;
	ms->cdclk.change_needed = change_needed;

	/* Succeeded: the need is recorded against the current state. */
	return 0;
}

/*
 * Enables a crtc's shared DPLL (intel_enable_shared_dpll()).
 *
 * The first pipe to use the PLL turns it on; a later one only joins its
 * active mask.
 */
void
drv_i915_enable_shared_dpll(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *i915;
	struct intel_shared_dpll *pll;
	unsigned int pipe_mask;

	/* Resolves the crtc, its device, its PLL and its pipe bit. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);
	pll = crtc_state->shared_dpll;
	pipe_mask = BIT(crtc->pipe);

	/* A crtc without a PLL is a warning of the Linux text. */
	if (pll == NULL) {
		drv_i915_lcd_error("WARN_ON(pll == NULL)\n");
		return;
	}

	/*
	 * Joins or turns on the PLL under the device's DPLL lock.  The Linux
	 * text reached the lock through the device the entry point had
	 * selected; the crtc's device is that device.
	 */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.dpll.lock);

	i915_enable_shared_dpll_locked(i915, crtc, pll, pipe_mask);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.dpll.lock);
}

/*
 * Disables a crtc's shared DPLL (intel_disable_shared_dpll()).
 *
 * The last pipe to leave the PLL turns it off.
 */
void
drv_i915_disable_shared_dpll(
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct drm_i915_private *i915;
	struct intel_shared_dpll *pll;
	unsigned int pipe_mask;

	/* Resolves the crtc, its device, its PLL and its pipe bit. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	i915 = i915_lcd_to_i915(crtc->base.dev);
	pll = crtc_state->shared_dpll;
	pipe_mask = BIT(crtc->pipe);

	/* PCH PLLs exist only on ILK+. */
	if (I915_LCD_DISPLAY_VER(i915) < 5)
		return;

	/* A crtc without a PLL has nothing to disable. */
	if (pll == NULL)
		return;

	/* Leaves or turns off the PLL under the device's DPLL lock. */
	I915_LCD_MUTEX_LOCK(i915, &i915->display.dpll.lock);

	i915_disable_shared_dpll_locked(i915, crtc, pll, pipe_mask);

	I915_LCD_MUTEX_UNLOCK(i915, &i915->display.dpll.lock);
}

/*
 * Finds the DPLL with an id in a device's pool (intel_get_shared_dpll_by_id()).
 */
struct intel_shared_dpll *
drv_i915_get_shared_dpll_by_id(
	struct drm_i915_private *i915,
	enum intel_dpll_id id)
{
	struct intel_shared_dpll *pll;
	int index;

	/* Looks the id up in the pool (for_each_shared_dpll). */
	for (index = 0; index < i915->display.dpll.num_shared_dpll; index++) {
		pll = &i915->display.dpll.shared_dplls[index];
		if (pll->info->id == id)
			return pll;
	}

	/* No PLL has the id. */
	I915_LCD_MISSING_CASE(id);
	return NULL;
}

/*
 * Drops a crtc's reference on a DPLL (intel_unreference_shared_dpll_crtc()).
 */
void
drv_i915_unreference_shared_dpll_crtc(
	const struct intel_crtc *crtc,
	const struct intel_shared_dpll *pll,
	struct intel_shared_dpll_state *shared_dpll_state)
{
	/* A crtc that holds no reference is a warning of the Linux text. */
	if ((shared_dpll_state->pipe_mask & BIT(crtc->pipe)) == 0)
		drv_i915_lcd_error("WARN_ON((shared_dpll_state->pipe_mask & BIT(crtc->pipe)) == 0)\n");

	/* The pipe mask names the crtcs that use the PLL. */
	shared_dpll_state->pipe_mask &= ~BIT(crtc->pipe);

	/* Notes the release (on the crtc's DRM device). */
	I915_LCD_DRM_DBG_KMS(crtc->base.dev, "[CRTC:%d:%s] releasing %s\n",
		crtc->base.base.id, crtc->base.name, pll->info->name);
}

/*
 * Calculates a DPLL's output frequency from a state (intel_dpll_get_freq()):
 * 0 when the PLL has no frequency hook.
 */
int
drv_i915_dpll_get_freq(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	int freq;

	/* A PLL without the hook is a warning of the Linux text. */
	if (pll->info->funcs->get_freq == NULL) {
		drv_i915_lcd_error("WARN_ON(!pll->info->funcs->get_freq)\n");
		return 0;
	}

	/* Asks the PLL kind. */
	freq = pll->info->funcs->get_freq(i915, pll, pll_state);

	/* Succeeded: reports the frequency. */
	return freq;
}

/*
 * Reads a DPLL's hardware state (intel_dpll_get_hw_state()): true when
 * the PLL is enabled and the state was read.
 */
bool
drv_i915_dpll_get_hw_state(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll,
	struct intel_dpll_hw_state *hw_state)
{
	bool enabled;

	/* Asks the PLL kind. */
	enabled = pll->info->funcs->get_hw_state(i915, pll, hw_state);

	/* Succeeded: reports whether the PLL is enabled. */
	return enabled;
}

/*
 * Reads every DPLL of the pool out (intel_dpll_readout_hw_state()).
 *
 * The crtcs whose readout uses a PLL are found in the takeover registry.
 */
void
drv_i915_n1_dpll_readout_hw_state(
	struct i915_takeover_world *takeover,
	struct drm_i915_private *i915)
{
	int index;

	/* Reads each PLL of the pool (for_each_shared_dpll). */
	for (index = 0; index < i915->display.dpll.num_shared_dpll; index++)
		i915_readout_dpll_hw_state(takeover, i915, &i915->display.dpll.shared_dplls[index]);
}

/*
 * Turns off every DPLL that is on but used by no pipe
 * (intel_dpll_sanitize_state()).
 */
void
drv_i915_dpll_sanitize_state(
	struct drm_i915_private *i915)
{
	int index;

	/* Sanitizes each PLL of the pool (for_each_shared_dpll). */
	for (index = 0; index < i915->display.dpll.num_shared_dpll; index++)
		i915_sanitize_dpll_state(i915, &i915->display.dpll.shared_dplls[index]);
}

/*
 * Computes the combo PLL words of an HDMI TMDS clock through the xd
 * clock.
 *
 * port_clock is the TMDS clock in kHz and ref_nssc the reference clock.
 * Returns 0, or I915_LCD_EINVAL when no divider fits.
 */
int
drv_i915_icl_hdmi_wrpll(
	int port_clock,
	int ref_nssc,
	uint32_t *cfgcr0,
	uint32_t *cfgcr1,
	uint32_t *div0)
{
	struct i915_xd_dpll_words words;
	int error;

	/* Chooses the dividers and packs them (no AFC override in a computed state). */
	error = drv_i915_xd_dpll_hdmi_words(drv_i915_lcd_xd_platform(), (uint32_t)ref_nssc, (uint32_t)port_clock,
		I915_XD_DPLL_AFC_NONE, &words);
	if (error != 0)
		return I915_LCD_EINVAL;

	/* Succeeded: the words are the caller's. */
	*cfgcr0 = words.cfgcr0;
	*cfgcr1 = words.cfgcr1;
	*div0 = words.div0;
	return 0;
}

/*
 * Computes the combo PLL words of a DP port clock through the xd clock
 * (the fraction halved on a 38.4 MHz reference where the platform needs
 * it).
 *
 * Returns 0, or I915_LCD_EINVAL for a link rate the table does not list.
 */
int
drv_i915_icl_dp_combo_pll(
	int port_clock,
	int ref_nssc,
	uint32_t *cfgcr0,
	uint32_t *cfgcr1,
	uint32_t *div0)
{
	struct i915_xd_dpll_words words;
	int error;

	/* Looks the link rate up and packs it (no AFC override in a computed state). */
	error = drv_i915_xd_dpll_dp_words(drv_i915_lcd_xd_platform(), (uint32_t)ref_nssc, (uint32_t)port_clock,
		I915_XD_DPLL_AFC_NONE, &words);
	if (error != 0) {
		I915_LCD_MISSING_CASE(port_clock);
		return I915_LCD_EINVAL;
	}

	/* Succeeded: the words are the caller's. */
	*cfgcr0 = words.cfgcr0;
	*cfgcr1 = words.cfgcr1;
	*div0 = words.div0;
	return 0;
}

/*
 * Returns the pool's shared_dpll[] state of a world (the atomic state's
 * shared DPLL state).
 */
struct intel_shared_dpll_state *
drv_i915_lcd_shared_dpll_state(
	struct i915_lcd_world *world)
{
	/* Succeeded: the pool state lives in the world. */
	return world->i915_lcd_dpll_pool_state;
}

/*
 * Binds a device view to the world's pool of PLLs, building the pool on
 * first use as intel_shared_dpll_init() leaves it for Alder Lake-P
 * (adlp_plls[]): DPLL 0 and DPLL 1, the Thunderbolt PLL, TC PLL 1 to 4.
 *
 * One pool serves every screen: two modeset objects that want the same
 * hardware state share one PLL object, and its active mask counts the
 * pipes that drive it, so one screen's stop never turns off a PLL the
 * other still uses.  A PLL's place in the pool is its id, as in the
 * reference's table.
 */
void
drv_i915_lcd_dpll_pool_bind(
	struct i915_lcd_world *world,
	struct drm_i915_private *i915)
{
	struct dpll_info *info;
	enum tc_port tc_port;
	int index;

	/* Builds the pool once. */
	if (!world->i915_lcd_dpll_pool_inited) {
		for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
			kern_memset(&world->i915_lcd_dpll_pool[index], 0, sizeof(world->i915_lcd_dpll_pool[index]));
			kern_memset(&world->i915_lcd_dpll_pool_state[index], 0, sizeof(world->i915_lcd_dpll_pool_state[index]));
			info = &world->i915_lcd_dpll_pool_info[index];

			/* Describes the PLL of this place (the reference's adlp_plls[] row). */
			if (index == DPLL_ID_ICL_DPLL0) {
				/* DPLL 0: the combo PLL hooks, no power domain. */
				info->name = "DPLL 0";
				info->id = DPLL_ID_ICL_DPLL0;
				info->funcs = &i915_combo_pll_funcs;
				info->power_domain = 0;
			} else if (index == DPLL_ID_ICL_DPLL1) {
				/* DPLL 1: the combo PLL hooks, no power domain. */
				info->name = "DPLL 1";
				info->id = DPLL_ID_ICL_DPLL1;
				info->funcs = &i915_combo_pll_funcs;
				info->power_domain = 0;
			} else if (index == DPLL_ID_ICL_TBTPLL) {
				/* The Thunderbolt PLL. */
				drv_i915_tbt_pll_describe(info);
			} else {
				/* TC PLL n of the Type-C port n. */
				tc_port = (enum tc_port)((int)TC_PORT_1 + index - I915_LCD_DPLL_POOL_FIRST_TC);
				drv_i915_dkl_pll_describe(info, tc_port);
			}

			/* Links the PLL to its description and its place in the pool. */
			world->i915_lcd_dpll_pool[index].info = info;
			world->i915_lcd_dpll_pool[index].index = (enum intel_dpll_id)index;
		}

		/* The pool is built. */
		world->i915_lcd_dpll_pool_inited = 1;
	}

	/* Points the device view at the pool. */
	i915->display.dpll.shared_dplls = world->i915_lcd_dpll_pool;
	i915->display.dpll.num_shared_dpll = I915_LCD_DPLL_POOL_SIZE;
}

/*
 * Gives a pipe's reference on every PLL of the pool back
 * (intel_release_shared_dplls()).
 */
void
drv_i915_lcd_ms_release_pipe(
	struct i915_lcd_world *world,
	enum pipe pipe)
{
	int index;

	/* Clears the pipe from each PLL's state and from its atomic state. */
	for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
		world->i915_lcd_dpll_pool_state[index].pipe_mask &= (u8)~BIT(pipe);
		world->i915_lcd_dpll_pool[index].state.pipe_mask &= (u8)~BIT(pipe);
	}
}

/*
 * Empties the world's pool, as intel_shared_dpll_init() leaves it when the
 * device is (re)created.
 */
void
drv_i915_lcd_dplls_reset(
	struct i915_lcd_world *world)
{
	int index;

	/* The pool is rebuilt on its next use. */
	world->i915_lcd_dpll_pool_inited = 0;

	/* Clears every PLL and its atomic state. */
	for (index = 0; index < I915_LCD_DPLL_POOL_SIZE; index++) {
		kern_memset(&world->i915_lcd_dpll_pool[index], 0, sizeof(world->i915_lcd_dpll_pool[index]));
		kern_memset(&world->i915_lcd_dpll_pool_state[index], 0, sizeof(world->i915_lcd_dpll_pool_state[index]));
	}
}

/*
 * Chooses the PLL of the pool that carries a modeset's PLL state (the
 * allocation half of icl_get_combo_phy_dpll()).
 *
 * The state was computed by the caller.  Returns the DPLL id, or
 * -I915_LCD_EINVAL (a refusal, not an id) when both PLLs are taken by
 * other pipes.
 */
int
drv_i915_lcd_ms_alloc_pll(
	struct i915_lcd_world *world,
	struct i915_lcd_modeset *ms,
	const struct intel_dpll_hw_state *hw_state)
{
	struct intel_shared_dpll *pll;

	/* Binds the pool and gives back what this pipe held. */
	i915_lcd_dpll_pool_init(world, ms);
	drv_i915_lcd_ms_release_pipe(world, ms->crtc.pipe);

	/* Finds a PLL with the same state, or a free one. */
	pll = i915_find_shared_dpll(world, &ms->state, &ms->crtc, hw_state,
		BIT(DPLL_ID_ICL_DPLL0) | BIT(DPLL_ID_ICL_DPLL1));
	if (pll == NULL)
		return -I915_LCD_EINVAL;

	/* References it for the crtc; the object carries the new state. */
	i915_reference_shared_dpll(world, &ms->state, &ms->crtc, pll, hw_state);
	ms->crtc_state.shared_dpll = pll;
	pll->state = world->i915_lcd_dpll_pool_state[pll->index];

	/* Succeeded: reports the PLL's id. */
	return (int)pll->info->id;
}

/*
 * Reserves the two PLLs a Type-C port's crtc may run on
 * (icl_get_tc_phy_dplls()): the Thunderbolt PLL as the default port PLL
 * and the port's TC PLL as the MG PHY one, each with its state the caller
 * computed into the crtc state's port PLLs (drv_i915_icl_compute_tc_phy_dplls()),
 * and makes the TC PLL the active one (DP-alt and legacy mode; the
 * pre-PLL-enable hook selects again once the port's link is held).
 *
 * Returns the TC PLL's id, or -I915_LCD_EINVAL (a refusal, not an id) when
 * another pipe holds either PLL with another state.
 */
int
drv_i915_lcd_ms_alloc_tc_plls(
	struct i915_lcd_world *world,
	struct i915_lcd_modeset *ms,
	enum tc_port tc_port)
{
	struct icl_port_dpll *tbt;
	struct icl_port_dpll *tc;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id tc_id;

	/* Binds the pool and gives back what this pipe held. */
	i915_lcd_dpll_pool_init(world, ms);
	drv_i915_lcd_ms_release_pipe(world, ms->crtc.pipe);

	/* The Thunderbolt PLL, the default port PLL. */
	tbt = &ms->crtc_state.icl_port_dplls[ICL_PORT_DPLL_DEFAULT];
	pll = i915_find_shared_dpll(world, &ms->state, &ms->crtc, &tbt->hw_state, BIT(DPLL_ID_ICL_TBTPLL));
	if (pll == NULL)
		return -I915_LCD_EINVAL;

	/* References it for the crtc; the object carries the new state. */
	i915_reference_shared_dpll(world, &ms->state, &ms->crtc, pll, &tbt->hw_state);
	tbt->pll = pll;
	pll->state = world->i915_lcd_dpll_pool_state[pll->index];

	/* The port's TC PLL, the MG PHY port PLL. */
	tc = &ms->crtc_state.icl_port_dplls[ICL_PORT_DPLL_MG_PHY];
	tc_id = (enum intel_dpll_id)((int)DPLL_ID_ICL_MGPLL1 + (int)tc_port - (int)TC_PORT_1);
	pll = i915_find_shared_dpll(world, &ms->state, &ms->crtc, &tc->hw_state, BIT(tc_id));
	if (pll == NULL) {
		i915_unreference_shared_dpll(world, &ms->state, &ms->crtc, tbt->pll);
		tbt->pll->state.pipe_mask = world->i915_lcd_dpll_pool_state[tbt->pll->index].pipe_mask;
		tbt->pll = NULL;
		return -I915_LCD_EINVAL;
	}

	/* References it for the crtc; the object carries the new state. */
	i915_reference_shared_dpll(world, &ms->state, &ms->crtc, pll, &tc->hw_state);
	tc->pll = pll;
	pll->state = world->i915_lcd_dpll_pool_state[pll->index];

	/* The TC PLL is the active one. */
	drv_i915_icl_set_active_port_dpll(&ms->crtc_state, ICL_PORT_DPLL_MG_PHY);

	/* Succeeded: reports the TC PLL's id. */
	return (int)pll->info->id;
}

/*
 * Gives a modeset's reference on its PLL back (intel_release_shared_dplls());
 * the object stays for the other pipe.  A Type-C port's crtc gives both
 * port PLLs back.
 */
void
drv_i915_lcd_ms_release_pll(
	struct i915_lcd_world *world,
	struct i915_lcd_modeset *ms)
{
	struct intel_shared_dpll *pll;
	enum icl_port_dpll_id id;
	int port_plls;

	/* A Type-C port's crtc gives each port PLL it reserved back. */
	port_plls = 0;
	for (id = ICL_PORT_DPLL_DEFAULT; id < ICL_PORT_DPLL_COUNT; id++) {
		pll = ms->crtc_state.icl_port_dplls[id].pll;
		if (pll == NULL)
			continue;

		/* Drops the crtc's reference and copies the pipe mask back to the object. */
		i915_unreference_shared_dpll(world, &ms->state, &ms->crtc, pll);
		pll->state.pipe_mask = world->i915_lcd_dpll_pool_state[pll->index].pipe_mask;
		ms->crtc_state.icl_port_dplls[id].pll = NULL;
		port_plls++;
	}

	/* The port PLLs were the crtc's whole reservation. */
	if (port_plls != 0)
		return;

	/* A crtc without a PLL has nothing to give back. */
	pll = ms->crtc_state.shared_dpll;
	if (pll == NULL)
		return;

	/* Drops the crtc's reference and copies the pipe mask back to the object. */
	i915_unreference_shared_dpll(world, &ms->state, &ms->crtc, pll);
	pll->state.pipe_mask = world->i915_lcd_dpll_pool_state[pll->index].pipe_mask;
}

/*
 * Binds a modeset object's own PLL: the reference's adlp_plls[] entry for
 * DPLL 0 or DPLL 1 (no power domain), used by this crtc alone.
 */
void
drv_i915_lcd_ms_bind_pll(
	struct i915_lcd_modeset *ms,
	int dpll_id)
{
	/* Describes the PLL. */
	if (dpll_id == DPLL_ID_ICL_DPLL1) {
		ms->pll_info.name = "DPLL 1";
		ms->pll_info.id = DPLL_ID_ICL_DPLL1;
	} else {
		ms->pll_info.name = "DPLL 0";
		ms->pll_info.id = DPLL_ID_ICL_DPLL0;
	}

	/* Binds the combo PLL hooks, no power domain. */
	ms->pll_info.funcs = &i915_combo_pll_funcs;
	ms->pll_info.power_domain = 0;

	/* The crtc is the PLL's only user (what intel_reference_shared_dpll() leaves). */
	ms->pll.info = &ms->pll_info;
	ms->pll.state.pipe_mask = (u8)BIT(ms->crtc.pipe);
	ms->crtc_state.shared_dpll = &ms->pll;
}

/*
 * Describes a Type-C port's DKL PLL as the reference's adlp_plls[] and
 * tgl_plls[] rows do: "TC PLL n", dkl_pll_funcs, no power domain.
 *
 * The PLL ids follow the Type-C ports (icl_tc_port_to_pll_id()).  A port
 * outside TC1..TC6 is reported and described as TC PLL 1, so the caller
 * never holds a description without hooks.
 */
void
drv_i915_dkl_pll_describe(
	struct dpll_info *info,
	enum tc_port tc_port)
{
	/* A port outside the six Type-C ports names no PLL. */
	if (tc_port < TC_PORT_1 || tc_port > TC_PORT_6) {
		drv_i915_lcd_error("drv_i915_dkl_pll_describe: not a Type-C port\n");
		tc_port = TC_PORT_1;
	}

	/* Names the PLL after its port and gives it the port's PLL id. */
	info->name = i915_tc_pll_names[tc_port];
	info->id = (enum intel_dpll_id)((int)DPLL_ID_ICL_MGPLL1 + (int)tc_port - (int)TC_PORT_1);

	/* Binds the DKL PLL hooks, no power domain. */
	info->funcs = &i915_dkl_pll_funcs;
	info->power_domain = 0;
}

/*
 * Describes the Thunderbolt PLL as the reference's adlp_plls[] row does:
 * "TBT PLL", tbt_pll_funcs, no power domain.
 */
void
drv_i915_tbt_pll_describe(
	struct dpll_info *info)
{
	/* Names the PLL and gives it its id. */
	info->name = "TBT PLL";
	info->id = DPLL_ID_ICL_TBTPLL;

	/* Binds the Thunderbolt PLL hooks, no power domain. */
	info->funcs = &i915_tbt_pll_funcs;
	info->power_domain = 0;
}

/*
 * Computes the DKL PLL words of a Type-C port's link clock
 * (icl_calc_mg_pll_state() of display version 12 and later).
 *
 * port_clock is the DP link rate or the HDMI TMDS clock in kHz, ref_nssc
 * the reference clock.  Returns 0, or I915_LCD_EINVAL when no divider fits
 * or the reference clock is not one the PHY takes.
 */
int
drv_i915_dkl_pll_calc(
	int port_clock,
	int is_hdmi,
	int ref_nssc,
	struct intel_dpll_hw_state *hw_state)
{
	struct drm_i915_private i915;
	struct intel_crtc crtc;
	struct intel_crtc_state crtc_state;
	int calc_result;

	/* Builds a device view with only the reference clock set. */
	kern_memset(&i915, 0, sizeof(i915));
	kern_memset(&crtc, 0, sizeof(crtc));
	kern_memset(&crtc_state, 0, sizeof(crtc_state));
	kern_memset(hw_state, 0, sizeof(*hw_state));
	i915.display.dpll.ref_clks.nssc = ref_nssc;

	/* Links a crtc state of the clock and the output kind to it. */
	crtc.base.dev = &i915.drm;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.port_clock = port_clock;
	if (is_hdmi) {
		crtc_state.output_types = BIT(INTEL_OUTPUT_HDMI);
	} else {
		crtc_state.output_types = BIT(INTEL_OUTPUT_DP);
	}

	/* Chooses the dividers and packs the PLL words. */
	calc_result = i915_icl_calc_mg_pll_state(&crtc_state, hw_state);
	if (calc_result != 0)
		return calc_result;

	/* Succeeded: the words are the caller's. */
	return 0;
}

/*
 * Computes the link clock a DKL PLL's words give, in kHz
 * (icl_ddi_mg_pll_get_freq()); 0 for words whose divider is not one the
 * PHY has.
 */
int
drv_i915_dkl_pll_freq(
	int ref_nssc,
	const struct intel_dpll_hw_state *hw_state)
{
	struct drm_i915_private i915;
	int freq;

	/* Builds a device view with only the reference clock set. */
	kern_memset(&i915, 0, sizeof(i915));
	i915.display.dpll.ref_clks.nssc = ref_nssc;

	/* Works the frequency out of the words. */
	freq = i915_icl_ddi_mg_pll_get_freq(&i915, NULL, hw_state);

	/* Succeeded: the link clock. */
	return freq;
}

/*
 * Computes both PLL states a Type-C port's crtc may use
 * (icl_compute_tc_phy_dplls()): the Thunderbolt PLL's, as the default,
 * and the port's DKL PLL's, as the MG PHY one.
 *
 * The active one is the DKL PLL, except that a crtc whose old state ran on
 * the Thunderbolt PLL keeps it (the reference's fastset check).  The link
 * clock is set to what the DKL PLL's words give.  old_crtc_state may be
 * NULL for a crtc without one.  Returns 0, or I915_LCD_EINVAL.
 */
int
drv_i915_icl_compute_tc_phy_dplls(
	struct intel_crtc_state *crtc_state,
	const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *i915;
	struct icl_port_dpll *port_dpll;
	struct i915_xd_dpll_words words;
	bool old_on_tbt;
	int calc_result;

	/* Finds the device. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);

	/* The default port PLL is the Thunderbolt PLL: its words from the xd clock. */
	port_dpll = &crtc_state->icl_port_dplls[ICL_PORT_DPLL_DEFAULT];
	drv_i915_xd_dpll_tbt_words(drv_i915_lcd_xd_platform(), (uint32_t)i915->display.dpll.ref_clks.nssc,
		i915_dpll_afc(i915), &words);
	port_dpll->hw_state.cfgcr0 = words.cfgcr0;
	port_dpll->hw_state.cfgcr1 = words.cfgcr1;
	port_dpll->hw_state.div0 = words.div0;

	/* The MG PHY port PLL is the port's DKL PLL. */
	port_dpll = &crtc_state->icl_port_dplls[ICL_PORT_DPLL_MG_PHY];
	calc_result = i915_icl_calc_mg_pll_state(crtc_state, &port_dpll->hw_state);
	if (calc_result != 0)
		return calc_result;

	/* A crtc that ran on the Thunderbolt PLL keeps it (mainly for the fastset check). */
	old_on_tbt = false;
	if (old_crtc_state != NULL && old_crtc_state->shared_dpll != NULL) {
		if (old_crtc_state->shared_dpll->info->id == DPLL_ID_ICL_TBTPLL)
			old_on_tbt = true;
	}

	/* Selects the active port PLL. */
	if (old_on_tbt) {
		drv_i915_icl_set_active_port_dpll(crtc_state, ICL_PORT_DPLL_DEFAULT);
	} else {
		drv_i915_icl_set_active_port_dpll(crtc_state, ICL_PORT_DPLL_MG_PHY);
	}

	/* The link clock is what the DKL PLL's words give. */
	crtc_state->port_clock = i915_icl_ddi_mg_pll_get_freq(i915, NULL, &port_dpll->hw_state);

	/* Succeeded: both states are computed. */
	return 0;
}

/*
 * Selects which of the PLLs reserved for a crtc is the active one
 * (icl_set_active_port_dpll()): the crtc state takes that port PLL and its
 * state.
 */
void
drv_i915_icl_set_active_port_dpll(
	struct intel_crtc_state *crtc_state,
	enum icl_port_dpll_id port_dpll_id)
{
	struct icl_port_dpll *port_dpll;

	/* Copies the port PLL and its state into the crtc state. */
	port_dpll = &crtc_state->icl_port_dplls[port_dpll_id];
	crtc_state->shared_dpll = port_dpll->pll;
	crtc_state->dpll_hw_state = port_dpll->hw_state;
}

/*
 * Selects a crtc's active port PLL by its encoder's Type-C mode
 * (icl_update_active_dpll()): the MG PHY PLL in DP-alt and legacy mode,
 * the default (Thunderbolt) PLL otherwise.
 *
 * The crtc state is the one crtc state of the atomic state; the crtc is
 * not read.  MST encoders are not part of this path.
 */
void
drv_i915_icl_update_active_dpll(
	struct intel_atomic_state *state,
	struct intel_crtc *crtc,
	struct intel_encoder *encoder)
{
	struct intel_crtc_state *crtc_state;
	struct intel_digital_port *primary_port;
	enum icl_port_dpll_id port_dpll_id;
	bool in_dp_alt_mode;
	bool in_legacy_mode;

	UNUSED_PARAMETER(crtc);

	/* Finds the crtc state and the encoder's port. */
	crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	primary_port = i915_lcd_enc_to_dig_port(encoder);
	port_dpll_id = ICL_PORT_DPLL_DEFAULT;

	/* A port in DP-alt mode runs on its own PLL. */
	in_dp_alt_mode = intel_tc_port_in_dp_alt_mode(primary_port);
	if (in_dp_alt_mode)
		port_dpll_id = ICL_PORT_DPLL_MG_PHY;

	/* So does one in legacy mode (asked only when it is not in DP-alt mode). */
	if (!in_dp_alt_mode) {
		in_legacy_mode = intel_tc_port_in_legacy_mode(primary_port);
		if (in_legacy_mode)
			port_dpll_id = ICL_PORT_DPLL_MG_PHY;
	}

	/* Selects that port PLL. */
	drv_i915_icl_set_active_port_dpll(crtc_state, port_dpll_id);
}

/* Copies a CDCLK state of the older code into the xd clock's form. */
static void
i915_cdclk_to_xd(
	const struct i915_cdclk_config *cfg,
	struct i915_xd_cdclk *state)
{
	/* Field by field, in kHz. */
	state->ref_khz = cfg->ref;
	state->vco_khz = cfg->vco;
	state->cdclk_khz = cfg->cdclk;
	state->bypass_khz = cfg->bypass;
	state->voltage = cfg->voltage_level;
}

/* Copies a CDCLK state of the xd clock into the older code's form. */
static void
i915_cdclk_from_xd(
	const struct i915_xd_cdclk *state,
	struct i915_cdclk_config *cfg)
{
	/* Field by field, in kHz. */
	cfg->ref = state->ref_khz;
	cfg->vco = state->vco_khz;
	cfg->cdclk = state->cdclk_khz;
	cfg->bypass = state->bypass_khz;
	cfg->voltage_level = state->voltage;
}

/*
 * Logs what a CDCLK change met and copies it into the device's
 * diagnostics: the state found and whether it was kept, the state asked
 * for, the firmware's answers, and whether the PLL and CDCLK_CTL were
 * written.
 */
static void
i915_cdclk_report(
	struct i915_cdclk_dev *cd,
	const struct i915_xd_cdclk_report *report)
{
	/* The state the start found, kept or replaced. */
	if (report->kept || report->replaced) {
		i915_cdclk_from_xd(&report->before, &cd->diag_observed_before);
		cd->diag_sanitized = cd->diag_observed_before;
		cd->diag_no_change = report->kept;
	}
	if (report->replaced) {
		kern_logf("i915: cdclk: sanitizing cdclk programmed by pre-os\n");
		cd->diag_sanitized.cdclk = 0u;
		cd->diag_sanitized.vco = I915_XD_CDCLK_VCO_UNKNOWN;
	}

	/* A start that kept the state asked for nothing. */
	if (report->kept)
		return;

	/* The state asked for and the firmware's answers. */
	i915_cdclk_from_xd(&report->requested, &cd->diag_requested);
	cd->diag_prepare_status = report->prepare_error;
	cd->diag_notify_status = report->voltage_error;
	cd->diag_hw_sequence_reached = report->programmed;
	if (report->prepare_error != 0)
		kern_logf("i915: cdclk: PCU prepare failed (err %d, freq %u)\n", report->prepare_error, report->requested.cdclk_khz);
	if (report->voltage_error != 0)
		kern_logf("i915: cdclk: PCODE freq set failed (err %d, freq %u)\n", report->voltage_error, report->requested.cdclk_khz);

	/* The PLL waits that ran out. */
	if (report->unlock_late)
		kern_logf("i915: cdclk: timeout waiting for CDCLK PLL unlock\n");
	if (report->lock_late)
		kern_logf("i915: cdclk: timeout waiting for CDCLK PLL lock\n");
	if (report->crawl_late)
		kern_logf("i915: cdclk: timeout waiting for FREQ change ack\n");
}

/* The lowest table CDCLK of the device view at or above a minimum (bxt_calc_cdclk()), or 0. */
static int
i915_bxt_calc_cdclk(
	struct drm_i915_private *dev_priv,
	int min_cdclk)
{
	const struct intel_cdclk_vals *table;
	int index;

	/* Looks for the first row of the reference clock that is fast enough. */
	table = dev_priv->display.cdclk.table;
	for (index = 0; table[index].refclk != 0u; index++) {
		if (table[index].refclk == dev_priv->display.cdclk.hw.ref &&
		    table[index].cdclk >= (u32)min_cdclk)
			return (int)table[index].cdclk;
	}

	/* drm_WARN(1): no row satisfies the minimum. */
	drv_i915_lcd_error("Cannot satisfy minimum cdclk %d with refclk %u\n");
	return 0;
}

/* The PLL VCO of a table CDCLK of the device view (bxt_calc_cdclk_pll_vco()), or 0. */
static int
i915_bxt_calc_cdclk_pll_vco(
	struct drm_i915_private *dev_priv,
	int cdclk)
{
	const struct intel_cdclk_vals *table;
	int index;

	/* The bypass frequency runs with the PLL off. */
	table = dev_priv->display.cdclk.table;
	if ((unsigned int)cdclk == dev_priv->display.cdclk.hw.bypass)
		return 0;

	/* Looks the frequency up for the reference clock. */
	for (index = 0; table[index].refclk != 0u; index++) {
		if (table[index].refclk == dev_priv->display.cdclk.hw.ref &&
		    table[index].cdclk == (u32)cdclk)
			return (int)(dev_priv->display.cdclk.hw.ref * table[index].ratio);
	}

	/* drm_WARN(1): the frequency is not valid for the reference clock. */
	drv_i915_lcd_error("cdclk %d not valid for refclk %u\n");
	return 0;
}

/* The first voltage level whose maximum CDCLK covers a frequency (calc_voltage_level()). */
static u8
i915_calc_voltage_level(
	int cdclk,
	int num_voltage_levels,
	const int voltage_level_max_cdclk[])
{
	int voltage_level;

	/* Walks the levels from the lowest. */
	for (voltage_level = 0; voltage_level < num_voltage_levels; voltage_level++) {
		if (cdclk <= voltage_level_max_cdclk[voltage_level])
			return (u8)voltage_level;
	}

	/* No level covers it: clamps to the top one. */
	I915_LCD_MISSING_CASE(cdclk);
	return (u8)(num_voltage_levels - 1);
}

/* The voltage level a CDCLK needs on these platforms (tgl_calc_voltage_level()). */
static u8
i915_tgl_calc_voltage_level(
	int cdclk)
{
	static const int tgl_voltage_level_max_cdclk[] = {
		[0] = 312000,
		[1] = 326400,
		[2] = 556800,
		[3] = 652800,
	};
	u8 level;

	/* Finds the level. */
	level = i915_calc_voltage_level(cdclk,
					(int)ARRAY_SIZE(tgl_voltage_level_max_cdclk),
					tgl_voltage_level_max_cdclk);

	/* Succeeded: reports the level. */
	return level;
}

/* The CDCLK a pixel rate needs (intel_pixel_rate_to_cdclk()). */
static int
i915_pixel_rate_to_cdclk(
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv;
	int pixel_rate;
	int display_ver;

	/* Resolves the device and the pixel rate. */
	dev_priv = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);
	pixel_rate = crtc_state->pixel_rate;
	display_ver = I915_LCD_DISPLAY_VER(dev_priv);

	/*
	 * The platform predicates of this environment answer for the device
	 * the probe found and do not read their argument.
	 */
	UNUSED_PARAMETER(dev_priv);

	/* Display version 10+ moves two pixels per CDCLK. */
	if (display_ver >= 10)
		return DIV_ROUND_UP(pixel_rate, 2);

	/* Version 9, BDW and HSW move one. */
	if (display_ver == 9 || IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		return pixel_rate;

	/* CHV needs 95%, a double-wide pipe half of 90%, the rest 90%. */
	if (IS_CHERRYVIEW(dev_priv))
		return DIV_ROUND_UP(pixel_rate * 100, 95);
	if (crtc_state->double_wide)
		return DIV_ROUND_UP(pixel_rate * 100, 90 * 2);

	/* Succeeded: the 90% rule. */
	return DIV_ROUND_UP(pixel_rate * 100, 90);
}

/*
 * The highest CDCLK need of the crtc's planes (intel_planes_min_cdclk()).
 *
 * The Linux for_each_intel_plane_on_crtc() of this text walked the
 * takeover registry: the one primary plane of the crtc's pipe while the
 * registry is live, none otherwise.
 */
static int
i915_planes_min_cdclk(
	struct i915_takeover_world *takeover,
	const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc;
	struct intel_plane *plane;
	int min_cdclk;

	/* Resolves the crtc. */
	crtc = to_intel_crtc(crtc_state->uapi.crtc);
	min_cdclk = 0;

	/* Without a registry there is no plane to walk. */
	if (takeover == NULL)
		return min_cdclk;

	/* Takes the need of the registry's plane of the pipe, if any. */
	plane = drv_i915_n1_plane_at(takeover, (unsigned)crtc->pipe);
	if (plane != NULL)
		min_cdclk = max(crtc_state->min_cdclk[plane->id], min_cdclk);

	/* Succeeded: reports the planes' need. */
	return min_cdclk;
}

/* Powers a combo PLL on (icl_pll_power_enable()). */
static void
i915_icl_pll_power_enable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll,
	i915_reg_t enable_reg)
{
	int wait_result;

	/* Requests the PLL power. */
	(void)i915_lcd_intel_de_rmw(i915, enable_reg, 0, PLL_POWER_ENABLE);

	/* The spec says to "wait" but also that it should be immediate. */
	wait_result = i915_lcd_wait(i915, enable_reg, PLL_POWER_STATE, PLL_POWER_STATE, 1);
	if (wait_result != 0) {
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d Power not enabled\n",
			pll->info->id);
	}
}

/* Enables a combo PLL and waits for its lock (icl_pll_enable()). */
static void
i915_icl_pll_enable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll,
	i915_reg_t enable_reg)
{
	int wait_result;

	/* Sets the enable. */
	(void)i915_lcd_intel_de_rmw(i915, enable_reg, 0, PLL_ENABLE);

	/* The timeout is actually 600 us. */
	wait_result = i915_lcd_wait(i915, enable_reg, PLL_LOCK, PLL_LOCK, 1);
	if (wait_result != 0)
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d not locked\n", pll->info->id);
}

/* Turns a shared DPLL on with its power domain (_intel_enable_shared_dpll()). */
static void
i915_intel_enable_shared_dpll(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	/* Takes the PLL's power domain, when it has one. */
	if (pll->info->power_domain)
		pll->wakeref = i915_lcd_intel_display_power_get(i915, pll->info->power_domain);

	/* Turns the PLL on. */
	pll->info->funcs->enable(i915, pll);
	pll->on = true;
}

/* The body of intel_enable_shared_dpll() under the DPLL lock. */
static void
i915_enable_shared_dpll_locked(
	struct drm_i915_private *i915,
	struct intel_crtc *crtc,
	struct intel_shared_dpll *pll,
	unsigned int pipe_mask)
{
	unsigned int old_mask;

	/* The pipes that drive the PLL before this one joins. */
	old_mask = pll->active_mask;

	/* A crtc not reserved on the PLL is a warning of the Linux text. */
	if (!(pll->state.pipe_mask & pipe_mask)) {
		drv_i915_lcd_error("WARN_ON(!(pll->state.pipe_mask & pipe_mask))\n");
		return;
	}

	/* A crtc already active on the PLL is a warning of the Linux text. */
	if (pll->active_mask & pipe_mask) {
		drv_i915_lcd_error("WARN_ON(pll->active_mask & pipe_mask)\n");
		return;
	}

	/* The active mask names the pipes that drive the PLL now. */
	pll->active_mask |= pipe_mask;

	/* Notes the enable. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
		"enable %s (active 0x%x, on? %d) for [CRTC:%d:%s]\n",
		pll->info->name, pll->active_mask, pll->on,
		crtc->base.base.id, crtc->base.name);

	/* Another pipe already runs the PLL: it must be on. */
	if (old_mask) {
		if (!pll->on)
			drv_i915_lcd_error("WARN_ON(!pll->on)\n");
		assert_shared_dpll_enabled(i915, pll);
		return;
	}

	/* The first pipe finds the PLL off. */
	if (pll->on)
		drv_i915_lcd_error("WARN_ON(pll->on)\n");

	/* Turns it on. */
	I915_LCD_DRM_DBG_KMS(&i915->drm, "enabling %s\n", pll->info->name);
	i915_intel_enable_shared_dpll(i915, pll);
}

/* Turns a shared DPLL off and gives its power domain back (_intel_disable_shared_dpll()). */
static void
i915_intel_disable_shared_dpll(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	/* Turns the PLL off. */
	pll->info->funcs->disable(i915, pll);
	pll->on = false;

	/* Gives the PLL's power domain back, when it has one. */
	if (pll->info->power_domain)
		i915_lcd_intel_display_power_put(i915, pll->info->power_domain, pll->wakeref);
}

/* The body of intel_disable_shared_dpll() under the DPLL lock. */
static void
i915_disable_shared_dpll_locked(
	struct drm_i915_private *i915,
	struct intel_crtc *crtc,
	struct intel_shared_dpll *pll,
	unsigned int pipe_mask)
{
	/* drm_WARN: the crtc does not drive the PLL. */
	if (!(pll->active_mask & pipe_mask)) {
		drv_i915_lcd_error("%s not used by [CRTC:%d:%s]\n");
		return;
	}

	/* Notes the disable. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
		"disable %s (active 0x%x, on? %d) for [CRTC:%d:%s]\n",
		pll->info->name, pll->active_mask, pll->on,
		crtc->base.base.id, crtc->base.name);

	/* The PLL of an active pipe must be on. */
	assert_shared_dpll_enabled(i915, pll);
	if (!pll->on)
		drv_i915_lcd_error("WARN_ON(!pll->on)\n");

	/* The active mask names the pipes that drive the PLL now; the last one turns it off. */
	pll->active_mask &= ~pipe_mask;
	if (pll->active_mask)
		return;

	/* Turns it off. */
	I915_LCD_DRM_DBG_KMS(&i915->drm, "disabling %s\n", pll->info->name);
	i915_intel_disable_shared_dpll(i915, pll);
}

/* The mask of every DPLL id in the pool (intel_dpll_mask_all()). */
static unsigned long
i915_dpll_mask_all(
	struct drm_i915_private *i915)
{
	struct intel_shared_dpll *pll;
	unsigned long dpll_mask;
	int index;

	/* Collects every PLL's id (for_each_shared_dpll). */
	dpll_mask = 0;
	for (index = 0; index < i915->display.dpll.num_shared_dpll; index++) {
		pll = &i915->display.dpll.shared_dplls[index];

		/* Two PLLs with one id are a warning of the Linux text. */
		if ((dpll_mask & BIT(pll->info->id)) != 0u)
			drv_i915_lcd_error("WARN_ON(dpll_mask & BIT(pll->info->id))\n");

		dpll_mask |= BIT(pll->info->id);
	}

	/* Succeeded: reports the ids. */
	return dpll_mask;
}

/*
 * Finds the PLL of a mask that already carries a state, or else a free one
 * (intel_find_shared_dpll()); NULL when every PLL is taken.
 */
static struct intel_shared_dpll *
i915_find_shared_dpll(
	struct i915_lcd_world *world,
	struct intel_atomic_state *state,
	const struct intel_crtc *crtc,
	const struct intel_dpll_hw_state *pll_state,
	unsigned long dpll_mask)
{
	struct drm_i915_private *i915;
	unsigned long dpll_mask_all;
	struct intel_shared_dpll_state *shared_dpll;
	struct intel_shared_dpll *unused_pll;
	struct intel_shared_dpll *pll;
	int id;
	int id_limit;
	int differs;

	UNUSED_PARAMETER(state);

	/* Resolves the device, its PLL ids and the atomic state's PLL states (the world's pool). */
	i915 = i915_lcd_to_i915(crtc->base.dev);
	dpll_mask_all = i915_dpll_mask_all(i915);
	shared_dpll = drv_i915_lcd_shared_dpll_state(world);
	unused_pll = NULL;

	/* Asking for a PLL the device does not have is a warning of the Linux text. */
	if ((dpll_mask & ~dpll_mask_all) != 0u)
		drv_i915_lcd_error("WARN_ON(dpll_mask & ~dpll_mask_all)\n");

	/* Walks the ids of the mask (for_each_set_bit up to fls(dpll_mask_all)). */
	id_limit = fls(dpll_mask_all);
	for (id = 0; id < id_limit; id++) {
		if ((dpll_mask & (1ul << id)) == 0u)
			continue;

		/* Resolves the PLL of the id. */
		pll = drv_i915_get_shared_dpll_by_id(i915, (enum intel_dpll_id)id);
		if (pll == NULL)
			continue;

		/* Only enabled timings are checked first; the first unused PLL is remembered. */
		if (shared_dpll[pll->index].pipe_mask == 0) {
			if (unused_pll == NULL)
				unused_pll = pll;
			continue;
		}

		/* A PLL with the same state is shared. */
		differs = kern_memcmp(pll_state,
				 &shared_dpll[pll->index].hw_state,
				 sizeof(*pll_state));
		if (differs == 0) {
			I915_LCD_DRM_DBG_KMS(&i915->drm,
				"[CRTC:%d:%s] sharing existing %s (pipe mask 0x%x, active 0x%x)\n",
				crtc->base.base.id, crtc->base.name,
				pll->info->name,
				shared_dpll[pll->index].pipe_mask,
				pll->active_mask);
			return pll;
		}
	}

	/* No matching timings: a free PLL, if there is one. */
	if (unused_pll != NULL) {
		I915_LCD_DRM_DBG_KMS(&i915->drm, "[CRTC:%d:%s] allocated %s\n",
			crtc->base.base.id, crtc->base.name,
			unused_pll->info->name);
		return unused_pll;
	}

	/* Every PLL is taken. */
	return NULL;
}

/* Takes a crtc's reference on a DPLL (intel_reference_shared_dpll_crtc()). */
static void
i915_reference_shared_dpll_crtc(
	const struct intel_crtc *crtc,
	const struct intel_shared_dpll *pll,
	struct intel_shared_dpll_state *shared_dpll_state)
{
	/* A crtc that already holds a reference is a warning of the Linux text. */
	if ((shared_dpll_state->pipe_mask & BIT(crtc->pipe)) != 0)
		drv_i915_lcd_error("WARN_ON((shared_dpll_state->pipe_mask & BIT(crtc->pipe)) != 0)\n");

	/* The pipe mask names the crtcs that use the PLL. */
	shared_dpll_state->pipe_mask |= BIT(crtc->pipe);

	/* Notes the reservation (on the crtc's DRM device). */
	I915_LCD_DRM_DBG_KMS(crtc->base.dev, "[CRTC:%d:%s] reserving %s\n",
		crtc->base.base.id, crtc->base.name, pll->info->name);
}

/* Takes a crtc's reference on a PLL in the atomic state (intel_reference_shared_dpll()). */
static void
i915_reference_shared_dpll(
	struct i915_lcd_world *world,
	struct intel_atomic_state *state,
	const struct intel_crtc *crtc,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	struct intel_shared_dpll_state *shared_dpll;

	UNUSED_PARAMETER(state);

	/* The atomic state's PLL states are the world's pool. */
	shared_dpll = drv_i915_lcd_shared_dpll_state(world);

	/* The first user sets the PLL's state. */
	if (shared_dpll[pll->index].pipe_mask == 0)
		shared_dpll[pll->index].hw_state = *pll_state;

	/* References it for the crtc. */
	i915_reference_shared_dpll_crtc(crtc, pll, &shared_dpll[pll->index]);
}

/* Drops a crtc's reference on a PLL in the atomic state (intel_unreference_shared_dpll()). */
static void
i915_unreference_shared_dpll(
	struct i915_lcd_world *world,
	struct intel_atomic_state *state,
	const struct intel_crtc *crtc,
	const struct intel_shared_dpll *pll)
{
	struct intel_shared_dpll_state *shared_dpll;

	UNUSED_PARAMETER(state);

	/* The atomic state's PLL states are the world's pool. */
	shared_dpll = drv_i915_lcd_shared_dpll_state(world);

	/* Drops the crtc's reference. */
	drv_i915_unreference_shared_dpll_crtc(crtc, pll, &shared_dpll[pll->index]);
}

/*
 * Reads one PLL out and finds the crtcs that use it (readout_dpll_hw_state()).
 *
 * The Linux for_each_intel_crtc() of this text walked the takeover
 * registry.
 */
static void
i915_readout_dpll_hw_state(
	struct i915_takeover_world *takeover,
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	struct intel_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	unsigned index;

	/* Reads the PLL; an enabled one takes its power domain. */
	pll->on = drv_i915_dpll_get_hw_state(i915, pll, &pll->state.hw_state);
	if (pll->on && pll->info->power_domain)
		pll->wakeref = i915_lcd_intel_display_power_get(i915, pll->info->power_domain);

	/* References the PLL for every active crtc of the registry that uses it. */
	pll->state.pipe_mask = 0;
	for (index = 0u; ; index++) {
		crtc = drv_i915_n1_crtc_at(takeover, index);
		if (crtc == NULL)
			break;

		/* Only an active crtc on this PLL uses it. */
		crtc_state = to_intel_crtc_state(crtc->base.state);
		if (crtc_state->hw.active && crtc_state->shared_dpll == pll)
			i915_reference_shared_dpll_crtc(crtc, pll, &pll->state);
	}

	/* The pipes that use the PLL are the pipes that drive it. */
	pll->active_mask = pll->state.pipe_mask;

	/* Notes the readout. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
		"%s hw state readout: pipe_mask 0x%x, on %i\n",
		pll->info->name, pll->state.pipe_mask, pll->on);
}

/* Turns off a PLL that is on but used by no pipe (sanitize_dpll_state()). */
static void
i915_sanitize_dpll_state(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	/* A PLL that is off needs nothing. */
	if (!pll->on)
		return;

	/* A PLL a pipe drives stays on. */
	if (pll->active_mask)
		return;

	/* Turns it off. */
	I915_LCD_DRM_DBG_KMS(&i915->drm,
		"%s enabled but not in use, disabling\n",
		pll->info->name);
	i915_intel_disable_shared_dpll(i915, pll);
}

/* Binds the modeset object's device view to the world's pool (built on first use). */
static void
i915_lcd_dpll_pool_init(
	struct i915_lcd_world *world,
	struct i915_lcd_modeset *ms)
{
	/* Binds the pool. */
	drv_i915_lcd_dpll_pool_bind(world, &ms->i915);
}

/* The Type-C port of a Type-C PLL id (icl_pll_id_to_tc_port()). */
static enum tc_port
i915_icl_pll_id_to_tc_port(
	enum intel_dpll_id id)
{
	/* Succeeded: TC PLL 1 serves TC1, and the ids follow the ports. */
	return (enum tc_port)((int)TC_PORT_1 + (int)id - (int)DPLL_ID_ICL_MGPLL1);
}

/*
 * The enable register of a Type-C PLL (intel_tc_pll_enable_reg()):
 * Alder Lake-P's PORTTC PLL enables, every other platform's MG PLL
 * enables.
 */
static i915_reg_t
i915_intel_tc_pll_enable_reg(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	enum tc_port tc_port;
	bool is_alderlake_p;

	UNUSED_PARAMETER(i915);

	/* Finds the port the PLL serves. */
	tc_port = i915_icl_pll_id_to_tc_port(pll->info->id);

	/* Alder Lake-P moved the Type-C PLL enables. */
	is_alderlake_p = I915_LCD_IS_ALDERLAKE_P(i915);
	if (is_alderlake_p)
		return ADLP_PORTTC_PLL_ENABLE(tc_port);

	/* Succeeded: every other platform's MG PLL enable. */
	return MG_PLL_ENABLE(tc_port);
}

/* The CLKTOP2 high-speed divider field of a first divider (the div1 switch of icl_mg_pll_find_divisors()). */
static u32
i915_mg_hsdiv_of_div1(
	int div1)
{
	u32 hsdiv;

	/* Picks the field value of the divider; an unknown one is reported and taken as 2. */
	switch (div1) {
	case 3:
		hsdiv = MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_3;
		break;
	case 5:
		hsdiv = MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_5;
		break;
	case 7:
		hsdiv = MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_7;
		break;
	case 2:
		hsdiv = MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_2;
		break;
	default:
		I915_LCD_MISSING_CASE(div1);
		hsdiv = MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_2;
		break;
	}

	/* Succeeded: the field value. */
	return hsdiv;
}

/*
 * Finds the first and second dividers that put a link clock's DCO in the
 * PLL's range, and fills the clock top words (icl_mg_pll_find_divisors()).
 *
 * The DCO is div1 * div2 * clock * 5; DP needs exactly 8.1 GHz, HDMI any
 * DCO in its range.  The first fit in the order of the reference wins.
 * Returns 0, or I915_LCD_EINVAL when no pair fits.
 */
static int
i915_icl_mg_pll_find_divisors(
	int clock_khz,
	bool is_dp,
	bool use_ssc,
	u32 *target_dco_khz,
	struct intel_dpll_hw_state *state,
	bool is_dkl)
{
	static const u8 div1_vals[] = { 7, 5, 3, 2 };
	u32 dco_min_freq;
	u32 dco_max_freq;
	unsigned int i;
	unsigned int div1_count;
	int div1;
	int div2;
	int dco;
	int a_divratio;
	int tlinedrv;
	int inputsel;
	u32 hsdiv;

	/* The DCO range: DP's one frequency, HDMI's range (lower with SSC). */
	if (is_dp) {
		dco_min_freq = 8100000;
		dco_max_freq = 8100000;
	} else {
		dco_max_freq = 10000000;
		if (use_ssc) {
			dco_min_freq = 8000000;
		} else {
			dco_min_freq = 7992000;
		}
	}

	/* Tries every first divider, largest first, and every second divider, largest first. */
	div1_count = sizeof(div1_vals) / sizeof(div1_vals[0]);
	for (i = 0; i < div1_count; i++) {
		div1 = div1_vals[i];

		/* Tries the second dividers of this first divider. */
		for (div2 = 10; div2 > 0; div2--) {
			/* A pair whose DCO is out of range does not fit. */
			dco = div1 * div2 * clock_khz * 5;
			if (dco < (int)dco_min_freq || dco > (int)dco_max_freq)
				continue;

			/*
			 * The core clock divider and the line driver select.
			 * Note: a_divratio does not match the TGL BSpec
			 * algorithm, but matches the hard-coded values and
			 * works on the hardware for DP alt mode at least.
			 */
			if (div2 >= 2) {
				if (is_dp) {
					a_divratio = 10;
				} else {
					a_divratio = 5;
				}

				/* The DKL PHY's line driver select differs from the MG PHY's. */
				if (is_dkl) {
					tlinedrv = 1;
				} else {
					tlinedrv = 2;
				}
			} else {
				a_divratio = 5;
				tlinedrv = 0;
			}

			/* The core input select: DP's 0, HDMI's 1. */
			if (is_dp) {
				inputsel = 0;
			} else {
				inputsel = 1;
			}

			/* The high-speed divider field of the first divider. */
			hsdiv = i915_mg_hsdiv_of_div1(div1);

			/* Reports the DCO and fills the clock top words. */
			*target_dco_khz = (u32)dco;
			state->mg_refclkin_ctl = MG_REFCLKIN_CTL_OD_2_MUX(1);
			state->mg_clktop2_coreclkctl1 =
				MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO(a_divratio);
			state->mg_clktop2_hsclkctl =
				MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL(tlinedrv) |
				MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL(inputsel) |
				hsdiv |
				MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO(div2);

			/* Succeeded: the first pair that fits. */
			return 0;
		}
	}

	/* No pair puts the DCO in range. */
	return I915_LCD_EINVAL;
}

/*
 * Computes a Type-C PLL's words for a crtc state's link clock
 * (icl_calc_mg_pll_state()).
 *
 * The specification works in real numbers; the reference adapted it to
 * integer arithmetic, and so does this text, with the same rounding.
 * Display version 12 and later have the DKL PHY, whose words are computed
 * here; Ice Lake's MG PHY words are not ported and are reported.  Spread
 * spectrum is never used.  Returns 0, or I915_LCD_EINVAL.
 */
static int
i915_icl_calc_mg_pll_state(
	struct intel_crtc_state *crtc_state,
	struct intel_dpll_hw_state *pll_state)
{
	struct drm_i915_private *i915;
	int refclk_khz;
	int clock;
	u32 dco_khz;
	u32 m1div;
	u32 m2div_int;
	u32 m2div_rem;
	u32 m2div_frac;
	u32 iref_ndiv;
	u32 iref_trim;
	u32 prop_coeff;
	u32 int_coeff;
	u32 tdc_targetcnt;
	u32 feedfwgain;
	u64 ssc_stepsize;
	u64 ssc_steplen;
	u64 ssc_steplog;
	u64 tmp;
	bool use_ssc;
	bool is_dp;
	bool is_hdmi;
	bool is_dkl;
	int display_ver;
	int divisors_result;
	u8 afc_startup;

	/* Finds the device, its reference clock and the link clock. */
	i915 = i915_lcd_to_i915(crtc_state->uapi.crtc->dev);
	refclk_khz = i915->display.dpll.ref_clks.nssc;
	clock = crtc_state->port_clock;
	use_ssc = false;

	/* Anything but HDMI is DP. */
	is_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	is_dp = true;
	if (is_hdmi)
		is_dp = false;

	/* Display version 12 and later have the DKL PHY; Ice Lake's MG PHY is not ported. */
	display_ver = I915_LCD_DISPLAY_VER(i915);
	is_dkl = false;
	if (display_ver >= 12)
		is_dkl = true;
	if (!is_dkl) {
		drv_i915_lcd_error("UNPORTED: the MG PHY PLL state of display version 11 (icl_calc_mg_pll_state)\n");
		return I915_LCD_EINVAL;
	}

	/* Finds the dividers that put the DCO in range. */
	divisors_result = i915_icl_mg_pll_find_divisors(clock, is_dp, use_ssc, &dco_khz, pll_state, is_dkl);
	if (divisors_result != 0)
		return divisors_result;

	/*
	 * The feedback divider: the pre-divider m1 is 2 and the integer part
	 * must fit in 8 bits (the DKL PHY has no pre-divider of 4).
	 */
	m1div = 2;
	m2div_int = dco_khz / ((u32)refclk_khz * m1div);
	if (m2div_int > 255)
		return I915_LCD_EINVAL;

	/* The fractional part of the feedback divider, in 22 bits. */
	m2div_rem = dco_khz % ((u32)refclk_khz * m1div);
	tmp = (u64)m2div_rem * (1 << 22);
	tmp = tmp / ((u64)refclk_khz * m1div);
	m2div_frac = (u32)tmp;

	/* The current reference of each reference clock. */
	switch (refclk_khz) {
	case 19200:
		iref_ndiv = 1;
		iref_trim = 28;
		break;
	case 24000:
		iref_ndiv = 1;
		iref_trim = 25;
		break;
	case 38400:
		iref_ndiv = 2;
		iref_trim = 28;
		break;
	default:
		I915_LCD_MISSING_CASE(refclk_khz);
		return I915_LCD_EINVAL;
	}

	/*
	 * The TDC target count:
	 *   tdc_res = 0.000003
	 *   tdc_targetcnt = int(2 / (tdc_res * 8 * 50 * 1.1) / refclk_mhz + 0.5)
	 * with the reference clock in kHz (the factor 1000), 0.00132 written
	 * as 132 / 100000, and the 0.5 as 5 after a factor of 10 that the last
	 * division takes back.  The operations are ordered so that no early
	 * division multiplies a rounding error.
	 */
	tdc_targetcnt = (2 * 1000 * 100000 * 10 / (132 * (u32)refclk_khz) + 5) / 10;

	/*
	 * The feed-forward gain, used only with a fractional divider (or
	 * SSC).  The DCO is divided by 10 so the dividend fits in 32 bits;
	 * the division rounds down anyway.
	 */
	feedfwgain = 0;
	if (use_ssc || m2div_rem > 0)
		feedfwgain = m1div * 1000000 * 100 / (dco_khz * 3 / 10);

	/* The loop filter's coefficients: higher above a 9 GHz DCO. */
	if (dco_khz >= 9000000) {
		prop_coeff = 5;
		int_coeff = 10;
	} else {
		prop_coeff = 4;
		int_coeff = 8;
	}

	/* The spread spectrum's step size and length (none without SSC). */
	if (use_ssc) {
		tmp = (u64)dco_khz * (47 * 32);
		tmp = tmp / ((u64)refclk_khz * m1div * 10000);
		ssc_stepsize = tmp;
		tmp = (u64)dco_khz * 1000;
		ssc_steplen = DIV_ROUND_UP_ULL(tmp, 32 * 2 * 32);
	} else {
		ssc_stepsize = 0;
		ssc_steplen = 0;
	}

	/* The spread spectrum's step count is fixed. */
	ssc_steplog = 4;

	/* DIV0: the loop filter's coefficients and the feedback divider. */
	pll_state->mg_pll_div0 = DKL_PLL_DIV0_INTEG_COEFF(int_coeff) |
				 DKL_PLL_DIV0_PROP_COEFF(prop_coeff) |
				 DKL_PLL_DIV0_FBPREDIV(m1div) |
				 DKL_PLL_DIV0_FBDIV_INT(m2div_int);

	/* The VBT's AFC startup override, when it has one. */
	if (i915->display.vbt.override_afc_startup) {
		afc_startup = i915->display.vbt.override_afc_startup_val;
		pll_state->mg_pll_div0 |= DKL_PLL_DIV0_AFC_STARTUP(afc_startup);
	}

	/* DIV1: the current reference trim and the TDC target count. */
	pll_state->mg_pll_div1 = DKL_PLL_DIV1_IREF_TRIM(iref_trim) |
				 DKL_PLL_DIV1_TDC_TARGET_CNT(tdc_targetcnt);

	/* SSC: the reference divider and the spread spectrum's steps. */
	pll_state->mg_pll_ssc = DKL_PLL_SSC_IREF_NDIV_RATIO(iref_ndiv) |
				DKL_PLL_SSC_STEP_LEN(ssc_steplen) |
				DKL_PLL_SSC_STEP_NUM(ssc_steplog);
	if (use_ssc)
		pll_state->mg_pll_ssc |= DKL_PLL_SSC_EN;

	/* BIAS: the fractional divider, enabled when it is not zero. */
	pll_state->mg_pll_bias = DKL_PLL_BIAS_FBDIV_FRAC(m2div_frac);
	if (m2div_frac != 0)
		pll_state->mg_pll_bias |= DKL_PLL_BIAS_FRAC_EN_H;

	/* TDC_COLDST_BIAS: the SSC step size and the feed-forward gain. */
	pll_state->mg_pll_tdc_coldst_bias =
			DKL_PLL_TDC_SSC_STEP_SIZE(ssc_stepsize) |
			DKL_PLL_TDC_FEED_FWD_GAIN(feedfwgain);

	/* Succeeded: the words are filled. */
	return 0;
}

/*
 * Computes the link clock a Type-C PLL's words give, in kHz
 * (icl_ddi_mg_pll_get_freq()); 0 for an unknown high-speed divider, or for
 * Ice Lake's MG PHY words, which are not ported.
 */
static int
i915_icl_ddi_mg_pll_get_freq(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	u32 m1;
	u32 m2_int;
	u32 m2_frac;
	u32 div1;
	u32 div2;
	u32 ref_clock;
	u64 tmp;
	int display_ver;

	UNUSED_PARAMETER(pll);

	/* Finds the reference clock and the display version. */
	ref_clock = (u32)i915->display.dpll.ref_clks.nssc;
	display_ver = I915_LCD_DISPLAY_VER(i915);

	/* Ice Lake's MG PHY words are not ported. */
	if (display_ver < 12) {
		drv_i915_lcd_error("UNPORTED: the MG PHY PLL frequency of display version 11 (icl_ddi_mg_pll_get_freq)\n");
		return 0;
	}

	/* The feedback pre-divider and the integer part of the feedback divider. */
	m1 = pll_state->mg_pll_div0 & DKL_PLL_DIV0_FBPREDIV_MASK;
	m1 = m1 >> DKL_PLL_DIV0_FBPREDIV_SHIFT;
	m2_int = pll_state->mg_pll_div0 & DKL_PLL_DIV0_FBDIV_INT_MASK;

	/* The fractional part, when it is enabled. */
	m2_frac = 0;
	if ((pll_state->mg_pll_bias & DKL_PLL_BIAS_FRAC_EN_H) != 0) {
		m2_frac = pll_state->mg_pll_bias & DKL_PLL_BIAS_FBDIV_FRAC_MASK;
		m2_frac = m2_frac >> DKL_PLL_BIAS_FBDIV_SHIFT;
	}

	/* The first divider from the high-speed divider field. */
	switch (pll_state->mg_clktop2_hsclkctl & MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_MASK) {
	case MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_2:
		div1 = 2;
		break;
	case MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_3:
		div1 = 3;
		break;
	case MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_5:
		div1 = 5;
		break;
	case MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_7:
		div1 = 7;
		break;
	default:
		I915_LCD_MISSING_CASE(pll_state->mg_clktop2_hsclkctl);
		return 0;
	}

	/* The second divider; 0 is the same as 1, no division. */
	div2 = (pll_state->mg_clktop2_hsclkctl & MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_MASK) >>
		MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_SHIFT;
	if (div2 == 0)
		div2 = 1;

	/*
	 * The original formula, with the division by 2^22 delayed to keep the
	 * rounding error small.
	 */
	tmp = (u64)m1 * m2_int * ref_clock +
	      (((u64)m1 * m2_frac * ref_clock) >> 22);
	tmp = tmp / (5 * div1 * div2);

	/* Succeeded: the link clock. */
	return (int)tmp;
}

/*
 * The Thunderbolt PLL's frequency (icl_ddi_tbt_pll_get_freq()): it puts
 * out every rate at once and the DDI clock select picks one, so asking is
 * a warning of the reference, answered 0.
 */
static int
i915_icl_ddi_tbt_pll_get_freq(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	UNUSED_PARAMETER(i915);
	UNUSED_PARAMETER(pll);
	UNUSED_PARAMETER(pll_state);

	/* The reference warns: the rate is chosen at the DDI clock mux. */
	drv_i915_lcd_error("WARN_ON(1): icl_ddi_tbt_pll_get_freq\n");

	/* The PLL has no single frequency. */
	return 0;
}

/*
 * Reads a Type-C PLL's DKL words when the PLL is enabled
 * (dkl_pll_get_hw_state()); the display core is powered only if it already
 * is.
 *
 * Every word read here is behind the same bank index, though the words
 * are in different blocks of the PHY.  Each word keeps only the fields the
 * PLL programs.
 */
static bool
i915_dkl_pll_get_hw_state(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll,
	struct intel_dpll_hw_state *hw_state)
{
	enum tc_port tc_port;
	intel_wakeref_t wakeref;
	i915_reg_t enable_reg;
	u32 val;
	u32 div0_mask;

	/* Finds the port the PLL serves and its enable register. */
	tc_port = i915_icl_pll_id_to_tc_port(pll->info->id);
	enable_reg = i915_intel_tc_pll_enable_reg(i915, pll);

	/* A readout never turns the display core on. */
	wakeref = drv_i915_n1_power_get_if_enabled(i915, POWER_DOMAIN_DISPLAY_CORE);
	if (!wakeref)
		return false;

	/* A PLL that is off has no state to read. */
	val = i915_lcd_intel_de_read(i915, enable_reg);
	if ((val & PLL_ENABLE) == 0) {
		i915_lcd_intel_display_power_put(i915, POWER_DOMAIN_DISPLAY_CORE, wakeref);
		return false;
	}

	/* The reference clock input's mux. */
	hw_state->mg_refclkin_ctl = i915_lcd_dkl_phy_read(i915, tc_port, DKL_REFCLKIN_CTL);
	hw_state->mg_refclkin_ctl &= MG_REFCLKIN_CTL_OD_2_MUX_MASK;

	/* The high-speed clock's line driver, input and dividers. */
	hw_state->mg_clktop2_hsclkctl = i915_lcd_dkl_phy_read(i915, tc_port, DKL_CLKTOP2_HSCLKCTL);
	hw_state->mg_clktop2_hsclkctl &= MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL_MASK |
					 MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL_MASK |
					 MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_MASK |
					 MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_MASK;

	/* The core clock's divider. */
	hw_state->mg_clktop2_coreclkctl1 = i915_lcd_dkl_phy_read(i915, tc_port, DKL_CLKTOP2_CORECLKCTL1);
	hw_state->mg_clktop2_coreclkctl1 &= MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO_MASK;

	/* DIV0, with the AFC startup field only when the VBT overrides it. */
	div0_mask = DKL_PLL_DIV0_MASK;
	if (i915->display.vbt.override_afc_startup)
		div0_mask |= DKL_PLL_DIV0_AFC_STARTUP_MASK;
	hw_state->mg_pll_div0 = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_DIV0);
	hw_state->mg_pll_div0 &= div0_mask;

	/* DIV1: the current reference trim and the TDC target count. */
	hw_state->mg_pll_div1 = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_DIV1);
	hw_state->mg_pll_div1 &= DKL_PLL_DIV1_IREF_TRIM_MASK |
				 DKL_PLL_DIV1_TDC_TARGET_CNT_MASK;

	/* SSC: the reference divider and the spread spectrum. */
	hw_state->mg_pll_ssc = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_SSC);
	hw_state->mg_pll_ssc &= DKL_PLL_SSC_IREF_NDIV_RATIO_MASK |
				DKL_PLL_SSC_STEP_LEN_MASK |
				DKL_PLL_SSC_STEP_NUM_MASK |
				DKL_PLL_SSC_EN;

	/* BIAS: the fractional divider. */
	hw_state->mg_pll_bias = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_BIAS);
	hw_state->mg_pll_bias &= DKL_PLL_BIAS_FRAC_EN_H |
				 DKL_PLL_BIAS_FBDIV_FRAC_MASK;

	/* TDC_COLDST_BIAS: the SSC step size and the feed-forward gain. */
	hw_state->mg_pll_tdc_coldst_bias = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_TDC_COLDST_BIAS);
	hw_state->mg_pll_tdc_coldst_bias &= DKL_PLL_TDC_SSC_STEP_SIZE_MASK |
					    DKL_PLL_TDC_FEED_FWD_GAIN_MASK;

	/* Gives the display core back. */
	i915_lcd_intel_display_power_put(i915, POWER_DOMAIN_DISPLAY_CORE, wakeref);

	/* Succeeded: the PLL is enabled and its words are read. */
	return true;
}

/*
 * Writes a Type-C PLL's DKL words (dkl_pll_write()).
 *
 * Every word is changed read-modify-write, only in the fields the PLL
 * programs, and every one is behind the same bank index though in
 * different blocks of the PHY.  A read of the last word posts them.
 */
static void
i915_dkl_pll_write(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	struct intel_dpll_hw_state *hw_state;
	enum tc_port tc_port;
	u32 val;

	/* Finds the state to write and the port the PLL serves. */
	hw_state = &pll->state.hw_state;
	tc_port = i915_icl_pll_id_to_tc_port(pll->info->id);

	/* The reference clock input's mux. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_REFCLKIN_CTL);
	val &= ~MG_REFCLKIN_CTL_OD_2_MUX_MASK;
	val |= hw_state->mg_refclkin_ctl;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_REFCLKIN_CTL, val);

	/* The core clock's divider. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_CLKTOP2_CORECLKCTL1);
	val &= ~MG_CLKTOP2_CORECLKCTL1_A_DIVRATIO_MASK;
	val |= hw_state->mg_clktop2_coreclkctl1;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_CLKTOP2_CORECLKCTL1, val);

	/* The high-speed clock's line driver, input and dividers. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_CLKTOP2_HSCLKCTL);
	val &= ~(MG_CLKTOP2_HSCLKCTL_TLINEDRV_CLKSEL_MASK |
		 MG_CLKTOP2_HSCLKCTL_CORE_INPUTSEL_MASK |
		 MG_CLKTOP2_HSCLKCTL_HSDIV_RATIO_MASK |
		 MG_CLKTOP2_HSCLKCTL_DSDIV_RATIO_MASK);
	val |= hw_state->mg_clktop2_hsclkctl;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_CLKTOP2_HSCLKCTL, val);

	/* DIV0, with the AFC startup field only when the VBT overrides it. */
	val = DKL_PLL_DIV0_MASK;
	if (i915->display.vbt.override_afc_startup)
		val |= DKL_PLL_DIV0_AFC_STARTUP_MASK;
	i915_lcd_dkl_phy_rmw(i915, tc_port, DKL_PLL_DIV0, val, hw_state->mg_pll_div0);

	/* DIV1: the current reference trim and the TDC target count. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_DIV1);
	val &= ~(DKL_PLL_DIV1_IREF_TRIM_MASK |
		 DKL_PLL_DIV1_TDC_TARGET_CNT_MASK);
	val |= hw_state->mg_pll_div1;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_PLL_DIV1, val);

	/* SSC: the reference divider and the spread spectrum. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_SSC);
	val &= ~(DKL_PLL_SSC_IREF_NDIV_RATIO_MASK |
		 DKL_PLL_SSC_STEP_LEN_MASK |
		 DKL_PLL_SSC_STEP_NUM_MASK |
		 DKL_PLL_SSC_EN);
	val |= hw_state->mg_pll_ssc;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_PLL_SSC, val);

	/* BIAS: the fractional divider. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_BIAS);
	val &= ~(DKL_PLL_BIAS_FRAC_EN_H |
		 DKL_PLL_BIAS_FBDIV_FRAC_MASK);
	val |= hw_state->mg_pll_bias;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_PLL_BIAS, val);

	/* TDC_COLDST_BIAS: the SSC step size and the feed-forward gain. */
	val = i915_lcd_dkl_phy_read(i915, tc_port, DKL_PLL_TDC_COLDST_BIAS);
	val &= ~(DKL_PLL_TDC_SSC_STEP_SIZE_MASK |
		 DKL_PLL_TDC_FEED_FWD_GAIN_MASK);
	val |= hw_state->mg_pll_tdc_coldst_bias;
	i915_lcd_dkl_phy_write(i915, tc_port, DKL_PLL_TDC_COLDST_BIAS, val);

	/* Posts the writes. */
	i915_lcd_dkl_phy_posting_read(i915, tc_port, DKL_PLL_TDC_COLDST_BIAS);
}

/*
 * Turns a Type-C PLL on (mg_pll_enable() with the DKL write of display
 * version 12 and later; Ice Lake's MG PHY write is not ported and is
 * reported).
 */
static void
i915_mg_pll_enable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	i915_reg_t enable_reg;
	int display_ver;

	/* Powers the PLL. */
	enable_reg = i915_intel_tc_pll_enable_reg(i915, pll);
	i915_icl_pll_power_enable(i915, pll, enable_reg);

	/* Writes its configuration (the DKL PHY's; Ice Lake's MG PHY is not ported). */
	display_ver = I915_LCD_DISPLAY_VER(i915);
	if (display_ver >= 12) {
		i915_dkl_pll_write(i915, pll);
	} else {
		drv_i915_lcd_error("UNPORTED: the MG PHY PLL write of display version 11 (icl_mg_pll_write)\n");
	}

	/*
	 * The DVFS pre sequence would be here; the CDCLK paths already set the
	 * voltage.
	 */

	/* Enables it and waits for the lock (an unlocked PLL is logged). */
	i915_icl_pll_enable(i915, pll, enable_reg);

	/* The DVFS post sequence would be here (see above). */
}

/*
 * The register window the port PLLs and the DDI clock map are reached
 * through: the backend hooks of the device that runs the modeset, so the
 * xd clock's writes go where the rest of the modeset's go (the hardware,
 * or a host test's model).  Every user holds the device's DPLL lock or
 * runs in the display's start, before a modeset can.
 */
static struct i915_xd_io i915_dpll_io;
static int i915_dpll_io_ready;

/* Points the window at a device's backend (prepared on first use) and returns it. */
static struct i915_xd_io *
i915_dpll_window(
	struct drm_i915_private *i915)
{
	struct i915_xd_io_hooks hooks;

	/* Prepares the window once. */
	if (i915_dpll_io_ready == 0) {
		drv_i915_xd_io_init(&i915_dpll_io, NULL, NULL);
		i915_dpll_io_ready = 1;
	}

	/* Points it at this device's backend. */
	hooks.read = i915_dpll_window_read;
	hooks.write = i915_dpll_window_write;
	hooks.wait = i915_dpll_window_wait;
	hooks.lock = NULL;
	hooks.context = i915;
	drv_i915_xd_io_set_hooks(&i915_dpll_io, &hooks);

	/* Succeeded: the window. */
	return &i915_dpll_io;
}

/* Reads a register through the device's backend; the recorder, which cannot read, answers 0. */
static uint32_t
i915_dpll_window_read(
	void *context,
	uint32_t offset)
{
	struct drm_i915_private *i915;

	/* The pure word recorder has no read hook. */
	i915 = context;
	if (i915->emit->read32 == NULL)
		return 0U;

	/* Succeeded: the register's value. */
	return i915->emit->read32(i915->emit->ctx, offset);
}

/* Writes a register through the device's backend. */
static void
i915_dpll_window_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	struct drm_i915_private *i915;

	/* Writes the register. */
	i915 = context;
	i915->emit->write32(i915->emit->ctx, offset, value);
}

/* Lets time pass through the device's backend, when it keeps time. */
static void
i915_dpll_window_wait(
	void *context,
	unsigned microseconds)
{
	struct drm_i915_private *i915;

	/* A backend without a clock lets no time pass. */
	i915 = context;
	if (i915->emit->usleep == NULL)
		return;

	/* Sleeps. */
	i915->emit->usleep(i915->emit->ctx, microseconds);
}

/*
 * Reports the xd description of a pool PLL: the platform's PLL whose
 * select value is the PLL's id.
 */
static const struct i915_xd_pll_desc *
i915_dpll_xd_desc(
	const struct intel_shared_dpll *pll)
{
	/* The ids of the pool are the platform's select values. */
	return drv_i915_xd_dpll_desc(drv_i915_lcd_xd_platform(), (unsigned)pll->info->id);
}

/* Reports the VBT's AFC startup override of a device, or I915_XD_DPLL_AFC_NONE. */
static int
i915_dpll_afc(
	const struct drm_i915_private *i915)
{
	/* No override. */
	if (!i915->display.vbt.override_afc_startup)
		return I915_XD_DPLL_AFC_NONE;

	/* Succeeded: the VBT's value. */
	return (int)i915->display.vbt.override_afc_startup_val;
}

/* Reports the waits of a PLL's switch that ran out, as the modeset's errors. */
static void
i915_dpll_report(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	unsigned late)
{
	UNUSED_PARAMETER(i915);

	/* One error per wait. */
	if ((late & I915_XD_DPLL_LATE_POWER_ON) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d Power not enabled\n", pll->info->id);
	if ((late & I915_XD_DPLL_LATE_LOCK) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d not locked\n", pll->info->id);
	if ((late & I915_XD_DPLL_LATE_UNLOCK) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d locked\n", pll->info->id);
	if ((late & I915_XD_DPLL_LATE_POWER_OFF) != 0U)
		I915_LCD_DRM_ERR(&i915->drm, "PLL %d Power not disabled\n", pll->info->id);
}

/*
 * Reads a combo PLL or the Thunderbolt PLL through the xd clock when it
 * is enabled; the display core is powered only if it already is.
 */
static bool
i915_combo_pll_get_hw_state(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll,
	struct intel_dpll_hw_state *hw_state)
{
	const struct i915_xd_pll_desc *desc;
	struct i915_xd_dpll_words words;
	intel_wakeref_t wakeref;
	int enabled;

	/* A PLL the platform does not have is off. */
	desc = i915_dpll_xd_desc(pll);
	if (desc == NULL)
		return false;

	/* A readout never turns the display core on. */
	wakeref = drv_i915_n1_power_get_if_enabled(i915, POWER_DOMAIN_DISPLAY_CORE);
	if (!wakeref)
		return false;

	/* Reads the PLL; the state keeps what it held where a word is not read. */
	words.cfgcr0 = hw_state->cfgcr0;
	words.cfgcr1 = hw_state->cfgcr1;
	words.div0 = hw_state->div0;
	words.div0_set = 0;
	enabled = drv_i915_xd_dpll_read(i915_dpll_window(i915), drv_i915_lcd_xd_platform(), desc, i915_dpll_afc(i915), &words);
	if (enabled) {
		hw_state->cfgcr0 = words.cfgcr0;
		hw_state->cfgcr1 = words.cfgcr1;
		hw_state->div0 = words.div0;
	}

	/* Gives the display core back. */
	i915_lcd_intel_display_power_put(i915, POWER_DOMAIN_DISPLAY_CORE, wakeref);

	/* Succeeded: whether the PLL is enabled. */
	return enabled != 0;
}

/* Reports a combo PLL state's link clock through the xd clock, or 0. */
static int
i915_combo_pll_get_freq(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	struct i915_xd_dpll_words words;
	uint32_t port_khz;

	UNUSED_PARAMETER(pll);

	/* The link clock of the words. */
	words.cfgcr0 = pll_state->cfgcr0;
	words.cfgcr1 = pll_state->cfgcr1;
	words.div0 = pll_state->div0;
	words.div0_set = 0;
	port_khz = drv_i915_xd_dpll_port_khz(drv_i915_lcd_xd_platform(), (uint32_t)i915->display.dpll.ref_clks.nssc, &words);

	/* A divider field without a divider is an error of the state. */
	if (port_khz == 0U)
		drv_i915_lcd_error("WARN_ON(p0 == 0 || p1 == 0 || p2 == 0)\n");

	/* Succeeded: the link clock. */
	return (int)port_khz;
}

/* Turns a combo PLL or the Thunderbolt PLL on with its state's words through the xd clock. */
static void
i915_combo_pll_enable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	const struct i915_xd_pll_desc *desc;
	struct i915_xd_dpll_words words;
	unsigned late;

	/* A PLL the platform does not have cannot be turned on. */
	desc = i915_dpll_xd_desc(pll);
	if (desc == NULL) {
		drv_i915_lcd_error("enable of a PLL the platform does not have\n");
		return;
	}

	/* The state's words; the AFC startup is written only with the VBT's override. */
	words.cfgcr0 = pll->state.hw_state.cfgcr0;
	words.cfgcr1 = pll->state.hw_state.cfgcr1;
	words.div0 = pll->state.hw_state.div0;
	words.div0_set = i915_dpll_afc(i915) != I915_XD_DPLL_AFC_NONE;

	/* Turns it on and reports the waits that ran out. */
	late = drv_i915_xd_dpll_enable(i915_dpll_window(i915), drv_i915_lcd_xd_platform(), desc, &words);
	i915_dpll_report(i915, pll, late);
}

/* Turns a port PLL of any kind off through the xd clock. */
static void
i915_port_pll_disable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	const struct i915_xd_pll_desc *desc;
	unsigned late;

	/* A PLL the platform does not have is off. */
	desc = i915_dpll_xd_desc(pll);
	if (desc == NULL)
		return;

	/* Turns it off and reports the waits that ran out. */
	late = drv_i915_xd_dpll_disable(i915_dpll_window(i915), desc);
	i915_dpll_report(i915, pll, late);
}

/*
 * Maps a combo PHY's DDI clock to a pool PLL through the xd clock: the
 * select, then the ungate.  The caller holds the device's DPLL lock.
 */
void
drv_i915_dpll_map_ddi(
	struct drm_i915_private *i915,
	unsigned phy,
	const struct intel_shared_dpll *pll)
{
	/* The PLL's id is the platform's select value. */
	drv_i915_xd_dpll_map(i915_dpll_window(i915), drv_i915_lcd_xd_platform(), phy, (unsigned)pll->info->id);
}

/* Gates a combo PHY's DDI clock through the xd clock.  The caller holds the device's DPLL lock. */
void
drv_i915_dpll_unmap_ddi(
	struct drm_i915_private *i915,
	unsigned phy)
{
	/* Sets the clock-off bit. */
	drv_i915_xd_dpll_unmap(i915_dpll_window(i915), drv_i915_lcd_xd_platform(), phy);
}

/*
 * Reads a combo PHY's DDI clock through the xd clock: the PLL id it
 * selects, and returns 1 when the clock is ungated.
 */
int
drv_i915_dpll_ddi_mapped(
	struct drm_i915_private *i915,
	unsigned phy,
	unsigned *pll_id)
{
	/* Reads the map. */
	return drv_i915_xd_dpll_mapped(i915_dpll_window(i915), drv_i915_lcd_xd_platform(), phy, pll_id);
}
