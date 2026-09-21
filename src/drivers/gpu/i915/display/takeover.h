/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The takeover of the display the firmware left running (takeover.c).
 *
 * The output setup and its readout and sanitize, the VGA plane, the check of
 * the firmware display before the first display write, and the readout,
 * sanitize and takeover of the Linux text on the takeover registry.  Only the
 * functions whose arguments are neutral display types are declared here;
 * the registry accessors that take the modeset environment's types are in
 * takeover-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H
#define DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H

#include "internal.h"

struct i915_display;

/*
 * ==== The takeover registry and its world ====
 */

/* Allocates and frees the takeover world (display->takeover_world); create returns 0 or ENOMEM. */
int drv_i915_takeover_world_create(struct i915_display *display);
void drv_i915_takeover_world_destroy(struct i915_display *display);

/*
 * The readout and sanitize of the firmware's display on the registry (0,
 * EINVAL, EBUSY: a readout is live, ENXIO: no screen is bound), the takeover
 * that stops every active crtc (0, EINVAL, EBUSY: a pipe is still active),
 * and the release of the registry.
 */
int drv_i915_n1_readout(struct i915_display *display, const struct i915_lcd_modeset_cfg *cfg, struct i915_lcd_emit *ops, struct i915_n1_report *out);
int drv_i915_n1_takeover(struct i915_display *display, struct i915_n1_report *out);
void drv_i915_n1_release(struct i915_display *display);

/*
 * ==== The output setup, the VGA plane and the firmware display check ====
 */

struct drv_pci_device;
struct zbl6_framebuffer;

/* The output setup: the front of the display probe and the crtc, DPLL and encoder records. */
int drv_i915_display_nogem_front(struct i915_display *display, struct i915_display_nogem *d, int display_ver, unsigned pipe_mask, struct i915_mmio *m, struct mutex *sb_lock, struct i915_cdclk_dev *cd, struct i915_bw_state *bw, struct i915_vga_client *vga);
void drv_i915_display_nogem_fini(struct i915_display_nogem *d);
void drv_i915_adjust_wm_latency(uint16_t wm[], int num_levels, int read_latency, int wm_lv_0_adjust_needed);
void drv_i915_skl_setup_wm_latency(struct i915_display_nogem *d, int display_ver, struct mutex *sb_lock, struct i915_mmio *m, int wm_lv_0_adjust_needed);
void drv_i915_shared_dpll_init(struct i915_display_nogem *d, int display_ver, int is_alderlake_p);
int drv_i915_crtc_init(struct i915_display_nogem *d, int display_ver, unsigned pipe);
void drv_i915_display_wa_apply(struct i915_display_nogem *d, struct i915_mmio *m, int display_ver, int is_alderlake_p);
void drv_i915_update_max_cdclk(struct i915_display_nogem *d, int display_ver, uint32_t cdclk_ref);
void drv_i915_setup_outputs(struct i915_display_nogem *d, int display_ver, unsigned port_mask, const struct i915_vbt_state *vbt, struct i915_mmio *m);
int drv_i915_ddi_crt_present(int display_ver);
int drv_i915_dvo_port_to_port(int display_ver, uint8_t dvo_port);
int drv_i915_port_to_phy(int display_ver, int port);
int drv_i915_phy_is_tc(int display_ver, int phy);
int drv_i915_ddi_is_tc(int display_ver, int port);
int drv_i915_ddi_get_hw_state(struct i915_display_nogem *d, struct i915_encoder *e, struct i915_mmio *m, unsigned pipe_mask_avail, unsigned *pipe_out);
int drv_i915_ddi_is_clock_enabled(struct i915_encoder *e, struct i915_mmio *m);
void drv_i915_ddi_disable_clock(struct i915_display_nogem *d, struct i915_encoder *e, struct i915_mmio *m);

/* The readout and sanitize of the output records. */
unsigned drv_i915_hsw_enabled_transcoders(struct i915_display_nogem *d, int display_ver, unsigned pipe, struct i915_mmio *m, struct i915_power_domains *pd, struct i915_pw_ctx *pwc);
void drv_i915_modeset_readout_hw_state(struct i915_display_nogem *d, int display_ver, struct i915_mmio *m, struct i915_power_domains *pd, struct i915_pw_ctx *pwc);
void drv_i915_dpll_readout(struct i915_display_nogem *d, struct i915_mmio *m);
void drv_i915_modeset_sanitize_hw_state(struct i915_display_nogem *d, int display_ver, int display_step, unsigned fbc_mask, struct i915_mmio *m, struct i915_power_domains *pd, struct i915_pw_ctx *pwc);
void drv_i915_nogem_dpll_sanitize_state(struct i915_display_nogem *d, struct i915_mmio *m, int display_ver, int display_step);

/* The VGA arbiter client and the VGA plane. */
int drv_i915_gmch_vga_set_state(struct i915_vga_client *c, int enable_decode);
unsigned drv_i915_gmch_vga_set_decode(struct i915_vga_client *c, int enable_decode);
int drv_i915_vga_register(struct i915_vga_client *c, struct drv_pci_device *gpu, unsigned display_ver, struct i915_trace *trace);
void drv_i915_vga_unregister(struct i915_vga_client *c);
void drv_i915_vga_io_test_set(struct i915_display *display, const struct i915_vga_io_ops *ops);
void drv_i915_vga_reset_io_mem(struct i915_vga_client *c);
int drv_i915_vga_disable(struct i915_display *display, struct i915_vga_client *c, struct i915_mmio *m);

/* The firmware display check (N0) and the OpRegion data. */
int drv_i915_native_precheck(struct i915_display *display, const struct i915_native_deps *d, struct i915_native_report *r);
void drv_i915_native_decide(struct i915_native_report *r);
void drv_i915_native_log_again(struct i915_display *display);
void drv_i915_native_log(struct i915_display *display, const struct i915_native_report *r);
int drv_i915_opregion_read_data(struct i915_display *display, uint32_t asls, struct i915_opregion_data *out);
void drv_i915_opregion_log(const struct i915_opregion_data *d);
int drv_i915_n0_pipe_powered(void *ctx, unsigned pipe);

/* The firmware plane and console of the takeover run (N1). */
void drv_i915_n1_read_plane(const struct i915_lcd_kernel_deps *d, int pipe, uint32_t *ctl, uint32_t *stride, uint32_t *size, uint32_t *surf, uint32_t *offset);
const struct zbl6_framebuffer *drv_i915_n1_console_fb(void);
int drv_i915_n1_check_ggtt(const struct i915_lcd_kernel_deps *d, uint64_t fw_surf, unsigned pages, uint64_t fb_phys, int in_aperture);
int drv_i915_n1_mirror_console(struct i915_scanout *so);

#endif /* DRIVERS_GPU_I915_DISPLAY_TAKEOVER_H */
