/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The descriptions of the display 9 hardware (Skylake, Kaby Lake,
 * Coffee Lake and Comet Lake) the xd display code reads: the PCI ids of
 * each part, the ports, and the platform rows that tie them together.
 *
 * A display 9 part has four DDIs with their own buffers (A to D) and a
 * fifth (E) that shares DDI A's lanes; no combo or Type-C PHY, one
 * display buffer slice, and a south display of the Sunrise Point family
 * (Sunrise Point, Kaby Point) or of the Cannon Point family (Cannon
 * Point, Comet Point).  The parts differ in their DDI buffer tables by
 * power class (the H and S parts, the U parts, the Y parts), so each
 * class of each platform has a row of its own.
 *
 * The header holds data only and is included by display/xd-platform.c
 * alone.  Fields the rows leave empty (power wells, PLLs, CD clock steps,
 * swing tables) are those the xd code does not yet reach on display 9.
 */

#ifndef DRIVERS_GPU_I915_INTEL_DISPLAY_TABLES_XD9_H
#define DRIVERS_GPU_I915_INTEL_DISPLAY_TABLES_XD9_H

#include <stddef.h>
#include <stdint.h>

/*
 * The PCI device ids of each display 9 row, by platform and power class
 * (the U parts and the Y parts have DDI buffer tables of their own).
 *
 * Values referenced: this tree's src/drivers/gpu/i915/intel/pci-ids.h
 * (I915_PCI_SKL_IDS, I915_PCI_KBL_IDS, I915_PCI_CFL_IDS, I915_PCI_CML_IDS),
 * and Linux v6.8.12 drivers/gpu/drm/i915/intel_device_info.c (the ULT and
 * ULX id lists of Skylake to Comet Lake).
 */
static const uint16_t i915_xd9_ids_skl[] = {
	0x1902, 0x190a, 0x190b, 0x1912, 0x1917, 0x191a, 0x191b, 0x191d,
	0x192a, 0x192b, 0x192d, 0x1932, 0x193a, 0x193b, 0x193d,
};

static const uint16_t i915_xd9_ids_skl_u[] = {
	0x1906, 0x1913, 0x1916, 0x1921, 0x1923, 0x1926, 0x1927,
};

static const uint16_t i915_xd9_ids_skl_y[] = {
	0x190e, 0x1915, 0x191e,
};

static const uint16_t i915_xd9_ids_kbl[] = {
	0x5902, 0x5908, 0x590a, 0x590b, 0x5912, 0x5917, 0x591a, 0x591b,
	0x591d, 0x5923, 0x5927, 0x593b,
};

static const uint16_t i915_xd9_ids_kbl_u[] = {
	0x5906, 0x5913, 0x5916, 0x5921, 0x5926,
};

static const uint16_t i915_xd9_ids_kbl_y[] = {
	0x590e, 0x5915, 0x591c, 0x591e, 0x87c0,
};

static const uint16_t i915_xd9_ids_cfl[] = {
	0x3e90, 0x3e91, 0x3e92, 0x3e93, 0x3e94, 0x3e96, 0x3e98, 0x3e99,
	0x3e9a, 0x3e9b, 0x3e9c,
};

static const uint16_t i915_xd9_ids_cfl_u[] = {
	0x3ea0, 0x3ea1, 0x3ea2, 0x3ea3, 0x3ea4, 0x3ea5, 0x3ea6, 0x3ea7,
	0x3ea8, 0x3ea9,
};

static const uint16_t i915_xd9_ids_cfl_y[] = {
	0x87ca,
};

static const uint16_t i915_xd9_ids_cml[] = {
	0x9ba2, 0x9ba4, 0x9ba5, 0x9ba8, 0x9bc2, 0x9bc4, 0x9bc5, 0x9bc6,
	0x9bc8, 0x9be6, 0x9bf6,
};

static const uint16_t i915_xd9_ids_cml_u[] = {
	0x9b21, 0x9b41, 0x9baa, 0x9bac, 0x9bca, 0x9bcc,
};

/*
 * The ports of display 9: DDIs A to D with their own buffers and AUX
 * channels, and their hotplug pins in the south display (port A's too:
 * the Sunrise Point family carries it in its second hotplug register).
 * DDI E shares DDI A's lanes and has no AUX channel; it is listed and
 * not driven.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 2c, Command Reference:
 * Registers (DDI_BUF_CTL, DDI_AUX_CTL, SHOTPLUG_CTL, SHOTPLUG_CTL2),
 * Intel Kaby Lake PRM Volume 12, Display Engine (the DDIs and their
 * lane sharing).
 */
