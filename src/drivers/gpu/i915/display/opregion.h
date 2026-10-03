/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The OpRegion service and the ACPI notifier chain: what the rest of the
 * driver sees of opregion.c.
 *
 * The OpRegion service runs the Linux intel_opregion.c lifecycle (setup,
 * register, unregister, cleanup) on a backend chosen when the instance is
 * bound: SHADOW (driver-owned RAM in the OpRegion format, for synthetic
 * tests) or FIRMWARE (the real shared region, which production does not
 * enable: it runs VBT_ONLY).  The ACPI notifier chain is the register /
 * unregister / call-chain contract the OpRegion registers its video event
 * callback on; its only event source today is synthetic.
 *
 * Every declaration here uses the world-neutral types of internal.h only;
 * the functions that take the OpRegion environment's Linux types are
 * declared in opregion-internal.h.  Every function returns zedBSD positive
 * errno values.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_OPREGION_H
#define DRIVERS_GPU_I915_DISPLAY_OPREGION_H

#include "internal.h"

struct i915_display;

/* The OpRegion environment's world (opregion.c). */
int drv_i915_opregion_world_create(struct i915_display *display);
void drv_i915_opregion_world_destroy(struct i915_display *display);

/* The ACPI notifier chain. */
void drv_i915_acpi_notifier_init(struct i915_display *display);
int drv_i915_register_acpi_notifier(struct i915_display *display, struct notifier_block *nb);
int drv_i915_unregister_acpi_notifier(struct i915_display *display, struct notifier_block *nb);
int drv_i915_acpi_notifier_call_chain(struct i915_display *display, const char *device_class, const char *bus_id, uint32_t type, uint32_t data, const char *event_source, struct i915_acpi_dispatch *out);
unsigned drv_i915_acpi_notifier_count(struct i915_display *display);

/* The OpRegion service instance: its backends and its Linux lifecycle. */
const char *drv_i915_opregion_mailbox_backend(struct i915_display *display);
unsigned drv_i915_opregion_service_epoch(struct i915_display *display);
int drv_i915_opregion_shadow_map(struct i915_display *display, uint64_t phys, void *ptr, uint32_t size);
int drv_i915_opregion_shadow_setup(struct i915_display *display, uint32_t asls_token);
int drv_i915_opregion_firmware_setup(struct i915_display *display, uint32_t asls);
void drv_i915_opregion_register(struct i915_display *display);
void drv_i915_opregion_unregister(struct i915_display *display);
int drv_i915_opregion_cleanup(struct i915_display *display);
int drv_i915_opregion_notify_adapter(struct i915_display *display, int pci_state);
int drv_i915_opregion_notify_encoder(int port, int output_type, int enable);

/* What the instance holds, for the records. */
uint32_t drv_i915_opregion_mbox_read(struct i915_display *display, unsigned off);
void drv_i915_opregion_mbox_write(struct i915_display *display, unsigned off, uint32_t v);
const void *drv_i915_opregion_vbt(struct i915_display *display, uint32_t *size);
int drv_i915_opregion_notifier_registered(struct i915_display *display);
void drv_i915_opregion_counters(struct i915_display *display, unsigned *unported, unsigned *boundaries, unsigned *unmaps);
void drv_i915_opregion_gate_counters(struct i915_display *display, unsigned *dropped, unsigned *cleanup_refused);
void drv_i915_opregion_worker_stats_get(struct i915_display *display, unsigned *started, unsigned *finished, unsigned *queued_new, unsigned *queued_pending);

/* The ASLE service: the worker queue, the backlight policy and targets, the GSE entry. */
int drv_i915_opregion_service_start(struct i915_display *display, struct i915_workqueue *wq, int policy);
void drv_i915_opregion_set_policy(struct i915_display *display, int policy);
int drv_i915_opregion_add_connector(struct i915_display *display, int drm_connector_type, void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx);
int drv_i915_opregion_add_backlight(struct i915_display *display, void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx);
void drv_i915_opregion_gse_entry(struct i915_display *display);
int drv_i915_opregion_asle_flush(struct i915_display *display, uint64_t deadline);

#endif
