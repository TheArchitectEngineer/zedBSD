/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The eDP panel power sequencer (PPS) of the DP environment.
 *
 * The functions keep the Linux v6.8.12 drivers/gpu/drm/i915/display/
 * intel_pps.c (sha256 97f12f42d1169d6e2666596ed190800bc116d5bf52c1c47c04dd99b45257af83)
 * entry points and their bookkeeping in this tree's style: the PPS lock and
 * the display core reference it takes, the AUX power reference VDD holds,
 * who wants VDD, the delayed VDD-off and its worker, the times the waits
 * count from, the sequencer's choice and the messages.  The register work
 * (the sequencer's status and control, the delays, VDD, power on and off,
 * the backlight enable and the waits on the status) is the xd panel's
 * (xd-panel.c), reached through the environment's register window.
 *
 * The messages of the Linux text are reported through the DP environment's
 * message macros (dp-internal.h); their arguments are evaluated only when
 * the message is printed.
 *
 * The Linux original is under the MIT licence:
 *
 * Copyright (C) 2020 Intel Corporation
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

#include "dp-internal.h"
#include "dp-sink.h"
#include "panel.h"
#include "xd-io.h"
#include "xd-panel.h"
#include "../intel/display-regs-xd-south.h"
#include <kern/kcrt.h>

/* The VBT's power cycle the T12 quirk raises to, in 100 us units (1.3 s). */
#define I915_PPS_QUIRK_T12	13000U

static const char *i915_pps_name(struct drm_i915_private *i915, struct intel_pps *pps);
static struct i915_xd_io *i915_pps_io(struct drm_i915_private *i915);
static unsigned i915_pps_index(struct intel_dp *intel_dp);
static int i915_num_pps(struct drm_i915_private *i915);
static int i915_pps_muxed(struct drm_i915_private *i915);
static bool i915_pps_initial_setup(struct intel_dp *intel_dp);
static bool i915_edp_have_panel_power(struct intel_dp *intel_dp);
static bool i915_edp_have_panel_vdd(struct intel_dp *intel_dp);
static void i915_pps_note_wait(struct intel_dp *intel_dp);
static void i915_pps_report_timeout(struct intel_dp *intel_dp, int error);
static void i915_wait_panel_power_cycle(struct intel_dp *intel_dp);
static void i915_wait_backlight_on(struct intel_dp *intel_dp);
static void i915_edp_wait_backlight_off(struct intel_dp *intel_dp);
static void i915_pps_vdd_off_sync_unlocked(struct intel_dp *intel_dp);
static void i915_edp_panel_vdd_work(struct work_struct *work);
static void i915_edp_panel_vdd_schedule_off(struct intel_dp *intel_dp);
static void i915_pps_vdd_init(struct intel_dp *intel_dp);
static void i915_pps_init_timestamps(struct intel_dp *intel_dp);
static void i915_pps_to_xd(const struct edp_power_seq *seq, struct i915_xd_pps_delays *delays);
static void i915_pps_from_xd(const struct i915_xd_pps_delays *delays, struct edp_power_seq *seq);
static void i915_pps_dump_state(struct intel_dp *intel_dp, const char *state_name, const struct edp_power_seq *seq);
static void i915_pps_verify_state(struct intel_dp *intel_dp);
static void i915_pps_init_delays(struct intel_dp *intel_dp);
static void i915_pps_init_registers(struct intel_dp *intel_dp);
static void i915_pps_init_late(struct intel_dp *intel_dp);

/*
 * Takes the PPS lock (the Linux intel_pps_lock()).
 *
 * The DISPLAY_CORE power reference comes first, then the PPS mutex; the
 * reference is returned for drv_i915_pps_unlock().  The environment never
 * reports a reference of 0, so the body a Linux with_intel_pps_lock() block
 * guards always runs once between the lock and the unlock.
 */
intel_wakeref_t
drv_i915_pps_lock(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	intel_wakeref_t wakeref;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	/*
	 * Holds the display core powered while the sequencer is used (see the
	 * Linux intel_pps_reset_all() for why), then serializes the sequencer.
	 */
	wakeref = i915_dp_intel_display_power_get(dev_priv, POWER_DOMAIN_DISPLAY_CORE);
	I915_DP_MUTEX_LOCK(&dev_priv->display.pps.mutex);

	/* Succeeded: the caller holds the lock and the reference. */
	return wakeref;
}

/*
 * Drops the PPS lock (the Linux intel_pps_unlock()).
 *
 * The mutex goes first, then the DISPLAY_CORE reference the lock took.  It
 * returns 0, which ends a Linux with_intel_pps_lock() block.
 */
intel_wakeref_t
drv_i915_pps_unlock(
	struct intel_dp *intel_dp,
	intel_wakeref_t wakeref)
{
	struct drm_i915_private *dev_priv;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	/* Releases the sequencer, then the display core reference. */
	I915_DP_MUTEX_UNLOCK(&dev_priv->display.pps.mutex);
	i915_dp_intel_display_power_put(dev_priv, POWER_DOMAIN_DISPLAY_CORE, wakeref);

	/* Succeeded: no reference is left to the caller. */
	return 0;
}

/*
 * Warns when an eDP panel has neither power nor VDD during an AUX transfer
 * (the Linux intel_pps_check_power_unlocked()).
 *
 * The caller holds the PPS lock.
 */
