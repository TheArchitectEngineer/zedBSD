/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The choice of the resident node's output (ws075-p012).
 *
 * The resident node has one display.  An external display found at the
 * device start comes first: by default (no display=, display=auto or
 * display=hdmi) it is the HDMI display of DDI B when a sink is connected,
 * so the machine shows one full screen on the external monitor while the
 * panel stays dark, and the eDP panel otherwise (2026-09-29 user decision).
 * display=edp (or display=panel) keeps the panel even with a sink.  The
 * HDMI output runs on pipe B in DVI mode (no infoframes, no audio): the
 * combination the HDMI-B test scenario proved on this machine.
 *
 * The HDMI mode is the one display.mode=WxH[@R] names -- looked up in the
 * sink's EDID, then in the CEA modes every HDMI sink takes, then computed
 * with the CVT reduced-blanking formula -- or, without it, the preferred
 * detailed timing of the EDID, or CEA format 4 (1280x720) when no EDID was
 * read.  The choice is made once and does not follow a later hotplug.
 */

#include "output.h"
#include "hdmi.h"
#include "hotplug.h"
#include "state.h"

#include <kern/boot.h>
#include <kern/clock.h>
#include <kern/kcrt.h>
#include <kern/klog.h>

#include <uapi/errno.h>

/* The connector status of a connected sink (enum connector_status). */
#define I915_OUTPUT_CONNECTED		1

/* The connector status of a sink that is not there (enum connector_status). */
#define I915_OUTPUT_DISCONNECTED	2

/* The reference clock the WRPLL is computed from when the CDCLK state has none (kHz, non-SSC). */
#define I915_OUTPUT_REF_KHZ		38400

/*
 * With display=hdmi the sink is asked again for this long before the panel
 * takes its place: on bare metal the probe runs a few seconds after power-on,
 * before a USB-powered LCD's controller answers the EDID read (ws084).
 */
#define I915_OUTPUT_HDMI_WAIT_MS	6000U
#define I915_OUTPUT_HDMI_RETRY_MS	250U

/* The highest TMDS clock this path drives: HDMI 1.4 without scrambling (kHz). */
#define I915_OUTPUT_MAX_CLOCK_KHZ	340000

/* The EDID block size, the first detailed descriptor and the descriptor size. */
#define I915_OUTPUT_EDID_BLOCK		128U
#define I915_OUTPUT_EDID_DTD_FIRST	54U
#define I915_OUTPUT_EDID_DTD_SIZE	18U
#define I915_OUTPUT_EDID_DTD_COUNT	4U

/* The EDID base block's screen size in centimetres, and the CEA-861 extension's tag. */
#define I915_OUTPUT_EDID_WIDTH_CM	21U
#define I915_OUTPUT_EDID_HEIGHT_CM	22U
#define I915_OUTPUT_EDID_CEA_TAG	0x02U

/* The detailed timing flags: interlaced, digital separate sync, and its two polarities. */
#define I915_OUTPUT_DTD_INTERLACED	0x80U
#define I915_OUTPUT_DTD_SYNC_MASK	0x18U
#define I915_OUTPUT_DTD_DIGITAL_SEP	0x18U
#define I915_OUTPUT_DTD_VSYNC_POS	0x04U
#define I915_OUTPUT_DTD_HSYNC_POS	0x02U

/* A refresh rate given with display.mode= matches a timing within this many hertz. */
#define I915_OUTPUT_REFRESH_SLACK_HZ	1U

/* The refresh rate display.mode= means without @R. */
#define I915_OUTPUT_DEFAULT_REFRESH_HZ	60U

/* CVT reduced blanking, version 1: the horizontal blank, sync and front porch (pixels). */
#define I915_OUTPUT_CVT_RB_H_BLANK	160U
#define I915_OUTPUT_CVT_RB_H_SYNC	32U
#define I915_OUTPUT_CVT_RB_H_FRONT	48U

/* CVT reduced blanking: the least vertical blank time (us), the front porch and the least back porch (lines). */
#define I915_OUTPUT_CVT_RB_MIN_VBLANK_US	460U
#define I915_OUTPUT_CVT_RB_V_FRONT	3U
#define I915_OUTPUT_CVT_RB_MIN_V_BACK	6U

