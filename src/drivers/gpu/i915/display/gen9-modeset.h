/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register sequences of the Gen9 mode set (gen9-modeset.c): the
 * power wells an external port needs, the DPLLs, lighting
 * and darkening an external DP or HDMI output on a pipe, the watermarks
 * and DDB of a pipe, the first plane of a pipe, and the hotplug sense of
 * the ports.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_MODESET_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_MODESET_H

#include "gen9-io.h"
#include "gen9-link.h"
#include "gen9-mode.h"

#include <stdint.h>

/*
 * What one external output is lit with: the port and its kind, the pipe
 * (its transcoder has the same number) and the DPLL, the mode, the DP
 * link or the HDMI clock, and the plane's first surface.
 */
struct i915_skl_output_config {
	int type;
	unsigned port;
	unsigned pipe;
	unsigned dpll;
	struct i915_display_mode mode;
	struct i915_skl_link link;
	struct i915_skl_dpll_words dpll_words;

	/* The part's power class and platform, which choose the buffer tables. */
	int sku;
	int platform;

	/* The plane: its surface (a GGTT offset), stride in 64-byte units, and whether pixels are R, G, B, X. */
	uint32_t surf;
	uint32_t stride_units;
	int rgbx;
};

/* Turns power well 2 on (pipes B and C, transcoders A to C, the AUX channels B to D): 0, or ETIMEDOUT. */
int drv_i915_skl_pw2_on(const struct i915_skl_io *io);

/* Turns a DDI port's IO power well on or off: 0, or ETIMEDOUT. */
int drv_i915_skl_ddi_io_power(const struct i915_skl_io *io, unsigned port, int on);

/* Starts a DPLL (1 to 3) with its words and waits for its lock: 0, or ETIMEDOUT. */
int drv_i915_skl_dpll_on(const struct i915_skl_io *io, unsigned id, const struct i915_skl_dpll_words *words);

/* Stops a DPLL (1 to 3). */
void drv_i915_skl_dpll_off(const struct i915_skl_io *io, unsigned id);

/*
 * Lights an external output (hsw_crtc_enable for display 9): the DPLL, the
 * port's clock and power, the link (DP: buffers, sink power, training;
 * HDMI: nothing before the transcoder), the transcoder's clock, MSA, M/N
 * and timings, the pipe, then the DDI function, the transcoder, the HDMI
 * buffer, and the plane on its first surface.  The watermarks of the pipe
 * are written before by the caller.  dp is used for DP only.  Returns 0,
 * or an errno with the output left dark.
 */
int drv_i915_skl_output_on(const struct i915_skl_io *io, const struct i915_skl_output_config *config, struct i915_skl_dp_port *dp);

/* Darkens an external output in the reverse order (the plane, the transcoder, the DDI, the clocks, the DPLL). */
void drv_i915_skl_output_off(const struct i915_skl_io *io, const struct i915_skl_output_config *config, struct i915_skl_dp_port *dp);

/* Writes a pipe's first plane's watermarks and DDB, the cursor's DDB and the line time (skl_write_plane_wm). */
void drv_i915_skl_write_pipe_wm(const struct i915_skl_io *io, unsigned pipe, const struct i915_skl_pipe_wm *wm);

/* Shows a surface on a pipe's first plane: control, stride, position, size, offset, then the surface (which arms them). */
void drv_i915_skl_plane_on(const struct i915_skl_io *io, unsigned pipe, uint32_t surf, uint32_t stride_units, uint32_t width, uint32_t height, int rgbx);

/* Turns a pipe's first plane off (its control and surface 0). */
void drv_i915_skl_plane_off(const struct i915_skl_io *io, unsigned pipe);

/* Waits for the next vertical blank of a running pipe (its frame counter moving): 0, or ETIMEDOUT. */
int drv_i915_skl_wait_vblank(const struct i915_skl_io *io, unsigned pipe);

/* Turns on the PCH's hotplug sense of ports B to D. */
void drv_i915_skl_hotplug_enable(const struct i915_skl_io *io);

/* Reports the ports (bit per port, B = bit 1) the board has and that have a sink attached now. */
unsigned drv_i915_skl_ports_present(const struct i915_skl_io *io);
unsigned drv_i915_skl_ports_connected(const struct i915_skl_io *io);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_MODESET_H */
