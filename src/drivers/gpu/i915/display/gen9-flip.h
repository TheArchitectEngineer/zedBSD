/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The flip display of a Gen9 node (gen9-flip.c).
 *
 * A Gen9 or Gen9.5 node keeps the mode the firmware set.  When the
 * firmware lit exactly one pipe whose first plane scans out a linear
 * XRGB8888 surface, the boot framebuffer display (bootfb.c) shows the
 * desktop by flipping that plane between two buffers of the driver's own
 * instead of copying every frame into the firmware's framebuffer: the GPU
 * draws a frame into the buffer the plane does not show, and the plane's
 * surface register is pointed at it, which takes effect at the next
 * vertical blank.  Nothing else of the display is written: the mode, the
 * clocks, the watermarks and the plane's control stay the firmware's.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_FLIP_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_FLIP_H

#include <stdint.h>

struct gpu_display_present;
struct i915_device;
struct i915_skl_panel;

/* How long a flip may take to latch before the flips are given up: six frames at 60 Hz. */
#define I915_GEN9_FLIP_LATCH_MS		100U

/* The refresh reported when the firmware's timing cannot be read. */
#define I915_GEN9_FLIP_REFRESH_ASSUMED	60000U

/* The scratch pages the display window keeps on each side of a buffer (the scanout buffer's VT-d guard). */
#define I915_GEN9_FLIP_GUARD_PAGES	168U

/* The pages a buffer's 256 KiB alignment may skip in the display window. */
#define I915_GEN9_FLIP_ALIGN_PAGES	64U

/*
 * The display registers the firmware left, as read before anything is
 * written.
 *
 * Filled by gen9-flip.c from the hardware, or by a host test by hand; a
 * transcoder or a pipe whose power well is down is not read and its
 * fields stay zero.  The transcoders are A, B, C and EDP (index 3); the
 * pipes are A, B and C.
 */
struct i915_skl_registers {
	/* The power wells' state, the DC states the DMC may enter, and the CD clock. */
	uint32_t power;
	uint32_t dc_state;
	uint32_t cdclk;

	/* The DPLL link rates and the DDI ports' clock selects. */
	uint32_t dpll_ctrl1;
	uint32_t dpll_ctrl2;

	/* Every transcoder: whether it was read, its DDI function, configuration, totals and DP link M and N. */
	int transcoder_read[4];
	uint32_t ddi_func[4];
	uint32_t transconf[4];
	uint32_t htotal[4];
	uint32_t vtotal[4];
	uint32_t link_m[4];
	uint32_t link_n[4];

	/* The eDP transcoder's PSR control, and the frame buffer compressor's control. */
	uint32_t psr_ctl;
	uint32_t fbc_ctl;

	/* Every pipe: whether it was read, its source size and its first plane, and its two scalers. */
	int pipe_read[3];
	uint32_t pipe_src[3];
	uint32_t plane_ctl[3];
	uint32_t plane_stride[3];
	uint32_t plane_pos[3];
	uint32_t plane_size[3];
	uint32_t plane_surf[3];
	uint32_t plane_offset[3];
	uint32_t plane_surflive[3];
	uint32_t scaler[3][2];
};

/*
 * What the firmware's display is, as the flip display uses it.
 *
 * Made by drv_i915_skl_readout_decode() from the registers.  reason names
 * the first condition that keeps the flips from being used, or is NULL
 * when they can be; the other fields hold what was found either way.
 */
struct i915_skl_readout {
	/* Why the flips cannot be used (a short word for the log), or NULL. */
	const char *reason;

	/* How many pipes the firmware lit, and the one used: its transcoder (3 = EDP). */
	unsigned lit_pipes;
	unsigned pipe;
	unsigned transcoder;

	/* The plane's extent in pixels and its row bytes. */
	uint32_t width;
	uint32_t height;
	uint32_t stride_bytes;

	/* Nonzero when a pixel is R, G, B, X in memory; zero for B, G, R, X. */
	int rgbx;

	/* The surface the firmware's plane scans out (a GGTT offset). */
	uint32_t surf;

	/* The refresh in millihertz, and whether it was measured from the timing (else assumed). */
	uint32_t refresh_millihz;
	int refresh_measured;

	/* The pixel clock in kHz the timing gave, or zero; with it, the line's total, the DDI port and the DPLL clocking it. */
	uint32_t pixel_khz;
	uint32_t htotal;
	unsigned port;
	unsigned dpll;

	/* Nonzero when one of the pipe's scalers is on (the firmware's mode is scaled to the panel). */
	int scaled;
};

/*
 * ==== The arithmetic (host-tested) ====
 */

/* Decodes the firmware's display registers and decides whether the flips can be used. */
int drv_i915_skl_readout_decode(const struct i915_skl_registers *registers, struct i915_skl_readout *readout);

/* Reports the display window pages two buffers of the given size need. */
unsigned drv_i915_skl_flip_window_pages(uint32_t buffer_bytes);

/* Reports the pixel clock in kHz of a DP link: link M times the port clock over link N, or zero. */
uint32_t drv_i915_skl_dp_pixel_khz(uint32_t link_m, uint32_t link_n, uint32_t port_khz);

/* Reports the port clock in kHz of a DPLL_CTRL1 link rate, or zero for one not known. */
uint32_t drv_i915_skl_port_khz(unsigned link_rate);

/*
 * ==== The node (bootfb.c calls these under the display's lease mutex) ====
 */

/* Reads the firmware's display and makes the two buffers; 0 when the flips are used. */
int drv_i915_gen9_flip_start(struct i915_device *device, uint64_t fb_physical);

/* Reports the extent and the refresh the flips show. */
void drv_i915_gen9_flip_extent(uint32_t *width, uint32_t *height, uint32_t *refresh_millihz);

/* Describes the flipped panel for the external outputs: 0, or ENOENT without flips or a measured pixel clock. */
int drv_i915_gen9_flip_panel(struct i915_skl_panel *panel);

/* Readies the buffers and the flip interrupt for a new lease. */
void drv_i915_gen9_flip_claim(struct i915_device *device);

/* Draws a frame into the buffer the plane does not show and flips to it (see drv_i915_gen9_flip_failed for its errors). */
int drv_i915_gen9_flip_present(struct i915_device *device, void *session, void *object, const struct gpu_display_present *request);

/* Waits until the newest flip has latched; reports the tick it latched at. */
int drv_i915_gen9_flip_wait(uint64_t *latched_tick);

/* Reports nonzero when the error a presentation or a wait just returned was the flips' own. */
int drv_i915_gen9_flip_failed(void);

/* Checks after a sleep in D0 that the flipped panel still runs, and logs it. */
void drv_i915_gen9_flip_resume(struct i915_device *device);

/* Gives the plane back the firmware's surface at the end of a lease. */
int drv_i915_gen9_flip_release(struct i915_device *device);

/* Gives the flips up for the rest of the boot after a flip did not latch. */
void drv_i915_gen9_flip_stop(struct i915_device *device);

/* Frees the buffers once the plane is shown not to read them. */
void drv_i915_gen9_flip_fini(struct i915_device *device);

/* Acknowledges the flip's pipe interrupt and wakes the waiter; runs in the interrupt handler. */
void drv_i915_gen9_flip_irq(uint32_t master_ctl);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_FLIP_H */
