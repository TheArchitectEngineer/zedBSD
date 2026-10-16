/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display the firmware (the GOP) left running, read before the
 * driver's first display write (xd-takeover.c).
 *
 * The readout only reads: the power well states, every pipe whose wells
 * are on (its transcoder, its DDI function, its source size and its
 * primary plane), the PLLs, the DDI A buffer, the panel's power
 * sequencer and backlight PWM, the DDI clock map and the VGA plane.  The
 * firmware's framebuffer and the GPU's VT-d unit come from the caller's
 * environment, since neither is a display register.
 *
 * From the record alone the decision says whether the driver may use the
 * display (the walls that forbid it are recorded, the first in order is
 * the one that stops), and the classification names the output the
 * firmware lit by the ports of the platform's description: nothing, the
 * built-in panel, an HDMI or DVI port, a Type-C DisplayPort, or another
 * output the driver does not light (the firmware's picture is kept).
 *
 * The takeover stops what the firmware lit before the driver's first
 * write, in the order an output is disabled: the planes, the panel's
 * backlight, the sink, the transcoder and its DDI function, the
 * scalers and the data islands, the DDI buffer, the transcoder's clock,
 * the panel's power, the DDI clock, the PLL nobody else uses, the pipe's
 * DMC.  The parts whose state the caller keeps (the backlight, the
 * panel's sequencer, the sink, the power references) go through its
 * hooks.  The VGA plane is turned off through the legacy sequencer
 * first, then its control.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_TAKEOVER_H
#define DRIVERS_GPU_I915_DISPLAY_XD_TAKEOVER_H

#include <stdint.h>

#include "xd-dmc.h"
#include "xd-io.h"
#include "xd-platform.h"

/* The pipes the readout reads (display 12 and 13 have at most four). */
#define I915_XD_FW_PIPES		4U

/*
 * What the readout found on one pipe: its wells are off (not read), it
 * reads and is dark, it reads and is lit (its transcoder or its primary
 * plane is enabled), or it did not read as register values.
 */
#define I915_XD_FW_PIPE_OFF		0U
#define I915_XD_FW_PIPE_DARK		1U
#define I915_XD_FW_PIPE_LIT		2U
#define I915_XD_FW_PIPE_GARBLED		3U

/*
 * The walls the decision records, as bits: a lit pipe (recorded, not a
 * stop: the driver takes the firmware's output over), a pipe that did
 * not read, a firmware scanout in the GGTT pages the driver writes, a
 * VT-d unit that is enabled but does not read, one that translates, one
 * with a protected memory region, one that remaps interrupts (recorded
 * for the MSI path, not a stop), an OpRegion (recorded), and a device
 * without a description (a stop: nothing on the display is known).
 */
#define I915_XD_FW_WALL_LIT		0x0001U
#define I915_XD_FW_WALL_GARBLED		0x0002U
#define I915_XD_FW_WALL_OVERLAP		0x0004U
#define I915_XD_FW_WALL_VTD_HIDDEN	0x0008U
#define I915_XD_FW_WALL_VTD_TRANSLATES	0x0010U
#define I915_XD_FW_WALL_VTD_PROTECTS	0x0020U
#define I915_XD_FW_WALL_VTD_REMAPS	0x0040U
#define I915_XD_FW_WALL_OPREGION	0x0080U
#define I915_XD_FW_WALL_UNDESCRIBED	0x0100U

/*
 * The kinds of the firmware's output: nothing lit, the built-in panel
 * (DisplayPort SST on the panel's port), HDMI or DVI on a combo port,
 * DisplayPort SST on a Type-C port, and any other output (one the driver
 * does not light: its picture is kept).
 */
#define I915_XD_FW_OUTPUT_NONE		0U
#define I915_XD_FW_OUTPUT_PANEL		1U
#define I915_XD_FW_OUTPUT_HDMI		2U
#define I915_XD_FW_OUTPUT_DP_TC		3U
#define I915_XD_FW_OUTPUT_OTHER		4U

/*
 * The parts of the firmware's state that are not display registers, and
 * the GGTT pages the driver itself writes: whether a hypervisor runs the
 * machine (a guest sees the host's VT-d unit), the firmware's
 * framebuffer, the graphics aperture, the GGTT size and the first page
 * the driver writes, and a reader of the GPU's VT-d unit at a physical
 * address (0, or nonzero when the unit could not be mapped).
 */
struct i915_xd_fw_env {
	int hypervisor;
	int fb_present;
	uint64_t fb_base;
	uint64_t fb_size;
	uint64_t aperture_base;
	uint64_t aperture_size;
	uint32_t ggtt_pages;
	uint32_t driver_first_page;
	int (*vtd_read)(void *context, uint64_t base, uint32_t *version, uint32_t *status, uint32_t *protect);
	void *context;
};

/*
 * One pipe as the firmware left it: what the readout found, and its
 * transcoder, DDI function, source size and primary plane words.
 */
struct i915_xd_fw_pipe {
	unsigned state;
	uint32_t trans_conf;
	uint32_t ddi_function;
	uint32_t source_size;
	uint32_t plane_ctl;
	uint32_t plane_stride;
	uint32_t plane_size;
	uint32_t plane_surface;
};

/*
 * The firmware's display: the readout, the framebuffer and the VT-d unit,
 * the decision, and the output the firmware lit.  One record lives in the
 * xd display from the check before the first display write until the
 * takeover; the shell's own report is kept beside it.
 */
struct i915_xd_firmware {
	/* The well states, every pipe, and the sets of lit and unreadable pipes. */
	uint32_t well_control;
	struct i915_xd_fw_pipe pipe[I915_XD_FW_PIPES];
	unsigned lit_pipes;
	unsigned unread_pipes;

	/* Pipe A's domain: the PLLs, the DDI A buffer, the panel and its backlight, the clock map. */
	uint32_t pll_enable[2];
	uint32_t ddi_a_buffer;
	uint32_t panel_status;
	uint32_t panel_control;
	uint32_t pwm_control;
	uint32_t pwm_duty;
	uint32_t clock_map;
	uint32_t vga_control;

	/* The firmware's framebuffer and the GGTT pages it takes in the aperture. */
	int fb_present;
	uint64_t fb_base;
	uint64_t fb_size;
	int fb_in_aperture;
	uint32_t fb_first_page;
	uint32_t fb_pages;

	/* The GPU's VT-d unit. */
	int hypervisor;
	uint64_t vtd_bar;
	int vtd_enabled;
	int vtd_readable;
	uint32_t vtd_version;
	uint32_t vtd_status;
	uint32_t vtd_protect;

	/* The GGTT the driver writes, and the OpRegion's address (0 for none). */
	uint32_t ggtt_pages;
	uint32_t driver_first_page;
	uint32_t opregion;

	/* The decision: the walls, the one that stops (0 when none), whether the display is used and taken over. */
	unsigned walls;
	unsigned stop;
	int use;
	int take_over;
	const char *reason;

	/* The firmware's output: its kind, pipe, DDI (-1 for none) and DDI mode; the panel's DDI it was named by. */
	int panel_ddi;
	unsigned output;
	unsigned output_pipe;
	int output_ddi;
	unsigned output_mode;
};

/*
 * The caller's parts of a takeover, each for one pipe: the panel's
 * backlight off, the sink told before the transcoder stops (an HDMI
 * sink's scrambling) and after it (a DisplayPort sink to sleep), the
 * panel's power off, the port's power references given back after the
 * DDI is off, and the link's after the PLL.  A hook may be NULL.
 */
struct i915_xd_takeover_hooks {
	void (*backlight_off)(void *context, unsigned pipe);
	void (*sink_before)(void *context, unsigned pipe);
	void (*sink_after)(void *context, unsigned pipe);
	void (*panel_off)(void *context, unsigned pipe);
	void (*port_release)(void *context, unsigned pipe);
	void (*link_release)(void *context, unsigned pipe);
	void *context;
};

/* The waits of a takeover that ran out, as bits. */
#define I915_XD_TAKEOVER_LATE_VBLANK	0x01U
#define I915_XD_TAKEOVER_LATE_PIPE	0x02U
#define I915_XD_TAKEOVER_LATE_BUFFER	0x04U
#define I915_XD_TAKEOVER_LATE_PLL	0x08U

/*
 * What a takeover of one pipe did: the DDI and the mode it stopped, the
 * PLL it turned off (-1 for none, kept when another lit pipe uses it),
 * and the waits that ran out.
 */
struct i915_xd_takeover_report {
	int ddi;
	unsigned mode;
	int pll;
	unsigned late;
};

int drv_i915_xd_firmware_read(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_fw_env *env, uint32_t opregion, struct i915_xd_firmware *firmware);
void drv_i915_xd_firmware_decide(struct i915_xd_firmware *firmware);
void drv_i915_xd_firmware_classify(const struct i915_xd_platform *platform, int panel_ddi, struct i915_xd_firmware *firmware);
const char *drv_i915_xd_firmware_output_name(unsigned output);
void drv_i915_xd_firmware_log(const struct i915_xd_firmware *firmware);
int drv_i915_xd_takeover_stop(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_dmc *dmc, struct i915_xd_firmware *firmware, unsigned pipe, const struct i915_xd_takeover_hooks *hooks, struct i915_xd_takeover_report *report);
/*
 * What the sanitize of the firmware's display changed: the pipes whose
 * DMC it switched on, the ports whose clock it gated, the PLLs it turned
 * off (bit n = the platform's PLL n), whether the frame buffer compressor
 * was turned off, and whether PLLs were left because a lit port's PLL
 * could not be read.
 */
struct i915_xd_sanitize_report {
	unsigned dmc_pipes;
	unsigned clocks_gated;
	unsigned plls_off;
	int fbc_off;
	int plls_kept;
};

void drv_i915_xd_firmware_sanitize(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_dmc *dmc, const struct i915_xd_firmware *firmware, struct i915_xd_sanitize_report *report);
int drv_i915_xd_vga_off(struct i915_xd_io *io, int (*screen_off)(void *context), void *context);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_TAKEOVER_H */