void
drv_i915_pps_check_power_unlocked(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct i915_xd_io *io;
	unsigned pps;
	bool powered;
	bool vdd;
	bool is_edp;

	/* Finds the device and the port. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* A panel with power needs nothing more. */
	powered = i915_edp_have_panel_power(intel_dp);
	if (powered)
		return;

	/* A panel with VDD forced on can talk AUX too. */
	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (vdd)
		return;

	/* Reports the powered-off panel and the sequencer's state. */
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);
	(void)I915_DP_DRM_WARN(&dev_priv->drm, 1,
			       "[ENCODER:%d:%s] %s powered off while attempting AUX CH communication.\n",
			       dig_port->base.base.base.id, dig_port->base.base.name,
			       i915_pps_name(dev_priv, &intel_dp->pps));
	I915_DP_DRM_DBG_KMS(&dev_priv->drm,
			    "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps),
			    drv_i915_xd_io_read(io, I915_XD_PP_STATUS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)));
}

/*
 * Waits out the panel power cycle under the PPS lock (the Linux
 * intel_pps_wait_power_cycle()).
 */
void
drv_i915_pps_wait_power_cycle(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool is_edp;

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Waits for the cycle with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	i915_wait_panel_power_cycle(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Forces VDD on for an access to the sink (the Linux
 * intel_pps_vdd_on_unlocked()).
 *
 * It must be paired with drv_i915_pps_vdd_off_unlocked() and the caller
 * holds the PPS lock around the whole on/off sequence; it nests with
 * drv_i915_pps_vdd_on() and drv_i915_pps_vdd_off_sync().  It reports whether
 * this call is the one that must turn VDD off again.
 */
bool
drv_i915_pps_vdd_on_unlocked(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct i915_xd_io *io;
	unsigned pps;
	bool need_to_disable;
	bool vdd;
	bool powered;
	bool is_edp;
	int powered_now;

	/* Finds the device and the port, and whether nobody wanted VDD yet. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	need_to_disable = !intel_dp->pps.want_panel_vdd;

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return false;

	/*
	 * A pending delayed VDD-off must not undo this request; want_panel_vdd
	 * tells the delayed worker and the other paths that VDD is in use.
	 */
	(void)cancel_delayed_work(&intel_dp->pps.panel_vdd_work);
	intel_dp->pps.want_panel_vdd = true;

	/* VDD already forced on: only the bookkeeping changes. */
	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (vdd)
		return need_to_disable;

	/* The forced VDD holds the AUX power domain for as long as it is on. */
	(void)I915_DP_DRM_WARN_ON(&dev_priv->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = i915_dp_intel_display_power_get(dev_priv, i915_aux_power_domain(dig_port));

	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s turning VDD on\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps));

	/* An unpowered panel must finish its power cycle before VDD comes on. */
	powered = i915_edp_have_panel_power(intel_dp);
	if (!powered)
		i915_wait_panel_power_cycle(intel_dp);

	/* Forces VDD on. */
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);
	powered_now = drv_i915_xd_pps_vdd_on(io, pps);
	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps),
			    drv_i915_xd_io_read(io, I915_XD_PP_STATUS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)));

	/* If the panel was not on, gives it the power-up delay before the AUX channel is used. */
	if (powered_now == 0) {
		I915_DP_DRM_DBG_KMS(&dev_priv->drm,
				    "[ENCODER:%d:%s] %s panel power wasn't enabled\n",
				    dig_port->base.base.base.id, dig_port->base.base.name,
				    i915_pps_name(dev_priv, &intel_dp->pps));
		i915_dp_msleep(intel_dp->pps.panel_power_up_delay);
	}

	/* Succeeded: reports whether this call must turn VDD off again. */
	return need_to_disable;
}

/*
 * Forces VDD on (the Linux intel_pps_vdd_on()).
 *
 * It must be paired with drv_i915_pps_off().  Nested calls are not allowed,
 * because the lock is dropped in between; the caller prevents them.
 */
void
drv_i915_pps_vdd_on(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915;
	intel_wakeref_t wakeref;
	bool vdd;
	bool is_edp;

	/* Finds the device the port belongs to. */
	i915 = i915_dp_dp_to_i915(intel_dp);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Forces VDD on with the sequencer held. */
	vdd = false;
	wakeref = drv_i915_pps_lock(intel_dp);

	vdd = drv_i915_pps_vdd_on_unlocked(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);

	/* Reports a VDD that someone had already requested. */
	(void)I915_STATE_WARN(i915, !vdd, "[ENCODER:%d:%s] %s VDD already requested on\n",
			      i915_dp_dp_to_dig_port(intel_dp)->base.base.base.id,
			      i915_dp_dp_to_dig_port(intel_dp)->base.base.name,
			      i915_pps_name(i915, &intel_dp->pps));
}

/*
 * Turns VDD off at once, cancelling a delayed VDD-off (the Linux
 * intel_pps_vdd_off_sync()).
 *
 * The delayed worker is cancelled before the PPS lock is taken, because the
 * worker takes that lock itself.
 */
