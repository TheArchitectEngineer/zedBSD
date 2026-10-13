/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Device start and stop.
 *
 * PCI attach only records the device.  The hardware is brought up later by a
 * start worker, once the kernel reports that regular threads and timed waits
 * work; the two events may arrive in either order.
 */

#ifndef DRIVERS_GPU_I915_DEVICE_H
#define DRIVERS_GPU_I915_DEVICE_H

#include <stdint.h>

struct i915_device;

/*
 * What a PCI device id says about the device: its graphics and display
 * versions, its platform (I915_PLATFORM_*) and a name for the log.  It is
 * filled by drv_i915_product_kind() from a constant table.
 */
struct i915_product_kind {
	unsigned graphics_ver;
	unsigned display_ver;
	int platform;
	int is_alderlake_p;
	const char *name;
};

int drv_i915_product_kind(uint16_t product, struct i915_product_kind *kind);

void drv_i915_device_start_ops_install(void);
void drv_i915_device_schedule_start(struct i915_device *device);
int drv_i915_device_start(struct i915_device *device);
int drv_i915_device_stop(struct i915_device *device);
void drv_i915_device_park_gt(struct i915_device *device);
int drv_i915_device_unpark_gt(struct i915_device *device);
int drv_i915_device_suspend_hw(struct i915_device *device);
int drv_i915_device_resume_hw(struct i915_device *device);
void drv_i915_device_attach_settled(struct i915_device *device);

#endif
