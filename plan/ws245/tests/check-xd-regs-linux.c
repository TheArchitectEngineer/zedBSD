/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks every value of the xd register headers against the Linux
 * v6.8.12 register header it was taken from (run by
 * check-xd-regs-linux.sh, which points the include path at an unpacked
 * Linux tree).  Every pair of tests/display/xd-regs-map.h must find its
 * Linux name here; a pair skipped for a missing name is a failure too.
 */

#include <stdint.h>
#include <stdio.h>

/*
 * The enumerations the Linux register macros subtract from or index
 * with.  Only differences of them reach a register value, so the
 * numbers chosen here are arbitrary but distinct.
 */
enum ws245_linux_names {
	PIPE_A = 0,
	PIPE_B = 1,
	PIPE_C = 2,
	PIPE_D = 3,
	TRANSCODER_A = 0,
	HPD_PORT_A = 10,
	HPD_PORT_TC1 = 20,
	TC_PORT_1 = 0,
	TC_PORT_4 = 3,
	PHY_E = 4
};

/*
 * The display description the Linux register macros read the pipe and
 * transcoder blocks from (display 12 and 13: 4 KiB apart from 0x70000
 * and 0x60000).
 */
struct ws245_display_info {
	uint32_t mmio_offset;
	uint32_t pipe_offsets[4];
	uint32_t trans_offsets[4];
	uint32_t cursor_offsets[4];
};

/*
 * The description of a display 12 or 13 device, which the Linux
 * DISPLAY_INFO() is pointed at.
 */
static const struct ws245_display_info ws245_display_info = {
	0U,
	{ 0x70000U, 0x71000U, 0x72000U, 0x73000U },
	{ 0x60000U, 0x61000U, 0x62000U, 0x63000U },
	{ 0x70080U, 0x71080U, 0x72080U, 0x73080U }
};

/*
 * The device the GMBUS macros take, with the PCH display's GMBUS base.
 */
struct ws245_gmbus_device {
	struct {
		struct {
			uint32_t mmio_base;
		} gmbus;
	} display;
};

#define DISPLAY_INFO(device) (&ws245_display_info)

#include <linux/types.h>

#include "i915_reg.h"
#include "display/intel_backlight_regs.h"
#include "display/intel_color_regs.h"
#include "display/intel_combo_phy_regs.h"
#include "display/intel_dkl_phy_regs.h"
#include "display/intel_dmc_regs.h"
#include "display/intel_dp_aux_regs.h"
#include "display/intel_gmbus_regs.h"
#include "display/intel_hti_regs.h"
#include "display/intel_mg_phy_regs.h"
#include "display/intel_pps_regs.h"
#include "display/skl_watermark_regs.h"
/* The DPCD header (include/drm/display/drm_dp.h) marks its packet structures packed. */
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#include "../../../../include/drm/display/drm_dp.h"

/* The GMBUS pin numbers, which the Linux GMBUS header defines beside its functions. */
#define GMBUS_PIN_DISABLED	0
#define GMBUS_PIN_1_BXT		1
#define GMBUS_PIN_2_BXT		2
#define GMBUS_PIN_3_BXT		3
#define GMBUS_PIN_9_TC1_ICP	9
#define GMBUS_PIN_10_TC2_ICP	10
#define GMBUS_PIN_11_TC3_ICP	11
#define GMBUS_PIN_12_TC4_ICP	12
#define GMBUS_PIN_13_TC5_TGP	13
#define GMBUS_PIN_14_TC6_TGP	14

#include "intel/display-regs-xd.h"
#include "intel/display-regs-xd-south.h"
#include "intel/display-dpcd-xd.h"

/* A pointer the Linux macros name as their device; never dereferenced but by the stubs above. */
static void *dev_priv;

/* The pairs that matched, and those that did not. */
static unsigned ws245_matched;
static unsigned ws245_mismatched;

static void ws245_compare(uint32_t xd, uint32_t linux_value, const char *xd_text, const char *linux_text);

/*
 * Compares the pairs and reports the count.
 */
int
main(void)
{
	(void)dev_priv;

#define I915_XD_MAP_REG(xd, linux) ws245_compare((uint32_t)(xd), i915_mmio_reg_offset(linux), #xd, #linux);
#define I915_XD_MAP_VAL(xd, linux) ws245_compare((uint32_t)(xd), (uint32_t)(linux), #xd, #linux);
#include "xd-regs-map.h"

	/* Reports the result: every pair must be present and equal. */
	printf("check_xd_regs_linux: %u pairs matched, %u differ\n", ws245_matched, ws245_mismatched);
	if (ws245_mismatched != 0U)
		return 1;

	/* Succeeded: every xd value is the Linux one. */
	return 0;
}

/* Compares one pair and reports a difference. */
static void
ws245_compare(
	uint32_t xd,
	uint32_t linux_value,
	const char *xd_text,
	const char *linux_text)
{
	/* Counts a match. */
	if (xd == linux_value) {
		ws245_matched++;
		return;
	}

	/* Reports a difference with both texts. */
	ws245_mismatched++;
	printf("DIFFER: %s = 0x%08x, %s = 0x%08x\n", xd_text, xd, linux_text, linux_value);
}
