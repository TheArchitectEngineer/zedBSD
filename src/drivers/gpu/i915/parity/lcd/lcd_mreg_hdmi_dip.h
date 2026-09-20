/* Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/i915_reg.h
 * (sha256 82d048e09a8472af07e7d3e9db146cd849988a10d8836e64116ff26a6e6b3ed3) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_HDMI_DIP_H
#define PARITY_LCD_MREG_HDMI_DIP_H

#define   VIDEO_DIP_ENABLE_DRM_GLK	(1 << 28)
#define   VIDEO_DIP_ENABLE_VSC_HSW	(1 << 20)
#define   VIDEO_DIP_ENABLE_GCP_HSW	(1 << 16)
#define   VIDEO_DIP_ENABLE_AVI_HSW	(1 << 12)
#define   VIDEO_DIP_ENABLE_VS_HSW	(1 << 8)
#define   VIDEO_DIP_ENABLE_GMP_HSW	(1 << 4)
#define   VIDEO_DIP_ENABLE_SPD_HSW	(1 << 0)

#endif /* PARITY_LCD_MREG_HDMI_DIP_H */