/* CVT: the pixel clock step (kHz). */
#define I915_OUTPUT_CVT_CLOCK_STEP_KHZ	250U

/*
 * The CEA-861 modes a display.mode= without a match in the EDID takes
 * before the CVT formula: every HDMI sink accepts formats 1, 4 and 16.
 * The table is constant.
 */
static const struct i915_lcd_mode i915_output_cea_modes[] = {
	/* Format 1: 640x480 at 60 Hz, 25.175 MHz, negative syncs. */
	{
		.clock_khz = 25175,
		.hdisplay = 640,
		.hsync_start = 656,
		.hsync_end = 752,
		.htotal = 800,
		.vdisplay = 480,
		.vsync_start = 490,
		.vsync_end = 492,
		.vtotal = 525,
		.hsync_positive = 0,
		.vsync_positive = 0,
		.edid_bpc = 8,
	},

	/* Format 4: 1280x720 at 60 Hz, 74.25 MHz, positive syncs. */
	{
		.clock_khz = 74250,
		.hdisplay = 1280,
		.hsync_start = 1390,
		.hsync_end = 1430,
		.htotal = 1650,
		.vdisplay = 720,
		.vsync_start = 725,
		.vsync_end = 730,
		.vtotal = 750,
		.hsync_positive = 1,
		.vsync_positive = 1,
		.edid_bpc = 8,
	},

	/* Format 16: 1920x1080 at 60 Hz, 148.5 MHz, positive syncs. */
	{
		.clock_khz = 148500,
		.hdisplay = 1920,
		.hsync_start = 2008,
		.hsync_end = 2052,
		.htotal = 2200,
		.vdisplay = 1080,
		.vsync_start = 1084,
		.vsync_end = 1089,
		.vtotal = 1125,
		.hsync_positive = 1,
		.vsync_positive = 1,
		.edid_bpc = 8,
	},
};

/* The index of CEA format 4 in the table: the mode of a sink without an EDID. */
#define I915_OUTPUT_CEA4_INDEX		1U

/* The number of modes in the CEA table. */
#define I915_OUTPUT_CEA_COUNT		(sizeof(i915_output_cea_modes) / sizeof(i915_output_cea_modes[0]))

static int i915_output_hdmi(struct i915_display *display, const char **reason);
static int i915_output_wanted_mode(uint32_t *width, uint32_t *height, uint32_t *refresh_hz);
static uint32_t i915_output_refresh_hz(const struct i915_lcd_mode *mode);
static int i915_output_mode_matches(const struct i915_lcd_mode *mode, uint32_t width, uint32_t height, uint32_t refresh_hz);
static int i915_output_edid_find(const uint8_t *edid, unsigned edid_size, uint32_t width, uint32_t height, uint32_t refresh_hz, struct i915_lcd_mode *mode);
static int i915_output_edid_block_find(const uint8_t *block, unsigned first, unsigned last, uint32_t width, uint32_t height, uint32_t refresh_hz, struct i915_lcd_mode *mode);
static void i915_output_edid_size(const uint8_t *edid, unsigned edid_size, struct i915_lcd_mode *mode);
static uint32_t i915_output_cvt_vsync(uint32_t width, uint32_t height);

/*
 * Chooses the output of the resident node from display= and the HDMI sink
 * connected now.
 *
 * Without display=edp or display=panel, a connected sink whose mode the
 * WRPLL can serve is the output; anything else leaves the panel as the
 * output, and the reason is logged.
 */
void
drv_i915_display_output_select(
	struct i915_display *display)
{
	const struct kern_boot_parameters *parameters;
	const char *reason;
	const char *wanted;
	uint32_t refresh;
	unsigned waited_ms;
	int compared;
	int error;

	/* The panel until HDMI is chosen and proven. */
	kern_memset(&display->output, 0, sizeof(display->output));

	/* display=edp and display=panel keep the panel; without display=, auto and hdmi look for the external display first. */
	parameters = kern_boot_parameters_current();
	wanted = kern_boot_parameters_value(parameters, KERN_BOOT_PARAMETER_DISPLAY);
	if (wanted == NULL)
		wanted = "auto";
	compared = kern_strcmp(wanted, "edp");
	if (compared == 0) {
		kern_logf("i915: display output: eDP panel (display=%s)\n", wanted);
		return;
	}
	compared = kern_strcmp(wanted, "panel");
	if (compared == 0) {
		kern_logf("i915: display output: eDP panel (display=%s)\n", wanted);
		return;
	}

