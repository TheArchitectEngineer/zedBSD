/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display part of the device start and stop (display.c).
 *
 * The device start calls these stages in the order of the Linux driver
 * probe, between its GT stages:
 *
 *   drv_i915_display_create           after the MSI vector (P2.9 position)
 *   drv_i915_display_init_opregion    OpRegion and its VBT, DRAM, bandwidth
 *   drv_i915_display_init_noirq       the display noirq probe, with the
 *                                     firmware display check before the
 *                                     first display write
 *   drv_i915_display_irq_prepare      the PCH and the display interrupt
 *                                     half, before the interrupt install
 *   drv_i915_display_init_nogem       the survey, the output setup, the
 *                                     readout and the sanitize
 *   drv_i915_display_register         the display probe, the hotplug path
 *                                     and the driver registration, after
 *                                     the GT
 *
 * The device stop calls drv_i915_display_stop_early before the GT stop,
 * drv_i915_display_stop_outputs after it, and drv_i915_display_fini after
 * the interrupt uninstall.  The display object is owned by the device
 * (device->display); every function takes the device and does nothing for
 * a device without a display.  The header is neutral: the device start and
 * the GPU node include it without the display's types.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DISPLAY_H
#define DRIVERS_GPU_I915_DISPLAY_DISPLAY_H

#include <stdint.h>

struct drv_gpu_ops;
struct drv_pci_device;
struct i915_device;
struct i915_display;
struct i915_drm_device;
struct i915_pch_state;

/*
 * ==== The device start ====
 */

/* Allocates the display of a device and its Linux environments: 0, ENOMEM or EBUSY. */
int drv_i915_display_create(struct i915_device *device);

/* Frees the display and its Linux environments. */
void drv_i915_display_destroy(struct i915_device *device);

/* Reads the OpRegion and its VBT, then the DRAM and the memory bandwidth (the end of the hardware probe). */
void drv_i915_display_init_opregion(struct i915_device *device);

/*
 * The noirq stage: the DRM device, the VBT, the VGA client, the power
 * domains, the firmware display check, and the display core, DMC,
 * workqueues and software state.  A firmware display check that stops
 * leaves the device without a display and returns 0.
 */
int drv_i915_display_init_noirq(struct i915_device *device);

/* Detects the PCH and binds the display interrupt half in place of the interim hooks (before install). */
void drv_i915_display_irq_prepare(struct i915_device *device);

/* Logs the display interrupt masks the install computed. */
void drv_i915_display_irq_log(struct i915_device *device);

/* The survey, the output setup, the readout and the sanitize of the hardware state. */
int drv_i915_display_init_nogem(struct i915_device *device);

/* The display probe, the hotplug path, the OpRegion registration and the driver registration. */
int drv_i915_display_register(struct i915_device *device);

/* Fills the panel dependencies of the resident run; the node has a panel from here on. */
void drv_i915_display_resident_deps(struct i915_device *device);

/*
 * ==== The device stop ====
 */

/* Stops anything still scanned out, the hotplug path, and unregisters the driver (before the GT stop). */
void drv_i915_display_stop_early(struct i915_device *device);

/* Releases the panel connector and the crtc, plane, DPLL and encoder records (before the interrupt uninstall). */
void drv_i915_display_stop_outputs(struct i915_device *device);

/* Takes the rest of the display apart in reverse order (after the interrupt uninstall). */
void drv_i915_display_fini(struct i915_device *device);

/* Reports whether a run kept the display's resources: DMA, scratch, BARs and bus mastering stay. */
int drv_i915_display_abandoned(struct i915_device *device);

/* Reports whether a run kept GPU objects the GPU was not shown to be done with. */
int drv_i915_display_gpu_retained(struct i915_device *device);

/* Reports whether a pipe's interrupt drain failed: the handler stays attached. */
int drv_i915_display_irq_sync_failed(struct i915_device *device);

/*
 * ==== The GPU node ====
 */

/* Binds the display and scanout operations and their capabilities when the node has a panel. */
void drv_i915_display_bind_ops(struct i915_device *device, struct drv_gpu_ops *ops);

/* Drops the display lease a closing session holds (the panel is stopped first). */
void drv_i915_display_session_close(struct i915_device *device, void *session);

/* Reads the panel's mode: 0, or ENXIO when the node has no panel. */
int drv_i915_display_panel(struct i915_display *display, uint32_t *width, uint32_t *height, uint32_t *refresh_millihz);

/*
 * ==== The DRM device and the PCH ====
 */

int drv_i915_drm_dev_init(struct i915_drm_device *ddev, struct i915_device *parent, uint32_t driver_features);
void drv_i915_drm_dev_fini(struct i915_drm_device *ddev);
int drv_i915_drmm_add_action_or_reset(struct i915_drm_device *ddev, void (*fn)(void *arg), void *arg);
int drv_i915_drm_vblank_init(struct i915_drm_device *ddev, unsigned num_crtcs);

int drv_i915_pch_type(uint16_t id);
int drv_i915_is_virt_pch(uint16_t id, uint16_t svendor, uint16_t sdevice);
void drv_i915_detect_pch(struct i915_display *display, struct i915_pch_state *p, int display_ver, int is_alderlake, int has_display, int run_as_guest);

#endif /* DRIVERS_GPU_I915_DISPLAY_DISPLAY_H */
