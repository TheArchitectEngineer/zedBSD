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
 * readout and sanitize); every PLL's words, enable, disable and read, and
 * the DDI clock map, are the xd clock's (xd-clock.c), reached through the
 * modeset's register window (drv_i915_lcd_xd_io()) and, for a Type-C
 * PLL's words in its PHY, the modeset's Dekel PHY route
 * (drv_i915_lcd_xd_dkl()).
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
static const struct i915_xd_pll_desc *i915_dpll_xd_desc(const struct intel_shared_dpll *pll);
static int i915_dpll_afc(const struct drm_i915_private *i915);
static void i915_dpll_report(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, unsigned late);
static void i915_port_pll_disable(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void i915_tc_pll_words_of_state(const struct intel_dpll_hw_state *hw_state, int afc_set, struct i915_xd_tc_pll_words *words);
static int i915_tc_pll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
static void i915_tc_pll_enable(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static uint32_t i915_lcd_xd_dkl_read(void *context, unsigned tc, uint32_t phy_offset);
static void i915_lcd_xd_dkl_write(void *context, unsigned tc, uint32_t phy_offset, uint32_t value);
static void i915_lcd_xd_dkl_rmw(void *context, unsigned tc, uint32_t phy_offset, uint32_t clear, uint32_t set);

/*
 * The hooks of a Type-C port's DKL PLL (the reference's dkl_pll_funcs,
 * TC PLL 1..4 of Alder Lake-P and Tiger Lake).  Constant and shared by
 * every Type-C PLL of every device.
 */
static const struct intel_shared_dpll_funcs i915_dkl_pll_funcs = {
	.get_freq = i915_tc_pll_get_freq,
	.enable = i915_tc_pll_enable,
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
 * The Dekel PHY windows the modeset's Type-C ports are reached through:
 * the xd PHY accesses routed to the device's own (its hooks take the lock
 * the ports share, or a host test's model takes the bank index and window
 * accesses as register writes).  Pointed at the device on every call;
 * used under the modeset's locks.
 */
static struct i915_xd_dkl i915_lcd_xd_dkl_route;

/* Points the modeset's Dekel PHY route at a device and returns it. */
struct i915_xd_dkl *
drv_i915_lcd_xd_dkl(
	struct drm_i915_private *i915)
{
	struct i915_xd_dkl_hooks hooks;

	/* Every access goes to the device's hooks. */
	hooks.read = i915_lcd_xd_dkl_read;
	hooks.write = i915_lcd_xd_dkl_write;
	hooks.rmw = i915_lcd_xd_dkl_rmw;
	hooks.context = i915;
	drv_i915_xd_dkl_set_hooks(&i915_lcd_xd_dkl_route, &hooks);

	/* Succeeded: the route. */
	return &i915_lcd_xd_dkl_route;
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
	late = drv_i915_xd_dpll_disable(drv_i915_lcd_xd_io(i915), desc);
	i915_dpll_report(i915, pll, late);
}

/* Copies the older code's PLL state of a Type-C PLL into the xd words. */
static void
i915_tc_pll_words_of_state(
	const struct intel_dpll_hw_state *hw_state,
	int afc_set,
	struct i915_xd_tc_pll_words *words)
{
	/* One word per register, and whether DIV0 carries the AFC startup. */
	words->refclkin = hw_state->mg_refclkin_ctl;
	words->coreclk = hw_state->mg_clktop2_coreclkctl1;
	words->hsclk = hw_state->mg_clktop2_hsclkctl;
	words->div0 = hw_state->mg_pll_div0;
	words->div1 = hw_state->mg_pll_div1;
	words->ssc = hw_state->mg_pll_ssc;
	words->bias = hw_state->mg_pll_bias;
	words->tdc = hw_state->mg_pll_tdc_coldst_bias;
	words->afc_set = afc_set;
}

/* Reports a Type-C PLL state's link clock through the xd clock, or 0. */
static int
i915_tc_pll_get_freq(
	struct drm_i915_private *i915,
	const struct intel_shared_dpll *pll,
	const struct intel_dpll_hw_state *pll_state)
{
	struct i915_xd_tc_pll_words words;
	uint32_t port_khz;

	UNUSED_PARAMETER(pll);

	/* The link clock of the words. */
	i915_tc_pll_words_of_state(pll_state, 0, &words);
	port_khz = drv_i915_xd_tc_pll_port_khz((uint32_t)i915->display.dpll.ref_clks.nssc, &words);

	/* Succeeded: the link clock (0 for a divider the PHY does not have). */
	return (int)port_khz;
}

/* Turns a Type-C PLL on with its state's words through the xd clock and the modeset's PHY route. */
static void
i915_tc_pll_enable(
	struct drm_i915_private *i915,
	struct intel_shared_dpll *pll)
{
	const struct i915_xd_pll_desc *desc;
	struct i915_xd_tc_pll_words words;
	unsigned late;
	int afc;
	int afc_set;

	/* A PLL the platform does not have cannot be turned on. */
	desc = i915_dpll_xd_desc(pll);
	if (desc == NULL) {
		drv_i915_lcd_error("enable of a PLL the platform does not have\n");
		return;
	}

	/* The state's words; the AFC startup is written only with the VBT's override. */
	afc = i915_dpll_afc(i915);
	afc_set = 0;
	if (afc != I915_XD_DPLL_AFC_NONE)
		afc_set = 1;
	i915_tc_pll_words_of_state(&pll->state.hw_state, afc_set, &words);

	/* Turns it on and reports the waits that ran out. */
	late = drv_i915_xd_tc_pll_enable(drv_i915_lcd_xd_io(i915), drv_i915_lcd_xd_dkl(i915), desc, &words);
	i915_dpll_report(i915, pll, late);
}

/* Reads a Type-C port's PHY register through the device's modeset hooks. */
static uint32_t
i915_lcd_xd_dkl_read(
	void *context,
	unsigned tc,
	uint32_t phy_offset)
{
	uint32_t value;

	/* The device reads it whole. */
	value = i915_lcd_dkl_phy_read(context, (enum tc_port)tc, phy_offset);

	/* Succeeded: the register's value. */
	return value;
}

/* Writes a Type-C port's PHY register through the device's modeset hooks. */
static void
i915_lcd_xd_dkl_write(
	void *context,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t value)
{
	/* The device writes it whole. */
	i915_lcd_dkl_phy_write(context, (enum tc_port)tc, phy_offset, value);
}

/* Changes bits of a Type-C port's PHY register through the device's modeset hooks. */
static void
i915_lcd_xd_dkl_rmw(
	void *context,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t clear,
	uint32_t set)
{
	/* The device changes it whole. */
	i915_lcd_dkl_phy_rmw(context, (enum tc_port)tc, phy_offset, clear, set);
}
