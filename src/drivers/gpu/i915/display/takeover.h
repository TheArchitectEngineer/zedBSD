/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The takeover of the display the firmware left running (takeover.c).
 *
 * The output setup and its readout and sanitize, the VGA plane, and the
 * readout,
 * sanitize and takeover of the Linux text on the takeover registry.  Only the
 * functions whose arguments are neutral display types are declared here;
 * the registry accessors that take the modeset environment's types are in
 * takeover-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H
#define DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H

#include "internal.h"

struct i915_xd_platform;

struct i915_display;

/*
 * ==== The takeover registry and its world ====
 */

/* Allocates and frees the takeover world (display->takeover_world); create returns 0 or ENOMEM. */
int drv_i915_takeover_world_create(struct i915_display *display);
void drv_i915_takeover_world_destroy(struct i915_display *display);

/*
 * ==== The output setup and the VGA plane ====
 */

struct drv_pci_device;
struct zbl6_framebuffer;

/* The output setup: the front of the display probe and the crtc, DPLL and encoder records. */
int drv_i915_display_nogem_front(struct i915_display *display, struct i915_display_nogem *d, int display_ver, unsigned pipe_mask, struct i915_mmio *m, struct mutex *sb_lock, struct i915_cdclk_dev *cd, struct i915_bw_state *bw, struct i915_vga_client *vga);
void drv_i915_display_nogem_fini(struct i915_display_nogem *d);
void drv_i915_adjust_wm_latency(uint16_t wm[], int num_levels, int read_latency, int wm_lv_0_adjust_needed);
void drv_i915_skl_setup_wm_latency(struct i915_display_nogem *d, int display_ver, struct mutex *sb_lock, struct i915_mmio *m, int wm_lv_0_adjust_needed);
void drv_i915_shared_dpll_init(struct i915_display_nogem *d, const struct i915_xd_platform *platform);
int drv_i915_crtc_init(struct i915_display_nogem *d, int display_ver, unsigned pipe);
void drv_i915_display_wa_apply(struct i915_display_nogem *d, struct i915_mmio *m, int display_ver, int is_alderlake_p);
void drv_i915_update_max_cdclk(struct i915_display_nogem *d, int display_ver, uint32_t cdclk_ref);
void drv_i915_setup_outputs(struct i915_display_nogem *d, int display_ver, unsigned port_mask, const struct i915_vbt_state *vbt, struct i915_mmio *m);
int drv_i915_ddi_crt_present(int display_ver);
int drv_i915_dvo_port_to_port(int display_ver, uint8_t dvo_port);
int drv_i915_port_to_phy(int display_ver, int port);
int drv_i915_phy_is_tc(int display_ver, int phy);
int drv_i915_ddi_is_tc(int display_ver, int port);

/* The VGA arbiter client and the VGA plane. */
int drv_i915_gmch_vga_set_state(struct i915_vga_client *c, int enable_decode);
unsigned drv_i915_gmch_vga_set_decode(struct i915_vga_client *c, int enable_decode);
int drv_i915_vga_register(struct i915_vga_client *c, struct drv_pci_device *gpu, unsigned display_ver, struct i915_trace *trace);
void drv_i915_vga_unregister(struct i915_vga_client *c);
void drv_i915_vga_io_test_set(struct i915_display *display, const struct i915_vga_io_ops *ops);
void drv_i915_vga_reset_io_mem(struct i915_vga_client *c);
int drv_i915_vga_disable(struct i915_display *display, struct i915_vga_client *c, struct i915_mmio *m);

#endif /* DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H */