void
drv_i915_pps_vdd_off_sync(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool is_edp;

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Stops the delayed VDD-off and waits for a running one. */
	(void)i915_dp_cancel_delayed_work_sync(&intel_dp->pps.panel_vdd_work);

	/* VDD may still be on because of the delayed VDD-off: turns it off here. */
	wakeref = drv_i915_pps_lock(intel_dp);

	i915_pps_vdd_off_sync_unlocked(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Ends a VDD request, turning VDD off now or later (the Linux
 * intel_pps_vdd_off_unlocked()).
 *
 * It must be paired with drv_i915_pps_vdd_on_unlocked() and the caller holds
 * the PPS lock around the whole on/off sequence.  With sync clear, VDD stays
 * on for a while so that a following access finds it on.
 */
void
drv_i915_pps_vdd_off_unlocked(
	struct intel_dp *intel_dp,
	bool sync)
{
	struct drm_i915_private *dev_priv;
	bool is_edp;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Reports a VDD-off without a matching VDD-on. */
	(void)I915_STATE_WARN(dev_priv, !intel_dp->pps.want_panel_vdd,
			      "[ENCODER:%d:%s] %s VDD not forced on",
			      i915_dp_dp_to_dig_port(intel_dp)->base.base.base.id,
			      i915_dp_dp_to_dig_port(intel_dp)->base.base.name,
			      i915_pps_name(dev_priv, &intel_dp->pps));

	/* Nobody wants VDD any more: the delayed worker may turn it off. */
	intel_dp->pps.want_panel_vdd = false;

	/* Turns VDD off now, or schedules the delayed VDD-off. */
	if (sync) {
		i915_pps_vdd_off_sync_unlocked(intel_dp);
	} else {
		i915_edp_panel_vdd_schedule_off(intel_dp);
	}
}

/*
 * Turns the panel power on (the Linux intel_pps_on_unlocked()).
 *
 * The caller holds the PPS lock.  The display 13 DPLS clock gating
 * workaround runs around the sequence.
 */
void
drv_i915_pps_on_unlocked(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_pps_delays program;
	bool powered;
	int warned;
	int dpls_wa;
	int error;
	bool is_edp;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s turn panel power on\n",
			    i915_dp_dp_to_dig_port(intel_dp)->base.base.base.id,
			    i915_dp_dp_to_dig_port(intel_dp)->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps));

	/* A panel that already has power is left alone, with a warning. */
	powered = i915_edp_have_panel_power(intel_dp);
	warned = I915_DP_DRM_WARN(&dev_priv->drm, powered,
				  "[ENCODER:%d:%s] %s panel power already on\n",
				  i915_dp_dp_to_dig_port(intel_dp)->base.base.base.id,
				  i915_dp_dp_to_dig_port(intel_dp)->base.base.name,
				  i915_pps_name(dev_priv, &intel_dp->pps));
	if (warned)
		return;

	/* The panel must have finished its last power cycle. */
	i915_wait_panel_power_cycle(intel_dp);

	/*
	 * Powers the panel with the selected delays and waits for it to be on;
	 * the backlight-on delay counts from then.
	 *
	 * XXX: the display version is the DP environment's fixed 13, so a
	 * display 12 (Tiger Lake) device takes the DPLS workaround too.
	 */
	dpls_wa = i915_dp_is_display_ver(dev_priv, 13, 14);
	i915_pps_note_wait(intel_dp);
	i915_pps_to_xd(&intel_dp->pps.pps_delays, &program);
	error = drv_i915_xd_pps_power_on(i915_pps_io(dev_priv), i915_pps_index(intel_dp), &program, dpls_wa);
	i915_pps_report_timeout(intel_dp, error);
	intel_dp->pps.last_power_on = i915_dp_jiffies();
}

/*
 * Turns the panel power on under the PPS lock (the Linux intel_pps_on()).
 */
