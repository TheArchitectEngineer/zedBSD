/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The choice of the resident node's output (output.c, ws075-p012; the GPU
 * scanout rule since ws113-p002).
 *
 * The resident node lights only the output the firmware (GOP) was
 * scanning out: the eDP panel, or the HDMI display of DDI B on pipe B in
 * DVI mode.  The firmware's output on another interface is not lit by this
 * driver: the display is left as the firmware left it.  display= no longer
 * chooses.  display.mode=WxH[@R] picks the HDMI mode; without it the
 * sink's EDID picks its preferred mode.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_OUTPUT_H
#define DRIVERS_GPU_I915_DISPLAY_OUTPUT_H

#include "internal.h"

/* The HDMI output: port B, pipe B, transcoder B. */
#define I915_OUTPUT_HDMI_PORT		1
#define I915_OUTPUT_HDMI_PIPE		1

/*
 * An external DisplayPort display's pipe and transcoder (ws051-p004b):
 * pipe B, as the HDMI output's (one output at a time, the DBUF and
 * watermark combination already proved on pipe B).
 */
#define I915_OUTPUT_DP_EXT_PIPE		1

/*
 * Reads the firmware's output from what it left on the display (the N0
 * report): the first lit pipe whose transcoder drives a port, and every
 * lit pipe.  Pure: reads only the report.
 */
void drv_i915_gop_output_read(const struct i915_native_report *report, struct i915_gop_output *gop);

/*
 * Names the firmware's output for the log ("eDP on DDI A", "HDMI on DDI B",
 * "DP SST on DDI TC1", "none") into a buffer of at least 32 bytes.
 */
void drv_i915_gop_output_name(const struct i915_gop_output *gop, char *name, unsigned size);

/*
 * Chooses the output of the resident node: the firmware's output (the GPU
 * scanout rule); the choice is logged and kept in display->output.
 */
void drv_i915_display_output_select(struct i915_display *display);

/*
 * Reports the mode of the chosen output: the HDMI mode, or the panel's
 * mode read from the resident eDP.  0, or ENXIO when the node has no
 * output.
 */
int drv_i915_display_output_mode(struct i915_display *display, uint32_t *width, uint32_t *height, uint32_t *refresh_millihz);

/*
 * Reports the physical size of the chosen output in millimetres: 0, or
 * ENODEV when it is not known.
 */
int drv_i915_display_output_size_mm(struct i915_display *display, uint32_t *width_mm, uint32_t *height_mm);

/* Names the chosen output for the display query: "HDMI" or "eDP panel". */
const char *drv_i915_display_output_name(const struct i915_display *display);
int drv_i915_display_output_prepare(struct i915_display *display, unsigned connector, struct i915_display_output *output, const char **reason);
unsigned drv_i915_display_output_pipe(const struct i915_display_output *output);
void drv_i915_display_output_panel(struct i915_display *display, struct i915_display_output *output);

/*
 * Decodes one EDID detailed timing descriptor (18 bytes) into a mode: 0,
 * or EINVAL for a descriptor that is not a progressive detailed timing.
 */
int drv_i915_output_edid_timing(const uint8_t *descriptor, struct i915_lcd_mode *mode);

/*
 * Computes the CVT reduced-blanking (version 1) timing of a mode: 0, or
 * EINVAL for a size or rate the formula cannot serve.
 */
int drv_i915_output_cvt_rb(uint32_t width, uint32_t height, uint32_t refresh_hz, struct i915_lcd_mode *mode);

/*
 * Picks the HDMI mode from display.mode= (width, height and refresh 0
 * when not given; refresh 0 also when given without @R) and the sink's
 * EDID (NULL when none was read): 0 with the mode and where it came from,
 * or EINVAL.
 */
int drv_i915_output_pick_mode(uint32_t want_width, uint32_t want_height, uint32_t want_refresh_hz, const uint8_t *edid, unsigned edid_size, struct i915_lcd_mode *mode, const char **source);

#endif /* DRIVERS_GPU_I915_DISPLAY_OUTPUT_H */