static const struct i915_xd_port_desc i915_xd9_ports[] = {
	{ 0, I915_XD_PHY_DDI, 0, 0, 0, 0, I915_XD_SOUTH_PIN_DDI, 0 },
	{ 1, I915_XD_PHY_DDI, 1, 0, 1, 0, I915_XD_SOUTH_PIN_DDI, 1 },
	{ 2, I915_XD_PHY_DDI, 2, 0, 2, 0, I915_XD_SOUTH_PIN_DDI, 2 },
	{ 3, I915_XD_PHY_DDI, 3, 0, 3, 0, I915_XD_SOUTH_PIN_DDI, 3 },
	{ 4, I915_XD_PHY_NONE, 4, 0, 0, 0, I915_XD_SOUTH_PIN_DDI, 4 },
};

/*
 * Builds one display 9 row from the parts that differ between rows: the
 * name, the kind, the south display's kind and the PCI ids.  Every other
 * field is the same on every display 9 part: three pipes, the ports
 * above, one display buffer slice of 892 blocks, eight watermark levels.
 *
 * Values referenced: Intel Kaby Lake PRM Volume 12, Display Engine (the
 * three pipes, the display buffer's size, the eight watermark levels).
 */
#define I915_XD9_ROW(name, kind, pch, ids) {				\
		name,							\
		kind,							\
		9U,							\
		pch,							\
		0x7U,							\
		0,							\
		ids,							\
		sizeof(ids) / sizeof(ids[0]),				\
		NULL,							\
		0U,							\
		i915_xd9_ports,						\
		sizeof(i915_xd9_ports) / sizeof(i915_xd9_ports[0]),	\
		0x0U,							\
		0x0U,							\
		0x0U,							\
		0U,							\
		I915_XD_TC_FLOW_NONE,					\
		I915_XD_TCCOLD_NONE,					\
		NULL,							\
		0U,							\
		NULL,							\
		0U,							\
		I915_XD_PLL_MAP_DPLL_CTRL2,				\
		0,							\
		NULL,							\
		0U,							\
		NULL,							\
		0U,							\
		0,							\
		1U,							\
		892U,							\
		NULL,							\
		0U,							\
		NULL,							\
		0x0U,							\
		0x0U,							\
		0,							\
		8U,							\
		0U,							\
		0,							\
		NULL,							\
		0U,							\
		NULL,							\
		0U,							\
		NULL,							\
		I915_XD_FIA_SINGLE,					\
		NULL,							\
		NULL							\
	}

/*
 * The display 9 rows: each platform's H and S parts, U parts and Y
 * parts.  Skylake and Kaby Lake pair with the Sunrise Point family,
 * Coffee Lake and Comet Lake with the Cannon Point family.
 */
static const struct i915_xd_platform i915_xd9_platforms[] = {
	I915_XD9_ROW("Skylake", I915_XD_KIND_SKL, I915_PCH_SPT, i915_xd9_ids_skl),
	I915_XD9_ROW("Skylake U", I915_XD_KIND_SKL, I915_PCH_SPT, i915_xd9_ids_skl_u),
	I915_XD9_ROW("Skylake Y", I915_XD_KIND_SKL, I915_PCH_SPT, i915_xd9_ids_skl_y),
	I915_XD9_ROW("Kaby Lake", I915_XD_KIND_KBL, I915_PCH_SPT, i915_xd9_ids_kbl),
	I915_XD9_ROW("Kaby Lake U", I915_XD_KIND_KBL, I915_PCH_SPT, i915_xd9_ids_kbl_u),
	I915_XD9_ROW("Kaby Lake Y", I915_XD_KIND_KBL, I915_PCH_SPT, i915_xd9_ids_kbl_y),
	I915_XD9_ROW("Coffee Lake", I915_XD_KIND_CFL, I915_PCH_CNP, i915_xd9_ids_cfl),
	I915_XD9_ROW("Coffee Lake U", I915_XD_KIND_CFL, I915_PCH_CNP, i915_xd9_ids_cfl_u),
	I915_XD9_ROW("Coffee Lake Y", I915_XD_KIND_CFL, I915_PCH_CNP, i915_xd9_ids_cfl_y),
	I915_XD9_ROW("Comet Lake", I915_XD_KIND_CML, I915_PCH_CNP, i915_xd9_ids_cml),
	I915_XD9_ROW("Comet Lake U", I915_XD_KIND_CML, I915_PCH_CNP, i915_xd9_ids_cml_u),
};

#undef I915_XD9_ROW

#endif /* DRIVERS_GPU_I915_INTEL_DISPLAY_TABLES_XD9_H */
