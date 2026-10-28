/*
 * ws291: host test of the amdrdna watermark table (thermal-format.c amdrdna_power_watermarks_build) against Linux's
 * Watermarks_t as navi10_set_watermarks_table fills it from the ranges dcn20_resource_construct gives for the memory
 * clock's levels (a reader row a level, one writer row).  Built by watermarks-test.sh.
 * Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

#include "smu11_driver_if_navi10.h"

static int failures;

static void
check(int good, const char *what, unsigned value, unsigned wanted)
{
	if (!good) {
		printf("FAIL %s: %u, wanted %u\n", what, value, wanted);
		failures++;
	}
}

static void
case_levels(const uint32_t *mhz, unsigned count)
{
	uint8_t table[AMDRDNA_SMU_WATERMARKS_BYTES];
	Watermarks_t linux_table;
	unsigned i;
	unsigned used;

	amdrdna_power_watermarks_build(mhz, count, table);

	/* Linux's table: zeroed, readers by dcn20_fpu_set_wm_ranges, writer row 0. */
	memset(&linux_table, 0, sizeof(linux_table));
	used = count > NUM_WM_RANGES ? NUM_WM_RANGES : count;
	for (i = 0; i < used; i++) {
		linux_table.WatermarkRow[WM_DCEFCLK][i].MinClock = 0;
		linux_table.WatermarkRow[WM_DCEFCLK][i].MaxClock = 0xffff;
		linux_table.WatermarkRow[WM_DCEFCLK][i].MinUclk = i > 0 ? mhz[i - 1] + 1 : 0;
		linux_table.WatermarkRow[WM_DCEFCLK][i].MaxUclk = mhz[i];
		linux_table.WatermarkRow[WM_DCEFCLK][i].WmSetting = i;
	}
	linux_table.WatermarkRow[WM_DCEFCLK][0].MinUclk = 0;
	linux_table.WatermarkRow[WM_DCEFCLK][used - 1].MaxUclk = 0xffff;
	linux_table.WatermarkRow[WM_SOCCLK][0].MinClock = 0;
	linux_table.WatermarkRow[WM_SOCCLK][0].MaxClock = 0xffff;
	linux_table.WatermarkRow[WM_SOCCLK][0].MinUclk = 0;
	linux_table.WatermarkRow[WM_SOCCLK][0].MaxUclk = 0xffff;
	linux_table.WatermarkRow[WM_SOCCLK][0].WmSetting = 0;

	check(sizeof(linux_table) == AMDRDNA_SMU_WATERMARKS_BYTES, "table size", (unsigned)sizeof(linux_table), AMDRDNA_SMU_WATERMARKS_BYTES);
	check(sizeof(WatermarkRowGeneric_t) == AMDRDNA_SMU_WATERMARK_ROW_BYTES, "row size", (unsigned)sizeof(WatermarkRowGeneric_t), AMDRDNA_SMU_WATERMARK_ROW_BYTES);
	for (i = 0; i < sizeof(linux_table); i++)
		check(table[i] == ((const uint8_t *)&linux_table)[i], "byte", table[i], ((const uint8_t *)&linux_table)[i]);
	printf("%s watermarks %u levels\n", failures == 0 ? "PASS" : "FAIL", count);
}

int
main(void)
{
	static const uint32_t four[] = { 100, 500, 750, 875 };
	static const uint32_t two[] = { 100, 875 };
	static const uint32_t five[] = { 100, 300, 500, 750, 875 };

	case_levels(four, 4);
	case_levels(two, 2);
	case_levels(five, 5);
	return failures == 0 ? 0 : 1;
}
