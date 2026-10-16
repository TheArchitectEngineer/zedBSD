/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The test's view of the reference display mode library (Linux v6.19
 * drivers/gpu/drm/amd/display/dc/dml, the Navi10 version 2 of DCN 2.0,
 * built on the host by amdrdna-dml-test.sh and never kept in the tree):
 * one plane on one pipe, linear 32 bits a pixel, unscaled, without ODM,
 * at the clocks the board runs.  The harness (harness.c) fills the
 * reference's own structures from this and copies its answers back, so
 * the test's code needs none of the reference's headers.
 */

#ifndef PLAN_WS039_TESTS_DML_ORACLE_ORACLE_API_H
#define PLAN_WS039_TESTS_DML_ORACLE_ORACLE_API_H

#include <stdbool.h>
#include <stdint.h>

/* The mode, the surface and the clocks in MHz (dram in MT/s). */
struct oracle_input {
	unsigned htotal;
	unsigned hactive;
	unsigned h_front_porch;
	unsigned vtotal;
	unsigned vactive;
	unsigned v_front_porch;
	double pixel_mhz;
	unsigned pitch_pixels;
	unsigned cursors;
	bool hdmi;
	double refclk_mhz;
	double dcfclk_mhz;
	double socclk_mhz;
	double fabricclk_mhz;
	double dispclk_mhz;
	double dppclk_mhz;
	double phyclk_mhz;
	double dram_mts;
};

/* The reference's answers: the support and the pipes the plane needs, the global sync, the watermarks in microseconds and the registers' fields. */
struct oracle_output {
	unsigned voltage_level;
	bool supported;
	unsigned pipes;
	bool pstate_support;
	bool cstate_enable;
	unsigned vstartup;
	unsigned vupdate_offset;
	unsigned vupdate_width;
	unsigned vready_offset;
	double required_dispclk;
	double required_dppclk;
	double dcfclk_deep_sleep;
	double wm_urgent;
	double wm_pstate;
	double wm_sr_exit;
	double wm_sr_enter_exit;
	double wm_memory_trip;
	unsigned dlg[64];
	unsigned dlg_count;
	unsigned ttu[32];
	unsigned ttu_count;
	unsigned rq[32];
	unsigned rq_count;
};

int
oracle_run(
	const struct oracle_input *input,
	struct oracle_output *output);

#endif
