/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/* Host test of the DC8200 modes (ws277-p005). */

#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/dc8200-mode.h"
#include <uapi/errno.h>

static int passes;
static int failures;

static void
check(int condition, const char *what)
{
	if (condition) {
		passes++;
	} else {
		failures++;
		printf("FAIL %s\n", what);
	}
}

static int
find_write(const struct powervr_dc_hdmi_write *writes, unsigned count, unsigned reg, unsigned *value, unsigned *at)
{
	unsigned index;

	for (index = 0; index < count; index++) {
		if (writes[index].reg == reg) {
			*value = writes[index].value;
			*at = index;
			return 1;
		}
	}
	return 0;
}

static void
test_line(void)
{
	uint32_t w, h, r;

	check(powervr_dc_mode_from_line("root=x display.mode=1280x720@60 quiet", &w, &h, &r) == 0 && w == 1280 && h == 720 && r == 60, "line with rate");
	check(powervr_dc_mode_from_line("display.mode=1920x1080", &w, &h, &r) == 0 && w == 1920 && h == 1080 && r == 60, "line without rate takes 60");
	check(powervr_dc_mode_from_line("xdisplay.mode=640x480", &w, &h, &r) == ENOENT, "word inside another word");
	check(powervr_dc_mode_from_line("kmsg=quiet", &w, &h, &r) == ENOENT, "absent word");
	check(powervr_dc_mode_from_line(NULL, &w, &h, &r) == ENOENT, "no line");
	check(powervr_dc_mode_from_line("display.mode=1280", &w, &h, &r) == EINVAL, "no height");
	check(powervr_dc_mode_from_line("display.mode=1280x720@", &w, &h, &r) == EINVAL, "empty rate");
	check(powervr_dc_mode_from_line("display.mode=1280x720@60hz", &w, &h, &r) == EINVAL, "trailing text");
	check(powervr_dc_mode_from_line("display.mode=99999x720", &w, &h, &r) == EINVAL, "number over limit");
	check(powervr_dc_mode_from_line("display.mode=", &w, &h, &r) == EINVAL, "empty value");
	check(powervr_dc_mode_from_line("a\tdisplay.mode=800x600@60", &w, &h, &r) == 0 && w == 800, "after a tab");
}

static void
test_modes(void)
{
	const struct powervr_dc_mode *mode;
	struct powervr_dc_pll pll;
	struct powervr_dc_panel_timing timing;
	static const unsigned sizes[5][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1920, 1080 } };
	unsigned index;
	uint64_t rate;

	mode = powervr_dc_mode_default();
	check(mode->width == 1920 && mode->height == 1080 && mode->refresh == 60 && mode->clock_khz == 148500, "default is 1080p60");
	check(powervr_dc_mode_find(1280, 720, 50, &mode) == ENOENT && mode == NULL, "720p50 unknown");
	check(powervr_dc_mode_find(3840, 2160, 60, &mode) == ENOENT, "4k unknown");

	for (index = 0; index < 5; index++) {
		check(powervr_dc_mode_find(sizes[index][0], sizes[index][1], 60, &mode) == 0, "known mode");
		/* Every mode has a PLL row of the same clock. */
		check(powervr_dc_pll_find(mode->clock_khz, &pll) == 0 && pll.clock_khz == mode->clock_khz, "PLL row");
		/* The timing is ordered and the refresh rate follows from the clock. */
		check(mode->width < mode->hsync_start && mode->hsync_start < mode->hsync_end && mode->hsync_end <= mode->htotal, "line order");
		check(mode->height < mode->vsync_start && mode->vsync_start < mode->vsync_end && mode->vsync_end <= mode->vtotal, "frame order");
		rate = (uint64_t)mode->clock_khz * 1000U * 100U / ((uint64_t)mode->htotal * mode->vtotal);
		check(rate >= 5950 && rate <= 6050, "refresh near 60 Hz");
		/* The words fit their 15-bit fields. */
		powervr_dc_panel_timing(mode, &timing);
		check((timing.horizontal & 0x7fffU) == mode->width && (timing.horizontal >> 16) == mode->htotal, "horizontal word");
		check((timing.vertical & 0x7fffU) == mode->height && (timing.vertical >> 16) == mode->vtotal, "vertical word");
		check((timing.horizontal_sync & 0x7fffU) == mode->hsync_start && ((timing.horizontal_sync >> 15) & 0x7fffU) == mode->hsync_end, "hsync fields");
		check((timing.horizontal_sync & (1U << 30)) != 0, "hsync enabled");
		check(((timing.horizontal_sync >> 31) != 0) == (mode->hsync_positive == 0), "hsync polarity");
		check(((timing.vertical_sync >> 31) != 0) == (mode->vsync_positive == 0), "vsync polarity");
	}

	check(powervr_dc_pll_find(297000, &pll) == ENOENT, "297 MHz not in the table");
}

