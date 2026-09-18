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

