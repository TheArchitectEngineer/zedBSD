/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display power wells, domains and core (power.c).
 *
 * The power-domain map, the power-well operations, the reference-counted
 * domain get and put with its asynchronous put, the DC states, the DBUF
 * slices, the display-core bring-up and the POWER_DOMAIN_INIT reference of
 * the driver probe.  The functions whose arguments are types of the modeset
 * environment are declared in modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_POWER_H
#define DRIVERS_GPU_I915_DISPLAY_POWER_H

#include "internal.h"

int drv_i915_power_domains_init(struct i915_power_domains *pd, unsigned display_ver, int enable_dc_param, int disable_pw_param, struct i915_trace *trace);
void drv_i915_power_domains_cleanup(struct i915_power_domains *pd);
uint64_t drv_i915_power_domain_wells(const struct i915_power_domains *pd, enum i915_power_domain domain);
int drv_i915_power_well_by_id(const struct i915_power_domains *pd, int id);

int drv_i915_power_well_enable(struct i915_power_well *well, struct i915_pw_ctx *pwc);
int drv_i915_power_well_disable(struct i915_power_well *well, struct i915_pw_ctx *pwc);
int drv_i915_power_well_is_enabled(struct i915_power_well *well, struct i915_pw_ctx *pwc);
void drv_i915_power_well_sync_hw(struct i915_power_well *well, struct i915_pw_ctx *pwc);
int drv_i915_power_well_get(struct i915_power_well *well, struct i915_pw_ctx *pwc);
void drv_i915_power_well_put(struct i915_power_well *well, struct i915_pw_ctx *pwc);

int drv_i915_display_power_is_enabled(struct i915_power_domains *pd, enum i915_power_domain domain, struct i915_pw_ctx *pwc);
int drv_i915_power_domain_hw_state_on(struct i915_power_domains *pd, enum i915_power_domain domain, struct i915_mmio *mmio);
int drv_i915_display_power_get(struct i915_power_domains *pd, enum i915_power_domain domain, struct i915_pw_ctx *pwc);
void drv_i915_display_power_put(struct i915_power_domains *pd, enum i915_power_domain domain, struct i915_pw_ctx *pwc);

void drv_i915_display_power_async_bind(struct i915_power_domains *pd, const struct i915_pw_async_ops *ops, void *ctx, struct i915_pw_ctx *pwc);
void drv_i915_display_power_put_async(struct i915_power_domains *pd, enum i915_power_domain domain, struct i915_pw_ctx *pwc, int delay_ms);
void drv_i915_display_power_async_work(struct i915_power_domains *pd);
void drv_i915_display_power_flush_work(struct i915_power_domains *pd);
void drv_i915_display_power_flush_work_sync(struct i915_power_domains *pd);

void drv_i915_pmdemand_init_early(struct i915_pmdemand *pm);
void drv_i915_gen9_set_dc_state(struct i915_pw_ctx *pwc, uint32_t state);

uint8_t drv_i915_enabled_dbuf_slices_mask(struct i915_display_core *dc);
uint32_t drv_i915_dbuf_ctl_reg(unsigned slice);
void drv_i915_gen9_dbuf_slices_update(struct i915_display_core *dc, uint8_t req_slices);

void drv_i915_dc_off_enable(struct i915_pw_ctx *pwc);
void drv_i915_power_domains_init_hw(struct i915_display_core *dc, int resume);
void drv_i915_power_domains_driver_remove(struct i915_display_core *dc);

unsigned drv_i915_power_domains_verify_state(struct i915_power_domains *pd, struct i915_pw_ctx *pwc);
void drv_i915_power_domains_enable(struct i915_driver_probe *probe, struct i915_display_core *dc);
void drv_i915_power_domains_disable(struct i915_display_core *dc);

/*
 * The power hooks of a panel run (the context is its struct
 * i915_lcd_kernel); the modeset path puts them in the run's hook table.
 */
int drv_i915_lcd_power_get(void *ctx, int domain);
int drv_i915_lcd_power_get_if_enabled(void *ctx, int domain);
void drv_i915_lcd_power_put(void *ctx, int domain, int wakeref);
void drv_i915_lcd_power_put_async(void *ctx, int domain, int wakeref, int delay_ms);

#endif
