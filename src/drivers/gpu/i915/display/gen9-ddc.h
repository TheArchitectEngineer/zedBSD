/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The side channels of the Gen9 outputs (gen9-ddc.c, WS237 stage 3): the
 * DP AUX channel of a DDI port (native DPCD reads and writes, and the
 * EDID over I2C-over-AUX) and the GMBUS of an HDMI port's DDC pins.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_DDC_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_DDC_H

#include "gen9-io.h"

#include <stddef.h>
#include <stdint.h>

/* The most bytes one AUX message carries. */
#define I915_SKL_AUX_MAX_BYTES		16U

/* Packs up to four bytes into an AUX data register, the first byte highest. */
uint32_t drv_i915_skl_aux_pack(const uint8_t *bytes, unsigned count);

/* Unpacks up to four bytes from an AUX data register. */
void drv_i915_skl_aux_unpack(uint32_t word, uint8_t *bytes, unsigned count);

/* Sends one AUX message and receives the reply: the reply's bytes, or a negative errno. */
int drv_i915_skl_aux_xfer(const struct i915_skl_io *io, unsigned port, const uint8_t *send, unsigned send_bytes, uint8_t *receive, unsigned receive_capacity);

/* Reads DPCD bytes (16 at a time, deferrals retried): 0, or an errno. */
int drv_i915_skl_dpcd_read(const struct i915_skl_io *io, unsigned port, uint32_t address, uint8_t *bytes, size_t count);

/* Writes DPCD bytes (16 at a time, deferrals retried): 0, or an errno. */
int drv_i915_skl_dpcd_write(const struct i915_skl_io *io, unsigned port, uint32_t address, const uint8_t *bytes, size_t count);

/* Reads EDID blocks through I2C-over-AUX: 0, or an errno. */
int drv_i915_skl_aux_edid(const struct i915_skl_io *io, unsigned port, uint8_t *edid, unsigned blocks);

/* Reads EDID blocks through the GMBUS on a pin: 0, ENXIO when nothing answers, or another errno. */
int drv_i915_skl_gmbus_edid(const struct i915_skl_io *io, unsigned pin, uint8_t *edid, unsigned blocks);

/* Reports the GMBUS pin of an HDMI port (B, C, D), or 0. */
unsigned drv_i915_skl_gmbus_pin(unsigned port);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_DDC_H */