	/* HDMI when a sink is connected and its mode can be driven; the panel otherwise. */
	reason = NULL;
	error = i915_output_hdmi(display, &reason);

	/* display=hdmi waits a while for a sink that is not answering yet. */
	compared = kern_strcmp(wanted, "hdmi");
	waited_ms = 0U;
	while (error == EAGAIN && compared == 0 && waited_ms < I915_OUTPUT_HDMI_WAIT_MS) {
		kern_usleep_range(I915_OUTPUT_HDMI_RETRY_MS * 1000U, I915_OUTPUT_HDMI_RETRY_MS * 1000U);
		waited_ms += I915_OUTPUT_HDMI_RETRY_MS;
		kern_memset(&display->output, 0, sizeof(display->output));
		error = i915_output_hdmi(display, &reason);
	}
	if (waited_ms != 0U)
		kern_logf("i915: display output: waited %u ms for the HDMI sink (rc=%d)\n", waited_ms, error);

	if (error != 0) {
		kern_memset(&display->output, 0, sizeof(display->output));
		kern_logf("i915: display output: eDP panel (display=%s, but %s: rc=%d)\n", wanted, reason, error);
		return;
	}

	/* The node shows the HDMI display from here on. */
	display->output.hdmi = 1;
	refresh = i915_output_refresh_hz(&display->output.state.mode);
	kern_logf("i915: display output: HDMI on DDI B, pipe B, DVI mode: %ux%u@%u Hz %d kHz (mode from %s) %ux%u mm; the eDP panel stays dark\n",
	    display->output.state.mode.hdisplay,
	    display->output.state.mode.vdisplay,
	    refresh,
	    display->output.state.mode.clock_khz,
	    display->output.mode_source,
	    display->output.state.mode.width_mm,
	    display->output.state.mode.height_mm);
}

/*
 * Reports the mode of the chosen output.
 *
 * The HDMI mode was fixed by the choice; the panel's mode is read from the
 * resident eDP's state.
 */
int
drv_i915_display_output_mode(
	struct i915_display *display,
	uint32_t *width,
	uint32_t *height,
	uint32_t *refresh_millihz)
{
	const struct i915_lcd_mode *m;
	uint64_t pixels;
	int error;

	/* The node has an output only once the resident dependencies exist. */
	if (display->rctx.lcd == NULL)
		return ENXIO;

	/* The panel: its own timing. */
	if (!display->output.hdmi) {
		error = drv_i915_display_panel_mode(display->rctx.lcd, width, height, refresh_millihz);
		if (error != 0)
			return ENXIO;

		/* Succeeded: the panel's mode is reported. */
		return 0;
	}

	/* The HDMI mode, and its refresh in millihertz. */
	m = &display->output.state.mode;
	pixels = (uint64_t)m->htotal * m->vtotal;
	*width = m->hdisplay;
	*height = m->vdisplay;
	*refresh_millihz = (uint32_t)(((uint64_t)m->clock_khz * 1000000ULL + pixels / 2U) / pixels);

	/* Succeeded: the HDMI mode is reported. */
	return 0;
}

/*
 * Reports the physical size of the chosen output in millimetres.
 */
int
drv_i915_display_output_size_mm(
	struct i915_display *display,
	uint32_t *width_mm,
	uint32_t *height_mm)
{
	int error;

	/* The panel: the size its EDID reported. */
	if (!display->output.hdmi) {
		error = drv_i915_display_panel_size_mm(display->rctx.lcd, width_mm, height_mm);
		if (error != 0)
			return ENODEV;

		/* Succeeded: the panel's size is reported. */
		return 0;
	}

	/* The HDMI display has a size only when its EDID reported one. */
	if (display->output.state.mode.width_mm == 0U || display->output.state.mode.height_mm == 0U)
		return ENODEV;

	/* The size the EDID reported. */
	*width_mm = display->output.state.mode.width_mm;
	*height_mm = display->output.state.mode.height_mm;

	/* Succeeded: the HDMI display's size is reported. */
	return 0;
}

