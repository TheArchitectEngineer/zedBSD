/*
 * Copyright © 2008 Intel Corporation
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
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
 *
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (sha256 36ff6ee288d212c55875f14c14d065e7df32d764eaa62bc6773190941db9f9ce) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_dp_is_uhbr, intel_dp_link_symbol_size, intel_dp_link_symbol_clock, intel_dp_link_required,
 *    intel_dp_effective_data_rate, intel_dp_max_data_rate, intel_dp_needs_vsc_sdp;
 *  - the includes are replaced by lcd_compat.h.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/, drm/ and i915 includes */
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static int intel_dp_rate_index(const int *rates, int len, int rate);
static bool downstream_hpd_needs_d0(struct intel_dp *intel_dp);
static void
intel_edp_init_source_oui(struct intel_dp *intel_dp, bool careful);

/* Is link rate UHBR and thus 128b/132b? */
bool intel_dp_is_uhbr(const struct intel_crtc_state *crtc_state)
{
	return drm_dp_is_uhbr_rate(crtc_state->port_clock);
}

/**
 * intel_dp_link_symbol_size - get the link symbol size for a given link rate
 * @rate: link rate in 10kbit/s units
 *
 * Returns the link symbol size in bits/symbol units depending on the link
 * rate -> channel coding.
 */
int intel_dp_link_symbol_size(int rate)
{
	return drm_dp_is_uhbr_rate(rate) ? 32 : 10;
}

/**
 * intel_dp_link_symbol_clock - convert link rate to link symbol clock
 * @rate: link rate in 10kbit/s units
 *
 * Returns the link symbol clock frequency in kHz units depending on the
 * link rate and channel coding.
 */
int intel_dp_link_symbol_clock(int rate)
{
	return DIV_ROUND_CLOSEST(rate * 10, intel_dp_link_symbol_size(rate));
}

/*
 * The required data bandwidth for a mode with given pixel clock and bpp. This
 * is the required net bandwidth independent of the data bandwidth efficiency.
 *
 * TODO: check if callers of this functions should use
 * intel_dp_effective_data_rate() instead.
 */
int
intel_dp_link_required(int pixel_clock, int bpp)
{
	/* pixel_clock is in kHz, divide bpp by 8 for bit to Byte conversion */
	return DIV_ROUND_UP(pixel_clock * bpp, 8);
}

/**
 * intel_dp_effective_data_rate - Return the pixel data rate accounting for BW allocation overhead
 * @pixel_clock: pixel clock in kHz
 * @bpp_x16: bits per pixel .4 fixed point format
 * @bw_overhead: BW allocation overhead in 1ppm units
 *
 * Return the effective pixel data rate in kB/sec units taking into account
 * the provided SSC, FEC, DSC BW allocation overhead.
 */
int intel_dp_effective_data_rate(int pixel_clock, int bpp_x16,
				 int bw_overhead)
{
	return DIV_ROUND_UP_ULL(mul_u32_u32(pixel_clock * bpp_x16, bw_overhead),
				1000000 * 16 * 8);
}

/*
 * Given a link rate and lanes, get the data bandwidth.
 *
 * Data bandwidth is the actual payload rate, which depends on the data
 * bandwidth efficiency and the link rate.
 *
 * For 8b/10b channel encoding, SST and non-FEC, the data bandwidth efficiency
 * is 80%. For example, for a 1.62 Gbps link, 1.62*10^9 bps * 0.80 * (1/8) =
 * 162000 kBps. With 8-bit symbols, we have 162000 kHz symbol clock. Just by
 * coincidence, the port clock in kHz matches the data bandwidth in kBps, and
 * they equal the link bit rate in Gbps multiplied by 100000. (Note that this no
 * longer holds for data bandwidth as soon as FEC or MST is taken into account!)
 *
 * For 128b/132b channel encoding, the data bandwidth efficiency is 96.71%. For
 * example, for a 10 Gbps link, 10*10^9 bps * 0.9671 * (1/8) = 1208875
 * kBps. With 32-bit symbols, we have 312500 kHz symbol clock. The value 1000000
 * does not match the symbol clock, the port clock (not even if you think in
 * terms of a byte clock), nor the data bandwidth. It only matches the link bit
 * rate in units of 10000 bps.
 */
