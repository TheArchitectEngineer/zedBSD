/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display clocks: CDCLK and the shared DPLLs (clock.c).
 *
 * The CDCLK bring-up of the display core, the combo PLL calculations and
 * the device's shared-DPLL pool.  The functions whose arguments are types
 * of the modeset environment are declared in modeset-internal.h and
 * takeover-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_CLOCK_H
#define DRIVERS_GPU_I915_DISPLAY_CLOCK_H

#include "internal.h"

const struct i915_cdclk_vals *drv_i915_adlp_cdclk_table(void);
const struct i915_cdclk_vals *drv_i915_icl_cdclk_table(void);
int drv_i915_adlp_display_step(uint8_t revid);
void drv_i915_init_cdclk_hooks(struct i915_cdclk_dev *cd, int display_ver, int display_step, int is_alderlake_p);
uint8_t drv_i915_tgl_calc_voltage_level(int cdclk);
int drv_i915_bxt_calc_cdclk(struct i915_cdclk_dev *cd, int min_cdclk);
int drv_i915_bxt_calc_cdclk_pll_vco(struct i915_cdclk_dev *cd, int cdclk);
void drv_i915_bxt_get_cdclk(struct i915_cdclk_dev *cd, struct i915_cdclk_config *cfg);
void drv_i915_update_cdclk(struct i915_cdclk_dev *cd);
void drv_i915_bxt_set_cdclk(struct i915_cdclk_dev *cd, const struct i915_cdclk_config *cfg);
void drv_i915_bxt_sanitize_cdclk(struct i915_cdclk_dev *cd);
void drv_i915_cdclk_init_hw(struct i915_cdclk_dev *cd);
uint32_t drv_i915_max_cdclk_freq(const struct i915_cdclk_dev *cd);

int drv_i915_icl_hdmi_wrpll(int port_clock, int ref_nssc, uint32_t *cfgcr0, uint32_t *cfgcr1, uint32_t *div0);
int drv_i915_icl_dp_combo_pll(int port_clock, int ref_nssc, uint32_t *cfgcr0, uint32_t *cfgcr1, uint32_t *div0);
void drv_i915_lcd_dplls_reset(struct i915_lcd_world *world);

#endif