/*
 * Names the chosen output for the display query.
 */
const char *
drv_i915_display_output_name(
	const struct i915_display *display)
{
	/* The HDMI display of DDI B. */
	if (display->output.hdmi)
		return "HDMI";

	/* The built-in panel. */
	return "eDP panel";
}

/*
 * Decodes one EDID detailed timing descriptor into a mode.
 *
 * The descriptor's layout is VESA E-EDID 1.4 section 3.10.2: the pixel
 * clock in 10 kHz units, then the active and blanking sizes, the sync
 * offsets and widths, the image size in millimetres and the flags.
 */
int
drv_i915_output_edid_timing(
	const uint8_t *d,
	struct i915_lcd_mode *mode)
{
	uint32_t clock;
	uint32_t hactive;
	uint32_t hblank;
	uint32_t vactive;
	uint32_t vblank;
	uint32_t hfront;
	uint32_t hsync;
	uint32_t vfront;
	uint32_t vsync;
	uint8_t flags;

	/* A descriptor with a zero clock is a display descriptor, not a timing. */
	clock = (uint32_t)d[0] | ((uint32_t)d[1] << 8);
	if (clock == 0U)
		return EINVAL;

	/* An interlaced timing is not driven here. */
	flags = d[17];
	if ((flags & I915_OUTPUT_DTD_INTERLACED) != 0U)
		return EINVAL;

	/* The horizontal active and blanking pixels (upper four bits of each in byte 4). */
	hactive = (uint32_t)d[2] | (((uint32_t)d[4] >> 4) << 8);
	hblank = (uint32_t)d[3] | (((uint32_t)d[4] & 0x0fU) << 8);

	/* The vertical active and blanking lines (upper four bits of each in byte 7). */
	vactive = (uint32_t)d[5] | (((uint32_t)d[7] >> 4) << 8);
	vblank = (uint32_t)d[6] | (((uint32_t)d[7] & 0x0fU) << 8);

	/* The sync offsets and widths, their upper bits in byte 11. */
	hfront = (uint32_t)d[8] | ((((uint32_t)d[11] >> 6) & 3U) << 8);
	hsync = (uint32_t)d[9] | ((((uint32_t)d[11] >> 4) & 3U) << 8);
	vfront = ((uint32_t)d[10] >> 4) | ((((uint32_t)d[11] >> 2) & 3U) << 4);
	vsync = ((uint32_t)d[10] & 0x0fU) | (((uint32_t)d[11] & 3U) << 4);

	/* Refuses an empty picture. */
	if (hactive == 0U || vactive == 0U)
		return EINVAL;

	/* The timing. */
	kern_memset(mode, 0, sizeof(*mode));
	mode->clock_khz = (int)(clock * 10U);
	mode->hdisplay = (uint16_t)hactive;
	mode->hsync_start = (uint16_t)(hactive + hfront);
	mode->hsync_end = (uint16_t)(hactive + hfront + hsync);
	mode->htotal = (uint16_t)(hactive + hblank);
	mode->vdisplay = (uint16_t)vactive;
	mode->vsync_start = (uint16_t)(vactive + vfront);
	mode->vsync_end = (uint16_t)(vactive + vfront + vsync);
	mode->vtotal = (uint16_t)(vactive + vblank);
	mode->edid_bpc = 8;

	/* The image size, in millimetres (upper four bits of each in byte 14). */
	mode->width_mm = (uint16_t)((uint32_t)d[12] | (((uint32_t)d[14] >> 4) << 8));
	mode->height_mm = (uint16_t)((uint32_t)d[13] | (((uint32_t)d[14] & 0x0fU) << 8));

	/* Digital separate sync carries both polarities; any other kind is taken as negative. */
	if ((flags & I915_OUTPUT_DTD_SYNC_MASK) == I915_OUTPUT_DTD_DIGITAL_SEP) {
		if ((flags & I915_OUTPUT_DTD_HSYNC_POS) != 0U)
			mode->hsync_positive = 1;
		if ((flags & I915_OUTPUT_DTD_VSYNC_POS) != 0U)
			mode->vsync_positive = 1;
	}

	/* Succeeded: the descriptor is a progressive timing. */
	return 0;
}

