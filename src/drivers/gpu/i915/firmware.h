/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The read-only firmware provider.
 *
 * It is the driver's request_firmware() and release_firmware(): fixed
 * reference blobs embedded in the kernel image are served by name.  A blob is
 * static read-only data that is never allocated or freed, so a release only
 * drops the handle.  No file system is involved.
 */

#ifndef DRIVERS_GPU_I915_FIRMWARE_H
#define DRIVERS_GPU_I915_FIRMWARE_H

#include <stdint.h>

/*
 * One firmware image a caller holds.
 *
 * The data points into the kernel image and stays valid for the kernel
 * lifetime; it is NULL until a request succeeds and again after the release.
 */
struct i915_firmware {
	/* The image bytes, or NULL. */
	const uint8_t *data;

	/* The image length in bytes. */
	unsigned size;
};

int drv_i915_firmware_request(struct i915_firmware *firmware, const char *name);
void drv_i915_firmware_release(struct i915_firmware *firmware);

#endif
