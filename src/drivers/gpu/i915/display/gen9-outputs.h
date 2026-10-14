/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The external outputs of a Gen9 display (gen9-outputs.c, WS237 stage 3):
 * which of ports B to D have a sink, what it is (DP or HDMI) and the mode
 * it is shown, and lighting and darkening one on a pipe beside the panel
 * the firmware lit, with the watermarks and DDB of every lit pipe moved
 * in an order that never lets two pipes share a block.  Everything goes
 * through the register table (gen9-io.h), so the host tests run it on the
 * model of the hardware; the kernel's node (gen9-node.c) calls it under
 * its mutex.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_OUTPUTS_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_OUTPUTS_H

#include "gen9-io.h"
#include "gen9-link.h"
#include "gen9-mode.h"
#include "gen9-modeset.h"

#include <stdint.h>

/* The external ports: B, C and D, kept at index port - 1. */
#define I915_SKL_OUTPUTS		3U

/* The largest mode an external output shows (the display window holds two buffers of it per output). */
#define I915_SKL_OUTPUT_MAX_WIDTH	4096U
#define I915_SKL_OUTPUT_MAX_HEIGHT	2160U

/* The bytes of EDID kept: the base block and one extension. */
#define I915_SKL_OUTPUT_EDID_BYTES	256U

/*
 * The panel the firmware lit, as the external outputs share the display
 * with it: its pipe, port and DPLL, and its plane's width, line total and
 * pixel clock, which its watermarks are computed from.
 */
struct i915_skl_panel {
	unsigned pipe;
	unsigned port;
	unsigned dpll;
	uint32_t hdisplay;
	uint32_t htotal;
	uint32_t pixel_khz;
};

/*
 * One external port: whether a sink is attached and what it is, the mode
 * it is shown, and whether it is lit and how.
 */
struct i915_skl_output {
	/* The port, whether a sink is attached, its kind (0, DP or HDMI) and the generation of its attachment. */
	unsigned port;
	int connected;
	int type;
	uint64_t generation;

	/* Its EDID (none: zero bytes) and the mode chosen from it. */
	uint8_t edid[I915_SKL_OUTPUT_EDID_BYTES];
	unsigned edid_bytes;
	struct i915_lcd_mode mode;

	/* A DP sink's receiver capabilities and link limits. */
	struct i915_skl_dp_port dp;
	uint32_t sink_max_khz;
	unsigned sink_lanes;

	/* Lit: on what, at what. */
	int lit;
	struct i915_skl_output_config config;
};

/*
 * The external outputs of one display.
 *
 * Made by drv_i915_skl_outputs_init() and changed only through these
 * functions, under the caller's lock.
 */
struct i915_skl_outputs {
	const struct i915_skl_io *io;
	struct i915_skl_panel panel;

	/* The part's power class and platform, the CD clock (kept as the firmware set it), and the memory latencies. */
	int sku;
	int platform;
	uint32_t cdclk_khz;
	uint16_t latency[8];

	/* The ports the board has (bit per port) and those with a sink, and the last generation given. */
	unsigned present;
	unsigned connected;
	uint64_t generation;

	struct i915_skl_output output[I915_SKL_OUTPUTS];
};

/*
 * Decodes the memory latencies PCODE answered (GEN9_PCODE_READ_MEM_LATENCY:
 * four 8-bit levels in each word) and adjusts them as Linux's
 * skl_read_wm_latency() and skl_wm_latency() do: levels after the first
 * zero above level 0 are off, 2 us more on every level when level 0 is 0,
 * 1 us more on level 0 (the DIMMs are not known), and 4 us more on every
 * level when IPC is on (Kaby Lake and later).
 */
void drv_i915_skl_wm_latency(uint32_t low, uint32_t high, int ipc, uint16_t latency[8]);

/* Sets the outputs up beside the panel and turns on the ports' hotplug sense. */
void drv_i915_skl_outputs_init(struct i915_skl_outputs *outputs, const struct i915_skl_io *io, const struct i915_skl_panel *panel, int sku, int platform, uint32_t cdclk_khz, const uint16_t latency[8]);

/*
 * Reads which ports have a sink and probes those that changed.  Returns
 * the ports (bit per port) whose attachment changed; a lit output whose
 * sink went stays lit for the caller to darken.
 */
unsigned drv_i915_skl_outputs_poll(struct i915_skl_outputs *outputs);

/* Lights an output on a free pipe and DPLL, its plane on a surface: 0, ENOSPC, ENOENT, or the mode set's error. */
int drv_i915_skl_outputs_light(struct i915_skl_outputs *outputs, unsigned index, uint32_t surf, uint32_t stride_units, int rgbx);

/* Darkens a lit output and gives its pipe's buffer back to the others. */
void drv_i915_skl_outputs_darken(struct i915_skl_outputs *outputs, unsigned index);

/* Points a lit output's plane at a surface, which takes effect at its next vertical blank. */
void drv_i915_skl_outputs_flip(struct i915_skl_outputs *outputs, unsigned index, uint32_t surf);

/* Reports the surface a lit output's plane scans out now. */
uint32_t drv_i915_skl_outputs_live(struct i915_skl_outputs *outputs, unsigned index);

/* Reports an output's mode's refresh in millihertz, or 0 without a mode. */
uint32_t drv_i915_skl_output_refresh(const struct i915_skl_output *output);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_OUTPUTS_H */
