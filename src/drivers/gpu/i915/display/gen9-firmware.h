/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The check of what the firmware left on a Gen9 display before the GT
 * starts (gen9-firmware.c).
 *
 * The driver does not light a Gen9 display at the start: the firmware's
 * picture stays on, and the GT must neither overwrite it nor run its DMA
 * through a VT-d unit zedBSD does not drive.  The check only reads
 * registers.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_FIRMWARE_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_FIRMWARE_H

struct i915_display;
struct i915_mmio;

int drv_i915_gen9_firmware_check(struct i915_display *display, struct i915_mmio *mmio, unsigned driver_ggtt_first);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_FIRMWARE_H */
