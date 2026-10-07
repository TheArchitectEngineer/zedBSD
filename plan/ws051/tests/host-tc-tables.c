/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p002b: the host test of the tables around the Type-C ports, with the
 * functions taken out of the driver by host-tc.sh (tc-tables.inc):
 *   - the AUX channels' power domains (power.c, H1): Linux v6.8.12's display
 *     version 12 and 13 port-domain tables;
 *   - the DKL PHY's window, bank index register and value (dkl-phy.c);
 *   - the long pulse of a Type-C pin in GEN11_TC_HOTPLUG_CTL (hotplug.c, H5).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;

/* The Type-C pins of the hotplug path (enum hpd_pin, intel/hotplug.h). */
enum hpd_pin {
	HPD_NONE = 0,
	HPD_PORT_A = HPD_NONE,
	HPD_PORT_B,
	HPD_PORT_C,
	HPD_PORT_D,
	HPD_PORT_E,
	HPD_PORT_TC1,
	HPD_PORT_TC2,
	HPD_PORT_TC3,
	HPD_PORT_TC4,
	HPD_PORT_TC5,
	HPD_PORT_TC6
};

#include "tc-tables.inc"

/* How many checks ran and failed. */
static unsigned checks;
static unsigned failures;

static void check(int condition, const char *what);

/*
 * Runs every check and reports the count.
 */
int
main(void)
{
	uint32_t index_reg;
	uint32_t index_value;
	uint32_t window;
	int channel;
	char what[96];

	/* Display version 13: A to C their AUX wells, USBC1 to 4 the AUX_USBC wells, D and E of XELPD the AUX wells D and E. */
	for (channel = 0; channel <= 2; channel++) {
		snprintf(what, sizeof(what), "d13 legacy domain of AUX %c", 'A' + channel);
		check((int)drv_i915_aux_legacy_power_domain(13u, channel) == (int)I915_PW_DOMAIN_AUX_A + channel, what);
	}
	for (channel = I915_AUX_CH_USBC1; channel <= I915_AUX_CH_USBC4; channel++) {
		snprintf(what, sizeof(what), "d13 legacy domain of USBC%d is AUX_USBC%d", channel - 2, channel - 2);
		check((int)drv_i915_aux_legacy_power_domain(13u, channel) == (int)I915_PW_DOMAIN_AUX_USBC1 + (channel - I915_AUX_CH_USBC1), what);
		snprintf(what, sizeof(what), "d13 Thunderbolt domain of USBC%d is AUX_TBT%d", channel - 2, channel - 2);
		check((int)drv_i915_aux_tbt_power_domain(13u, channel) == (int)I915_PW_DOMAIN_AUX_TBT1 + (channel - I915_AUX_CH_USBC1), what);
		check(drv_i915_aux_io_power_domain(13u, channel) == I915_PW_DOMAIN_AUX_IO_A, "d13 a Type-C channel has no AUX I/O (AUX_IO_A)");
	}
	check(drv_i915_aux_legacy_power_domain(13u, I915_AUX_CH_D_XELPD) == I915_PW_DOMAIN_AUX_D, "d13 D_XELPD is AUX_D");
	check(drv_i915_aux_legacy_power_domain(13u, I915_AUX_CH_E_XELPD) == I915_PW_DOMAIN_AUX_E, "d13 E_XELPD is AUX_E");
	check(drv_i915_aux_io_power_domain(13u, I915_AUX_CH_D_XELPD) == I915_PW_DOMAIN_AUX_IO_D, "d13 D_XELPD's I/O is AUX_IO_D");
	check(drv_i915_aux_tbt_power_domain(13u, I915_AUX_CH_D_XELPD) == I915_PW_DOMAIN_NUM, "d13 D_XELPD has no Thunderbolt domain");
	check(drv_i915_aux_tbt_power_domain(13u, 0) == I915_PW_DOMAIN_NUM, "d13 AUX A has no Thunderbolt domain");
	check(drv_i915_aux_io_power_domain(13u, 1) == I915_PW_DOMAIN_AUX_IO_B, "d13 AUX B's I/O");

	/* The H1 error: TC1's channel (AUX_CH_D = 3) is AUX_USBC1, not AUX_D. */
	check(drv_i915_aux_legacy_power_domain(13u, 3) != I915_PW_DOMAIN_AUX_D, "H1: TC1 is not AUX_D any more");

	/* Display version 12: six Type-C channels. */
	check(drv_i915_aux_legacy_power_domain(12u, I915_AUX_CH_USBC6) == I915_PW_DOMAIN_AUX_USBC6, "d12 USBC6");
	check(drv_i915_aux_tbt_power_domain(12u, I915_AUX_CH_USBC6) == I915_PW_DOMAIN_AUX_TBT6, "d12 TBT6");

	/* The DKL window: TC1's CMN_UC_DW27 (0x236c): window 0x168000 + 0x36c, bank 2 in byte 0 of 0x1010a0. */
	window = drv_i915_dkl_phy_window(0u, 0x236cu, &index_reg, &index_value);
	check(window == 0x16836cu, "dkl: TC1 window");
	check(index_reg == 0x1010a0u && index_value == 0x2u, "dkl: TC1 bank index");

	/* TC2: the next 4 KB window, its bank in byte 1. */
	window = drv_i915_dkl_phy_window(1u, 0x236cu, &index_reg, &index_value);
	check(window == 0x16936cu && index_reg == 0x1010a0u && index_value == 0x200u, "dkl: TC2 window and bank");

	/* TC5: the high index register, byte 0. */
	window = drv_i915_dkl_phy_window(4u, 0x1014u, &index_reg, &index_value);
	check(window == 0x16c014u && index_reg == 0x1010a4u && index_value == 0x1u, "dkl: TC5 window and bank");

	/* The long pulse: bit 1 of each Type-C pin's 4-bit field; a short one (bit 0) is not long. */
	check(i915_gen11_port_hotplug_long_detect(HPD_PORT_TC1, 0x2u), "long: TC1 bit 1");
	check(!i915_gen11_port_hotplug_long_detect(HPD_PORT_TC1, 0x1u), "long: TC1 short");
	check(i915_gen11_port_hotplug_long_detect(HPD_PORT_TC2, 0x20u), "long: TC2 bit 5");
	check(!i915_gen11_port_hotplug_long_detect(HPD_PORT_TC2, 0x2u), "long: TC2 not TC1's bit");
	check(i915_gen11_port_hotplug_long_detect(HPD_PORT_TC4, 0x2000u), "long: TC4 bit 13");
	check(!i915_gen11_port_hotplug_long_detect(HPD_PORT_A, 0xffffffffu), "long: a combo pin has no field");

	/* The summary the script reads. */
	printf("ws051-p002b host-tc-tables checks=%u failures=%u\n", checks, failures);
	if (failures != 0u)
		return 1;

	/* Succeeded: every check held. */
	return 0;
}

/* Counts a check and reports a failed one. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check; a failed one is named. */
	checks++;
	if (!condition) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}
