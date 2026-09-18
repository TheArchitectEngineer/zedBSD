/*
 * Copyright (c) 2016 Intel Corporation
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
 * zedBSD WS031: enum drm_colorspace extracted textually from Linux v6.8.12 include/drm/drm_connector.h
 * (sha256 1eb905598bb46d733afe49917276689bfbef7284f3395ffa72131d4ef14c4ce3) by tools/port_lcd_calc.py.
 * The copyright / permission notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_DRM_COLORSPACE_H
#define PARITY_LCD_DRM_COLORSPACE_H

enum drm_colorspace {
	/* For Default case, driver will set the colorspace */
	DRM_MODE_COLORIMETRY_DEFAULT 		= 0,
	/* CEA 861 Normal Colorimetry options */
	DRM_MODE_COLORIMETRY_NO_DATA		= 0,
	DRM_MODE_COLORIMETRY_SMPTE_170M_YCC	= 1,
	DRM_MODE_COLORIMETRY_BT709_YCC		= 2,
	/* CEA 861 Extended Colorimetry Options */
	DRM_MODE_COLORIMETRY_XVYCC_601		= 3,
	DRM_MODE_COLORIMETRY_XVYCC_709		= 4,
	DRM_MODE_COLORIMETRY_SYCC_601		= 5,
	DRM_MODE_COLORIMETRY_OPYCC_601		= 6,
	DRM_MODE_COLORIMETRY_OPRGB		= 7,
	DRM_MODE_COLORIMETRY_BT2020_CYCC	= 8,
	DRM_MODE_COLORIMETRY_BT2020_RGB		= 9,
	DRM_MODE_COLORIMETRY_BT2020_YCC		= 10,
	/* Additional Colorimetry extension added as part of CTA 861.G */
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65	= 11,
	DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER	= 12,
	/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
	DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED	= 13,
	DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT	= 14,
	DRM_MODE_COLORIMETRY_BT601_YCC		= 15,
	DRM_MODE_COLORIMETRY_COUNT
};

#endif /* PARITY_LCD_DRM_COLORSPACE_H */