void
drv_i915_pps_on(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool is_edp;

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Powers the panel with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	drv_i915_pps_on_unlocked(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Turns the panel power and the forced VDD off (the Linux
 * intel_pps_off_unlocked()).
 *
 * The caller holds the PPS lock and had forced VDD on; the AUX power
 * reference that VDD held is returned.
 */
void
drv_i915_pps_off_unlocked(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	intel_wakeref_t vdd_wakeref;
	int error;
	bool is_edp;

	/* Finds the device and the port. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s turn panel power off\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps));

	/* Reports a power-off without VDD forced on. */
	(void)I915_DP_DRM_WARN(&dev_priv->drm, !intel_dp->pps.want_panel_vdd,
			       "[ENCODER:%d:%s] %s need VDD to turn off panel\n",
			       dig_port->base.base.base.id, dig_port->base.base.name,
			       i915_pps_name(dev_priv, &intel_dp->pps));

	/* Nobody wants VDD any more. */
	intel_dp->pps.want_panel_vdd = false;

	/*
	 * Switches off panel power, forced VDD and the backlight bit together
	 * and waits for the panel to be off; the power cycle counts from here.
	 */
	i915_pps_note_wait(intel_dp);
	error = drv_i915_xd_pps_power_off(i915_pps_io(dev_priv), i915_pps_index(intel_dp));
	i915_pps_report_timeout(intel_dp, error);
	intel_dp->pps.panel_power_off_time = ktime_get_boottime();

	/* Returns the reference taken when VDD was enabled. */
	vdd_wakeref = intel_dp->pps.vdd_wakeref;
	intel_dp->pps.vdd_wakeref = 0;
	i915_dp_intel_display_power_put(dev_priv, i915_aux_power_domain(dig_port), vdd_wakeref);
}

/*
 * Turns the panel power off under the PPS lock (the Linux intel_pps_off()).
 */
void
drv_i915_pps_off(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool is_edp;

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Powers the panel off with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	drv_i915_pps_off_unlocked(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Sets the backlight-enable bit of the panel power control (the Linux
 * intel_pps_backlight_on()).
 *
 * The backlight-on delay since the panel power came on is waited out first:
 * a backlight enabled right away may flicker while the panel syncs with the
 * eDP link.
 */
void
drv_i915_pps_backlight_on(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	intel_wakeref_t wakeref;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	/* Lets the image become solid before the backlight shows it. */
	i915_wait_backlight_on(intel_dp);

	/* Sets the backlight-enable bit with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	drv_i915_xd_pps_backlight(i915_pps_io(dev_priv), i915_pps_index(intel_dp), 1);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Clears the backlight-enable bit of the panel power control (the Linux
 * intel_pps_backlight_off()).
 *
 * The backlight-off delay is waited out afterwards.
 */
void
drv_i915_pps_backlight_off(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	intel_wakeref_t wakeref;
	bool is_edp;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Clears the backlight-enable bit with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	drv_i915_xd_pps_backlight(i915_pps_io(dev_priv), i915_pps_index(intel_dp), 0);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);

	/* Waits out the backlight-off delay from now. */
	intel_dp->pps.last_backlight_off = i915_dp_jiffies();
	i915_edp_wait_backlight_off(intel_dp);
}

/*
 * Tells whether the panel has power or forced VDD (the Linux
 * intel_pps_have_panel_power_or_vdd()).
 */
bool
drv_i915_pps_have_panel_power_or_vdd(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool have_power;
	bool vdd;

	/* Samples the power and, only without power, the VDD state with the sequencer held. */
	have_power = false;
	wakeref = drv_i915_pps_lock(intel_dp);

	have_power = i915_edp_have_panel_power(intel_dp);
	if (!have_power) {
		vdd = i915_edp_have_panel_vdd(intel_dp);
		if (vdd)
			have_power = true;
	}

	(void)drv_i915_pps_unlock(intel_dp, wakeref);

	/* Reports a panel with neither. */
	if (!have_power)
		return false;

	/* Succeeded: the panel has power or VDD. */
	return true;
}

/*
 * Reprograms the power sequencer, as on resume (the Linux
 * intel_pps_encoder_reset()).
 *
 * A VDD found on is adopted and scheduled off.
 */
void
drv_i915_pps_encoder_reset(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool vdd;
	bool is_edp;

	/* Only an eDP panel has a power sequencer. */
	is_edp = i915_dp_is_edp(intel_dp);
	if (!is_edp)
		return;

	/* Reinitializes the sequencer with it held, in case the firmware changed it. */
	wakeref = drv_i915_pps_lock(intel_dp);

	i915_pps_init_delays(intel_dp);
	i915_pps_init_registers(intel_dp);
	i915_pps_vdd_init(intel_dp);

	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (vdd)
		i915_edp_panel_vdd_schedule_off(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Sets up the power sequencer of an eDP (the Linux intel_pps_init()).
 *
 * It picks the sequencer, selects the delays, programs the registers and
 * adopts a VDD the firmware left on.  VDD stays on while the connector is
 * initializing.  It reports whether the sequencer picked is usable.
 */
bool
drv_i915_pps_init(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool valid;

	/*
	 * Keeps VDD on until the real delays are known, and prepares the
	 * delayed VDD-off.
	 */
	intel_dp->pps.initializing = true;
	i915_dp_init_delayed_work(&intel_dp->pps.panel_vdd_work, i915_edp_panel_vdd_work);

	/* Starts the power timestamps. */
	i915_pps_init_timestamps(intel_dp);

	/* Picks and programs the sequencer with it held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	valid = i915_pps_initial_setup(intel_dp);

	i915_pps_init_delays(intel_dp);
	i915_pps_init_registers(intel_dp);
	i915_pps_vdd_init(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);

	/* Reports an unusable sequencer. */
	if (!valid)
		return false;

	/* Succeeded: the sequencer is usable. */
	return true;
}

/*
 * Finishes the power sequencer set-up once the panel's VBT data is known
 * (the Linux intel_pps_init_late()).
 *
 * The delays are selected again, the registers reprogrammed, and the
 * delayed VDD-off is scheduled when VDD is on.
 */
void
drv_i915_pps_init_late(
	struct intel_dp *intel_dp)
{
	intel_wakeref_t wakeref;
	bool vdd;

	/* Reinitializes the delays with the sequencer held. */
	wakeref = drv_i915_pps_lock(intel_dp);

	i915_pps_init_late(intel_dp);

	kern_memset(&intel_dp->pps.pps_delays, 0, sizeof(intel_dp->pps.pps_delays));
	i915_pps_init_delays(intel_dp);
	i915_pps_init_registers(intel_dp);

	/* The delays are final: VDD may now be turned off when unused. */
	intel_dp->pps.initializing = false;

	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (vdd)
		i915_edp_panel_vdd_schedule_off(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Runs one panel power operation of the modeset hooks on the panel's sink.
 *
 * It is the panel hook of the modeset's register and panel hooks (struct
 * i915_lcd_emit): ctx is the panel run (struct i915_lcd_kernel), and op an
 * enum i915_lcd_panel_op.  It returns 0, or -I915_EDP_EINVAL without a live
 * eDP or for an unknown operation.
 */
int
drv_i915_edp_emit_panel(
	void *ctx,
	int op)
{
	struct i915_lcd_kernel *kernel;
	unsigned step;
	int error;

	/* The step of the panel's sink the operation names. */
	kernel = ctx;
	switch (op) {
	case I915_LCD_PANEL_ON:
		step = I915_XD_EDP_PANEL_ON;
		break;
	case I915_LCD_PANEL_OFF:
		step = I915_XD_EDP_PANEL_OFF;
		break;
	case I915_LCD_PANEL_VDD_ON:
		step = I915_XD_EDP_PANEL_VDD_ON;
		break;
	case I915_LCD_PANEL_VDD_OFF_SYNC:
		step = I915_XD_EDP_PANEL_VDD_OFF;
		break;
	case I915_LCD_PANEL_BACKLIGHT_ON:
		step = I915_XD_EDP_BACKLIGHT_ON;
		break;
	case I915_LCD_PANEL_BACKLIGHT_OFF:
		step = I915_XD_EDP_BACKLIGHT_OFF;
		break;
	default:
		return -I915_EDP_EINVAL;
	}

	/* Runs it on the display's panel. */
	error = drv_i915_xd_edp_panel(&kernel->d->edp->xe, step);
	if (error != 0)
		return -I915_EDP_EINVAL;

	/* Succeeded: the step ran. */
	return 0;
}

/* Names a power sequencer for the messages. */
static const char *
i915_pps_name(
	struct drm_i915_private *i915,
	struct intel_pps *pps)
{
	UNUSED_PARAMETER(i915);

	/* Names the sequencer's index. */
	switch (pps->pps_idx) {
	case 0:
		return "PPS 0";
	case 1:
		return "PPS 1";
	default:
		I915_DP_MISSING_CASE(pps->pps_idx);
		break;
	}

	/* Reports a sequencer that has no name. */
	return "PPS <invalid>";
}

/*
 * Returns the register window the sequencer is reached through: the
 * environment's, which the AUX transactions use too (both run under the
 * PPS lock).
 */
static struct i915_xd_io *
i915_pps_io(
	struct drm_i915_private *i915)
{
	/* The window the environment's binding opened. */
	return i915_dp_env_of(i915)->aux_io;
}

/* Returns the index of the eDP's power sequencer. */
static unsigned
i915_pps_index(
	struct intel_dp *intel_dp)
{
	/* The sequencer picked when the eDP was set up. */
	return (unsigned)intel_dp->pps.pps_idx;
}

/* Reports how many power sequencers the platform has. */
static int
i915_num_pps(
	struct drm_i915_private *i915)
{
	UNUSED_PARAMETER(i915);

	/* MTL and later PCHs have two. */
	if (INTEL_PCH_TYPE(i915) >= PCH_MTL)
		return 2;

	/* DG1 and DG2 have one. */
	if (INTEL_PCH_TYPE(i915) >= PCH_DG1)
		return 1;

	/* ICP up to ADP have two. */
	if (INTEL_PCH_TYPE(i915) >= PCH_ICP)
		return 2;

	/* Older PCHs have one. */
	return 1;
}

/* Tells whether the second sequencer shares its pins (ICP up to ADP). */
static int
i915_pps_muxed(
	struct drm_i915_private *i915)
{
	UNUSED_PARAMETER(i915);

	/* Before ICP there is no second sequencer to mux. */
	if (INTEL_PCH_TYPE(i915) < PCH_ICP)
		return 0;

	/* After ADP the second sequencer has pins of its own. */
	if (INTEL_PCH_TYPE(i915) > PCH_ADP)
		return 0;

	/* Succeeded: the second sequencer needs its pins selected. */
	return 1;
}

/* Picks the eDP's initial power sequencer and tells whether it is usable. */
static bool
i915_pps_initial_setup(
	struct intel_dp *intel_dp)
{
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	struct drm_i915_private *i915;
	struct i915_xd_io *io;
	int pps_num;
	int too_large;
	int usable;

	/* Finds the encoder, the connector, the device and the window. */
	encoder = &i915_dp_dp_to_dig_port(intel_dp)->base;
	connector = intel_dp->attached_connector;
	i915 = i915_dp_to_i915(encoder->base.dev);
	io = i915_pps_io(i915);

	I915_DP_LOCKDEP_ASSERT_HELD(&i915->display.pps.mutex);

	/* Asks the VBT first. */
	pps_num = i915_num_pps(i915);
	if (pps_num > 1) {
		intel_dp->pps.pps_idx = connector->panel.vbt.backlight.controller;
	} else {
		intel_dp->pps.pps_idx = 0;
	}

	/* A controller index the platform does not have means no answer. */
	too_large = I915_DP_DRM_WARN_ON(&i915->drm, intel_dp->pps.pps_idx >= pps_num);
	if (too_large)
		intel_dp->pps.pps_idx = -1;

	/* The VBT was not parsed yet: picks one where the panel is on. */
	if (intel_dp->pps.pps_idx < 0)
		intel_dp->pps.pps_idx = drv_i915_xd_pps_find(io, (unsigned)pps_num, I915_XD_PPS_FIND_POWERED);

	/* None found: picks one where VDD is on. */
	if (intel_dp->pps.pps_idx < 0)
		intel_dp->pps.pps_idx = drv_i915_xd_pps_find(io, (unsigned)pps_num, I915_XD_PPS_FIND_VDD);

	/* None found: picks any, and says which sequencer is assumed. */
	if (intel_dp->pps.pps_idx < 0) {
		intel_dp->pps.pps_idx = drv_i915_xd_pps_find(io, (unsigned)pps_num, I915_XD_PPS_FIND_ANY);

		I915_DP_DRM_DBG_KMS(&i915->drm,
				    "[ENCODER:%d:%s] no initial power sequencer, assuming %s\n",
				    encoder->base.base.id, encoder->base.name,
				    i915_pps_name(i915, &intel_dp->pps));
	} else {
		I915_DP_DRM_DBG_KMS(&i915->drm,
				    "[ENCODER:%d:%s] initial power sequencer: %s\n",
				    encoder->base.base.id, encoder->base.name,
				    i915_pps_name(i915, &intel_dp->pps));
	}

	/* Reports a sequencer that cannot drive the panel. */
	usable = drv_i915_xd_pps_usable(io, i915_pps_index(intel_dp), (unsigned)pps_num, i915_pps_muxed(i915));
	if (usable == 0)
		return false;

	/* Succeeded: the chosen sequencer is usable. */
	return true;
}

/* Tells whether the panel has power. */
static bool
i915_edp_have_panel_power(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	int powered;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Asks the sequencer. */
	powered = drv_i915_xd_pps_powered(i915_pps_io(dev_priv), i915_pps_index(intel_dp));
	if (powered == 0)
		return false;

	/* Succeeded: the panel has power. */
	return true;
}

/* Tells whether the panel has VDD forced on. */
static bool
i915_edp_have_panel_vdd(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	int forced;

	/* Finds the device the port belongs to. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Asks the sequencer. */
	forced = drv_i915_xd_pps_vdd_forced(i915_pps_io(dev_priv), i915_pps_index(intel_dp));
	if (forced == 0)
		return false;

	/* Succeeded: VDD is forced on. */
	return true;
}

/* Notes the sequencer's state before a step that waits on it. */
static void
i915_pps_note_wait(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct i915_xd_io *io;
	unsigned pps;

	/* Finds the device, the port, the window and the sequencer. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);

	/* Compares the delays the hardware holds with the ones selected, and notes the state. */
	i915_pps_verify_state(intel_dp);
	I915_DP_DRM_DBG_KMS(&dev_priv->drm,
			    "[ENCODER:%d:%s] %s waits from PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps),
			    drv_i915_xd_io_read(io, I915_XD_PP_STATUS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)));
}

/* Reports a sequencer step that did not finish in time; the sequence goes on. */
static void
i915_pps_report_timeout(
	struct intel_dp *intel_dp,
	int error)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct i915_xd_io *io;
	unsigned pps;

	/* A step that finished needs no report. */
	if (error == 0) {
		I915_DP_DRM_DBG_KMS(NULL, "Wait complete\n");
		return;
	}

	/* Reports the status and the control word the step left. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);
	I915_DP_DRM_ERR(&dev_priv->drm,
			"[ENCODER:%d:%s] %s panel status timeout: PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			dig_port->base.base.base.id, dig_port->base.base.name,
			i915_pps_name(dev_priv, &intel_dp->pps),
			drv_i915_xd_io_read(io, I915_XD_PP_STATUS(pps)),
			drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)));
}

/*
 * Waits for the panel power cycle (T11/T12) to finish.
 *
 * When VDD was the last thing turned off, the sequencer did not time the
 * cycle, so the remaining time since the power went off is slept here
 * before the sequencer is waited for.
 */
static void
i915_wait_panel_power_cycle(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915;
	struct intel_digital_port *dig_port;
	ktime_t panel_power_on_time;
	s64 panel_power_off_duration;
	unsigned long now;
	int error;

	/* Finds the device and the port. */
	i915 = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);

	I915_DP_DRM_DBG_KMS(&i915->drm, "[ENCODER:%d:%s] %s wait for panel power cycle\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(i915, &intel_dp->pps));

	/* Measures how long the panel has been off. */
	panel_power_on_time = ktime_get_boottime();
	panel_power_off_duration = ktime_ms_delta(panel_power_on_time, intel_dp->pps.panel_power_off_time);

	/* Sleeps for what remains of the power-cycle delay. */
	if (panel_power_off_duration < (s64)intel_dp->pps.panel_power_cycle_delay) {
		now = i915_dp_jiffies();
		i915_wait_remaining_ms_from_jiffies(now, (int)(intel_dp->pps.panel_power_cycle_delay - panel_power_off_duration));
	}

	/* Waits for the sequencer to report the cycle over. */
	i915_pps_note_wait(intel_dp);
	error = drv_i915_xd_pps_wait_cycle(i915_pps_io(i915), i915_pps_index(intel_dp));
	i915_pps_report_timeout(intel_dp, error);
}

/* Waits out the backlight-on delay since the panel power came on. */
static void
i915_wait_backlight_on(
	struct intel_dp *intel_dp)
{
	/* Sleeps for what remains of the delay. */
	i915_wait_remaining_ms_from_jiffies(intel_dp->pps.last_power_on, intel_dp->pps.backlight_on_delay);
}

/* Waits out the backlight-off delay since the backlight went off. */
static void
i915_edp_wait_backlight_off(
	struct intel_dp *intel_dp)
{
	/* Sleeps for what remains of the delay. */
	i915_wait_remaining_ms_from_jiffies(intel_dp->pps.last_backlight_off, intel_dp->pps.backlight_off_delay);
}

/* Turns forced VDD off now and returns the AUX reference it held; the PPS lock is held. */
static void
i915_pps_vdd_off_sync_unlocked(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	struct i915_xd_io *io;
	unsigned pps;
	u32 pp;
	intel_wakeref_t vdd_wakeref;
	bool vdd;

	/* Finds the device and the port. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Reports a VDD-off while someone still wants VDD. */
	(void)I915_DP_DRM_WARN_ON(&dev_priv->drm, intel_dp->pps.want_panel_vdd);

	/* Nothing to do when VDD is not forced on. */
	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (!vdd)
		return;

	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s turning VDD off\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps));

	/* Clears the forced VDD. */
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);
	pp = drv_i915_xd_pps_vdd_off(io, pps);
	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "[ENCODER:%d:%s] %s PP_STATUS: 0x%08x PP_CONTROL: 0x%08x\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps),
			    drv_i915_xd_io_read(io, I915_XD_PP_STATUS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)));

	/* Without panel power, the power cycle counts from now. */
	if ((pp & I915_XD_PP_CONTROL_POWER_ON) == 0)
		intel_dp->pps.panel_power_off_time = ktime_get_boottime();

	/* Returns the AUX reference the forced VDD held. */
	vdd_wakeref = intel_dp->pps.vdd_wakeref;
	intel_dp->pps.vdd_wakeref = 0;
	i915_dp_intel_display_power_put(dev_priv, i915_aux_power_domain(dig_port), vdd_wakeref);
}

/* Runs the delayed VDD-off: turns VDD off unless someone wants it again. */
static void
i915_edp_panel_vdd_work(
	struct work_struct *work)
{
	struct intel_pps *pps;
	struct intel_dp *intel_dp;
	intel_wakeref_t wakeref;

	/* Finds the sequencer state and the port the work belongs to. */
	pps = container_of(to_delayed_work(work), struct intel_pps, panel_vdd_work);
	intel_dp = container_of(pps, struct intel_dp, pps);

	/* Turns VDD off with the sequencer held, unless it is wanted again. */
	wakeref = drv_i915_pps_lock(intel_dp);

	if (!intel_dp->pps.want_panel_vdd)
		i915_pps_vdd_off_sync_unlocked(intel_dp);

	(void)drv_i915_pps_unlock(intel_dp, wakeref);
}

/*
 * Schedules the delayed VDD-off.
 *
 * The delay is five power-cycle delays, long enough to keep the panel
 * powered across a sequence of operations.  Nothing is scheduled while the
 * connector is initializing, because the real delays are not known yet.
 */
static void
i915_edp_panel_vdd_schedule_off(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915;
	unsigned long delay;

	/* Finds the device the port belongs to. */
	i915 = i915_dp_dp_to_i915(intel_dp);

	/* Keeps VDD on until the initialization is done. */
	if (intel_dp->pps.initializing)
		return;

	/* Queues the VDD-off a long time from now, relative to the power-down delay. */
	delay = i915_dp_msecs_to_jiffies(intel_dp->pps.panel_power_cycle_delay * 5);
	(void)i915_dp_queue_delayed_work(i915->unordered_wq, &intel_dp->pps.panel_vdd_work, delay);
}

/*
 * Adopts a VDD the firmware left on.
 *
 * The forced VDD needs a power domain reference, so the reference is taken
 * here; the caller schedules the VDD-off so that it is not held forever.
 */
static void
i915_pps_vdd_init(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_digital_port *dig_port;
	bool vdd;

	/* Finds the device and the port. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	dig_port = i915_dp_dp_to_dig_port(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Nothing to adopt when VDD is off. */
	vdd = i915_edp_have_panel_vdd(intel_dp);
	if (!vdd)
		return;

	I915_DP_DRM_DBG_KMS(&dev_priv->drm,
			    "[ENCODER:%d:%s] %s VDD left on by BIOS, adjusting state tracking\n",
			    dig_port->base.base.base.id, dig_port->base.base.name,
			    i915_pps_name(dev_priv, &intel_dp->pps));

	/* Takes the AUX reference the forced VDD holds. */
	(void)I915_DP_DRM_WARN_ON(&dev_priv->drm, intel_dp->pps.vdd_wakeref);
	intel_dp->pps.vdd_wakeref = i915_dp_intel_display_power_get(dev_priv, i915_aux_power_domain(dig_port));
}

/*
 * Starts the power timestamps.
 *
 * The panel power-off time starts at 0, assuming that only a previously
 * loaded driver could have toggled the panel power since boot, and that it
 * waited out the power-off delay itself.
 */
static void
i915_pps_init_timestamps(
	struct intel_dp *intel_dp)
{
	/* Counts the power cycle as long over and the other delays from now. */
	intel_dp->pps.panel_power_off_time = 0;
	intel_dp->pps.last_power_on = i915_dp_jiffies();
	intel_dp->pps.last_backlight_off = i915_dp_jiffies();
}

/* Copies a set of delays into the xd panel's form. */
static void
i915_pps_to_xd(
	const struct edp_power_seq *seq,
	struct i915_xd_pps_delays *delays)
{
	/* The same delays in the same units. */
	delays->power_up = seq->t1_t3;
	delays->backlight_on = seq->t8;
	delays->backlight_off = seq->t9;
	delays->power_down = seq->t10;
	delays->power_cycle = seq->t11_t12;
}

/* Copies a set of delays back from the xd panel's form. */
static void
i915_pps_from_xd(
	const struct i915_xd_pps_delays *delays,
	struct edp_power_seq *seq)
{
	/* The same delays in the same units. */
	seq->t1_t3 = (u16)delays->power_up;
	seq->t8 = (u16)delays->backlight_on;
	seq->t9 = (u16)delays->backlight_off;
	seq->t10 = (u16)delays->power_down;
	seq->t11_t12 = (u16)delays->power_cycle;
}

/* Reports one set of delays in a debug message. */
static void
i915_pps_dump_state(
	struct intel_dp *intel_dp,
	const char *state_name,
	const struct edp_power_seq *seq)
{
	UNUSED_PARAMETER(intel_dp);

	/* The message names the set and its five delays. */
	I915_DP_DRM_DBG_KMS(NULL, "%s t1_t3 %d t8 %d t9 %d t10 %d t11_t12 %d\n",
			    state_name,
			    seq->t1_t3, seq->t8, seq->t9, seq->t10, seq->t11_t12);
}

/* Reports delays in the hardware that differ from the ones selected. */
static void
i915_pps_verify_state(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_pps_delays read;
	struct edp_power_seq hw;
	struct edp_power_seq *sw;

	/* Finds the device and the delays selected. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	sw = &intel_dp->pps.pps_delays;

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Reads the delays the hardware holds. */
	drv_i915_xd_pps_read_delays(i915_pps_io(dev_priv), i915_pps_index(intel_dp), &read);
	i915_pps_from_xd(&read, &hw);

	/* Reports any delay that differs, with both sets. */
	if (hw.t1_t3 != sw->t1_t3 ||
	    hw.t8 != sw->t8 ||
	    hw.t9 != sw->t9 ||
	    hw.t10 != sw->t10 ||
	    hw.t11_t12 != sw->t11_t12) {
		I915_DP_DRM_ERR(NULL, "PPS state mismatch\n");
		i915_pps_dump_state(intel_dp, "sw", sw);
		i915_pps_dump_state(intel_dp, "hw", &hw);
	}
}

/*
 * Selects the panel delays once: the larger of the firmware's and the
 * VBT's, or the spec limits, and the millisecond delays the waits use.
 */
static void
i915_pps_init_delays(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct intel_connector *connector;
	struct i915_xd_pps_delays firmware;
	struct i915_xd_pps_delays vbt;
	struct i915_xd_pps_delays spec;
	struct i915_xd_pps_delays program;
	struct i915_xd_pps_waits waits;
	struct edp_power_seq seq;
	int initialized;
	int known;
	int vbt_set;
	int quirk;

	/* Finds the device and the connector. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	connector = intel_dp->attached_connector;

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Delays already selected are kept. */
	i915_pps_to_xd(&intel_dp->pps.pps_delays, &program);
	initialized = drv_i915_xd_pps_delays_set(&program);
	if (initialized != 0)
		return;

	/* The firmware's delays, read the first time only and kept. */
	i915_pps_to_xd(&intel_dp->pps.bios_pps_delays, &firmware);
	known = drv_i915_xd_pps_delays_set(&firmware);
	if (known == 0) {
		drv_i915_xd_pps_read_delays(i915_pps_io(dev_priv), i915_pps_index(intel_dp), &firmware);
		i915_pps_from_xd(&firmware, &intel_dp->pps.bios_pps_delays);
	}
	i915_pps_dump_state(intel_dp, "bios", &intel_dp->pps.bios_pps_delays);

	/*
	 * The VBT's delays.  On the Toshiba Satellite P50-C-18C the VBT's T12
	 * of 500 ms is too short and the panel occasionally fails to power
	 * back on; 800 ms is enough.
	 */
	i915_pps_to_xd(&connector->panel.vbt.edp.pps, &vbt);
	vbt_set = drv_i915_xd_pps_delays_set(&vbt);
	quirk = i915_dp_intel_has_quirk(dev_priv, QUIRK_INCREASE_T12_DELAY);
	if (quirk && vbt_set != 0) {
		if (vbt.power_cycle < I915_PPS_QUIRK_T12)
			vbt.power_cycle = I915_PPS_QUIRK_T12;
		I915_DP_DRM_DBG_KMS(&dev_priv->drm,
				    "Increasing T12 panel delay as per the quirk to %d\n",
				    vbt.power_cycle);
	}

	/* Counts the VBT's power cycle as the sequencer does. */
	drv_i915_xd_pps_from_vbt(&vbt);
	if (vbt_set != 0) {
		i915_pps_from_xd(&vbt, &seq);
		i915_pps_dump_state(intel_dp, "vbt", &seq);
	}

	/* The specification's limits, where neither gives a delay. */
	drv_i915_xd_pps_spec(&spec);
	i915_pps_from_xd(&spec, &seq);
	i915_pps_dump_state(intel_dp, "spec", &seq);

	/* Chooses each delay and the waits. */
	drv_i915_xd_pps_choose(&firmware, &vbt, &program, &waits);
	i915_pps_from_xd(&program, &intel_dp->pps.pps_delays);
	intel_dp->pps.panel_power_up_delay = (int)waits.power_up_ms;
	intel_dp->pps.backlight_on_delay = (int)waits.backlight_on_ms;
	intel_dp->pps.backlight_off_delay = (int)waits.backlight_off_ms;
	intel_dp->pps.panel_power_down_delay = (int)waits.power_down_ms;
	intel_dp->pps.panel_power_cycle_delay = (int)waits.power_cycle_ms;

	I915_DP_DRM_DBG_KMS(&dev_priv->drm,
			    "panel power up delay %d, power down delay %d, power cycle delay %d\n",
			    intel_dp->pps.panel_power_up_delay,
			    intel_dp->pps.panel_power_down_delay,
			    intel_dp->pps.panel_power_cycle_delay);

	I915_DP_DRM_DBG_KMS(&dev_priv->drm, "backlight on delay %d, off delay %d\n",
			    intel_dp->pps.backlight_on_delay,
			    intel_dp->pps.backlight_off_delay);
}

/* Programs the selected delays into the sequencer. */
static void
i915_pps_init_registers(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv;
	struct i915_xd_pps_delays program;
	struct i915_xd_io *io;
	unsigned pps;

	/* Finds the device, the window and the sequencer. */
	dev_priv = i915_dp_dp_to_i915(intel_dp);
	io = i915_pps_io(dev_priv);
	pps = i915_pps_index(intel_dp);

	I915_DP_LOCKDEP_ASSERT_HELD(&dev_priv->display.pps.mutex);

	/* Writes the delay registers and the power cycle. */
	i915_pps_to_xd(&intel_dp->pps.pps_delays, &program);
	drv_i915_xd_pps_write_delays(io, pps, &program);

	I915_DP_DRM_DBG_KMS(&dev_priv->drm,
			    "panel power sequencer register settings: PP_ON %#x, PP_OFF %#x, PP_DIV %#x\n",
			    drv_i915_xd_io_read(io, I915_XD_PP_ON_DELAYS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_OFF_DELAYS(pps)),
			    drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(pps)) & I915_XD_PP_CONTROL_CYCLE);
}

