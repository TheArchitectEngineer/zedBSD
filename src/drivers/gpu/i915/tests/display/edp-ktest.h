/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP world the eDP ktest parts (edp-ktest.c, edp-sync-ktest.c) drive
 * the register model in.
 *
 * The Linux text of the eDP reaches its environment through the one DP world
 * the display created, and only one eDP may be live in it.  On a started
 * device that world holds the resident panel.  A part therefore borrows it:
 * it waits until the resident panel's delayed VDD-off has run, sets the
 * world's contents aside, drives the register model in the emptied world and
 * puts the contents back.  Without a display world the part creates one and
 * destroys it afterwards.
 *
 * Nothing but the ktest touches the resident panel while its world is
 * borrowed: the part runs on the device's start worker before the node is
 * served.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_EDP_KTEST_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_EDP_KTEST_H

struct i915_display;
struct i915_dp_world;
struct i915_ktest;

/*
 * One borrowing of the DP world by a ktest part.
 *
 * It lives on the part's stack from the enter to the leave.  The carrier is
 * a display of the test's own that holds the borrowed world and whose eDP
 * device lends its kernel backend to the register model, because the
 * backend's delayed VDD-off finds its world through the display it is
 * embedded in.
 */
struct i915_edp_ktest_world {
	/* The world the part drives the register model in. */
	struct i915_dp_world *world;

	/* The test's own display, whose dp_world is the borrowed world. */
	struct i915_display *carrier;

	/* The contents of the device's world while they are set aside; NULL when the world was created. */
	struct i915_dp_world *saved;

	/* Nonzero when the world was created for the part and is destroyed by the leave. */
	int created;
};

int drv_i915_display_ktest_edp_world_enter(struct i915_ktest *ktest, struct i915_edp_ktest_world *borrow);
void drv_i915_display_ktest_edp_world_leave(struct i915_edp_ktest_world *borrow);

#endif