int
intel_dp_max_data_rate(int max_link_rate, int max_lanes)
{
	int ch_coding_efficiency =
		drm_dp_bw_channel_coding_efficiency(drm_dp_is_uhbr_rate(max_link_rate));
	int max_link_rate_kbps = max_link_rate * 10;

	/*
	 * UHBR rates always use 128b/132b channel encoding, and have
	 * 97.71% data bandwidth efficiency. Consider max_link_rate the
	 * link bit rate in units of 10000 bps.
	 */
	/*
	 * Lower than UHBR rates always use 8b/10b channel encoding, and have
	 * 80% data bandwidth efficiency for SST non-FEC. However, this turns
	 * out to be a nop by coincidence:
	 *
	 *	int max_link_rate_kbps = max_link_rate * 10;
	 *	max_link_rate_kbps = DIV_ROUND_DOWN_ULL(max_link_rate_kbps * 8, 10);
	 *	max_link_rate = max_link_rate_kbps / 8;
	 */
	return DIV_ROUND_DOWN_ULL(mul_u32_u32(max_link_rate_kbps * max_lanes,
					      ch_coding_efficiency),
				  1000000 * 8);
}

bool
intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state,
		       const struct drm_connector_state *conn_state)
{
	/*
	 * As per DP 1.4a spec section 2.2.4.3 [MSA Field for Indication
	 * of Color Encoding Format and Content Color Gamut], in order to
	 * sending YCBCR 420 or HDR BT.2020 signals we should use DP VSC SDP.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		return true;

	switch (conn_state->colorspace) {
	case DRM_MODE_COLORIMETRY_SYCC_601:
	case DRM_MODE_COLORIMETRY_OPYCC_601:
	case DRM_MODE_COLORIMETRY_BT2020_YCC:
	case DRM_MODE_COLORIMETRY_BT2020_RGB:
	case DRM_MODE_COLORIMETRY_BT2020_CYCC:
		return true;
	default:
		break;
	}

	return false;
}

/* return index of rate in rates array, or -1 if not found */
static int intel_dp_rate_index(const int *rates, int len, int rate)
{
	int i;

	for (i = 0; i < len; i++)
		if (rate == rates[i])
			return i;

	return -1;
}

/**
 * intel_dp_is_edp - is the given port attached to an eDP panel (either CPU or PCH)
 * @intel_dp: DP struct
 *
 * If a CPU or PCH DP output is attached to an eDP panel, this function
 * will return true, and false otherwise.
 *
 * This function is not safe to use prior to encoder type being set.
 */
bool intel_dp_is_edp(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);

	return dig_port->base.type == INTEL_OUTPUT_EDP;
}

bool intel_dp_source_supports_tps3(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 9 || IS_BROADWELL(i915) || IS_HASWELL(i915);
}

bool intel_dp_source_supports_tps4(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 10;
}

int intel_dp_rate_select(struct intel_dp *intel_dp, int rate)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int i = intel_dp_rate_index(intel_dp->sink_rates,
				    intel_dp->num_sink_rates, rate);

	if (drm_WARN_ON(&i915->drm, i < 0))
		i = 0;

	return i;
}

void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   u8 *link_bw, u8 *rate_select)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	/* FIXME g4x can't generate an exact 2.7GHz with the 96MHz non-SSC refclk */
	if (IS_G4X(i915) && port_clock == 268800)
		port_clock = 270000;

	/* eDP 1.4 rate select method. */
	if (intel_dp->use_rate_select) {
		*link_bw = 0;
		*rate_select =
			intel_dp_rate_select(intel_dp, port_clock);
	} else {
		*link_bw = drm_dp_link_rate_to_bw_code(port_clock);
		*rate_select = 0;
	}
}

void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, int lane_count)
{
	memset(intel_dp->train_set, 0, sizeof(intel_dp->train_set));
	intel_dp->link_trained = false;
	intel_dp->link_rate = link_rate;
	intel_dp->lane_count = lane_count;
}

static bool downstream_hpd_needs_d0(struct intel_dp *intel_dp)
{
	/*
	 * DPCD 1.2+ should support BRANCH_DEVICE_CTRL, and thus
	 * be capable of signalling downstream hpd with a long pulse.
	 * Whether or not that means D3 is safe to use is not clear,
	 * but let's assume so until proven otherwise.
	 *
	 * FIXME should really check all downstream ports...
	 */
	return intel_dp->dpcd[DP_DPCD_REV] == 0x11 &&
		drm_dp_is_branch(intel_dp->dpcd) &&
		intel_dp->downstream_ports[0] & DP_DS_PORT_HPD;
}

