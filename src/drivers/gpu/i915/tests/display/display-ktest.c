/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display ktest scenario: the display's in-kernel unit tests.
 *
 * The parts drive the display layers against the register and sink models
 * (the eDP stage, the modeset, the show body, the hotplug path), on the
 * started device's GT memory (the scanout buffer, the release contract of
 * a GPU-drawn picture), or on a shadow OpRegion; none of them lights the
 * panel.  They share one tally of the kernel ktest, reported as one line.
 */

#include "display-ktest.h"
#include "../execution/ktest.h"

#include <kern/klog.h>

#include <stddef.h>

/*
 * Runs the display's in-kernel unit tests on the started device.
 *
 * The parts run in the order the old suite ran them: the eDP first stage
 * and its stage on real threads, the one-screen modeset, then on the GT
 * memory the scanout buffer, the show body and the release contract, and
 * last the OpRegion receive side and the hotplug receive path.
 */
void
drv_i915_test_display_ktest(
	struct i915_device *device)
{
	struct i915_ktest ktest;

	/* An empty tally over the started device. */
	ktest.device = device;
	ktest.checks = 0U;
	ktest.failures = 0U;
	ktest.skipped = 0U;
	kern_logf("i915: display ktest begin\n");

	/* The eDP first stage on the register model, then on real threads, locks and ticks. */
	drv_i915_display_ktest_edp(&ktest);
	drv_i915_display_ktest_edp_sync(&ktest);

	/* The one-screen modeset on the register and sink models. */
	drv_i915_display_ktest_lcd_modeset(&ktest);

	/* The scanout buffer, the show body and the release contract on the GT memory. */
	drv_i915_display_ktest_scanout(&ktest);
	drv_i915_display_ktest_lcd_show(&ktest);
	drv_i915_display_ktest_lcdg(&ktest);

	/* The OpRegion receive side on a shadow mailbox. */
	drv_i915_display_ktest_opregion(&ktest);

	/* The HDMI hotplug receive path on fake status registers. */
	drv_i915_display_ktest_hpd(&ktest);

	kern_logf("i915: display ktest: %u checks, %u failures, %u skipped\n",
	    ktest.checks,
	    ktest.failures,
	    ktest.skipped);
	kern_logf("i915: display ktest verdict: %s\n",
	    ktest.failures == 0U ? "PASS" : "FAIL");
}
