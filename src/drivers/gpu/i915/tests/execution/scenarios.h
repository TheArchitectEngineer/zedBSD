/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The execution scenarios the test runner can select.
 *
 * Each runs on the device's start worker after the device has started and
 * before its node is served, logs what it observed, and returns 0 when the
 * scenario passed or a positive errno when it did not.  The GPU scenarios
 * submit on the render engine while nothing else uses it and leave the engine
 * parked on its kernel context, so the node is served normally afterwards.
 */

#ifndef DRIVERS_GPU_I915_TESTS_EXECUTION_SCENARIOS_H
#define DRIVERS_GPU_I915_TESTS_EXECUTION_SCENARIOS_H

struct i915_device;

int drv_i915_test_execution_ktest(struct i915_device *device);
int drv_i915_test_execution_eu(struct i915_device *device);
int drv_i915_test_execution_draw(struct i915_device *device);
int drv_i915_test_execution_r1(struct i915_device *device);
int drv_i915_test_execution_tex(struct i915_device *device);
int drv_i915_test_execution_t3(struct i915_device *device);
int drv_i915_test_execution_bl(struct i915_device *device);

#endif
