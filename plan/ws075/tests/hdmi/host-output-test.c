/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws075-p012: host test of the resident display's output choice.
 *
 * Links src/kern/boot.c (the display= and display.mode= parameters) and
 * src/drivers/gpu/i915/display/output.c (the EDID timings, the CVT formula
 * and the HDMI mode choice), and checks them against the EDID of the
 * ws075 H1 LCD (plan/ws075/phase011/lcd-edid.hex), VESA DMT
 * reduced-blanking modes and hand-computed values.  Built and run by
 * host-output-test.sh.
 */

#include <kern/boot.h>

#include "drivers/gpu/i915/display/output.h"

#include <uapi/errno.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The checks run and the checks failed; the tally is printed at the end. */
static unsigned checks;
static unsigned failures;

/* The EDID of the H1 LCD, read from the hex dump. */
static uint8_t lcd_edid[256];
static unsigned lcd_edid_size;

static void check(int ok, const char *what);
static void load_edid(const char *path);
static void check_mode(const struct i915_lcd_mode *m, unsigned w, unsigned h, int clock, unsigned htotal, unsigned vtotal, int hpos, int vpos, const char *what);
static void test_parameters(void);
static void test_mode_parse(void);
static void test_edid(void);
static void test_cvt(void);
static void test_pick(void);