/* Switches to the sequencer the VBT names once the panel's VBT data is known. */
static void
i915_pps_init_late(
	struct intel_dp *intel_dp)
{
	struct drm_i915_private *i915;
	struct intel_encoder *encoder;
	struct intel_connector *connector;
	int pps_num;
	int controller;
	int mismatch;

	/* Finds the device, the encoder and the connector. */
	i915 = i915_dp_dp_to_i915(intel_dp);
	encoder = &i915_dp_dp_to_dig_port(intel_dp)->base;
	connector = intel_dp->attached_connector;

	/* With one sequencer there is nothing to switch to. */
	pps_num = i915_num_pps(i915);
	if (pps_num < 2)
		return;

	/* Reports a VBT controller that differs from the sequencer chosen at first. */
	controller = connector->panel.vbt.backlight.controller;
	mismatch = 0;
	if (controller >= 0 && intel_dp->pps.pps_idx != controller)
		mismatch = 1;

	(void)I915_DP_DRM_WARN(&i915->drm, mismatch,
			       "[ENCODER:%d:%s] power sequencer mismatch: %d (initial) vs. %d (VBT)\n",
			       encoder->base.base.id, encoder->base.name,
			       intel_dp->pps.pps_idx, connector->panel.vbt.backlight.controller);

	/* Takes the VBT's controller when it names one. */
	if (controller >= 0)
		intel_dp->pps.pps_idx = controller;
}
