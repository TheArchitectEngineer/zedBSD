/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

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
 * The connector definitions of Linux drm_connector.h: the colorimetry
 * values (for the modeset environment) and the connector status and poll
 * flags (for the hotplug environment).  The DRM prefix of the names is
 * dropped: enum drm_colorspace is enum colorspace, DRM_MODE_COLORIMETRY_* is
 * MODE_COLORIMETRY_*, enum drm_connector_status is enum connector_status and
 * DRM_CONNECTOR_POLL_* is CONNECTOR_POLL_*.
 *
 * The connector colorimetry values (Linux include/drm/drm_connector.h).
 * zedBSD WS031: enum drm_colorspace extracted textually from Linux v6.8.12 include/drm/drm_connector.h
 * (sha256 1eb905598bb46d733afe49917276689bfbef7284f3395ffa72131d4ef14c4ce3) by tools/port_lcd_calc.py.
 * The copyright / permission notice above is the source file's own.
 *
 * The connector status and poll flags (Linux include/drm/drm_connector.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference include/drm/drm_connector.h
 * (sha256 1eb905598bb46d733afe49917276689bfbef7284f3395ffa72131d4ef14c4ce3) by tools/port_lcd_calc.py:
 * enum drm_connector_status, DRM_CONNECTOR_POLL_*.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_CONNECTOR_H
#define DRIVERS_GPU_I915_INTEL_CONNECTOR_H

/* The connector colorimetry values (Linux include/drm/drm_connector.h). */

enum colorspace {
	/* For Default case, driver will set the colorspace */
	MODE_COLORIMETRY_DEFAULT 		= 0,
	/* CEA 861 Normal Colorimetry options */
	MODE_COLORIMETRY_NO_DATA		= 0,
	MODE_COLORIMETRY_SMPTE_170M_YCC	= 1,
	MODE_COLORIMETRY_BT709_YCC		= 2,
	/* CEA 861 Extended Colorimetry Options */
	MODE_COLORIMETRY_XVYCC_601		= 3,
	MODE_COLORIMETRY_XVYCC_709		= 4,
	MODE_COLORIMETRY_SYCC_601		= 5,
	MODE_COLORIMETRY_OPYCC_601		= 6,
	MODE_COLORIMETRY_OPRGB		= 7,
	MODE_COLORIMETRY_BT2020_CYCC	= 8,
	MODE_COLORIMETRY_BT2020_RGB		= 9,
	MODE_COLORIMETRY_BT2020_YCC		= 10,
	/* Additional Colorimetry extension added as part of CTA 861.G */
	MODE_COLORIMETRY_DCI_P3_RGB_D65	= 11,
	MODE_COLORIMETRY_DCI_P3_RGB_THEATER	= 12,
	/* Additional Colorimetry Options added for DP 1.4a VSC Colorimetry Format */
	MODE_COLORIMETRY_RGB_WIDE_FIXED	= 13,
	MODE_COLORIMETRY_RGB_WIDE_FLOAT	= 14,
	MODE_COLORIMETRY_BT601_YCC		= 15,
	MODE_COLORIMETRY_COUNT
};

/* The connector status and poll flags (Linux include/drm/drm_connector.h). */

enum connector_status {
	/**
	 * @connector_status_connected: The connector is definitely connected to
	 * a sink device, and can be enabled.
	 */
	connector_status_connected = 1,
	/**
	 * @connector_status_disconnected: The connector isn't connected to a
	 * sink device which can be autodetect. For digital outputs like DP or
	 * HDMI (which can be realiable probed) this means there's really
	 * nothing there. It is driver-dependent whether a connector with this
	 * status can be lit up or not.
	 */
	connector_status_disconnected = 2,
	/**
	 * @connector_status_unknown: The connector's status could not be
	 * reliably detected. This happens when probing would either cause
	 * flicker (like load-detection when the connector is in use), or when a
	 * hardware resource isn't available (like when load-detection needs a
	 * free CRTC). It should be possible to light up the connector with one
	 * of the listed fallback modes. For default configuration userspace
	 * should only try to light up connectors with unknown status when
	 * there's not connector with @connector_status_connected.
	 */
	connector_status_unknown = 3,
};

#define CONNECTOR_POLL_HPD (1 << 0)
#define CONNECTOR_POLL_CONNECT (1 << 1)
#define CONNECTOR_POLL_DISCONNECT (1 << 2)

#endif /* DRIVERS_GPU_I915_INTEL_CONNECTOR_H */