/*
 * Computes the CVT reduced-blanking (version 1) timing of a mode.
 *
 * VESA CVT 1.2 section 5.4: a fixed horizontal blank of 160 pixels, a
 * vertical blank of at least 460 us, a vertical sync width that names the
 * aspect ratio, and a pixel clock in steps of 0.25 MHz; syncs are positive
 * horizontally and negative vertically.
 */
int
drv_i915_output_cvt_rb(
	uint32_t width,
	uint32_t height,
	uint32_t refresh_hz,
	struct i915_lcd_mode *mode)
{
	uint64_t frame_ps;
	uint64_t line_ps;
	uint64_t clock_khz;
	uint32_t vsync;
	uint32_t vblank;
	uint32_t min_vblank;
	uint32_t htotal;
	uint32_t vtotal;

	/* Refuses a size or rate the formula cannot serve. */
	if (width == 0U || height == 0U)
		return EINVAL;
	if (refresh_hz == 0U)
		return EINVAL;

	/* The frame time less the least vertical blank, over the active lines: the estimated line time (ps). */
	frame_ps = 1000000000000ULL / refresh_hz;
	if (frame_ps <= (uint64_t)I915_OUTPUT_CVT_RB_MIN_VBLANK_US * 1000000ULL)
		return EINVAL;

	/* A frame too short for its lines has no line time. */
	line_ps = (frame_ps - (uint64_t)I915_OUTPUT_CVT_RB_MIN_VBLANK_US * 1000000ULL) / height;
	if (line_ps == 0U)
		return EINVAL;

	/* The vertical blank: the lines 460 us takes plus one, at least front porch, sync and back porch. */
	vsync = i915_output_cvt_vsync(width, height);
	vblank = (uint32_t)((uint64_t)I915_OUTPUT_CVT_RB_MIN_VBLANK_US * 1000000ULL / line_ps) + 1U;
	min_vblank = I915_OUTPUT_CVT_RB_V_FRONT + vsync + I915_OUTPUT_CVT_RB_MIN_V_BACK;
	if (vblank < min_vblank)
		vblank = min_vblank;

	/* The totals, and the pixel clock rounded down to its 0.25 MHz step. */
	htotal = width + I915_OUTPUT_CVT_RB_H_BLANK;
	vtotal = height + vblank;
	clock_khz = (uint64_t)refresh_hz * htotal * vtotal / 1000U;
	clock_khz = clock_khz / I915_OUTPUT_CVT_CLOCK_STEP_KHZ * I915_OUTPUT_CVT_CLOCK_STEP_KHZ;

	/* Refuses a timing the 16-bit fields or the clock cannot hold. */
	if (htotal > 0xffffU || vtotal > 0xffffU)
		return EINVAL;
	if (clock_khz == 0U || clock_khz > 0x7fffffffULL)
		return EINVAL;

	/* The timing. */
	kern_memset(mode, 0, sizeof(*mode));
	mode->clock_khz = (int)clock_khz;
	mode->hdisplay = (uint16_t)width;
	mode->hsync_start = (uint16_t)(width + I915_OUTPUT_CVT_RB_H_FRONT);
	mode->hsync_end = (uint16_t)(width + I915_OUTPUT_CVT_RB_H_FRONT + I915_OUTPUT_CVT_RB_H_SYNC);
	mode->htotal = (uint16_t)htotal;
	mode->vdisplay = (uint16_t)height;
	mode->vsync_start = (uint16_t)(height + I915_OUTPUT_CVT_RB_V_FRONT);
	mode->vsync_end = (uint16_t)(height + I915_OUTPUT_CVT_RB_V_FRONT + vsync);
	mode->vtotal = (uint16_t)vtotal;
	mode->hsync_positive = 1;
	mode->vsync_positive = 0;
	mode->edid_bpc = 8;

	/* Succeeded: the timing is computed. */
	return 0;
}

/*
 * Picks the HDMI mode from display.mode= and the sink's EDID.
 *
 * A wanted size is looked up in the EDID's detailed timings, then in the
 * CEA table, then computed with CVT reduced blanking (at 60 Hz without
 * @R).  Without a wanted size the EDID's first detailed timing is taken,
 * and CEA format 4 when there is none.  The physical size comes from the
 * EDID when the mode has none of its own.
 */