static void
test_writes(void)
{
	const struct powervr_dc_mode *mode;
	struct powervr_dc_pll pll;
	struct powervr_dc_hdmi_write writes[POWERVR_DC_HDMI_WRITES_MAX];
	unsigned count;
	unsigned value;
	unsigned at;
	unsigned up;

	/* 1080p60: no fraction, post divider on. */
	(void)powervr_dc_mode_find(1920, 1080, 60, &mode);
	(void)powervr_dc_pll_find(mode->clock_khz, &pll);
	count = powervr_dc_hdmi_pll_writes(&pll, writes);
	check(count == 13, "1080p PLL write count");
	check(writes[0].reg == 0x1a0 && writes[0].value == 1, "pre-PLL down first");
	check(writes[count - 1].reg == 0x1a0 && writes[count - 1].value == 0, "pre-PLL up last");
	check(find_write(writes, count, 0x1a2, &value, &at) && value == 0xf0, "feedback high integer");
	check(find_write(writes, count, 0x1a3, &value, &at) && value == 99, "feedback low");
	check(find_write(writes, count, 0x1a4, &value, &at) && value == ((1 << 4) | (1 << 2) | 1), "tmds dividers");
	check(find_write(writes, count, 0x1a5, &value, &at) && value == ((2 << 5) | 1), "pixel a b");
	check(find_write(writes, count, 0x1a6, &value, &at) && value == ((2 << 5) | 2), "pixel c d");
	check(find_write(writes, count, 0x1ad, &value, &at) && value == 1, "post output divider");
	check(find_write(writes, count, 0x1d3, &value, &at) == 0, "no fraction written");
	/* The post-PLL's power: down second, up later. */
	check(writes[1].reg == 0x1aa && writes[1].value == 0x0f, "post-PLL down");
	up = 0;
	for (at = 2; at < count; at++)
		if (writes[at].reg == 0x1aa)
			up = writes[at].value;
	check(up == 0x0e, "post-PLL up divided");

	/* 640x480: fraction, feedback 100. */
	(void)powervr_dc_mode_find(640, 480, 60, &mode);
	(void)powervr_dc_pll_find(mode->clock_khz, &pll);
	count = powervr_dc_hdmi_pll_writes(&pll, writes);
	check(count == 16, "480p PLL write count");
	check(find_write(writes, count, 0x1a2, &value, &at) && value == 0xc0, "feedback high fractional");
	check(find_write(writes, count, 0x1d3, &value, &at) && value == 0x55, "fraction low");
	check(find_write(writes, count, 0x1d1, &value, &at) && value == 0xf5, "fraction high");
	check(find_write(writes, count, 0x1ad, &value, &at) && value == 13, "post output 13");

	/* 720p timing on the transmitter. */
	(void)powervr_dc_mode_find(1280, 720, 60, &mode);
	count = powervr_dc_hdmi_timing_writes(mode, writes);
	check(count == 14, "timing write count");
	check(find_write(writes, count, 0x09, &value, &at) && value == (1650 & 0xff), "htotal low");
	check(find_write(writes, count, 0x0a, &value, &at) && value == (1650 >> 8), "htotal high");
	check(find_write(writes, count, 0x0b, &value, &at) && value == (370 & 0xff), "hblank low");
	check(find_write(writes, count, 0x0d, &value, &at) && value == (260 & 0xff), "hdelay");
	check(find_write(writes, count, 0x0f, &value, &at) && value == 40, "hsync width");
	check(find_write(writes, count, 0x13, &value, &at) && value == 30, "vblank");
	check(find_write(writes, count, 0x14, &value, &at) && value == 25, "vdelay");
	check(find_write(writes, count, 0x15, &value, &at) && value == 5, "vsync width");
	check(writes[count - 1].reg == 0x08 && writes[count - 1].value == 0x0d, "timing control last, both high");

	(void)powervr_dc_mode_find(1024, 768, 60, &mode);
	count = powervr_dc_hdmi_timing_writes(mode, writes);
	check(writes[count - 1].value == 0x01, "XGA both low");

	/* Drive settings. */
	(void)powervr_dc_mode_find(1920, 1080, 60, &mode);
	count = powervr_dc_hdmi_drive_writes(mode, writes);
	check(count == 2 && writes[0].value == 0x02 && writes[1].value == 0x22, "1080p drive");
	(void)powervr_dc_mode_find(800, 600, 60, &mode);
	check(powervr_dc_hdmi_drive_writes(mode, writes) == 0, "SVGA no drive");
}

int
main(void)
{
	test_line();
	test_modes();
	test_writes();
	printf("%s mode-host-test (%d passes, %d failures)\n", failures == 0 ? "PASS" : "FAIL", passes, failures);
	return failures == 0 ? 0 : 1;
}
