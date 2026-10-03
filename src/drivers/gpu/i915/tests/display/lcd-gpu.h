/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU-drawn pictures of the display scenarios (lcd-gpu.c).
 *
 * A buffer the GPU draws into and the display shows has two users.  It may
 * be freed only when both are done: the display never took it or its stop
 * was confirmed, and the GPU never got it or its request retired and the
 * engine parked.  The release decision is shared with the display ktest.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_GPU_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_LCD_GPU_H

struct i915_display;
struct i915_gt_engines;
struct i915_gt_mem;
struct i915_gt_ppgtt;
struct i915_gt_tlb;
struct i915_mmio;
struct i915_scanout;
struct i915_test_fhd_render;
struct spinlock;

/*
 * Releases a GPU-drawn scanout buffer when both of its users are done.
 *
 * Then, and only then, the render mappings, the TLB, the draw's objects
 * and finally the buffer go, each step only after the previous one
 * succeeded; otherwise everything the unfinished user may touch is kept
 * and the display's retained-GPU latch is set.  *render_rc is the draw
 * release's result, EBUSY when it was not attempted.  Returns 1 when the
 * buffer was freed.
 */
int drv_i915_test_lcdg_finish(struct i915_display *display, struct i915_test_fhd_render *fr, struct i915_scanout *so, int display_acquired, int display_released, struct i915_gt_mem *gm, struct i915_gt_ppgtt *vm, struct i915_gt_tlb *tlb, struct i915_gt_engines *es, struct i915_mmio *m, struct spinlock *uncore_lock, int *render_rc);

#endif