int
drv_i915_output_pick_mode(
	uint32_t want_width,
	uint32_t want_height,
	uint32_t want_refresh_hz,
	const uint8_t *edid,
	unsigned edid_size,
	struct i915_lcd_mode *mode,
	const char **source)
{
	uint32_t refresh;
	unsigned i;
	int matches;
	int error;

	/* No wanted size: the sink's preferred timing, or CEA format 4. */
	if (want_width == 0U) {
		error = EINVAL;
		if (edid != NULL && edid_size >= I915_OUTPUT_EDID_BLOCK)
			error = drv_i915_output_edid_timing(edid + I915_OUTPUT_EDID_DTD_FIRST, mode);
		if (error == 0) {
			*source = "EDID";
		} else {
			*mode = i915_output_cea_modes[I915_OUTPUT_CEA4_INDEX];
			*source = "CEA 4 (no EDID timing)";
		}

		/* Succeeded: the preferred mode, with the EDID's size when it has none of its own. */
		i915_output_edid_size(edid, edid_size, mode);
		return 0;
	}

	/* The wanted size among the sink's own timings. */
	error = i915_output_edid_find(edid, edid_size, want_width, want_height, want_refresh_hz, mode);
	if (error == 0) {
		/* Succeeded: the sink's own timing of that size. */
		*source = "display.mode, EDID timing";
		i915_output_edid_size(edid, edid_size, mode);
		return 0;
	}

	/* The wanted size among the CEA modes every HDMI sink takes. */
	for (i = 0U; i < I915_OUTPUT_CEA_COUNT; i++) {
		matches = i915_output_mode_matches(&i915_output_cea_modes[i], want_width, want_height, want_refresh_hz);
		if (matches) {
			/* Succeeded: the CEA mode of that size, with the EDID's size. */
			*mode = i915_output_cea_modes[i];
			*source = "display.mode, CEA mode";
			i915_output_edid_size(edid, edid_size, mode);
			return 0;
		}
	}

	/* The CVT reduced-blanking timing is at 60 Hz when no rate was given. */
	refresh = want_refresh_hz;
	if (refresh == 0U)
		refresh = I915_OUTPUT_DEFAULT_REFRESH_HZ;

	/* Computes the CVT reduced-blanking timing. */
	error = drv_i915_output_cvt_rb(want_width, want_height, refresh, mode);
	if (error != 0)
		return error;

	/* The EDID's size, when the sink stated one. */
	*source = "display.mode, CVT reduced blanking";
	i915_output_edid_size(edid, edid_size, mode);

	/* Succeeded: the computed timing is the mode. */
	return 0;
}

/*
 * Makes the HDMI display the output when its sink is connected and its
 * mode can be driven; reason says what refused it.
 */
static int
i915_output_hdmi(
	struct i915_display *display,
	const char **reason)
{
	struct i915_hpd_summary summary;
	struct i915_lcd_mode mode;
	const uint8_t *edid;
	unsigned edid_size;
	uint32_t want_width;
	uint32_t want_height;
	uint32_t want_refresh;
	int ref_khz;
	int status;
	int error;

	/* The hotplug path must run and know the HDMI connector. */
	if (!display->hpd_started) {
		*reason = "the hotplug path did not start";
		return ENODEV;
	}

	/* Reads which connector is the HDMI one. */
	drv_i915_hpd_summary(display, &summary);
	if (!summary.started || summary.hdmi_connector < 0) {
		*reason = "no HDMI connector";
		return ENODEV;
	}

	/* Detects the sink once, the way the connector's first probe does; it reads the EDID too. */
	status = drv_i915_hpd_probe_connector(display, (unsigned)summary.hdmi_connector);
#ifdef I915_TEST_HDMI_ABSENT
	/*
	 * The panel fallback test (ws075-p013): the probe ran, but its answer is
	 * taken as a disconnected sink, the state an unplugged cable leaves.
	 */
	kern_logf("i915: display output: I915_TEST_HDMI_ABSENT takes the HDMI sink as absent (probe said %d)\n", status);
	status = I915_OUTPUT_DISCONNECTED;
#endif
	if (status != I915_OUTPUT_CONNECTED) {
		*reason = "no HDMI sink is connected at boot";
		return EAGAIN;
	}

	/* The EDID the detection read, if it read one. */
	edid_size = 0U;
	edid = drv_i915_hpd_edid_bytes(display->hpd_world, (unsigned)summary.hdmi_connector, &edid_size);
	if (edid == NULL)
		edid_size = 0U;

	/* The mode display.mode= asks for, if any. */
	error = i915_output_wanted_mode(&want_width, &want_height, &want_refresh);
	if (error != 0) {
		*reason = "display.mode is unreadable";
		return error;
	}

	/* The mode to drive. */
	error = drv_i915_output_pick_mode(want_width, want_height, want_refresh, edid, edid_size, &mode, &display->output.mode_source);
	if (error != 0) {
		*reason = "no timing serves display.mode";
		return error;
	}

	/* Refuses a clock that needs scrambling, which this path does not program. */
	if (mode.clock_khz > I915_OUTPUT_MAX_CLOCK_KHZ) {
		*reason = "the mode's clock is above 340 MHz";
		return EINVAL;
	}

	/* The display's reference clock, or the platform's when the CDCLK state has none. */
	ref_khz = (int)display->cdclk.hw.ref;
	if (ref_khz <= 0)
		ref_khz = I915_OUTPUT_REF_KHZ;

	/* Computes the link and the WRPLL of the mode; the state is written whole. */
	error = drv_i915_lcd_compute_hdmi(&mode, ref_khz, &display->output.state);
	if (error != 0) {
		*reason = "the WRPLL refused the mode's clock";
		return error;
	}

	/* Succeeded: the HDMI display can be lit. */
	return 0;
}

