/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the N0 decision rules (drv_i915_native_decide() in
 * display/takeover.c), including the situation the target laptop presents
 * natively: the firmware (GOP) keeps pipe A lit from GGTT page 0 of the
 * aperture.
 *
 *   sh plan/ws031/tests/run-native-decide-host-test.sh
 */

#include "host-test.h"

#include "../../display/internal.h"
#include "../../display/takeover.h"

#include <string.h>

static void i915_native_base(struct i915_native_report *r);
static int i915_native_reason_has(const struct i915_native_report *r, const char *word);

/*
 * Runs the decision on each start situation and checks what it decides.
 */
int
main(void)
{
	struct i915_native_report r;
	int named;
	int status;

	/* An idle native start: pipe A off, B..D powered off, VT-d not translating. */
	i915_native_base(&r);
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed != 0, "IDLE native: pipe A off, B..D power off (counted inactive), VT-d not translating -> PROCEED");

	/* The expected native start: GOP lit pipe A from GGTT page 0. */
	i915_native_base(&r);
	r.pipe[0].transconf = 0x80000000U;
	r.pipe[0].plane_ctl = 0x84000000U;
	r.pipe[0].plane_surf = 0U;
	r.active_pipes = 1U;
	r.fb_present = 1;
	r.fb_in_aperture = 1;
	r.fb_ggtt_first = 0U;
	r.fb_ggtt_pages = 2025U;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "active");
	i915_host_check(r.proceed == 0 &&
			named != 0 &&
			r.overlap == 0,
			"GOP-LIT native: STOP before any display write, reason = active pipe (takeover not ported); no GGTT overlap");

	/* A firmware scanout inside the GGTT pages the driver writes. */
	i915_native_base(&r);
	r.fb_present = 1;
	r.fb_in_aperture = 1;
	r.fb_ggtt_first = r.driver_ggtt_first - 10U;
	r.fb_ggtt_pages = 2025U;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "overlap");
	i915_host_check(r.proceed == 0 &&
			r.overlap != 0 &&
			named != 0,
			"OVERLAP a firmware scanout in the driver's GGTT pages -> STOP");

	/* The GPU's VT-d unit translates. */
	i915_native_base(&r);
	r.vtd_gsts = 0x80000000U;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "VT-d");
	i915_host_check(r.proceed == 0 && named != 0,
			"TES native: the GPU's VT-d unit translates -> STOP (no IOMMU driver)");

	/* A protected memory region is enabled. */
	i915_native_base(&r);
	r.vtd_pmen = 0x80000001U;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "VT-d");
	i915_host_check(r.proceed == 0 && named != 0,
			"PMR native: a protected memory region is enabled -> STOP");

	/* The VT-d unit is enabled but does not read. */
	i915_native_base(&r);
	r.vtd_readable = 0;
	r.vtd_ver = 0U;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "not readable");
	i915_host_check(r.proceed == 0 && named != 0,
			"VTD-UNKNOWN native: enabled but unreadable -> STOP");

	/* A guest reads zeros at the host unit's address. */
	i915_native_base(&r);
	r.hypervisor = 1;
	r.vtd_gsts = 0U;
	r.vtd_ver = 0U;
	r.vtd_readable = 0;
	drv_i915_native_decide(&r);
	named = i915_native_reason_has(&r, "guest view");
	i915_host_check(r.proceed != 0 && named != 0,
			"GUEST: the VT-d reading is a guest view (host owns it) -> PROCEED as before");

	/* No VT-d unit for the GPU. */
	i915_native_base(&r);
	r.vtd_enabled = 0;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed != 0, "NO-VTD: GFXVTBAR not enabled -> DMA untranslated -> PROCEED");

	/* The real native picture: GOP lit, ASLS set, GSTS 0x40000000 (RTPS only). */
	i915_native_base(&r);
	r.asls = 0x614e5018U;
	r.pipe[0].cls = I915_N0_READABLE_ACTIVE;
	r.active_pipes = 1U;
	r.vtd_gsts = 0x40000000U;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed == 0 &&
			r.primary_stop == I915_N0_C_ACTIVE_PIPE &&
			r.conditions == (I915_N0_C_ACTIVE_PIPE | I915_N0_C_OPREGION_REGISTER),
			"NATIVE-1: primary ACTIVE_PIPE; also observed: the later intel_opregion_register wall; RTPS alone is no condition");

	/* An active pipe and translation together. */
	i915_native_base(&r);
	r.pipe[0].cls = I915_N0_READABLE_ACTIVE;
	r.active_pipes = 1U;
	r.vtd_gsts = 0x80000000U;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed == 0 &&
			r.primary_stop == I915_N0_C_ACTIVE_PIPE &&
			(r.conditions & I915_N0_C_VTD_TRANSLATION) != 0U,
			"TWO-WALLS: active pipe AND translation: primary = ACTIVE_PIPE, the translation is recorded too (not found only later)");

	/* Interrupt remapping alone. */
	i915_native_base(&r);
	r.vtd_gsts = 0x02000000U;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed != 0 &&
			(r.conditions & I915_N0_C_VTD_IR_ENABLED) != 0U &&
			r.primary_stop == 0U,
			"IRES: interrupt remapping on is recorded for the MSI path, not a DMA stop");

	/* A pipe that did not read as register values. */
	i915_native_base(&r);
	r.pipe[1].cls = I915_N0_READ_ERROR;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed == 0 && r.primary_stop == I915_N0_C_PIPE_READ_ERROR,
			"READ-ERROR: a pipe that did not read as register values is not rounded to inactive -> STOP");

	/* A valid reading of a powered-off pipe. */
	i915_native_base(&r);
	r.pipe[1].cls = I915_N0_POWER_OFF;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed != 0 && r.conditions == 0U, "POWER-OFF: a valid 'off' reading is inactive -> no condition");

	/* An OpRegion VBT that is not the bytes the parser consumed. */
	i915_native_base(&r);
	r.vbt_valid = 1;
	r.parser_sha_known = 1;
	r.vbt_same_bytes = 0;
	drv_i915_native_decide(&r);
	i915_host_check(r.proceed != 0 && (r.conditions & I915_N0_C_VBT_DIFFERS) != 0U,
			"VBT-DIFFERS: the OpRegion VBT is not the bytes the parser consumed -> recorded");

	/* Reports the tally. */
	status = i915_host_report("native_decide_host_test");
	if (status != 0)
		return status;

	/* Succeeded: every decision is the expected one. */
	return 0;
}

/* Fills a report with an idle native start: an 8 MiB GGTT, pipe A readable and off, VT-d readable and idle. */
static void
i915_native_base(
	struct i915_native_report *r)
{
	/* Starts from an empty report. */
	memset(r, 0, sizeof(*r));

	/* The 8 MiB GGTT and the pages the driver writes at its top. */
	r->ggtt_pages = 1048576U;
	r->driver_ggtt_first = 1048576U - 256U - 8192U;

	/* Pipe A reads; the power domains of pipes B..D are off. */
	r->pipe[0].readable = 1;
	r->unreadable_pipes = 0xeU;

	/* The GPU's VT-d unit is enabled, readable and idle. */
	r->vtd_enabled = 1;
	r->vtd_readable = 1;
	r->vtd_ver = 0x10U;
}

/* Tells whether the reason of a decision names a word. */
static int
i915_native_reason_has(
	const struct i915_native_report *r,
	const char *word)
{
	const char *found;

	/* A decision without a reason names nothing. */
	if (r->reason == NULL)
		return 0;

	/* Looks for the word in the reason. */
	found = strstr(r->reason, word);
	if (found == NULL)
		return 0;

	/* Succeeded: the reason names the word. */
	return 1;
}
