/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * How the Gen9 mode set reaches the display registers.
 *
 * The mode set, the DDC and the link training (gen9-modeset.c,
 * gen9-ddc.c, gen9-link.c) read and write the registers and wait through
 * this table, so a host test can stand a model of the hardware in for the
 * device; in the kernel it is the device's MMIO and the scheduler's sleep
 * (gen9-node.c).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_IO_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_IO_H

#include <stdint.h>

/*
 * The register access and the waits of one display.
 *
 * read and write are the raw 32-bit accesses of an offset; udelay spins,
 * msleep sleeps (and may be a spin in a test).  context is handed back to
 * each.  The table lives as long as the display it belongs to.
 */
struct i915_skl_io {
	uint32_t (*read)(void *context, uint32_t offset);
	void (*write)(void *context, uint32_t offset, uint32_t value);
	void (*udelay)(void *context, unsigned microseconds);
	void (*msleep)(void *context, unsigned milliseconds);
	void *context;
};

/* Reads a register. */
uint32_t drv_i915_skl_read(const struct i915_skl_io *io, uint32_t offset);

/* Writes a register. */
void drv_i915_skl_write(const struct i915_skl_io *io, uint32_t offset, uint32_t value);

/* Clears and sets bits of a register; reports the value written. */
uint32_t drv_i915_skl_rmw(const struct i915_skl_io *io, uint32_t offset, uint32_t clear, uint32_t set);

/* Waits until (register & mask) == value, polling every 10 us up to timeout_us: 0, or ETIMEDOUT with the last value. */
int drv_i915_skl_wait(const struct i915_skl_io *io, uint32_t offset, uint32_t mask, uint32_t value, unsigned timeout_us, uint32_t *last);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_IO_H */
