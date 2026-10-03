/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The VBT: what the rest of the display sees of vbt.c.
 *
 * vbt.c chooses the VBT bytes (the explicit pinned blob of the test build,
 * the OpRegion's copy, or the PCI expansion ROM), runs the Linux VBT parser
 * on them, and
 * flattens what the parser found into struct i915_vbt and struct
 * i915_vbt_state (internal.h), which the display code reads without the
 * parser's Linux types.  It also holds the SHA-256 the explicit blob is
 * pinned with, and the pure OpRegion reader that says where the OpRegion
 * keeps its VBT.
 *
 * Every declaration here uses the world-neutral types of internal.h only;
 * vbt.h is the parser's Linux environment and is included by vbt.c alone.
 * Every function returns zedBSD positive errno values.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_VBT_PARSE_H
#define DRIVERS_GPU_I915_DISPLAY_VBT_PARSE_H

#include "internal.h"

struct i915_display;
struct i915_pci;
struct i915_trace;

/*
 * One VBT child device as the Linux parser keeps it.
 *
 * The layout is private to vbt.c; a caller only passes the pointer on.
 */
struct intel_bios_encoder_data;

/* The parser's world (vbt.c). */
int drv_i915_vbt_world_create(struct i915_display *display);
void drv_i915_vbt_world_destroy(struct i915_display *display);

/* The VBT acquisition (intel_bios_init() and its byte sources). */
int drv_i915_bios_is_valid_vbt_header(const void *buf, size_t size);
int drv_i915_bios_process_vbt(struct i915_vbt_state *vbt, const void *buf, size_t size);
void drv_i915_bios_init_vbt_missing_defaults(struct i915_vbt_state *vbt);
void drv_i915_bios_set_opregion_vbt(struct i915_display *display, const void *buf, size_t size);
int drv_i915_bios_init_ex(struct i915_display *display, struct i915_vbt_state *vbt, struct i915_pci *pci, int opregion_has_vbt, int explicit_blob, struct i915_trace *trace);
int drv_i915_bios_init(struct i915_display *display, struct i915_vbt_state *vbt, struct i915_pci *pci, int opregion_has_vbt, struct i915_trace *trace);
void drv_i915_bios_driver_remove(struct i915_vbt_state *vbt);
#ifdef I915_TEST_VBT
/* XXX: the test build's explicit VBT; delete once the GPU tests run on bare metal. */
const uint8_t *drv_i915_vbt_explicit_pin(struct i915_display *display);
#endif

/* The SHA-256 the VBT copies are hashed and pinned with. */
void drv_i915_sha256(const void *data, size_t len, uint8_t out[32]);

/* The parser's message hooks. */
void drv_i915_vbt_emit(const char *text);
int drv_i915_vbt_fmtcheck(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Where the OpRegion keeps its VBT. */
int drv_i915_opregion_locate_vbt(const uint8_t *op, size_t len, uint64_t asls, struct i915_opregion_info *out);

/* The parser's result as the display reads it. */
int drv_i915_vbt_validate(const void *bytes, size_t size);
int drv_i915_vbt_init(struct i915_vbt *v, const void *bytes, size_t size, int origin);
const struct i915_vbt_encoder *drv_i915_vbt_encoder_for_port(const struct i915_vbt *v, int port);
int drv_i915_vbt_init_panel(struct i915_vbt *v, int port, const uint8_t *edid128, struct i915_vbt_panel *out);
void drv_i915_vbt_fini(struct i915_vbt *v);
void drv_i915_vbt_set_log_level(int level);

/* The Linux accessors other files name (intel_bios.c). */
bool drv_i915_bios_is_valid_vbt(const void *buf, size_t size);
int drv_i915_bios_hdmi_level_shift(const struct intel_bios_encoder_data *devdata);

#endif