static void
intel_edp_init_source_oui(struct intel_dp *intel_dp, bool careful)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	u8 oui[] = { 0x00, 0xaa, 0x01 };
	u8 buf[3] = {};

	/*
	 * During driver init, we want to be careful and avoid changing the source OUI if it's
	 * already set to what we want, so as to avoid clearing any state by accident
	 */
	if (careful) {
		if (drm_dp_dpcd_read(&intel_dp->aux, DP_SOURCE_OUI, buf, sizeof(buf)) < 0)
			drm_err(&i915->drm, "Failed to read source OUI\n");

		if (memcmp(oui, buf, sizeof(oui)) == 0) {
			/* Assume the OUI was written now. */
			intel_dp->last_oui_write = jiffies;
			return;
		}
	}

	if (drm_dp_dpcd_write(&intel_dp->aux, DP_SOURCE_OUI, oui, sizeof(oui)) < 0)
		drm_err(&i915->drm, "Failed to write source OUI\n");

	intel_dp->last_oui_write = jiffies;
}

/* If the device supports it, try to set the power state appropriately */
void intel_dp_set_power(struct intel_dp *intel_dp, u8 mode)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	int ret, i;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	if (mode != DP_SET_POWER_D0) {
		if (downstream_hpd_needs_d0(intel_dp))
			return;

		ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, mode);
	} else {
		struct intel_lspcon *lspcon = dp_to_lspcon(intel_dp);

		lspcon_resume(dp_to_dig_port(intel_dp));

		/* Write the source OUI as early as possible */
		if (intel_dp_is_edp(intel_dp))
			intel_edp_init_source_oui(intel_dp, false);

		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = drm_dp_dpcd_writeb(&intel_dp->aux, DP_SET_POWER, mode);
			if (ret == 1)
				break;
			msleep(1);
		}

		if (ret == 1 && lspcon->active)
			lspcon_wait_pcon_mode(lspcon);
	}

	if (ret != 1)
		drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] Set power to %s failed\n",
			    encoder->base.base.id, encoder->base.name,
			    mode == DP_SET_POWER_D0 ? "D0" : "D3");
}

/* Enable backlight PWM and backlight PP control. */
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(conn_state->best_encoder));
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "\n");

	intel_backlight_enable(crtc_state, conn_state);
	intel_pps_backlight_on(intel_dp);
}

/* Disable backlight PP control and backlight PWM. */
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder));
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!intel_dp_is_edp(intel_dp))
		return;

	drm_dbg_kms(&i915->drm, "\n");

	intel_pps_backlight_off(intel_dp);
	intel_backlight_disable(old_conn_state);
}

void intel_dp_set_infoframes(struct intel_encoder *encoder,
			     bool enable,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	i915_reg_t reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	u32 dip_enable = VIDEO_DIP_ENABLE_AVI_HSW | VIDEO_DIP_ENABLE_GCP_HSW |
			 VIDEO_DIP_ENABLE_VS_HSW | VIDEO_DIP_ENABLE_GMP_HSW |
			 VIDEO_DIP_ENABLE_SPD_HSW | VIDEO_DIP_ENABLE_DRM_GLK;
	u32 val = intel_de_read(dev_priv, reg) & ~dip_enable;

	/* TODO: Sanitize DSC enabling wrt. intel_dsc_dp_pps_write(). */
	if (!enable && HAS_DSC(dev_priv))
		val &= ~VDIP_ENABLE_PPS;

	/*
	 * This routine disables VSC DIP if the function is called
	 * to disable SDP or if it does not have PSR
	 */
	if (!enable || !crtc_state->has_psr)
		val &= ~VIDEO_DIP_ENABLE_VSC_HSW;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	if (!enable)
		return;

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (!crtc_state->has_psr)
		intel_write_dp_sdp(encoder, crtc_state, DP_SDP_VSC);

	intel_write_dp_sdp(encoder, crtc_state, HDMI_PACKET_TYPE_GAMUT_METADATA);
}