/* Reads display.mode=: 0 with the wanted size and rate, all 0 when it is not given, or EINVAL. */
static int
i915_output_wanted_mode(
	uint32_t *width,
	uint32_t *height,
	uint32_t *refresh_hz)
{
	const struct kern_boot_parameters *parameters;
	const char *text;
	int error;

	/* Nothing is wanted without the parameter. */
	*width = 0U;
	*height = 0U;
	*refresh_hz = 0U;
	parameters = kern_boot_parameters_current();
	text = kern_boot_parameters_value(parameters, KERN_BOOT_PARAMETER_DISPLAY_MODE);
	if (text == NULL)
		return 0;

	/* The boot parser accepted the text already; it is read again here. */
	error = kern_boot_display_mode_parse(text, kern_strlen(text), width, height, refresh_hz);
	if (error != 0)
		return error;

	/* Succeeded: the wanted mode is read. */
	return 0;
}

/* Reports a mode's refresh rate, rounded to whole hertz. */
static uint32_t
i915_output_refresh_hz(
	const struct i915_lcd_mode *mode)
{
	uint64_t pixels;

	/* The pixels of one frame; an empty timing has no rate. */
	pixels = (uint64_t)mode->htotal * mode->vtotal;
	if (pixels == 0U)
		return 0U;

	/* The clock over the frame, rounded. */
	return (uint32_t)(((uint64_t)mode->clock_khz * 1000U + pixels / 2U) / pixels);
}

/* Reports whether a mode has the wanted size and, when one is wanted, its refresh rate. */
static int
i915_output_mode_matches(
	const struct i915_lcd_mode *mode,
	uint32_t width,
	uint32_t height,
	uint32_t refresh_hz)
{
	uint32_t refresh;

	/* The size must be the same. */
	if (mode->hdisplay != width || mode->vdisplay != height)
		return 0;

	/* Any rate matches when none is wanted. */
	if (refresh_hz == 0U)
		return 1;

	/* The rate must be within a hertz of the wanted one. */
	refresh = i915_output_refresh_hz(mode);
	if (refresh + I915_OUTPUT_REFRESH_SLACK_HZ < refresh_hz)
		return 0;
	if (refresh > refresh_hz + I915_OUTPUT_REFRESH_SLACK_HZ)
		return 0;

	/* Reports a match. */
	return 1;
}

/*
 * Finds a detailed timing of the wanted size in the EDID: the base block's
 * four descriptors, then those of each CEA-861 extension.
 */
static int
i915_output_edid_find(
	const uint8_t *edid,
	unsigned edid_size,
	uint32_t width,
	uint32_t height,
	uint32_t refresh_hz,
	struct i915_lcd_mode *mode)
{
	const uint8_t *block;
	unsigned offset;
	unsigned dtd_start;
	int error;

