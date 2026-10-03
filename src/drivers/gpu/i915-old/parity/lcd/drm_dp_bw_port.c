/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (sha256 030568524ac5db3fbd09725df196b22a430ec18fc1432dbb952298ce7c791a73) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: drm_dp_bw_channel_coding_efficiency (drm_dp_helper.c) and the inline drm_dp_is_uhbr_rate
 *    (include/drm/display/drm_dp_helper.h, sha256 1969846dd3fdb5d7511319caf5f8e8481eacecad610fa429669450b9e5775b73,
 *    same copyright holder and permission notice as above);
 *  - the includes are replaced by lcd_compat.h; drm_dp_is_uhbr_rate loses `static inline` so the
 *    other generated files can call it.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/ and drm/ includes */

/**
 * drm_dp_is_uhbr_rate - Determine if a link rate is UHBR
 * @link_rate: link rate in 10kbits/s units
 *
 * Determine if the provided link rate is an UHBR rate.
 *
 * Returns: %True if @link_rate is an UHBR rate.
 */
bool drm_dp_is_uhbr_rate(int link_rate)
{
	return link_rate >= 1000000;
}

/**
 * drm_dp_bw_channel_coding_efficiency - Get a DP link's channel coding efficiency
 * @is_uhbr: Whether the link has a 128b/132b channel coding
 *
 * Return the channel coding efficiency of the given DP link type, which is
 * either 8b/10b or 128b/132b (aka UHBR). The corresponding overhead includes
 * the 8b -> 10b, 128b -> 132b pixel data to link symbol conversion overhead
 * and for 128b/132b any link or PHY level control symbol insertion overhead
 * (LLCP, FEC, PHY sync, see DP Standard v2.1 3.5.2.18). For 8b/10b the
 * corresponding FEC overhead is BW allocation specific, included in the value
 * returned by drm_dp_bw_overhead().
 *
 * Returns the efficiency in the 100%/coding-overhead% ratio in
 * 1ppm units.
 */
int drm_dp_bw_channel_coding_efficiency(bool is_uhbr)
{
	if (is_uhbr)
		return 967100;
	else
		/*
		 * Note that on 8b/10b MST the efficiency is only
		 * 78.75% due to the 1 out of 64 MTPH packet overhead,
		 * not accounted for here.
		 */
		return 800000;
}

