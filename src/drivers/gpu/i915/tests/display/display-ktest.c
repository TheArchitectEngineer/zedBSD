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
 * The parts.
 *
 * They are weak because no test kernel holds them all (q762: the kernel
 * with every scenario is past AMD64_KERNEL_MAX_BYTES): the test set
 * "display_ktest" links the first four, "display_ktest2" the last four
 * (platform/amd64/vmunix.mk), and a part the set leaves out is a null this
 * scenario reports as not linked.
 */
extern void drv_i915_display_ktest_edp(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_edp_sync(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_lcd_modeset(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_scanout(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_lcd_show(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_lcdg(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_opregion(struct i915_ktest *ktest) __attribute__((weak));
extern void drv_i915_display_ktest_hpd(struct i915_ktest *ktest) __attribute__((weak));

static void display_ktest_part(struct i915_ktest *ktest, void (*part)(struct i915_ktest *), const char *name);

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
	display_ktest_part(&ktest, drv_i915_display_ktest_edp, "edp");
	display_ktest_part(&ktest, drv_i915_display_ktest_edp_sync, "edp_sync");

	/* The one-screen modeset on the register and sink models. */
	display_ktest_part(&ktest, drv_i915_display_ktest_lcd_modeset, "lcd_modeset");

	/* The scanout buffer, the show body and the release contract on the GT memory. */
	display_ktest_part(&ktest, drv_i915_display_ktest_scanout, "scanout");
	display_ktest_part(&ktest, drv_i915_display_ktest_lcd_show, "lcd_show");
	display_ktest_part(&ktest, drv_i915_display_ktest_lcdg, "lcdg");

	/* The OpRegion receive side on a shadow mailbox. */
	display_ktest_part(&ktest, drv_i915_display_ktest_opregion, "opregion");

	/* The HDMI hotplug receive path on fake status registers. */
	display_ktest_part(&ktest, drv_i915_display_ktest_hpd, "hpd");

	kern_logf("i915: display ktest: %u checks, %u failures, %u skipped\n",
	    ktest.checks,
	    ktest.failures,
	    ktest.skipped);
	kern_logf("i915: display ktest verdict: %s\n",
	    ktest.failures == 0U ? "PASS" : "FAIL");
}

/* Runs one part of the display ktest, or says that the test set left it out. */
static void
display_ktest_part(
	struct i915_ktest *ktest,
	void (*part)(struct i915_ktest *),
	const char *name)
{
	/* A part this test set does not link is reported, not counted as a failure. */
	if (part == NULL) {
		kern_logf("i915: display ktest: %s not linked in this test set\n", name);
		return;
	}

	/* Runs the part on the shared tally. */
	part(ktest);
}
