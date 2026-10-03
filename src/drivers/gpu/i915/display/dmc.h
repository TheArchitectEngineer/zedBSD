/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DMC firmware: parse, load and the asynchronous loader (dmc.c).
 *
 * The pipe DMC enable and disable take types of the modeset environment and
 * are declared in modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DMC_H
#define DRIVERS_GPU_I915_DISPLAY_DMC_H

#include "internal.h"

void drv_i915_dmc_prepare(struct i915_dmc *dmc, int display_ver, char stepping, char substepping);
int drv_i915_parse_dmc_fw(struct i915_dmc *dmc, const uint8_t *data, unsigned size);
void drv_i915_dmc_parse_reset(struct i915_dmc *dmc);
int drv_i915_dmc_has_payload(const struct i915_dmc *dmc);
void drv_i915_dmc_load_program(struct i915_dmc *dmc, struct i915_mmio *mmio, uint32_t *dc_state_out);
void drv_i915_dmc_init(struct i915_dmc_dev *dev, struct i915_workqueue *wq, struct i915_mmio *mmio, struct i915_power_domains *pd, struct i915_pw_ctx *pwc, int display_ver, int is_alderlake_p, char stepping, char substepping, const char *fw_path);
void drv_i915_dmc_fini(struct i915_dmc_dev *dev, uint64_t deadline);

#endif
