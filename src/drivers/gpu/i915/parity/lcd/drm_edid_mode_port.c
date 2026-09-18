/*
 * Copyright (c) 2006 Luc Verhaegen (quirks list)
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2010 Red Hat, Inc.
 *
 * DDC probing routines (drm_ddc_read & drm_do_probe_ddc_edid) originally from
 * FB layer.
 *   Copyright (C) 2006 Dennis Munsie <dmunsie@cecropia.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/drm_edid.c
 * (sha256 a01138078180d234149ac4403839a99b9c85232955a9830ad5552637cd8661a7) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept, in the reference order: the EDID_QUIRK_* bit definitions, drm_mode_do_interlace_quirk(),
 *    drm_mode_detailed();
 *  - the includes are replaced by lcd_compat.h (connector / mode objects, logging, drm_mode_create);
 *  - parity_edid_mode_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/ and drm/ includes */

#define EDID_QUIRK_PREFER_LARGE_60		(1 << 0)
#define EDID_QUIRK_135_CLOCK_TOO_HIGH		(1 << 1)
#define EDID_QUIRK_PREFER_LARGE_75		(1 << 2)
#define EDID_QUIRK_DETAILED_IN_CM		(1 << 3)
#define EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE	(1 << 4)
#define EDID_QUIRK_DETAILED_SYNC_PP		(1 << 6)
#define EDID_QUIRK_FORCE_REDUCED_BLANKING	(1 << 7)
#define EDID_QUIRK_FORCE_8BPC			(1 << 8)
#define EDID_QUIRK_FORCE_12BPC			(1 << 9)
#define EDID_QUIRK_FORCE_6BPC			(1 << 10)
#define EDID_QUIRK_FORCE_10BPC			(1 << 11)
#define EDID_QUIRK_NON_DESKTOP			(1 << 12)
#define EDID_QUIRK_CAP_DSC_15BPP		(1 << 13)

/*
 * EDID is delightfully ambiguous about how interlaced modes are to be
 * encoded.  Our internal representation is of frame height, but some
 * HDTV detailed timings are encoded as field height.
 *
 * The format list here is from CEA, in frame size.  Technically we
 * should be checking refresh rate too.  Whatever.
 */
static void
drm_mode_do_interlace_quirk(struct drm_display_mode *mode,
			    const struct detailed_pixel_timing *pt)
{
	int i;
	static const struct {
		int w, h;
	} cea_interlaced[] = {
		{ 1920, 1080 },
		{  720,  480 },
		{ 1440,  480 },
		{ 2880,  480 },
		{  720,  576 },
		{ 1440,  576 },
		{ 2880,  576 },
	};

	if (!(pt->misc & DRM_EDID_PT_INTERLACED))
		return;

	for (i = 0; i < ARRAY_SIZE(cea_interlaced); i++) {
		if ((mode->hdisplay == cea_interlaced[i].w) &&
		    (mode->vdisplay == cea_interlaced[i].h / 2)) {
			mode->vdisplay *= 2;
			mode->vsync_start *= 2;
			mode->vsync_end *= 2;
			mode->vtotal *= 2;
			mode->vtotal |= 1;
		}
	}

	mode->flags |= DRM_MODE_FLAG_INTERLACE;
}

/*
 * Create a new mode from an EDID detailed timing section. An EDID detailed
 * timing block contains enough info for us to create and return a new struct
 * drm_display_mode.
 */
