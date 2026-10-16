/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test (m) of ws039-p015: the amdrdna display plan (mode-plan.c,
 * fixed point) against the reference display mode library (Linux v6.19
 * drivers/gpu/drm/amd/display/dc/dml, Navi10 version 2, built on the host
 * from the temp tree by amdrdna-dml-test.sh through dml-oracle/harness.c,
 * never kept in the tree), for the modes a monitor's EDID lists and some
 * larger ones, at several clock sets, and with a surface wider than the
 * mode (the reference, like the driver it comes from, always counts the
 * plane's cursor).  The global sync and the request
 * fields must be equal; a field the reference works out through floating
 * point may differ by one in its last unit (the plan's file comment says
 * why); and a mode the reference refuses, or shows only on two pipes,
 * must be refused.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <uapi/errno.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

#include "dml-oracle/oracle-api.h"

/* A mode: the totals, the active area, the front porches and the pixel clock in kHz. */
struct test_mode {
	const char *name;
	uint32_t h_total;
	uint32_t width;
	uint32_t h_front;
	uint32_t v_total;
	uint32_t height;
	uint32_t v_front;
	uint32_t pixel_khz;
};

/* A clock set in kHz (the memory in MT/s). */
struct test_clocks {
	const char *name;
	uint32_t reference_khz;
	uint32_t return_khz;
	uint32_t fabric_khz;
	uint32_t display_khz;
	uint32_t pipe_khz;
	uint32_t memory_mts;
};

/* The modes: the DMT and CTA timings an EDID lists, a reduced-blanking one and the larger ones. */
static const struct test_mode modes[] = {
	{ "640x480@60", 800, 640, 16, 525, 480, 10, 25175 },
	{ "640x480@75", 840, 640, 16, 500, 480, 1, 31500 },
	{ "720x400@70", 900, 720, 18, 449, 400, 12, 28322 },
	{ "800x600@60", 1056, 800, 40, 628, 600, 1, 40000 },
	{ "800x600@75", 1056, 800, 16, 625, 600, 1, 49500 },
	{ "1024x768@60", 1344, 1024, 24, 806, 768, 3, 65000 },
	{ "1024x768@75", 1312, 1024, 16, 800, 768, 1, 78750 },
	{ "1152x864@75", 1600, 1152, 64, 900, 864, 1, 108000 },
	{ "1280x720@60", 1650, 1280, 110, 750, 720, 5, 74250 },
	{ "1280x1024@60", 1688, 1280, 48, 1066, 1024, 1, 108000 },
	{ "1280x1024@75", 1688, 1280, 16, 1066, 1024, 1, 135000 },
	{ "1600x900@60", 1800, 1600, 24, 1000, 900, 1, 108000 },
	{ "1920x1080@60", 2200, 1920, 88, 1125, 1080, 4, 148500 },
	{ "1920x1080@60rb", 2080, 1920, 48, 1111, 1080, 3, 138500 },
	{ "2560x1440@60", 2720, 2560, 48, 1481, 1440, 3, 241500 },
	{ "3840x2160@30", 4400, 3840, 176, 2250, 2160, 8, 297000 },
	{ "3840x2160@60", 4400, 3840, 176, 2250, 2160, 8, 594000 },
};

/* The clock sets: the board's boot clocks as far as known, and faster ones. */
static const struct test_clocks clocks[] = {
	{ "boot", 100000, 506000, 506000, 625000, 625000, 14000 },
	{ "hub50", 50000, 506000, 506000, 625000, 625000, 14000 },
	{ "fast", 100000, 875000, 875000, 1100000, 1100000, 14000 },
	{ "slow", 100000, 310000, 400000, 400000, 300000, 1600 },
};

/* The checks run and those that failed, and the fields the reference rounds differently by one. */
static unsigned checks;
static unsigned failures;
static unsigned near;

static void check(int ok, const char *format, ...);
static void compare(const char *what, const char *field, uint32_t ours, uint32_t theirs, bool exact);
static void run_case(const struct test_mode *mode, const struct test_clocks *clock, uint32_t pitch);

/* Reports one check. */
static void
check(
	int ok,
	const char *format,
	...)
{
	va_list arguments;

	/* Counts it and says a failure. */
	checks++;
	if (ok)
		return;
	failures++;
	va_start(arguments, format);
	printf("FAIL ");
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
}

/* Compares a field: equal, or within one when the reference rounds it through floating point. */
static void
compare(
	const char *what,
	const char *field,
	uint32_t ours,
	uint32_t theirs,
	bool exact)
{
	int64_t difference;

	/* The difference. */
	difference = (int64_t)ours - (int64_t)theirs;
	if (difference != 0 && !exact && (difference == 1 || difference == -1)) {
		near++;
		checks++;
		return;
	}

	/* Equal, else a failure. */
	check(difference == 0, "%s %s: ours %u reference %u", what, field, (unsigned)ours, (unsigned)theirs);
}

/* Runs one mode at one clock set through both and compares them. */
static void
run_case(
	const struct test_mode *mode,
	const struct test_clocks *clock,
	uint32_t pitch)
{
	struct amdrdna_mode_plan_input input;
	struct amdrdna_mode_plan plan;
	struct oracle_input reference_input;
	struct oracle_output reference;
	char what[128];
	int ours;
	int theirs;

	/* The case's name. */
	snprintf(what, sizeof(what), "%s %s pitch=%u", mode->name, clock->name, (unsigned)pitch);

	/* Ours. */
	input = (struct amdrdna_mode_plan_input){ 0 };
	input.h_total = mode->h_total;
	input.width = mode->width;
	input.h_front = mode->h_front;
	input.v_total = mode->v_total;
	input.height = mode->height;
	input.v_front = mode->v_front;
	input.pixel_khz = mode->pixel_khz;
	input.pitch = pitch;
	input.reference_khz = clock->reference_khz;
	input.return_khz = clock->return_khz;
	input.fabric_khz = clock->fabric_khz;
	input.display_khz = clock->display_khz;
	input.pipe_khz = clock->pipe_khz;
	input.memory_mts = clock->memory_mts;
	ours = amdrdna_mode_plan(&input, &plan);

	/* The reference's. */
	reference_input = (struct oracle_input){ 0 };
	reference_input.htotal = mode->h_total;
	reference_input.hactive = mode->width;
	reference_input.h_front_porch = mode->h_front;
	reference_input.vtotal = mode->v_total;
	reference_input.vactive = mode->height;
	reference_input.v_front_porch = mode->v_front;
	reference_input.pixel_mhz = mode->pixel_khz / 1000.0;
	reference_input.pitch_pixels = pitch;
	reference_input.cursors = 1U;
	reference_input.hdmi = true;
	reference_input.refclk_mhz = clock->reference_khz / 1000.0;
	reference_input.dcfclk_mhz = clock->return_khz / 1000.0;
	reference_input.socclk_mhz = clock->return_khz / 1000.0;
	reference_input.fabricclk_mhz = clock->fabric_khz / 1000.0;
	reference_input.dispclk_mhz = clock->display_khz / 1000.0;
	reference_input.dppclk_mhz = clock->pipe_khz / 1000.0;
	reference_input.phyclk_mhz = 810.0;
	reference_input.dram_mts = clock->memory_mts;
	theirs = oracle_run(&reference_input, &reference);

	/* Both refuse, or both plan; a mode the reference shows on two pipes is refused (the driver uses one). */
	if (theirs == 0 && reference.pipes > 1U) {
		check(ours == ERANGE, "%s: ours %d, the reference needs %u pipes", what, ours, reference.pipes);
		return;
	}
	if (theirs != 0 || ours != 0) {
		check(theirs != 0 && ours != 0, "%s: ours %d reference %d (level %u supported %d)", what, ours, theirs,
		    reference.voltage_level, reference.supported);
		return;
	}

	/* The use of the blank, the memory clock's change and the global sync. */
	compare(what, "cstate", plan.vblank_use != 2U, reference.cstate_enable, true);
	compare(what, "pstate", plan.memory_switch != 0U, reference.pstate_support, true);
	compare(what, "vstartup", plan.start_lines, reference.vstartup, true);
	compare(what, "vupdate_offset", plan.update_offset, reference.vupdate_offset, true);
	compare(what, "vupdate_width", plan.update_width, reference.vupdate_width, false);
	compare(what, "vready_offset", plan.ready_offset, reference.vready_offset, false);

	/* The watermarks in ns. */
	compare(what, "wm_urgent", plan.urgent_ns, (uint32_t)(reference.wm_urgent * 1000), false);
	compare(what, "wm_pstate", plan.switch_ns, (uint32_t)(reference.wm_pstate * 1000), false);
	compare(what, "wm_sr_exit", plan.refresh_exit_ns, (uint32_t)(reference.wm_sr_exit * 1000), false);
	compare(what, "wm_sr_enter_exit", plan.refresh_enter_exit_ns, (uint32_t)(reference.wm_sr_enter_exit * 1000), false);
	compare(what, "wm_memory_trip", plan.trip_ns, (uint32_t)(reference.wm_memory_trip * 1000), false);

	/* The request fields (the reference's order: luma, chroma, expansion modes). */
	compare(what, "rq chunk_size", plan.chunk_log, reference.rq[0], true);
	compare(what, "rq min_chunk_size", plan.least_chunk_log, reference.rq[1], true);
	compare(what, "rq meta_chunk_size", plan.meta_chunk_log, reference.rq[2], true);
	compare(what, "rq min_meta_chunk_size", plan.least_meta_chunk_log, reference.rq[3], true);
	compare(what, "rq dpte_group_size", plan.pte_group_log, reference.rq[4], true);
	compare(what, "rq mpte_group_size", plan.meta_pte_group_log, reference.rq[5], true);
	compare(what, "rq swath_height", plan.swath_log, reference.rq[6], true);
	compare(what, "rq pte_row_height_linear", plan.pte_rows_log, reference.rq[7], true);
	compare(what, "rq drq_expansion_mode", plan.data_expansion, reference.rq[16], true);
	compare(what, "rq prq_expansion_mode", plan.pte_expansion, reference.rq[17], true);
	compare(what, "rq mrq_expansion_mode", plan.meta_expansion, reference.rq[18], true);
	compare(what, "rq crq_expansion_mode", plan.cursor_expansion, reference.rq[19], true);

	/* The deadline fields (the reference's struct order). */
	compare(what, "refcyc_h_blank_end", plan.blank_end_cycles, reference.dlg[0], false);
	compare(what, "dlg_vblank_end", plan.blank_end_line, reference.dlg[1], true);
	compare(what, "min_dst_y_next_start", plan.next_start_quarters, reference.dlg[2], false);
	compare(what, "refcyc_per_htotal", plan.line_cycles, reference.dlg[4], false);
	compare(what, "refcyc_x_after_scaler", plan.after_scaler_cycles, reference.dlg[5], false);
	compare(what, "dst_y_after_scaler", plan.after_scaler_lines, reference.dlg[6], true);
	compare(what, "dst_y_prefetch", plan.prefetch_quarters, reference.dlg[7], true);
	compare(what, "dst_y_per_vm_vblank", plan.vm_vblank_quarters, reference.dlg[8], true);
	compare(what, "dst_y_per_row_vblank", plan.row_vblank_quarters, reference.dlg[9], true);
	compare(what, "dst_y_per_vm_flip", plan.vm_flip_quarters, reference.dlg[10], true);
	compare(what, "dst_y_per_row_flip", plan.row_flip_quarters, reference.dlg[11], true);
	compare(what, "ref_freq_to_pix_freq", plan.reference_to_pixel, reference.dlg[12], false);
	compare(what, "vratio_prefetch", plan.prefetch_ratio, reference.dlg[13], false);
	compare(what, "vratio_prefetch_c", plan.prefetch_ratio_chroma, reference.dlg[14], true);
	compare(what, "refcyc_per_pte_group_vblank_l", plan.pte_group_vblank_cycles, reference.dlg[16], false);
	compare(what, "refcyc_per_meta_chunk_vblank_l", plan.meta_chunk_vblank_cycles, reference.dlg[18], false);
	compare(what, "refcyc_per_meta_chunk_vblank_c", plan.meta_chunk_vblank_cycles, reference.dlg[19], false);
	compare(what, "refcyc_per_pte_group_flip_l", plan.pte_group_flip_cycles, reference.dlg[20], true);
	compare(what, "refcyc_per_meta_chunk_flip_l", plan.meta_chunk_flip_cycles, reference.dlg[22], true);
	compare(what, "dst_y_per_pte_row_nom_l", plan.pte_row_quarters, reference.dlg[24], true);
	compare(what, "refcyc_per_pte_group_nom_l", plan.pte_group_cycles, reference.dlg[26], false);
	compare(what, "dst_y_per_meta_row_nom_l", plan.meta_row_quarters, reference.dlg[28], true);
	compare(what, "dst_y_per_meta_row_nom_c", plan.meta_row_quarters, reference.dlg[29], true);
	compare(what, "refcyc_per_meta_chunk_nom_l", plan.meta_chunk_cycles, reference.dlg[30], false);
	compare(what, "refcyc_per_line_delivery_pre_l", plan.line_delivery_prefetch_cycles, reference.dlg[32], false);
	compare(what, "refcyc_per_line_delivery_l", plan.line_delivery_cycles, reference.dlg[34], false);
	compare(what, "chunk_hdl_adjust_cur0", plan.cursor_chunk_adjust, reference.dlg[36], true);
	compare(what, "chunk_hdl_adjust_cur1", plan.cursor_chunk_adjust, reference.dlg[37], true);
	compare(what, "vready_after_vcount0", plan.ready_after_zero, reference.dlg[38], true);
	compare(what, "dst_y_delta_drq_limit", plan.request_limit, reference.dlg[45], true);

	/* The arbitration fields. */
	compare(what, "qos_level_low_wm", plan.low_level_mark, reference.ttu[0], true);
	compare(what, "qos_level_high_wm", plan.high_level_mark, reference.ttu[1], false);
	compare(what, "min_ttu_vblank", plan.vblank_ttu_cycles, reference.ttu[2], false);
	compare(what, "qos_level_flip", plan.flip_level, reference.ttu[3], true);
	compare(what, "refcyc_per_req_delivery_l", plan.request_delivery_cycles, reference.ttu[4], false);
	compare(what, "refcyc_per_req_delivery_cur0", plan.cursor_delivery_cycles, reference.ttu[6], false);
	compare(what, "refcyc_per_req_delivery_pre_l", plan.request_delivery_prefetch_cycles, reference.ttu[8], false);
	compare(what, "refcyc_per_req_delivery_pre_cur0", plan.cursor_delivery_prefetch_cycles, reference.ttu[10], false);
	compare(what, "qos_level_fixed_l", plan.fixed_level, reference.ttu[12], true);
	compare(what, "qos_level_fixed_c", plan.fixed_level, reference.ttu[13], true);
	compare(what, "qos_level_fixed_cur0", plan.fixed_level, reference.ttu[14], true);
}

/* Runs every mode at every clock set, and on a surface wider than the mode. */
int
main(void)
{
	size_t mode;
	size_t clock;
	uint32_t pitch;

	/* Every case. */
	for (mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
		for (clock = 0; clock < sizeof(clocks) / sizeof(clocks[0]); clock++) {
			pitch = (modes[mode].width + 63U) / 64U * 64U;
			run_case(&modes[mode], &clocks[clock], pitch);
			run_case(&modes[mode], &clocks[clock], 3840U);
		}
	}

	/* The result. */
	printf("%s amdrdna mode plan: %u checks, %u failures, %u within one of the reference\n",
	    failures == 0 ? "PASS" : "FAIL", checks, failures, near);
	return failures == 0 ? 0 : 1;
}