	/* No EDID has no timings. */
	if (edid == NULL || edid_size < I915_OUTPUT_EDID_BLOCK)
		return ENOENT;

	/* The base block's four descriptors. */
	error = i915_output_edid_block_find(edid, I915_OUTPUT_EDID_DTD_FIRST, I915_OUTPUT_EDID_DTD_FIRST + I915_OUTPUT_EDID_DTD_COUNT * I915_OUTPUT_EDID_DTD_SIZE, width, height, refresh_hz, mode);
	if (error == 0)
		return 0;

	/* The detailed timings of each CEA-861 extension, from the offset its byte 2 gives to the checksum. */
	for (offset = I915_OUTPUT_EDID_BLOCK; offset + I915_OUTPUT_EDID_BLOCK <= edid_size; offset += I915_OUTPUT_EDID_BLOCK) {
		/* Only a CEA-861 extension carries detailed timings here. */
		block = edid + offset;
		if (block[0] != I915_OUTPUT_EDID_CEA_TAG)
			continue;

		/* An extension whose offset points outside its block has none. */
		dtd_start = block[2];
		if (dtd_start < 4U || dtd_start >= I915_OUTPUT_EDID_BLOCK - 1U)
			continue;

		/* Its descriptors up to the checksum byte. */
		error = i915_output_edid_block_find(block, dtd_start, I915_OUTPUT_EDID_BLOCK - 1U, width, height, refresh_hz, mode);
		if (error == 0)
			return 0;
	}

	/* Reports that no timing of the EDID has the wanted size. */
	return ENOENT;
}

/* Finds a detailed timing of the wanted size among the descriptors of one EDID block from first up to last. */
static int
i915_output_edid_block_find(
	const uint8_t *block,
	unsigned first,
	unsigned last,
	uint32_t width,
	uint32_t height,
	uint32_t refresh_hz,
	struct i915_lcd_mode *mode)
{
	struct i915_lcd_mode candidate;
	unsigned position;
	int matches;
	int error;

	/* Searches each whole descriptor of the range. */
	for (position = first; position + I915_OUTPUT_EDID_DTD_SIZE <= last; position += I915_OUTPUT_EDID_DTD_SIZE) {
		/* Decodes the descriptor; one that is no progressive timing is passed over. */
		error = drv_i915_output_edid_timing(block + position, &candidate);
		if (error != 0)
			continue;

		/* Takes the first timing of the wanted size. */
		matches = i915_output_mode_matches(&candidate, width, height, refresh_hz);
		if (matches) {
			*mode = candidate;
			return 0;
		}
	}

	/* Reports that no descriptor of the block has the wanted size. */
	return ENOENT;
}

/* Takes the EDID's screen size (centimetres, bytes 21 and 22) for a mode that has no image size of its own. */
static void
i915_output_edid_size(
	const uint8_t *edid,
	unsigned edid_size,
	struct i915_lcd_mode *mode)
{
	/* A mode with its own image size keeps it. */
	if (mode->width_mm != 0U && mode->height_mm != 0U)
		return;

	/* No EDID has no size. */
	if (edid == NULL || edid_size < I915_OUTPUT_EDID_BLOCK)
		return;

	/* The screen size, 0 when the sink does not state one. */
	mode->width_mm = (uint16_t)(edid[I915_OUTPUT_EDID_WIDTH_CM] * 10U);
	mode->height_mm = (uint16_t)(edid[I915_OUTPUT_EDID_HEIGHT_CM] * 10U);
}

/* Names the CVT vertical sync width of an aspect ratio: 4:3, 16:9, 16:10, 5:4 and 15:9 have their own. */
static uint32_t
i915_output_cvt_vsync(
	uint32_t width,
	uint32_t height)
{
	/* 4:3. */
	if (width * 3U == height * 4U)
		return 4U;

	/* 16:9. */
	if (width * 9U == height * 16U)
		return 5U;

	/* 16:10. */
	if (width * 10U == height * 16U)
		return 6U;

	/* 5:4 and 15:9. */
	if (width * 4U == height * 5U)
		return 7U;
	if (width * 9U == height * 15U)
		return 7U;

	/* Any other aspect ratio. */
	return 10U;
}
