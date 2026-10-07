/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p002: the host test of the VBT DVO port mapping and ADL-P's Type-C
 * PLL enable registers, taken out of src/drivers/gpu/i915/display/takeover.c
 * by host-vbt-pll.sh.  Every DVO code from 0 to 24 maps as Linux v6.8.12's
 * xelpd_port_mapping (intel_bios.c) does for the ports Alder Lake-P has (A,
 * B, C, TC1..TC4; D_XELPD and E_XELPD are not ADL-P's), and TC PLL 1..4 are
 * enabled at ADLP_PORTTC_PLL_ENABLE (i915_reg.h).  Prints "PASS name" or
 * "FAIL name ..." and exits with 1 when one failed.
 */

#include <stdint.h>
#include <stdio.h>

/* What the function and the table read of the driver's types (internal.h, takeover.c). */
#define I915_PORT_NONE		(-1)
#define I915_PORT_A		0
#define I915_PORT_B		1
#define I915_PORT_C		2
#define I915_PORT_D		3
#define I915_PORT_TC1		3
#define I915_PORT_TC2		4
#define I915_PORT_TC3		5
#define I915_PORT_TC4		6

enum {
	I915_DPLL_FUNCS_COMBO,
	I915_DPLL_FUNCS_TBT,
	I915_DPLL_FUNCS_DKL
};

struct i915_dvo_port_map {
	int port;
	uint8_t hdmi;
	uint8_t dp;
};

struct i915_adlp_pll_desc {
	const char *name;
	int id;
	int funcs;
	uint32_t reg;
};

int drv_i915_dvo_port_to_port(int display_ver, uint8_t dvo_port);

/* The constants, the function and the table rows of takeover.c (host-vbt-pll.sh writes them here). */
#include "vbt-pll-functions.inc"

/*
 * Linux's DVO codes (intel_vbt_defs.h) and the ADL-P port each names in its
 * xelpd mapping (-1: none on ADL-P).
 */
static const int expected_port[25] = {
	/* HDMIA 0, HDMIB 1, HDMIC 2, HDMID 3 (PORT_D_XELPD, not ADL-P's), LVDS 4, TV 5, CRT 6 */
	I915_PORT_A, I915_PORT_B, I915_PORT_C, I915_PORT_NONE, I915_PORT_NONE, I915_PORT_NONE, I915_PORT_NONE,
	/* DPB 7, DPC 8, DPD 9 (D_XELPD), DPA 10, DPE 11 (E_XELPD), HDMIE 12 (E_XELPD) */
	I915_PORT_B, I915_PORT_C, I915_PORT_NONE, I915_PORT_A, I915_PORT_NONE, I915_PORT_NONE,
	/* DPF 13, HDMIF 14, DPG 15, HDMIG 16, DPH 17, HDMIH 18, DPI 19, HDMII 20 */
	I915_PORT_TC1, I915_PORT_TC1, I915_PORT_TC2, I915_PORT_TC2, I915_PORT_TC3, I915_PORT_TC3, I915_PORT_TC4, I915_PORT_TC4,
	/* MIPIA 21 .. MIPID 24 are DSI's (dsi_dvo_port_to_port), not this mapping's */
	I915_PORT_NONE, I915_PORT_NONE, I915_PORT_NONE, I915_PORT_NONE
};

/* ADL-P's TC PLL enable registers: PORTTC1 0x46038, PORTTC2 0x46040, _MMIO_PORT's step of 8. */
static const uint32_t expected_tc_enable[4] = { 0x46038U, 0x46040U, 0x46048U, 0x46050U };

/*
 * Runs the checks.
 */
int
main(void)
{
	unsigned failures;
	unsigned code;
	unsigned index;
	int port;

	/* Every code on display 13. */
	failures = 0;
	for (code = 0; code < 25U; code++) {
		port = drv_i915_dvo_port_to_port(13, (uint8_t)code);
		if (port != expected_port[code]) {
			printf("FAIL dvo-%u: port %d, Linux's xelpd %d\n", code, port, expected_port[code]);
			failures++;
		}
	}

	/* All of them. */
	if (failures == 0)
		printf("PASS dvo: codes 0..24 map as Linux's xelpd_port_mapping on ADL-P\n");

	/* The legacy mapping still gives HDMID port D. */
	port = drv_i915_dvo_port_to_port(12, 3U);
	if (port != I915_PORT_D) {
		printf("FAIL dvo-legacy: HDMID on display 12 gives %d\n", port);
		failures++;
	} else {
		printf("PASS dvo-legacy: HDMID on display 12 is port D\n");
	}

	/* The seven PLLs, the TC ones at PORTTC_PLL_ENABLE. */
	if (sizeof(adlp_plls) / sizeof(adlp_plls[0]) != 7U) {
		printf("FAIL pll-count: %u rows\n", (unsigned)(sizeof(adlp_plls) / sizeof(adlp_plls[0])));
		return 1;
	}

	/* The TC PLLs at PORTTC_PLL_ENABLE. */
	for (index = 0; index < 4U; index++) {
		if (adlp_plls[3U + index].reg != expected_tc_enable[index] || adlp_plls[3U + index].funcs != I915_DPLL_FUNCS_DKL) {
			printf("FAIL pll-tc%u: %s at 0x%05x, ADLP_PORTTC_PLL_ENABLE 0x%05x\n", index + 1U, adlp_plls[3U + index].name, (unsigned)adlp_plls[3U + index].reg, (unsigned)expected_tc_enable[index]);
			failures++;
		}
	}

	/* The combo PLLs and the TBT PLL where they were. */
	if (adlp_plls[0].reg != 0x46010U || adlp_plls[1].reg != 0x46014U || adlp_plls[2].reg != 0x46020U) {
		printf("FAIL pll-combo: DPLL0/1 or the TBT PLL moved\n");
		failures++;
	}

	/* All of them. */
	if (failures == 0)
		printf("PASS pll: DPLL0 0x46010, DPLL1 0x46014, TBT 0x46020, TC1..4 0x46038/0x46040/0x46048/0x46050\n");

	/* Reports whether every check passed. */
	if (failures != 0)
		return 1;

	/* Succeeded. */
	return 0;
}