int
main(
	int argc,
	char **argv)
{
	/* The LCD's EDID is the one argument. */
	if (argc != 2) {
		fprintf(stderr, "usage: %s lcd-edid.hex\n", argv[0]);
		return 2;
	}

	/* Reads the LCD's EDID. */
	load_edid(argv[1]);

	/* Each part of the choice. */
	test_parameters();
	test_mode_parse();
	test_edid();
	test_cvt();
	test_pick();

	/* The tally. */
	printf("host-output-test: %u checks, %u failures\n", checks, failures);
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts a check and reports a failed one. */
static void
check(
	int ok,
	const char *what)
{
	/* Every check counts; a failed one is named. */
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Reads the EDID hex dump: lines of "OFF: b0 b1 ... b15". */
static void
load_edid(
	const char *path)
{
	char line[256];
	char *cursor;
	char *end;
	char *read;
	unsigned long byte;
	FILE *file;

	/* Opens the dump. */
	file = fopen(path, "r");
	if (file == NULL) {
		perror(path);
		exit(2);
	}

	/* Every byte after each line's colon. */
	for (;;) {
		/* The next line, until the end of the file. */
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;

		/* A line without an offset carries no bytes. */
		cursor = strchr(line, ':');
		if (cursor == NULL)
			continue;

		/* Each hexadecimal byte after the colon. */
		cursor++;
		for (;;) {
			byte = strtoul(cursor, &end, 16);
			if (end == cursor || lcd_edid_size >= sizeof(lcd_edid))
				break;

			/* Keeps the byte. */
			lcd_edid[lcd_edid_size] = (uint8_t)byte;
			lcd_edid_size++;
			cursor = end;
		}
	}

	/* Closes the dump. */
	fclose(file);
}

/* Checks a timing's size, clock, totals and sync polarities. */
static void
check_mode(
	const struct i915_lcd_mode *m,
	unsigned w,
	unsigned h,
	int clock,
	unsigned htotal,
	unsigned vtotal,
	int hpos,
	int vpos,
	const char *what)
{
	char text[256];

	/* One check per property, each named. */
	snprintf(text, sizeof(text), "%s: size %ux%u (want %ux%u)", what, m->hdisplay, m->vdisplay, w, h);
	check(m->hdisplay == w && m->vdisplay == h, text);
	snprintf(text, sizeof(text), "%s: clock %d kHz (want %d)", what, m->clock_khz, clock);
	check(m->clock_khz == clock, text);
	snprintf(text, sizeof(text), "%s: totals %ux%u (want %ux%u)", what, m->htotal, m->vtotal, htotal, vtotal);
	check(m->htotal == htotal && m->vtotal == vtotal, text);
	snprintf(text, sizeof(text), "%s: syncs %d/%d (want %d/%d)", what, m->hsync_positive, m->vsync_positive, hpos, vpos);
	check(m->hsync_positive == hpos && m->vsync_positive == vpos, text);
}

/* The boot parser takes display= and display.mode= and refuses what they do not take. */
static void
test_parameters(void)
{
	struct kern_boot_parameters p;
	const char *value;
	char line[128];
	int error;

	/* display=hdmi and a mode are kept. */
	strcpy(line, "rootpart=PARTLABEL=zedBSD-root display=hdmi display.mode=1920x1080@60");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	check(error == 0, "parse display=hdmi display.mode=1920x1080@60");
	value = kern_boot_parameters_value(&p, KERN_BOOT_PARAMETER_DISPLAY);
	check(value != NULL && strcmp(value, "hdmi") == 0, "display is hdmi");
	value = kern_boot_parameters_value(&p, KERN_BOOT_PARAMETER_DISPLAY_MODE);
	check(value != NULL && strcmp(value, "1920x1080@60") == 0, "display.mode is 1920x1080@60");

	/* display=auto is taken; no display= at all is absent. */
	strcpy(line, "display=auto");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	check(error == 0, "parse display=auto");
	strcpy(line, "kmsg=quiet");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	value = kern_boot_parameters_value(&p, KERN_BOOT_PARAMETER_DISPLAY);
	check(error == 0 && value == NULL, "no display= is absent");

	/* Another word, a bad mode and a repeat are refused. */
	strcpy(line, "display=vga");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	check(error == EINVAL, "display=vga is refused");
	strcpy(line, "display.mode=1920*1080");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	check(error == EINVAL, "display.mode=1920*1080 is refused");
	strcpy(line, "display=hdmi display=auto");
	error = kern_boot_parameters_parse(&p, line, sizeof(line));
	check(error == EEXIST, "a repeated display= is refused");
}

/* The WxH[@R] reader. */
static void
test_mode_parse(void)
{
	uint32_t w;
	uint32_t h;
	uint32_t r;
	int error;

	/* Good forms. */
	error = kern_boot_display_mode_parse("1920x1280", 9, &w, &h, &r);
	check(error == 0 && w == 1920U && h == 1280U && r == 0U, "1920x1280");
	error = kern_boot_display_mode_parse("1280x720@60", 11, &w, &h, &r);
	check(error == 0 && w == 1280U && h == 720U && r == 60U, "1280x720@60");
	error = kern_boot_display_mode_parse("16384x16384@1000", 16, &w, &h, &r);
	check(error == 0 && w == 16384U && h == 16384U && r == 1000U, "the largest mode");

	/* Bad forms. */
	error = kern_boot_display_mode_parse("1920x", 5, &w, &h, &r);
	check(error == EINVAL, "1920x");
	error = kern_boot_display_mode_parse("x1080", 5, &w, &h, &r);
	check(error == EINVAL, "x1080");
	error = kern_boot_display_mode_parse("0x1080", 6, &w, &h, &r);
	check(error == EINVAL, "0x1080");
	error = kern_boot_display_mode_parse("1920x1080@", 10, &w, &h, &r);
	check(error == EINVAL, "1920x1080@");
	error = kern_boot_display_mode_parse("1920x1080@60Hz", 14, &w, &h, &r);
	check(error == EINVAL, "1920x1080@60Hz");
	error = kern_boot_display_mode_parse("16385x100", 9, &w, &h, &r);
	check(error == EINVAL, "16385x100");
	error = kern_boot_display_mode_parse("123456x100", 10, &w, &h, &r);
	check(error == EINVAL, "123456x100");
	error = kern_boot_display_mode_parse("1920 x1080", 10, &w, &h, &r);
	check(error == EINVAL, "1920 x1080");
}

/* The detailed timings of the LCD's EDID. */
static void
test_edid(void)
{
	struct i915_lcd_mode m;
	int error;

	/* The EDID was read whole. */
	check(lcd_edid_size == 256U, "the LCD EDID has 256 bytes");

	/* The base block's first descriptor: 1920x1280, 164.36 MHz, +h -v, 259x173 mm. */
	error = drv_i915_output_edid_timing(lcd_edid + 54, &m);
	check(error == 0, "the first descriptor is a timing");
	check_mode(&m, 1920U, 1280U, 164360, 2080U, 1317U, 1, 0, "EDID DTD1");
	check(m.hsync_start == 1968U && m.hsync_end == 2000U, "EDID DTD1 hsync 1968/2000");
	check(m.vsync_start == 1283U && m.vsync_end == 1293U, "EDID DTD1 vsync 1283/1293");
	check(m.width_mm == 259U && m.height_mm == 173U, "EDID DTD1 259x173 mm");

	/* The other three base descriptors are display descriptors. */
	error = drv_i915_output_edid_timing(lcd_edid + 72, &m);
	check(error == EINVAL, "the serial descriptor is no timing");

	/* The CEA extension's descriptor (offset 26): 1920x1080, 148.5 MHz, +h +v. */
	error = drv_i915_output_edid_timing(lcd_edid + 128 + 26, &m);
	check(error == 0, "the CEA descriptor is a timing");
	check_mode(&m, 1920U, 1080U, 148500, 2200U, 1125U, 1, 1, "CEA DTD");
}

/* The CVT reduced-blanking formula against the LCD's own timing and VESA's published modes. */
static void
test_cvt(void)
{
	struct i915_lcd_mode m;
	int error;

	/* 1920x1280@60 (3:2, sync 10): the LCD's timing, at the 0.25 MHz step. */
	error = drv_i915_output_cvt_rb(1920U, 1280U, 60U, &m);
	check(error == 0, "CVT-RB 1920x1280@60");
	check_mode(&m, 1920U, 1280U, 164250, 2080U, 1317U, 1, 0, "CVT-RB 1920x1280@60");
	check(m.vsync_end - m.vsync_start == 10U, "CVT-RB 1920x1280 vsync 10");

	/* 1920x1200@60 reduced blanking (VESA DMT 0x44): 154 MHz, 2080x1235. */
	error = drv_i915_output_cvt_rb(1920U, 1200U, 60U, &m);
	check(error == 0, "CVT-RB 1920x1200@60");
	check_mode(&m, 1920U, 1200U, 154000, 2080U, 1235U, 1, 0, "CVT-RB 1920x1200@60");

	/* 1280x800@60 reduced blanking (VESA DMT 0x1B): 71 MHz, 1440x823. */
	error = drv_i915_output_cvt_rb(1280U, 800U, 60U, &m);
	check(error == 0, "CVT-RB 1280x800@60");
	check_mode(&m, 1280U, 800U, 71000, 1440U, 823U, 1, 0, "CVT-RB 1280x800@60");

	/* No rate is refused. */
	error = drv_i915_output_cvt_rb(1920U, 1080U, 0U, &m);
	check(error == EINVAL, "CVT-RB without a rate");
}

/* The mode choice. */
static void
test_pick(void)
{
	struct i915_lcd_mode m;
	const char *source;
	int error;

	/* No display.mode: the EDID's preferred timing. */
	error = drv_i915_output_pick_mode(0U, 0U, 0U, lcd_edid, lcd_edid_size, &m, &source);
	check(error == 0 && strcmp(source, "EDID") == 0, "no display.mode takes the EDID");
	check_mode(&m, 1920U, 1280U, 164360, 2080U, 1317U, 1, 0, "pick EDID");

	/* No display.mode and no EDID: CEA 4, no size. */
	error = drv_i915_output_pick_mode(0U, 0U, 0U, NULL, 0U, &m, &source);
	check(error == 0 && strncmp(source, "CEA 4", 5) == 0, "no EDID takes CEA 4");
	check_mode(&m, 1280U, 720U, 74250, 1650U, 750U, 1, 1, "pick CEA 4");
	check(m.width_mm == 0U, "CEA 4 without an EDID has no size");

	/* display.mode=1920x1080: the EDID's own 1080p timing (from its CEA extension). */
	error = drv_i915_output_pick_mode(1920U, 1080U, 0U, lcd_edid, lcd_edid_size, &m, &source);
	check(error == 0 && strcmp(source, "display.mode, EDID timing") == 0, "1920x1080 from the EDID");
	check_mode(&m, 1920U, 1080U, 148500, 2200U, 1125U, 1, 1, "pick 1920x1080");

	/* display.mode=1920x1280@60 matches DTD1 within a hertz. */
	error = drv_i915_output_pick_mode(1920U, 1280U, 60U, lcd_edid, lcd_edid_size, &m, &source);
	check(error == 0 && m.clock_khz == 164360, "1920x1280@60 is DTD1");

	/* display.mode=1280x720 without an EDID: the CEA table. */
	error = drv_i915_output_pick_mode(1280U, 720U, 60U, NULL, 0U, &m, &source);
	check(error == 0 && strcmp(source, "display.mode, CEA mode") == 0, "1280x720@60 from the CEA table");
	check_mode(&m, 1280U, 720U, 74250, 1650U, 750U, 1, 1, "pick CEA 1280x720");

	/* display.mode=1920x1200 with the LCD's EDID: not in it, CVT-RB, and the EDID's size. */
	error = drv_i915_output_pick_mode(1920U, 1200U, 0U, lcd_edid, lcd_edid_size, &m, &source);
	check(error == 0 && strcmp(source, "display.mode, CVT reduced blanking") == 0, "1920x1200 by CVT-RB");
	check_mode(&m, 1920U, 1200U, 154000, 2080U, 1235U, 1, 0, "pick CVT 1920x1200");
	check(m.width_mm == 260U && m.height_mm == 170U, "the CVT mode takes the EDID's 26x17 cm");

	/* display.mode=1920x1080@50 is not in the EDID's detailed timings: CVT-RB at 50 Hz. */
	error = drv_i915_output_pick_mode(1920U, 1080U, 50U, lcd_edid, lcd_edid_size, &m, &source);
	check(error == 0 && strcmp(source, "display.mode, CVT reduced blanking") == 0, "1920x1080@50 by CVT-RB");
}