static struct drm_display_mode *drm_mode_detailed(struct drm_connector *connector,
						  const struct drm_edid *drm_edid,
						  const struct detailed_timing *timing)
{
	const struct drm_display_info *info = &connector->display_info;
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode;
	const struct detailed_pixel_timing *pt = &timing->data.pixel_data;
	unsigned hactive = (pt->hactive_hblank_hi & 0xf0) << 4 | pt->hactive_lo;
	unsigned vactive = (pt->vactive_vblank_hi & 0xf0) << 4 | pt->vactive_lo;
	unsigned hblank = (pt->hactive_hblank_hi & 0xf) << 8 | pt->hblank_lo;
	unsigned vblank = (pt->vactive_vblank_hi & 0xf) << 8 | pt->vblank_lo;
	unsigned hsync_offset = (pt->hsync_vsync_offset_pulse_width_hi & 0xc0) << 2 | pt->hsync_offset_lo;
	unsigned hsync_pulse_width = (pt->hsync_vsync_offset_pulse_width_hi & 0x30) << 4 | pt->hsync_pulse_width_lo;
	unsigned vsync_offset = (pt->hsync_vsync_offset_pulse_width_hi & 0xc) << 2 | pt->vsync_offset_pulse_width_lo >> 4;
	unsigned vsync_pulse_width = (pt->hsync_vsync_offset_pulse_width_hi & 0x3) << 4 | (pt->vsync_offset_pulse_width_lo & 0xf);

	/* ignore tiny modes */
	if (hactive < 64 || vactive < 64)
		return NULL;

	if (pt->misc & DRM_EDID_PT_STEREO) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Stereo mode not supported\n",
			    connector->base.id, connector->name);
		return NULL;
	}
	if (!(pt->misc & DRM_EDID_PT_SEPARATE_SYNC)) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Composite sync not supported\n",
			    connector->base.id, connector->name);
	}

	/* it is incorrect if hsync/vsync width is zero */
	if (!hsync_pulse_width || !vsync_pulse_width) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] Incorrect Detailed timing. Wrong Hsync/Vsync pulse width\n",
			    connector->base.id, connector->name);
		return NULL;
	}

	if (info->quirks & EDID_QUIRK_FORCE_REDUCED_BLANKING) {
		mode = drm_cvt_mode(dev, hactive, vactive, 60, true, false, false);
		if (!mode)
			return NULL;

		goto set_size;
	}

	mode = drm_mode_create(dev);
	if (!mode)
		return NULL;

	if (info->quirks & EDID_QUIRK_135_CLOCK_TOO_HIGH)
		mode->clock = 1088 * 10;
	else
		mode->clock = le16_to_cpu(timing->pixel_clock) * 10;

	mode->hdisplay = hactive;
	mode->hsync_start = mode->hdisplay + hsync_offset;
	mode->hsync_end = mode->hsync_start + hsync_pulse_width;
	mode->htotal = mode->hdisplay + hblank;

	mode->vdisplay = vactive;
	mode->vsync_start = mode->vdisplay + vsync_offset;
	mode->vsync_end = mode->vsync_start + vsync_pulse_width;
	mode->vtotal = mode->vdisplay + vblank;

	/* Some EDIDs have bogus h/vsync_end values */
	if (mode->hsync_end > mode->htotal) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] reducing hsync_end %d->%d\n",
			    connector->base.id, connector->name,
			    mode->hsync_end, mode->htotal);
		mode->hsync_end = mode->htotal;
	}
	if (mode->vsync_end > mode->vtotal) {
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] reducing vsync_end %d->%d\n",
			    connector->base.id, connector->name,
			    mode->vsync_end, mode->vtotal);
		mode->vsync_end = mode->vtotal;
	}

	drm_mode_do_interlace_quirk(mode, pt);

	if (info->quirks & EDID_QUIRK_DETAILED_SYNC_PP) {
		mode->flags |= DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
	} else {
		mode->flags |= (pt->misc & DRM_EDID_PT_HSYNC_POSITIVE) ?
			DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
		mode->flags |= (pt->misc & DRM_EDID_PT_VSYNC_POSITIVE) ?
			DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
	}

set_size:
	mode->width_mm = pt->width_mm_lo | (pt->width_height_mm_hi & 0xf0) << 4;
	mode->height_mm = pt->height_mm_lo | (pt->width_height_mm_hi & 0xf) << 8;

	if (info->quirks & EDID_QUIRK_DETAILED_IN_CM) {
		mode->width_mm *= 10;
		mode->height_mm *= 10;
	}

	if (info->quirks & EDID_QUIRK_DETAILED_USE_MAXIMUM_SIZE) {
		mode->width_mm = drm_edid->edid->width_cm * 10;
		mode->height_mm = drm_edid->edid->height_cm * 10;
	}

	mode->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_set_name(mode);

	return mode;
}

#include "parity_edid_mode_glue.inc"	/* zedBSD: picks the preferred detailed timing */
