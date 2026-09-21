/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host integration test of the one-screen LCD modeset: the Linux
 * text's callers and callees (display/modeset.c and the modeset environment
 * it drives) and the resident eDP (dp-sink.c: PPS, AUX, DPCD) on the
 * register and sink models (lcd-fake-hw.c on dp-fake-hw.c), from "prepare"
 * to "everything given back".
 *
 * The families (the ownership boundaries of the first light-up):
 *   A. normal: prepare -> enable -> plane update -> frames advance -> plane off -> disable -> released
 *   B. an early failure (the PLL does not lock, the sink never reports clock recovery): no success is
 *      invented, the normal disable gives back what the enable took
 *   C. trouble after the plane was armed (the pipe does not stop): the buffer stays protected
 *   D. the commit's outer part (DC_OFF, the crtc's power domains, DBUF, MBUS, CDCLK, bandwidth) and
 *      the observation of the running pipe
 *   E. a time-base fault is not a timeout
 *   F. brightness and backlight while the picture stays up
 *   G. show / stop / show again on the same resident eDP
 *   H. the pipe's power cannot be released
 *   I. the synchronous flip; J. its event after a timeout, the stop, the relight, the evasion sleep
 * The pass criteria are read back from the models and the production objects, never from the trace.
 *
 *   sh plan/ws031/tests/run-lcd-modeset-host-test.sh
 */

#include "host-test.h"
#include "dp-fake-hw.h"
#include "lcd-fake-hw.h"

#include "../../display/internal.h"
#include "../../display/clock.h"
#include "../../display/diagnostics.h"
#include "../../display/dp-sink.h"
#include "../../display/modeset.h"
#include "../../display/panel-backlight.h"
#include "../../display/state.h"
#include "../../display/takeover.h"
#include "../../display/vblank.h"
#include "../../display/watermark.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The two scanout buffers the flips alternate between. */
#define I915_MS_SURF_A 0xfdfc0000U
#define I915_MS_SURF_B 0xfd000000U

/*
 * The captured panel: DPCD pages 0x000, 0x100 and 0x700, and the EDID.
 *
 * Read once by main() and never changed.
 */
static uint8_t i915_ms_d000[256];
static uint8_t i915_ms_d100[256];
static uint8_t i915_ms_d700[256];
static uint8_t i915_ms_edid[128];

/*
 * The display that owns the DP, modeset, watermark and takeover worlds.
 *
 * The worlds are created by main() for the whole run.
 */
static struct i915_display i915_ms_display;

/*
 * The resident eDP on its register model, and what its acquisition found.
 *
 * Started afresh by i915_ms_bring_up() for every case; ended by the case.
 */
static struct i915_dp_fake_hw i915_ms_dpf;
static struct i915_dp_env i915_ms_env;
static struct i915_edp_result i915_ms_res;

/*
 * The display engine model, the run log stacked on it, and the state
 * computed from the eDP.
 *
 * Started afresh by i915_ms_bring_up(); the run log is also restarted where
 * a case wants a fresh log for its second commit.
 */
static struct i915_lcd_fake_hw i915_ms_lcd;
static struct i915_lcd_trace i915_ms_trace;
static struct i915_lcd_state i915_ms_state;

/*
 * The observer of family D.
 *
 * Restarted by each observation case.
 */
static struct i915_lcd_observer i915_ms_obs;

/*
 * The names of the run log's entry kinds (enum i915_lcd_trace_kind), for the dump.
 */
static const char *const i915_ms_kind_name[] = {
	"?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put",
	"STEP", "ERROR", "PHASE", "decide", "dbuf", "observ"
};

static struct i915_edp_config i915_ms_vbt_config(void);
static struct i915_lcd_modeset_cfg i915_ms_config(void);
static void i915_ms_bring_up(void);
static int i915_ms_prepare_enable(const struct i915_lcd_modeset_cfg *config);
static void i915_ms_dump_trace(const char *title);
static int i915_ms_all_released(const char *what);
static int i915_ms_find(int kind, uint32_t a, const char *name, unsigned from);
static const char *i915_ms_first_error(const struct i915_lcd_modeset_status *s, const char *none);
static int i915_ms_error_names(const struct i915_lcd_modeset_status *s, const char *word);
static void i915_ms_test_normal(void);
static void i915_ms_test_early_failures(void);
static void i915_ms_test_refused(void);
static void i915_ms_test_ddb_model(void);
static void i915_ms_test_stuck_pipe(void);
static void i915_ms_test_outer(void);
static void i915_ms_test_outer_refusals(void);
static void i915_ms_test_observation(void);
static void i915_ms_test_backlight(void);
static void i915_ms_test_cycles(void);
static void i915_ms_test_power_kept(void);
static void i915_ms_test_flip(void);
static void i915_ms_test_flip_event(void);
static void i915_ms_test_time_base(void);
static void i915_ms_test_dither(void);

/*
 * Runs the one-screen modeset on the models, family by family.
 *
 * Usage: host-lcd-modeset-test <display-ref directory> [-v]
 */
int
main(
	int argc,
	char **argv)
{
	int error;
	int status;

	/* Needs the reference directory; a second argument makes the run verbose. */
	if (argc < 2) {
		fprintf(stderr, "usage: %s <display-ref dir> [-v]\n", argv[0]);
		return 2;
	}

	if (argc > 2)
		i915_host_verbose = 1;

	/* Reads the captured panel. */
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-000.bin", i915_ms_d000, sizeof(i915_ms_d000));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-100.bin", i915_ms_d100, sizeof(i915_ms_d100));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-700.bin", i915_ms_d700, sizeof(i915_ms_d700));
	i915_host_read_reference(argv[1], "edid-eDP-1.bin", i915_ms_edid, sizeof(i915_ms_edid));

	/* Creates the worlds the display owns: the resident eDP's, the modeset's, the watermarks' and the takeover's. */
	error = drv_i915_dp_world_create(&i915_ms_display);
	if (error != 0) {
		printf("dp world: error %d\n", error);
		return 2;
	}

	error = drv_i915_lcd_world_create(&i915_ms_display);
	if (error != 0) {
		printf("lcd world: error %d\n", error);
		return 2;
	}

	error = drv_i915_wm_world_create(&i915_ms_display);
	if (error != 0) {
		printf("wm world: error %d\n", error);
		return 2;
	}

	error = drv_i915_takeover_world_create(&i915_ms_display);
	if (error != 0) {
		printf("takeover world: error %d\n", error);
		return 2;
	}

	/* Runs the families in the order the state they leave requires. */
	i915_ms_test_normal();
	i915_ms_test_early_failures();
	i915_ms_test_refused();
	i915_ms_test_ddb_model();
	i915_ms_test_stuck_pipe();
	i915_ms_test_outer();
	i915_ms_test_outer_refusals();
	i915_ms_test_observation();
	i915_ms_test_backlight();
	i915_ms_test_cycles();
	i915_ms_test_power_kept();
	i915_ms_test_flip();
	i915_ms_test_flip_event();
	i915_ms_test_time_base();
	i915_ms_test_dither();

	/* Releases the worlds. */
	drv_i915_takeover_world_destroy(&i915_ms_display);
	drv_i915_wm_world_destroy(&i915_ms_display);
	drv_i915_lcd_world_destroy(&i915_ms_display);
	drv_i915_dp_world_destroy(&i915_ms_display);

	/* Reports the tally. */
	status = i915_host_report("lcd_modeset_host_test");
	if (status != 0)
		return status;

	/* Succeeded: every check passed. */
	return 0;
}

/* Returns the target's VBT panel power sequence (see host-dp-test.c), without the log. */
static struct i915_edp_config
i915_ms_vbt_config(void)
{
	struct i915_edp_config config;

	/* Port A, AUX A, the 19.2 MHz raw clock. */
	memset(&config, 0, sizeof(config));
	config.port = 0;
	config.aux_ch = 0;
	config.rawclk_khz = 19200U;

	/* The panel power sequence in 100 us units. */
	config.t1_t3 = 2000U;
	config.t8 = 800U;
	config.t9 = 2000U;
	config.t10 = 1100U;
	config.t11_t12 = 5000U;
	config.log_level = -1;

	/* Succeeded: the configuration. */
	return config;
}

/* Returns the modeset configuration of the target: its panel, its initialisation's state and the buffer. */
static struct i915_lcd_modeset_cfg
i915_ms_config(void)
{
	static const uint16_t latency[8] = { 3, 54, 83, 102, 147, 147, 144, 144 };
	struct i915_lcd_modeset_cfg config;

	/* The panel as the resident eDP read it, and an XRGB8888 framebuffer. */
	memset(&config, 0, sizeof(config));
	memcpy(config.dpcd, i915_ms_res.dpcd, 15U);
	memcpy(config.edp_dpcd, i915_ms_res.edp_dpcd, 3U);
	config.fb_fourcc = 0x34325258U;

	/*
	 * The watermark inputs as the target's normal initialisation logs them:
	 * 6 levels, latency 3/54/83/102/147/147/144/144, SAGV block time 35 us;
	 * XE_LPD: DBUF 4096 blocks in 4 slices, IPC; slice 1 enabled after the
	 * power-domain initialisation.
	 */
	memcpy(config.wm_latency, latency, sizeof(latency));
	config.wm_num_levels = 6U;
	config.wm_ipc_enabled = 1;
	config.sagv_block_time_us = 35U;
	config.dbuf_size = 4096U;
	config.dbuf_slice_mask = 0x0fU;
	config.dbuf_enabled_slices = 0x01U;

	/*
	 * The state the target's normal initialisation leaves (real-machine
	 * log): CDCLK 179200 kHz from VCO 537600 on a 38.4 MHz reference,
	 * voltage level 0; MBUS not joined; SAGV off with QGV point 2 allowed
	 * (derated bandwidth 11707 MB/s); the DMC firmware of pipe A.
	 */
	config.cdclk_khz = 179200U;
	config.cdclk_vco_khz = 537600U;
	config.cdclk_ref_khz = 38400U;
	config.cdclk_bypass_khz = 19200U;
	config.cdclk_max_khz = 652800U;
	config.cdclk_voltage_level = 0U;
	config.mbus_joined = 0;
	config.qgv_allowed_bw = 11707U;
	config.dmc_fw_mask = 1U << 1;

	/* The target's VBT backlight block (display-ref/vbt-decode.txt): PWM 200 Hz, min 6, controller 0; rawclk 19.2 MHz. */
	config.vbt_backlight_present = 1;
	config.vbt_backlight_pwm_freq_hz = 200U;
	config.vbt_backlight_min_brightness = 6U;
	config.rawclk_khz = 19200U;

	/* The 1920x1080 scanout buffer. */
	config.fb_width = 1920U;
	config.fb_height = 1080U;
	config.fb_pitch = 7680U;
	config.fb_surf = I915_MS_SURF_A;

	/* Succeeded: the configuration. */
	return config;
}

/* Brings the resident eDP up as the probe leaves it, computes the LCD state from what it read, and starts the models. */
static void
i915_ms_bring_up(void)
{
	struct i915_edp_config config;
	int error;

	/* A fresh device: its shared DPLLs are unused and its DBUF / MBUS state is read from the hardware again. */
	drv_i915_lcd_dplls_reset(i915_ms_display.lcd_world);
	drv_i915_lcd_dbuf_forget(i915_ms_display.wm_world);

	/* The resident eDP: acquisition and late init on a fresh AUX / PPS model. */
	config = i915_ms_vbt_config();
	drv_i915_dp_fake_init(&i915_ms_dpf, i915_ms_d000, i915_ms_d100, i915_ms_d700, i915_ms_edid, sizeof(i915_ms_edid));
	drv_i915_dp_fake_bind_env(&i915_ms_dpf, &i915_ms_env, i915_ms_display.dp_world);
	error = drv_i915_edp_begin(i915_ms_display.dp_world, &i915_ms_env, &config, &i915_ms_res);
	if (error == 0)
		error = drv_i915_edp_init_late(i915_ms_display.dp_world, &config, &i915_ms_res);

	if (error != 0) {
		printf("eDP bring-up failed rc=%d\n", error);
		exit(2);
	}

	/* The LCD state from what the eDP read. */
	error = drv_i915_lcd_compute(i915_ms_display.lcd_world, i915_ms_res.edid, i915_ms_res.dpcd, i915_ms_res.edp_dpcd, 18, 38400, &i915_ms_state);
	if (error != 0) {
		printf("lcd compute failed rc=%d\n", error);
		exit(2);
	}

	/* The display engine model and the run log stacked on it. */
	drv_i915_lcd_fake_init(&i915_ms_lcd, &i915_ms_dpf, 0, 0, 0, i915_ms_state.mode.vtotal);
	i915_ms_lcd.dbuf_size = 4096U;
	drv_i915_lcd_trace_init(&i915_ms_trace, &i915_ms_lcd.ops);
}

/* Prepares a modeset of the state on the run log and commits its enable; the prepare's error, or the commit's result. */
static int
i915_ms_prepare_enable(
	const struct i915_lcd_modeset_cfg *config)
{
	int error;
	int result;

	/* The check phase. */
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, config, &i915_ms_trace.ops);
	if (error != 0)
		return error;

	/* The enable commit. */
	result = drv_i915_lcd_modeset_commit_enable(&i915_ms_display);
	if (result != I915_LCD_MS_OK)
		return result;

	/* Succeeded: the picture is up. */
	return I915_LCD_MS_OK;
}

/* Prints the run log (reads only in a verbose run). */
static void
i915_ms_dump_trace(
	const char *title)
{
	const struct i915_lcd_trace_entry *e;
	size_t length;
	unsigned i;
	int named;

	/* The summary line. */
	printf("  ---- %s: %u entries (%u writes, %u rmw, %u waits [%u timed out], %u steps not ported, %u errors, dropped %u)\n",
	       title,
	       i915_ms_trace.n,
	       i915_ms_trace.writes,
	       i915_ms_trace.rmws,
	       i915_ms_trace.waits,
	       i915_ms_trace.wait_timeouts,
	       i915_ms_trace.steps,
	       i915_ms_trace.errors,
	       i915_ms_trace.dropped);

	/* One line per entry: a named entry by its name, an operation by its operands. */
	for (i = 0U; i < i915_ms_trace.n; i++) {
		e = &i915_ms_trace.e[i];
		if (e->kind == I915_LCD_T_READ && !i915_host_verbose)
			continue;

		named = 0;
		if (e->name != NULL && e->kind >= I915_LCD_T_STEP && e->kind <= I915_LCD_T_DECIDED)
			named = 1;

		if (named) {
			printf("  t[%3u] %-6s %s", i, i915_ms_kind_name[e->kind], e->name);
		} else {
			printf("  t[%3u] %-6s 0x%05x b=0x%08x c=0x%08x d=0x%08x rc=%d n=%u%s%s\n",
			       i,
			       i915_ms_kind_name[e->kind],
			       e->a,
			       e->b,
			       e->c,
			       e->d,
			       e->rc,
			       e->n,
			       e->name != NULL ? " " : "",
			       e->name != NULL ? e->name : "");
		}

		/* A name without its own line end gets one. */
		if (named) {
			length = strlen(e->name);
			if (length == 0U || e->name[length - 1U] != '\n')
				printf("\n");
		}
	}
}

/* Ends the eDP and tells whether nothing is held anywhere: power references, locks, VDD, panel power. */
static int
i915_ms_all_released(
	const char *what)
{
	int end;
	int refs;
	int released;

	/* Ends the eDP and releases what the power layer parked. */
	end = drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
	drv_i915_dp_fake_flush_async(&i915_ms_dpf);
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);

	/* Nothing may be left on either model. */
	released = 0;
	if (end == 0 &&
	    refs == 0 &&
	    i915_ms_lcd.lock_held[0] == 0 &&
	    i915_ms_lcd.lock_held[1] == 0 &&
	    i915_ms_dpf.refs_core == 0 &&
	    i915_ms_dpf.refs_aux == 0 &&
	    (i915_ms_dpf.pp_control & 9U) == 0U)
		released = 1;

	/* Shows what is still held. */
	if (!released) {
		printf("  [%s] end=%d lcd refs=%d locks=%d/%d dp refs core=%d aux=%d pp_control=0x%x\n",
		       what,
		       end,
		       refs,
		       i915_ms_lcd.lock_held[0],
		       i915_ms_lcd.lock_held[1],
		       i915_ms_dpf.refs_core,
		       i915_ms_dpf.refs_aux,
		       i915_ms_dpf.pp_control);
	}

	/* Succeeded: reports whether everything is back. */
	return released;
}

/* Returns the index of the first run-log entry of a kind at or after from, or -1. */
static int
i915_ms_find(
	int kind,
	uint32_t a,
	const char *name,
	unsigned from)
{
	int at;

	/* Searches the run log. */
	at = drv_i915_lcd_trace_find(&i915_ms_trace, kind, a, name, from);

	/* Succeeded: the index, or -1. */
	return at;
}

/* Returns the first error of a status for printing, or a placeholder. */
static const char *
i915_ms_first_error(
	const struct i915_lcd_modeset_status *s,
	const char *none)
{
	/* No error: the placeholder. */
	if (s->first_error == NULL)
		return none;

	/* Succeeded: the error's text. */
	return s->first_error;
}

/* Tells whether the first error of a status names a word. */
static int
i915_ms_error_names(
	const struct i915_lcd_modeset_status *s,
	const char *word)
{
	const char *found;

	/* No error names nothing. */
	if (s->first_error == NULL)
		return 0;

	/* Looks for the word. */
	found = strstr(s->first_error, word);
	if (found == NULL)
		return 0;

	/* Succeeded: the error names the word. */
	return 1;
}

/* A. prepare -> enable -> plane update -> frames advance -> plane off -> disable -> released. */
static void
i915_ms_test_normal(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	uint32_t frame0;
	uint32_t frame1;
	int error;
	int result;
	int refs;
	int pps;
	int pll;
	int clk;
	int tp1;
	int src;
	int mn;
	int conf;
	int bl;
	int released;

	/* The check phase touches nothing. */
	i915_ms_bring_up();
	config = i915_ms_config();
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == 0, "A: prepare");
	i915_host_check(i915_ms_trace.n == 0U, "A: prepare touches nothing");

	/* The enable commit. */
	drv_i915_lcd_trace_phase(&i915_ms_trace, "commit: enable");
	result = drv_i915_lcd_modeset_commit_enable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	drv_i915_lcd_modeset_link_status(&i915_ms_display, &s);
	i915_ms_dump_trace("A: enable");
	printf("  A: enable rc=%d errors=%u first=%s | link %d x%d trained_flag=%d status %02x %02x %02x cr=%d eq=%d train_set %02x %02x | DP=0x%08x | pll on=%d mask=%d | wakerefs io=%d aux=%d\n",
	       result,
	       s.errors,
	       i915_ms_first_error(&s, "-"),
	       s.link_rate,
	       s.lane_count,
	       s.link_trained_flag,
	       s.link_status[0],
	       s.link_status[1],
	       s.link_status[2],
	       s.cr_ok,
	       s.eq_ok,
	       s.train_set[0],
	       s.train_set[1],
	       s.ddi_buf_ctl_value,
	       s.pll_on,
	       s.pll_active_mask,
	       s.ddi_io_wakeref,
	       s.aux_wakeref);
	i915_host_check(result == I915_LCD_MS_OK && s.errors == 0U, "A: enable succeeds without a reference error");

	/* The sink itself reports the link, not just the driver's flag. */
	i915_host_check(s.crtc_active == 1 &&
			s.cr_ok == 1 &&
			s.eq_ok == 1 &&
			(s.link_status[0] & 0x77U) == 0x77U &&
			(s.link_status[2] & 1U) == 1U,
			"A: the SINK reports CR + EQ + symbol lock on both lanes and inter-lane alignment (not just the driver's flag)");
	i915_host_check(s.link_rate == 270000 && s.lane_count == 2 && s.ddi_buf_ctl_value == 0x80000002U,
			"A: link 270000 x2; intel_dp->DP = 0x80000002 = Linux's DDI_BUF_CTL_A dump");

	/* The registers as Linux left them. */
	i915_host_check(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x64000U) == 0x80000002U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x60400U) == 0x8a210002U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x46140U) == 0x10000000U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70008U) & 0xc0000000U) == 0xc0000000U,
			"A: registers as Linux left them: DDI_BUF_CTL_A, TRANS_DDI_FUNC_CTL_A, TRANS_CLK_SEL_A 0x10000000, TRANSCONF enabled + active");
	i915_host_check((drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x46010U) & 0xcc000000U) == 0xcc000000U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x164284U) == 0x00e001a5U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x164288U) == 0x00000088U,
			"A: DPLL0 enable = power + enable + lock (dump 0xcc000000), CFGCR0 / CFGCR1 = Linux's dump");
	i915_host_check((drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x164280U) & 0x403U) == 0U,
			"A: DPCLKA_CFGCR0: DDI A takes DPLL0 and its clock is ungated");
	printf("  A: PIPE_MISC_A=0x%08x\n", drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70030U));
	i915_host_check((drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70030U) & 0xf0U) == 0x50U,
			"A: PIPE_MISC: 6 bpc WITH dithering (the reference dithers exactly the 18 bpp pipes; Linux reports dither=yes for this one)");

	/* What the enable owns. */
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(s.pll_on == 1 &&
			s.pll_active_mask == 1 &&
			s.ddi_io_wakeref != 0 &&
			s.aux_wakeref != 0 &&
			refs == 6,
			"A: the enable owns: the PLL (pipe A), the DDI IO and the AUX power references, and the crtc its four domains");
	i915_host_check((i915_ms_dpf.pp_control & 1U) == 1U && (i915_ms_dpf.pp_control & 4U) == 4U,
			"A: panel power on and the PPS backlight-enable bit set (resident eDP's PPS code)");
	i915_host_check(drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U,
			"A: no ordering violation seen by the model (PLL before DDI, DDI before pipe, panel power before training)");
	i915_host_check(s.backlight_present == 1 &&
			s.backlight_enabled == 1 &&
			s.backlight_pwm_max == 0x17700U &&
			s.backlight_level == 0x17700U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8254U) == 0x17700U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == 0x17700U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8250U) & 0x80000000U) != 0U,
			"A: PWM backlight: 19.2 MHz / 200 Hz = 96000 = Linux's BLC_PWM_PCH_CTL2 dump (0x17700), duty = max, PWM enabled");

	/* The order from the run log. */
	pps = i915_ms_find(I915_LCD_T_PANEL, 0U, NULL, 0U);
	pll = i915_ms_find(I915_LCD_T_WAIT, 0x46010U, NULL, 0U);
	clk = i915_ms_find(I915_LCD_T_RMW, 0x164280U, NULL, 0U);
	tp1 = i915_ms_find(I915_LCD_T_DPCD_WRITE, 0x102U, NULL, 0U);
	src = i915_ms_find(I915_LCD_T_WRITE, 0x6001cU, NULL, 0U);
	mn = i915_ms_find(I915_LCD_T_WRITE, 0x60030U, NULL, 0U);
	conf = i915_ms_find(I915_LCD_T_WRITE, 0x70008U, NULL, mn > 0 ? (unsigned)(mn + 20) : 0U);
	bl = i915_ms_find(I915_LCD_T_PANEL, 4U, NULL, 0U);
	i915_host_check(pll >= 0 &&
			pll < pps &&
			pps < clk &&
			clk < tp1 &&
			tp1 < src &&
			src < mn &&
			mn < conf &&
			conf < bl,
			"A: order from the run: PLL lock < panel power < DDI clock < training < PIPESRC < M/N < transcoder enable < PPS backlight");
	i915_host_check(i915_ms_trace.dropped == 0U, "A: the run log holds the whole commit");

	/* The plane is armed once on the running pipe with this buffer. */
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			s.plane_armed == 1 &&
			i915_ms_lcd.plane_arms == 1U &&
			i915_ms_lcd.plane_ctl_at_arm == 0x94000000U &&
			i915_ms_lcd.plane_surf_at_arm == I915_MS_SURF_A &&
			i915_ms_lcd.plane_armed_without_pipe == 0U,
			"A: plane armed once on the running pipe with PLANE_CTL 0x94000000 and THIS buffer's GGTT address");
	i915_host_check(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70188U) == 0x78U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70190U) == 0x0437077fU,
			"A: PLANE_STRIDE / PLANE_SIZE = Linux's dump");

	/* The watermarks and the DDB from the target's latencies. */
	printf("  A: wm rc=%d ddb=[%u,%u) wm0 en=%d blocks=%u lines=%u slices=0x%x mbus_joined=%d | PLANE_WM_1_A_0=0x%08x BUF_CFG=0x%08x WM_TRANS=0x%08x WM_SAGV=0x%08x\n",
	       s.wm_rc,
	       s.ddb_start,
	       s.ddb_end,
	       s.wm0_enable,
	       s.wm0_blocks,
	       s.wm0_lines,
	       s.dbuf_slices_wanted,
	       s.mbus_joined,
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70240U),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x7027cU),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70268U),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70258U));
	i915_host_check(s.wm_rc == 0 &&
			s.ddb_start == 0U &&
			s.ddb_end == 4060U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x7027cU) == 0x0fdb0000U,
			"A: DDB [0, 4060) of the joined 4096-block DBUF (the rest is the cursor's reserve); PLANE_BUF_CFG = end - 1 encoded = Linux's dump 0x0fdb0000");
	i915_host_check(s.wm0_enable == 1 && drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70240U) == 0x80004010U,
			"A: watermark level 0 from the target's latencies = Linux's dump 0x80004010 (enable, 1 line, 16 blocks)");
	i915_host_check(i915_ms_lcd.plane_armed_without_ddb == 0U,
			"A: the plane was armed with a valid DDB range and an enabled level-0 watermark");

	/* The frame counter advances while the picture is up (the observation window). */
	frame0 = i915_ms_lcd.ops.read32(i915_ms_lcd.ops.ctx, 0x70040U);
	i915_ms_lcd.ops.usleep(i915_ms_lcd.ops.ctx, 500000U);
	frame1 = i915_ms_lcd.ops.read32(i915_ms_lcd.ops.ctx, 0x70040U);
	i915_host_check(frame1 >= frame0 + 29U, "A: the pipe's frame counter advances while the picture is up (the observation window)");

	/* The disable commit, on a fresh log (the backend keeps its state). */
	drv_i915_lcd_trace_init(&i915_ms_trace, &i915_ms_lcd.ops);
	drv_i915_lcd_trace_phase(&i915_ms_trace, "commit: disable");
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	i915_host_check(result == I915_LCD_MS_OK &&
			i915_ms_lcd.plane_disarms == 1U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70180U) == 0U,
			"A: plane disabled (PLANE_CTL 0, then the arming PLANE_SURF write)");
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(s.plane_armed == 1, "A: the modeset does NOT declare the buffer free on its own");

	/* The caller saw the pipe stand still. */
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	i915_ms_dump_trace("A: plane update / disable / crtc disable");
	i915_host_check(result == I915_LCD_MS_OK && s.errors == 0U && s.crtc_active == 0, "A: disable succeeds");
	i915_host_check((drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70008U) & 0xc0000000U) == 0U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x64000U) & 0x80000080U) == 0x80U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x46010U) & 0xcc000000U) == 0U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x164280U) & 0x400U) == 0x400U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x60400U) & 0x80000000U) == 0U,
			"A: read back: pipe off, DDI buffer off and idle, PLL off and unpowered, DDI clock gated, transcoder function off");
	i915_host_check(s.pll_on == 0 &&
			s.pll_active_mask == 0 &&
			s.ddi_io_wakeref == 0 &&
			s.aux_wakeref == 0 &&
			s.link_trained_flag == 0,
			"A: ownership given back: PLL, DDI IO and AUX references; link_trained cleared");
	i915_host_check((i915_ms_dpf.pp_control & 5U) == 0U && i915_ms_dpf.dpcd[0x600] == 2U,
			"A: panel power and PPS backlight off; the sink was put to D3");
	i915_host_check((drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8250U) & 0x80000000U) == 0U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == 0U &&
			s.backlight_enabled == 0,
			"A: PWM backlight off: duty 0, PWM disabled");
	i915_host_check(drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U,
			"A: no ordering violation on the way down (backlight, plane, pipe, DDI, PLL)");
	released = i915_ms_all_released("A");
	i915_host_check(released, "A: after the eDP ends nothing is held anywhere (power refs, locks, VDD)");
}

/* B. Early failures: the PLL does not lock, the sink never reports clock recovery, the sink asks for other levels. */
static void
i915_ms_test_early_failures(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	int result;
	int refs;
	int named;
	int at;
	int released;

	/* B1: a PLL that does not lock is reported; the enable is not a success. */
	i915_ms_bring_up();
	i915_ms_lcd.fault_pll_no_lock = 1;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	drv_i915_lcd_modeset_link_status(&i915_ms_display, &s);
	printf("  B1: enable rc=%d errors=%u first=%s", result, s.errors, i915_ms_first_error(&s, "-\n"));
	named = i915_ms_error_names(&s, "not locked");
	i915_host_check(result == I915_LCD_MS_ERRORS && named,
			"B1: a PLL that does not lock is reported (first error names it); enable is NOT a success");
	i915_host_check(s.cr_ok == 0 && s.eq_ok == 0, "B1: and the sink indeed has no link");
	i915_host_check(s.link_trained_flag == 1,
			"B1: ... although intel_dp->link_trained was set by the stop function (why it is not used as evidence)");
	i915_host_check(s.first_error != NULL &&
			i915_ms_trace.first_error_at >= 0 &&
			i915_ms_trace.e[i915_ms_trace.first_error_at].name == s.first_error,
			"B1: the run log's first ERROR entry is that error, at the position it happened");

	/* B1: the reference's disable gives back what the failed enable had taken. */
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(s.crtc_active == 0 &&
			s.pll_on == 0 &&
			s.ddi_io_wakeref == 0 &&
			s.aux_wakeref == 0 &&
			refs == 0,
			"B1: the reference's disable gives back what the failed enable had taken");
	released = i915_ms_all_released("B1");
	i915_host_check(released, "B1: nothing held after the eDP ends");

	/* B2: the sink never reports clock recovery. */
	i915_ms_bring_up();
	i915_ms_lcd.fault_cr_never = 1;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	drv_i915_lcd_modeset_link_status(&i915_ms_display, &s);
	printf("  B2: enable rc=%d errors=%u first=%s", result, s.errors, i915_ms_first_error(&s, "-\n"));
	i915_host_check(result == I915_LCD_MS_ERRORS && s.cr_ok == 0, "B2: the sink never reports clock recovery: enable fails");
	at = i915_ms_find(I915_LCD_T_ERROR, 0U, "FALLBACK", 0U);
	i915_host_check(at >= 0,
			"B2: the reference asked for the (unported) fallback: recorded as an error, no other rate / lane count was tried");
	printf("  B2: training-pattern writes=%u train_set=%02x %02x\n", i915_ms_lcd.training_pattern_writes, s.train_set[0], s.train_set[1]);
	i915_host_check(i915_ms_lcd.training_pattern_writes >= 2U &&
			i915_ms_lcd.training_pattern_writes <= 12U &&
			(s.train_set[0] & 3U) == 0U,
			"B2: the reference re-tried the level the sink kept requesting a bounded number of times, then gave up (no endless loop, no invented level)");
	at = i915_ms_find(I915_LCD_T_WRITE, 0x70008U, NULL, 0U);
	i915_host_check(at >= 0 && (drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70008U) & 0x80000000U) != 0U,
			"B2: as in the reference, the pipe is still enabled after a failed training (the panel stays black) -- so the disable must run");

	/* B2: the disable stops the pipe and gives everything back. */
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(s.crtc_active == 0 &&
			s.pll_on == 0 &&
			refs == 0 &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70008U) & 0xc0000000U) == 0U,
			"B2: the disable stops the pipe and gives everything back");
	released = i915_ms_all_released("B2");
	i915_host_check(released, "B2: nothing held after the eDP ends");

	/* B3: a sink that asks for swing 2 / pre-emphasis 1 gets them. */
	i915_ms_bring_up();
	i915_ms_lcd.sink_want_vswing = 2U;
	i915_ms_lcd.sink_want_preemph = 1U;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	drv_i915_lcd_modeset_link_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			s.cr_ok != 0 &&
			s.eq_ok != 0 &&
			(s.train_set[0] & 3U) == 2U &&
			((s.train_set[0] >> 3) & 3U) == 1U,
			"B3: a sink that asks for swing 2 / pre-emphasis 1 gets them (the levels come from the sink's answers at run time)");
	i915_host_check(drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U, "B3: no violation");
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	released = i915_ms_all_released("B3");
	i915_host_check(result == I915_LCD_MS_OK && released, "B3: disable + released");
}

/* What this path does not cover is refused before anything is touched. */
static void
i915_ms_test_refused(void)
{
	struct i915_lcd_modeset_cfg config;
	int error;

	/* A Type-C port. */
	i915_ms_bring_up();
	config = i915_ms_config();
	config.port = 3;
	config.aux_ch = 3;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EINVAL && i915_ms_trace.n == 0U, "a Type-C port is refused, nothing touched");

	/* A tiled framebuffer. */
	config = i915_ms_config();
	config.fb_modifier = 0x0100000000000002ULL;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EINVAL && i915_ms_trace.n == 0U, "a tiled framebuffer is refused, nothing touched");

	/* A backend without the panel hook. */
	config = i915_ms_config();
	i915_ms_trace.ops.panel = NULL;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EINVAL, "a backend without the panel hook is refused (no half-connected run)");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* The model really checks the DDB: lose the PLANE_BUF_CFG write. */
static void
i915_ms_test_ddb_model(void)
{
	struct i915_lcd_modeset_cfg config;
	int result;

	/* The arm without its DDB is counted. */
	i915_ms_bring_up();
	i915_ms_lcd.fault_drop_write_reg = 0x7027cU;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_host_check(result == I915_LCD_MS_OK &&
			i915_ms_lcd.plane_armed_without_ddb == 1U &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 1U,
			"model self-test: with the PLANE_BUF_CFG write lost, arming the plane is counted as a violation");
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* C. Trouble after the plane was armed: the pipe does not stop. */
static void
i915_ms_test_stuck_pipe(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	struct i915_lcd_emit not_model;
	int result;
	int error;
	int retained;

	/* The picture is up; then the pipe does not stop. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_host_check(result == I915_LCD_MS_OK, "C: picture up");
	i915_ms_lcd.fault_pipe_stuck_on = 1;
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  C: disable rc=%d errors=%u first=%s", result, s.errors, i915_ms_first_error(&s, "-\n"));
	i915_host_check(result == I915_LCD_MS_ERRORS && s.first_error != NULL,
			"C: a pipe that does not stop is reported by the reference's wait; the disable is NOT a success");
	i915_host_check(s.plane_armed == 1,
			"C: the framebuffer stays marked as possibly scanned out: the caller must keep it (no forged cleanup)");

	/* Nothing is taken away from under the pipe. */
	i915_host_check(s.stop_unconfirmed == 1 &&
			s.dc_off_held == 1 &&
			s.crtc_domains_held == 4U &&
			i915_ms_lcd.dbuf_enabled == 0x0fU &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x4438cU) & 0x80000000U) != 0U &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_DC_OFF] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_PIPE_A] == 1 &&
			i915_ms_lcd.power_dropped_with_pipe_on == 0U &&
			i915_ms_lcd.dbuf_shrunk_under_plane == 0U,
			"C: nothing was taken away from under the pipe: DC_OFF, the crtc's power domains, the DBUF slices and the MBUS joining all stay");
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EBUSY, "C: and a new modeset is refused while that is so");

	/* After abandon, a second call of the same entry is refused (prepare and commit). */
	drv_i915_lcd_modeset_abandoned(&i915_ms_display);
	not_model = i915_ms_trace.ops;
	not_model.model = 0;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	result = I915_LCD_MS_OK;
	if (error == EBUSY)
		result = drv_i915_lcd_modeset_commit_enable(&i915_ms_display);

	i915_host_check(error == EBUSY && result == I915_LCD_MS_NOT_PREPARED,
			"C: after abandon, a second call of the same entry is refused (prepare and commit)");

	/* The retained state is still recorded: nothing was forgotten by the refused calls. */
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(s.retained == 1 &&
			s.stop_unconfirmed == 1 &&
			s.dc_off_held == 1 &&
			s.crtc_domains_held == 4U &&
			s.plane_armed == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_DC_OFF] == 1 &&
			i915_ms_lcd.dbuf_enabled == 0x0fU,
			"C: ... and the retained state is still recorded: nothing was forgotten by the refused calls");

	/* A backend that is not a model cannot release it; discarding the model that holds it can. */
	error = drv_i915_lcd_modeset_discard_model(&i915_ms_display, &not_model);
	retained = drv_i915_lcd_modeset_retained(&i915_ms_display);
	i915_host_check(error == EPERM && retained == 1, "C: a backend that is not a model (real hardware) cannot release it");
	error = drv_i915_lcd_modeset_discard_model(&i915_ms_display, &i915_ms_trace.ops);
	retained = drv_i915_lcd_modeset_retained(&i915_ms_display);
	i915_host_check(error == 0 && retained == 0,
			"C: discarding the MODEL that holds it is the isolation: only then is the object free again");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* D. The commit's outer part: CDCLK and bandwidth in the check phase, DC_OFF, the crtc's domains, DBUF, MBUS, the observer. */
static void
i915_ms_test_outer(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	uint32_t frame0;
	uint32_t frame1;
	unsigned i;
	unsigned n_dbuf;
	int error;
	int result;
	int refs;
	int dc;
	int dom;
	int mbus;
	int dbuf1;
	int dbox;
	int pll;
	int surf;
	int dbuf2;
	int put;
	int off;
	int shrink;
	int grow;
	int released;

	/* The check phase computes CDCLK and bandwidth. */
	frame0 = 0U;
	frame1 = 0U;
	i915_ms_bring_up();
	drv_i915_lcd_observer_init(&i915_ms_obs, &i915_ms_lcd.ops, 0);
	i915_ms_lcd.on_observe = drv_i915_lcd_observer_point;
	i915_ms_lcd.on_observe_ctx = &i915_ms_obs;
	config = i915_ms_config();
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  D: cdclk: crtc min %d, bw min %d -> required %d kHz vco %d level %d, change needed %d | data rate %u MB/s" "\n",
	       s.cdclk_crtc_min,
	       s.cdclk_bw_min,
	       s.cdclk_required_khz,
	       s.cdclk_required_vco,
	       s.cdclk_required_level,
	       s.cdclk_change_needed,
	       s.bw_data_rate);
	i915_host_check(error == 0 &&
			s.cdclk_crtc_min == 70400 &&
			s.cdclk_bw_min == 11000 &&
			s.cdclk_required_khz == 179200 &&
			s.cdclk_required_vco == 537600 &&
			s.cdclk_required_level == 0 &&
			s.cdclk_change_needed == 0,
			"D: CDCLK the reference computes for this state (pixel rate / 2, plane, bandwidth -> first table entry at 38.4 MHz) = the current 179200 kHz: its no-change path");
	i915_host_check(s.bw_data_rate == 564U,
			"D: memory bandwidth of the one XRGB plane: 140.8 MHz x 4 bytes = 564 MB/s, below the allowed QGV point");

	/* The enable commit. */
	result = drv_i915_lcd_modeset_commit_enable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK && s.errors == 0U && drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U,
			"D: the enable commit succeeds without a model violation");
	i915_host_check(s.dc_off_held == 0 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_DC_OFF] == 0 &&
			i915_ms_lcd.async_puts == 1U &&
			i915_ms_lcd.last_async_delay_ms == 17,
			"D: DC_OFF was held around the whole commit and dropped at its end the reference's way (asynchronous, 17 ms)");
	i915_host_check(s.crtc_domains_held == 4U &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_PIPE_A] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_TRANSCODER_A] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_PORT_DDI_LANES_A] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_DISPLAY_CORE] == 1,
			"D: get_crtc_power_domains(): pipe A, transcoder A, the encoder's DDI lanes, display core (shared DPLL) -- each held once");
	i915_host_check(i915_ms_lcd.power_refs[I915_PW_DOMAIN_PORT_DDI_IO_A] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_AUX_IO_A] + i915_ms_lcd.power_refs[I915_PW_DOMAIN_AUX_A] == 1,
			"D: next to them the encoder's own references (DDI IO, AUX): different owners, no double acquisition");
	i915_host_check(i915_ms_lcd.dbuf_enabled == 0x0fU &&
			s.dbuf_slices_now == 0x0fU &&
			s.mbus_joined_now == 1 &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x4438cU) & 0xfc000000U) == 0xdc000000U &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x45008U) & 0xc0070000U) == 0xc0030000U,
			"D: DBUF: four slices on, MBUS joined (MBUS_CTL upper bits 0xdc = Linux's dump), tracker state service 3 (= dump's DBUF_CTL_S1)");
	printf("  D: PIPE_MBUS_DBOX_CTL_A=0x%08x MBUS_CTL=0x%08x DBUF_CTL_S1..4=0x%08x 0x%08x 0x%08x 0x%08x" "\n",
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x7003cU),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x4438cU),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x45008U),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x44fe8U),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x44300U),
	       drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x44304U));
	i915_host_check(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x7003cU) == 0x01038806U,
			"D: MBUS DBOX credits of ADL-P with joined MBUS (A 6, BW 2, B 8, B2B 16 / delay 1 / regulate)");

	/* The order from the run log. */
	dc = i915_ms_find(I915_LCD_T_POWER_GET, I915_PW_DOMAIN_DC_OFF, NULL, 0U);
	dom = i915_ms_find(I915_LCD_T_POWER_GET, I915_PW_DOMAIN_PIPE_A, NULL, 0U);
	mbus = i915_ms_find(I915_LCD_T_RMW, 0x4438cU, NULL, 0U);
	dbuf1 = i915_ms_find(I915_LCD_T_DBUF, 0x0fU, NULL, 0U);
	dbox = i915_ms_find(I915_LCD_T_WRITE, 0x7003cU, NULL, 0U);
	pll = i915_ms_find(I915_LCD_T_WAIT, 0x46010U, NULL, 0U);
	surf = i915_ms_find(I915_LCD_T_WRITE, 0x7019cU, NULL, 0U);
	dbuf2 = i915_ms_find(I915_LCD_T_DBUF, 0x0fU, NULL, surf > 0 ? (unsigned)surf : 0U);
	put = i915_ms_find(I915_LCD_T_POWER_PUT, I915_PW_DOMAIN_DC_OFF, NULL, 0U);
	n_dbuf = 0U;
	for (i = 0U; i < i915_ms_trace.n; i++) {
		if (i915_ms_trace.e[i].kind == I915_LCD_T_DBUF)
			n_dbuf++;
	}

	i915_host_check(dc >= 0 &&
			dc < dom &&
			dom < mbus &&
			mbus < dbuf1 &&
			dbuf1 < dbox &&
			dbox < pll &&
			pll < surf &&
			surf < dbuf2 &&
			dbuf2 < put &&
			n_dbuf == 2U &&
			i915_ms_trace.e[put].d == 1U &&
			i915_ms_trace.dropped == 0U,
			"D: order from the run: DC_OFF < crtc domains < MBUS_CTL < DBUF slices (old|new) < DBOX < PLL ... plane arm < DBUF slices (new) < DC_OFF put");
	i915_host_check(i915_ms_lcd.nobs == 5U &&
			i915_ms_lcd.obs[0] == I915_LCD_OBS_COMMIT_BEGIN &&
			i915_ms_lcd.obs[1] == I915_LCD_OBS_UNDERRUN_ARM &&
			i915_ms_lcd.obs[2] == I915_LCD_OBS_PIPE_ENABLED &&
			i915_ms_lcd.obs[3] == I915_LCD_OBS_PLANE_ARMED &&
			i915_ms_lcd.obs[4] == I915_LCD_OBS_COMMIT_END,
			"D: the observer is called at the commit's points, the underrun clear at the reference's position inside the crtc enable");

	/* Frames advance and no underrun is seen. */
	error = drv_i915_lcd_observer_frames(&i915_ms_obs, 500U, 10U, &frame0, &frame1);
	drv_i915_lcd_observer_steady_begin(&i915_ms_obs);
	i915_host_check(error == 0 && frame1 - frame0 >= 10U, "D: frames advance (from the frame counter)");
	error = drv_i915_lcd_observer_frames(&i915_ms_obs, 1000U, 30U, &frame0, &frame1);
	drv_i915_lcd_observer_steady_sample(&i915_ms_obs);
	i915_host_check(error == 0 &&
			i915_ms_obs.seen_transition == 0U &&
			i915_ms_obs.seen_steady == 0U &&
			i915_ms_obs.vblank_unmasked_seen == 0 &&
			i915_ms_obs.dropped == 0U,
			"D: no underrun in either period; the pipe's vblank interrupt was masked at every sample");
	i915_host_check(i915_ms_obs.s[0].point == I915_LCD_OBS_COMMIT_BEGIN &&
			i915_ms_obs.s[0].imr == 0U &&
			i915_ms_obs.s[1].imr == 0xffffffffU,
			"D: before the crtc holds the pipe's power domain its interrupt mask reads 0 (well off): recorded, not judged");

	/* The disable commit returns the domains and shrinks DBUF only after the pipe stopped. */
	drv_i915_lcd_trace_init(&i915_ms_trace, &i915_ms_lcd.ops);
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(result == I915_LCD_MS_OK &&
			s.crtc_domains_held == 0U &&
			s.dc_off_held == 0 &&
			refs == 0 &&
			i915_ms_lcd.dbuf_enabled == 0x01U &&
			s.dbuf_slices_now == 0x01U &&
			s.mbus_joined_now == 0 &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x4438cU) & 0x80000000U) == 0U &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U,
			"D: the disable commit: domains returned, DBUF back to slice 1 only AFTER the pipe stopped, MBUS un-joined, no violation");
	off = i915_ms_find(I915_LCD_T_WAIT, 0x70008U, NULL, 0U);
	shrink = i915_ms_find(I915_LCD_T_DBUF, 0x01U, NULL, 0U);
	grow = i915_ms_find(I915_LCD_T_DBUF, 0x0fU, NULL, 0U);
	i915_host_check(off >= 0 && shrink > off && grow > off,
			"D: order from the run: the pipe-off wait comes before both DBUF updates (old|new, then new)");
	error = drv_i915_lcd_observer_stopped(&i915_ms_obs, 100U, &frame0, &frame1);
	i915_host_check(error == 0, "D: after the stop the frame counter stands still");
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	released = i915_ms_all_released("D");
	i915_host_check(released, "D: nothing held after the eDP ends");

	/* A lost DBUF slice request is noticed (the model really depends on the new parts). */
	i915_ms_bring_up();
	i915_ms_lcd.fault_drop_dbuf_update = 1;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_host_check(result == I915_LCD_MS_OK &&
			i915_ms_lcd.plane_armed_outside_slices == 1U &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 1U,
			"model self-test: DBUF slice requests lost -> the plane's DDB lies in slices that are off: counted");
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);

	/* A lost MBUS_CTL write is noticed. */
	i915_ms_bring_up();
	i915_ms_lcd.fault_drop_write_reg = 0x4438cU;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_host_check(result == I915_LCD_MS_OK &&
			i915_ms_lcd.plane_armed_outside_slices == 1U &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 1U,
			"model self-test: MBUS_CTL write lost -> a DDB beyond the un-joined half: counted");
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* D. What the commit cannot do is refused in the check phase. */
static void
i915_ms_test_outer_refusals(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	int error;
	int named;

	/* A current CDCLK other than the required one (the reference would reprogram it). */
	i915_ms_bring_up();
	config = i915_ms_config();
	config.cdclk_khz = 307200U;
	config.cdclk_vco_khz = 614400U;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	named = i915_ms_error_names(&s, "CDCLK");
	i915_host_check(error == EINVAL &&
			i915_ms_trace.writes + i915_ms_trace.rmws == 0U &&
			s.cdclk_change_needed == 1 &&
			named,
			"a current CDCLK other than the required one (the reference would reprogram it) is refused with its reason, nothing touched");

	/* An unknown memory bandwidth. */
	config = i915_ms_config();
	config.qgv_allowed_bw = 0U;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EINVAL && i915_ms_trace.writes + i915_ms_trace.rmws == 0U,
			"an unknown memory bandwidth is refused, nothing touched");

	/* A backend without the DBUF hook. */
	config = i915_ms_config();
	i915_ms_trace.ops.dbuf_slices_update = NULL;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EINVAL, "a backend without the DBUF hook is refused");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* D. Observation: progress is what the frame counter says, and an underrun is kept where it happened. */
static void
i915_ms_test_observation(void)
{
	struct i915_lcd_modeset_cfg config;
	uint64_t t0;
	uint32_t frame0;
	uint32_t frame1;
	int error;
	int result;

	/* Half a second passes but the frame counter does not move: not progress. */
	frame0 = 0U;
	frame1 = 0U;
	i915_ms_bring_up();
	drv_i915_lcd_observer_init(&i915_ms_obs, &i915_ms_lcd.ops, 0);
	i915_ms_lcd.on_observe = drv_i915_lcd_observer_point;
	i915_ms_lcd.on_observe_ctx = &i915_ms_obs;
	i915_ms_lcd.fault_frame_counter_frozen = 1;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	t0 = i915_ms_dpf.now_us;
	error = ETIMEDOUT;
	if (result == I915_LCD_MS_OK)
		error = drv_i915_lcd_observer_frames(&i915_ms_obs, 500U, 10U, &frame0, &frame1);

	i915_host_check(result == I915_LCD_MS_OK &&
			error == ETIMEDOUT &&
			frame0 == frame1 &&
			i915_ms_dpf.now_us - t0 >= 500000U,
			"observation: half a second passed, the frame counter did not move -> NOT progress");
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);

	/* An underrun while the pipe starts is recorded as such, and is not lost by the clear. */
	i915_ms_bring_up();
	drv_i915_lcd_observer_init(&i915_ms_obs, &i915_ms_lcd.ops, 0);
	i915_ms_lcd.on_observe = drv_i915_lcd_observer_point;
	i915_ms_lcd.on_observe_ctx = &i915_ms_obs;
	i915_ms_lcd.fault_underrun_at_arm = 1;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	(void)drv_i915_lcd_observer_frames(&i915_ms_obs, 500U, 10U, &frame0, &frame1);
	drv_i915_lcd_observer_steady_begin(&i915_ms_obs);
	(void)drv_i915_lcd_observer_frames(&i915_ms_obs, 1000U, 30U, &frame0, &frame1);
	drv_i915_lcd_observer_steady_sample(&i915_ms_obs);
	i915_host_check(result == I915_LCD_MS_OK &&
			(i915_ms_obs.seen_transition & 0x80000000U) != 0U &&
			i915_ms_obs.seen_steady == 0U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0x70058U) == 0U,
			"observation: an underrun at the plane arm is kept in the START record; the steady record stays clean; the status was cleared after recording");

	/* ... and one while the picture stands goes to the steady record. */
	i915_ms_lcd.fault_underrun_steady_after = (int)i915_ms_lcd.frame_reads_after_arm + 2;
	(void)drv_i915_lcd_observer_frames(&i915_ms_obs, 1000U, 30U, &frame0, &frame1);
	drv_i915_lcd_observer_steady_sample(&i915_ms_obs);
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	i915_host_check((i915_ms_obs.seen_steady & 0x80000000U) != 0U && i915_ms_obs.n >= 8U && i915_ms_obs.dropped == 0U,
			"observation: an underrun during the steady picture is kept in the STEADY record (and survives the stop's samples)");
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* F. Brightness and backlight off / on while the picture stays up; the eDP stays for G. */
static void
i915_ms_test_backlight(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	uint32_t max;
	uint32_t min;
	uint32_t original;
	uint32_t half;
	uint32_t duty0;
	uint32_t user0;
	uint32_t frame0;
	uint32_t frame1;
	unsigned arms;
	unsigned disarms;
	int result;
	int refused;

	/* The backlight range from the reference. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			s.backlight_max == 96000U &&
			s.backlight_min == (6U * 96000U + 127U) / 255U,
			"F: backlight range from the reference: max = the PWM period (96000), min = VBT min_brightness 6 as a 0..255 coefficient of it (2259)");
	max = s.backlight_max;
	min = s.backlight_min;
	original = s.backlight_level;
	arms = i915_ms_lcd.plane_arms;
	disarms = i915_ms_lcd.plane_disarms;

	/* User max, half and zero (scale_user_to_hw, rounded to closest). */
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, max, max);
	i915_host_check(result == I915_LCD_MS_OK && drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == max, "F: user max -> duty = max");
	half = min + (uint32_t)(((uint64_t)(max / 2U) * (max - min) + max / 2U) / max);
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, max / 2U, max);
	i915_host_check(result == I915_LCD_MS_OK && drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == half,
			"F: user half -> duty = min + half of (max - min) (scale_user_to_hw, rounded to closest)");
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, 0U, max);
	i915_host_check(result == I915_LCD_MS_OK &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == min &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8254U) == max,
			"F: user 0 -> duty = the VBT-derived minimum, never 0; the PWM period is unchanged");

	/* Backlight off at half: the pipe keeps scanning out the same buffer. */
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, max / 2U, max);
	frame0 = i915_ms_lcd.ops.read32(i915_ms_lcd.ops.ctx, 0x70040U);
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_backlight(&i915_ms_display, 0);

	i915_ms_lcd.ops.usleep(i915_ms_lcd.ops.ctx, 200000U);
	frame1 = i915_ms_lcd.ops.read32(i915_ms_lcd.ops.ctx, 0x70040U);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8250U) & 0x80000000U) == 0U &&
			(i915_ms_dpf.pp_control & 4U) == 0U &&
			s.backlight_enabled == 0 &&
			s.crtc_active == 1 &&
			s.plane_armed == 1 &&
			frame1 > frame0 &&
			i915_ms_lcd.plane_disarms == disarms,
			"F: backlight off = PWM disabled + PPS backlight bit clear, while the pipe keeps scanning out the SAME buffer (still in use)");

	/* Backlight on again restores the last level; no new modeset. */
	result = drv_i915_lcd_modeset_backlight(&i915_ms_display, 1);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			(drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8250U) & 0x80000000U) != 0U &&
			(i915_ms_dpf.pp_control & 4U) == 4U &&
			s.backlight_enabled == 1 &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == half &&
			i915_ms_lcd.plane_arms == arms,
			"F: backlight on again restores the last level (half); no new modeset, no plane re-arm");

	/* From the minimum it comes back at max (__intel_backlight_enable: level <= min -> max). */
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, 0U, max);
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_backlight(&i915_ms_display, 0);
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_backlight(&i915_ms_display, 1);

	i915_host_check(result == I915_LCD_MS_OK && drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == max,
			"F: ... but from the minimum it comes back at MAX (__intel_backlight_enable: level <= min -> max)");
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, original, max);
	i915_host_check(result == I915_LCD_MS_OK && drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == original, "F: back to the original level");

	/* A restore uses the saved user level, not the hardware level fed back as a user level. */
	result = drv_i915_lcd_modeset_brightness(&i915_ms_display, 30000U, max);
	duty0 = drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	user0 = s.backlight_user;
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_brightness(&i915_ms_display, max, max);
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_brightness(&i915_ms_display, 0U, max);
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_brightness(&i915_ms_display, user0, s.backlight_user_max);

	i915_host_check(result == I915_LCD_MS_OK &&
			user0 == 30000U &&
			drv_i915_lcd_fake_reg(&i915_ms_lcd, 0xc8258U) == duty0 &&
			duty0 != 30000U,
			"F: restore uses the saved USER level (30000 -> hw duty again), not the hw level fed back as a user level");
	i915_host_check(drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U, "F: no model violation");

	/* After the stop, brightness requests are refused (no active crtc). */
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	refused = drv_i915_lcd_modeset_brightness(&i915_ms_display, 1U, 2U);
	i915_host_check(result == I915_LCD_MS_OK && refused == I915_LCD_MS_NOT_PREPARED,
			"F: after the stop, brightness requests are refused (no active crtc)");
}

/* G. The same resident eDP shows, stops, shows again (three cycles). */
static void
i915_ms_test_cycles(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	unsigned cycle;
	unsigned good;
	int result;
	int refs;
	int released;

	/* Each cycle trains, arms, stops and returns everything. */
	good = 0U;
	for (cycle = 0U; cycle < 3U; cycle++) {
		/* The shared state as the previous cycle left it. */
		config = i915_ms_config();
		config.dbuf_enabled_slices = i915_ms_lcd.dbuf_enabled;
		result = i915_ms_prepare_enable(&config);
		drv_i915_lcd_modeset_status(&i915_ms_display, &s);
		drv_i915_lcd_modeset_link_status(&i915_ms_display, &s);
		if (result == I915_LCD_MS_OK &&
		    s.cr_ok != 0 &&
		    s.eq_ok != 0 &&
		    i915_ms_lcd.plane_surf_at_arm == I915_MS_SURF_A)
			good++;

		/* The stop must leave nothing for the next cycle. */
		result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
		drv_i915_lcd_modeset_plane_released(&i915_ms_display);
		drv_i915_lcd_modeset_status(&i915_ms_display, &s);
		if (result != I915_LCD_MS_OK ||
		    s.crtc_domains_held != 0U ||
		    i915_ms_lcd.dbuf_enabled != 0x01U ||
		    (i915_ms_dpf.pp_control & 1U) != 0U)
			break;
	}

	/* Every cycle succeeded without a violation or a reference left. */
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(good == 3U &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U &&
			i915_ms_lcd.power_underflows == 0U &&
			refs == 0,
			"G: three show / stop cycles on one resident eDP, each trained and armed, each stop returns everything, no violation");
	released = i915_ms_all_released("G");
	i915_host_check(released, "G: nothing held after the eDP ends");
}

/* H. The pipe's power cannot be released (interrupt drain failed): the stop is not confirmed. */
static void
i915_ms_test_power_kept(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	int result;
	int error;
	int named;

	/* The pipe power release is refused in the commit tail. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_ms_lcd.fault_put_refused_domain = I915_PW_DOMAIN_PIPE_A + 1;
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);

	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	named = i915_ms_error_names(&s, "kept");
	i915_host_check(result == I915_LCD_MS_ERRORS &&
			s.stop_unconfirmed == 1 &&
			s.dc_off_held == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_DC_OFF] == 1 &&
			i915_ms_lcd.power_refs[I915_PW_DOMAIN_PIPE_A] == 1 &&
			named,
			"H: a pipe power release refused in the commit tail -> the disable is NOT a success, DC_OFF and the pipe's power stay held");

	/* Nothing new starts on top of it; the model is discarded. */
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == EBUSY, "H: and nothing new starts on top of it");
	drv_i915_lcd_modeset_abandoned(&i915_ms_display);
	error = drv_i915_lcd_modeset_discard_model(&i915_ms_display, &i915_ms_trace.ops);
	i915_host_check(error == 0, "H: (the model is discarded)");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* I. The synchronous flip of the running picture. */
static void
i915_ms_test_flip(void)
{
	static const uint32_t sequence[4] = { I915_MS_SURF_B, I915_MS_SURF_A, I915_MS_SURF_B, I915_MS_SURF_A };
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	struct i915_lcd_flip_result flip;
	uint32_t old;
	unsigned k;
	unsigned good;
	unsigned arms0;
	unsigned arms1;
	unsigned outside0;
	unsigned events0;
	int result;
	int released;

	/* The picture is up on buffer A. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_host_check(result == I915_LCD_MS_OK && i915_ms_lcd.surf_live == I915_MS_SURF_A,
			"I: the picture is up on buffer A (live surface = A)");
	arms0 = i915_ms_lcd.plane_arms;
	outside0 = i915_ms_lcd.arm_outside_section;
	events0 = i915_ms_lcd.events_done;

	/* B, A, B, A: each flip completes only with its event and the new live surface. */
	good = 0U;
	for (k = 0U; k < 4U; k++) {
		old = I915_MS_SURF_A;
		if (k != 0U)
			old = sequence[k - 1U];

		result = drv_i915_lcd_modeset_flip(&i915_ms_display, sequence[k], &flip);
		drv_i915_lcd_modeset_status(&i915_ms_display, &s);
		if (result == I915_LCD_MS_OK &&
		    flip.result == I915_LCD_FLIP_DONE &&
		    flip.gen == k + 1U &&
		    flip.old_surf == old &&
		    flip.live_before == old &&
		    flip.live_after == sequence[k] &&
		    flip.frame_after > flip.frame_before &&
		    flip.event_rc == 0 &&
		    flip.update_errors == 0 &&
		    s.cur_surf == sequence[k] &&
		    s.flip_pending == 0 &&
		    i915_ms_lcd.vblank_refs == 0 &&
		    i915_ms_lcd.plane_surf_at_arm == sequence[k] &&
		    !i915_ms_lcd.irq_off)
			good++;

		printf("  I: flip %u gen %u %08x -> %08x live %08x -> %08x frames %u -> %u event %d result %d" "\n",
		       k + 1U,
		       flip.gen,
		       flip.old_surf,
		       flip.new_surf,
		       flip.live_before,
		       flip.live_after,
		       flip.frame_before,
		       flip.frame_after,
		       flip.event_rc,
		       flip.result);
	}

	i915_host_check(good == 4U,
			"I: B, A, B, A: each flip completes only with its event AND the live surface = the new buffer; the old one is released");
	printf("  I: arms %u->%u outside %u->%u events %u->%u lock_errors %u irq_off_calls %u\n",
	       arms0,
	       i915_ms_lcd.plane_arms,
	       outside0,
	       i915_ms_lcd.arm_outside_section,
	       events0,
	       i915_ms_lcd.events_done,
	       i915_ms_lcd.lock_errors,
	       i915_ms_lcd.irq_off_calls);
	i915_host_check(i915_ms_lcd.plane_arms == arms0 + 4U &&
			i915_ms_lcd.arm_outside_section == outside0 &&
			i915_ms_lcd.events_done == events0 + 4U &&
			i915_ms_lcd.lock_errors == 0U &&
			i915_ms_lcd.irq_off_calls >= 4U,
			"I: every arm happened inside the update section (interrupts off, balanced), one event per flip, vblank references returned");

	/* A flip to the buffer already shown is refused. */
	arms1 = i915_ms_lcd.plane_arms;
	result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_A, &flip);
	i915_host_check(result == I915_LCD_MS_NOT_PREPARED &&
			flip.result == I915_LCD_FLIP_REFUSED &&
			i915_ms_lcd.plane_arms == arms1,
			"I: a flip to the buffer already shown is refused, nothing written");

	/* An early (stale) completion: the event reports before any vblank; the live surface is still the old buffer. */
	i915_ms_lcd.fault_early_event = 1;
	result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_ERRORS &&
			flip.result == I915_LCD_FLIP_NOT_LATCHED &&
			flip.live_after == I915_MS_SURF_A &&
			s.flip_pending == 1 &&
			s.flip_stuck == 1 &&
			s.cur_surf == I915_MS_SURF_A &&
			s.pend_surf == I915_MS_SURF_B,
			"I: STALE an event without the new surface live is NOT a completion: both buffers stay protected");
	i915_ms_lcd.fault_early_event = 0;

	/* No further flip is submitted while one is unresolved. */
	arms1 = i915_ms_lcd.plane_arms;
	result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_A, &flip);
	i915_host_check(result == I915_LCD_MS_NOT_PREPARED && i915_ms_lcd.plane_arms == arms1,
			"I: STALE and no further flip is submitted while one is unresolved");

	/* The stop path still runs; once the display is stopped neither buffer is read. */
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  I: stop rc=%d pending=%d stuck=%d violations=%u\n", result, s.flip_pending, s.flip_stuck, drv_i915_lcd_fake_violations(&i915_ms_lcd));
	i915_host_check(result == I915_LCD_MS_OK &&
			s.flip_pending == 0 &&
			s.flip_stuck == 0 &&
			drv_i915_lcd_fake_violations(&i915_ms_lcd) == 0U,
			"I: the stop path still runs; once the display is stopped neither buffer is read any more");
	released = i915_ms_all_released("I");
	i915_host_check(released, "I: nothing held after the eDP ends");

	/* The arm never reaches the live surface. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_ms_lcd.fault_flip_never_latch = 1;
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);

	i915_host_check(result == I915_LCD_MS_ERRORS &&
			flip.result == I915_LCD_FLIP_NOT_LATCHED &&
			flip.event_rc == 0 &&
			flip.live_after == I915_MS_SURF_A,
			"I: NOT-LIVE the register write alone (live surface unchanged after the vblank) is not a completion");
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);

	/* No vblank arrives. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	i915_ms_lcd.fault_no_vblank = 1;
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);

	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_ERRORS &&
			flip.result == I915_LCD_FLIP_TIMEOUT &&
			flip.event_rc == I915_LCD_ETIMEDOUT &&
			s.flip_stuck == 1 &&
			i915_ms_lcd.vblank_refs == 1,
			"I: TIMEOUT no completion within the limit: both kept, the event's vblank reference stays (it may still complete)");
	i915_ms_lcd.fault_no_vblank = 0;
	(void)drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* J. The event after a timeout, the stop, the relight; the evasion sleep. */
static void
i915_ms_test_flip_event(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	struct i915_lcd_flip_result flip;
	unsigned events0;
	unsigned underflows0;
	unsigned refused0;
	int window_min;
	int window_max;
	int vblank_start;
	int result;
	int waited;

	/* J1: a flip times out; the armed event keeps its vblank reference. */
	window_min = 0;
	window_max = 0;
	vblank_start = 0;
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	underflows0 = i915_ms_lcd.power_underflows;
	i915_ms_lcd.fault_no_vblank = 1;
	if (result == I915_LCD_MS_OK)
		(void)drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);

	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(flip.result == I915_LCD_FLIP_TIMEOUT &&
			s.flip_event_ref == 1 &&
			i915_ms_lcd.vblank_refs == 1 &&
			i915_ms_lcd.event_armed == 1,
			"J1: TIMEOUT the armed event keeps its vblank reference (it may still complete)");

	/* J1: the stop settles the event once. */
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_OK &&
			s.flip_event_ref == 0 &&
			s.events_cancelled == 1U &&
			i915_ms_lcd.events_cancelled >= 1U &&
			i915_ms_lcd.vblank_refs == 0 &&
			i915_ms_lcd.power_underflows == underflows0 &&
			i915_ms_lcd.event_armed == 0,
			"J1: the stop (intel_crtc_vblank_off) settles the event: cancelled, its reference returned exactly once (no underflow)");

	/* J1: the late vblank arrives now; a late completion of the cancelled event is refused. */
	i915_ms_lcd.fault_no_vblank = 0;
	refused0 = i915_ms_lcd.waits_refused;
	waited = i915_ms_lcd.ops.wait_event(i915_ms_lcd.ops.ctx, 0, 100U);
	i915_host_check(waited == I915_LCD_FAKE_EINVAL && i915_ms_lcd.waits_refused == refused0 + 1U,
			"J1: a late completion of the cancelled event is refused (no event is armed any more)");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);

	/* J1: after the relight the next flip needs, and gets, its own new event. */
	i915_ms_bring_up();
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	events0 = i915_ms_lcd.events_done;
	if (result == I915_LCD_MS_OK)
		result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);

	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  J1: relight rc=%d result=%d events %u->%u ref=%d refs=%d cancelled ms=%u model=%u\n",
	       result,
	       flip.result,
	       events0,
	       i915_ms_lcd.events_done,
	       s.flip_event_ref,
	       i915_ms_lcd.vblank_refs,
	       s.events_cancelled,
	       i915_ms_lcd.events_cancelled);
	i915_host_check(result == I915_LCD_MS_OK &&
			flip.result == I915_LCD_FLIP_DONE &&
			i915_ms_lcd.events_done == events0 + 1U &&
			s.flip_event_ref == 0 &&
			i915_ms_lcd.vblank_refs == 0 &&
			s.events_cancelled == 0U,
			"J1: after the relight the next flip needs (and gets) its own new event; the cancelled one was not counted twice");

	/* J2: the reference's evasion window of the running mode. */
	result = drv_i915_lcd_modeset_evade_window(&i915_ms_display, &window_min, &window_max, &vblank_start);
	i915_host_check(result == I915_LCD_MS_OK &&
			window_min > 0 &&
			window_max >= window_min &&
			window_max < vblank_start,
			"J2: the reference's evasion window of the running mode: scanlines [min, max] before vblank start");
	printf("  J: evasion window %d..%d, vblank start %d" "\n", window_min, window_max, vblank_start);

	/* J2: the update starts inside the window: it sleeps (IRQs on), wakes after the vblank, re-reads, arms. */
	i915_ms_lcd.vblank_sleeps = 0U;
	i915_ms_lcd.sleep_irq_off = 0U;
	i915_ms_lcd.scanline_hold = (uint32_t)window_min;
	i915_ms_lcd.scanline_hold_reads = 1U;
	result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_A, &flip);
	i915_host_check(result == I915_LCD_MS_OK &&
			flip.result == I915_LCD_FLIP_DONE &&
			i915_ms_lcd.vblank_sleeps == 1U &&
			i915_ms_lcd.sleep_irq_off == 0U &&
			!i915_ms_lcd.irq_off &&
			i915_ms_lcd.lock_errors == 0U &&
			i915_ms_lcd.vblank_refs == 0 &&
			flip.update_errors == 0,
			"J2: SLEEP inside the window: one sleep entered with IRQs ON, woke after the vblank, re-read the scanline, armed; "
			"IRQ state and references restored");

	/* J3: inside the window and no vblank ever comes: a finite end, no fabricated completion. */
	i915_ms_lcd.vblank_sleeps = 0U;
	i915_ms_lcd.scanline_hold = (uint32_t)window_min;
	i915_ms_lcd.scanline_hold_reads = 1000U;
	i915_ms_lcd.fault_no_vblank = 1;
	result = drv_i915_lcd_modeset_flip(&i915_ms_display, I915_MS_SURF_B, &flip);
	i915_ms_lcd.scanline_hold_reads = 0U;
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(result == I915_LCD_MS_ERRORS &&
			flip.result == I915_LCD_FLIP_TIMEOUT &&
			i915_ms_lcd.vblank_sleeps >= 1U &&
			i915_ms_lcd.vblank_sleeps <= 3U &&
			flip.update_errors > 0 &&
			!i915_ms_lcd.irq_off &&
			i915_ms_lcd.lock_errors == 0U &&
			i915_ms_lcd.sleep_irq_off == 0U &&
			i915_ms_lcd.vblank_refs == 1 &&
			s.flip_event_ref == 1,
			"J3: NO-VBLANK the evasion gives up after its timeout (reported as an update error), the flip is not completed, "
			"IRQs restored; only the event's own reference is held");

	/* J3: the stop settles that event too. */
	i915_ms_lcd.fault_no_vblank = 0;
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  J3: stop rc=%d refs=%d ref=%d cancelled ms=%u model=%u under %u/%u\n",
	       result,
	       i915_ms_lcd.vblank_refs,
	       s.flip_event_ref,
	       s.events_cancelled,
	       i915_ms_lcd.events_cancelled,
	       i915_ms_lcd.power_underflows,
	       underflows0);
	i915_host_check(result == I915_LCD_MS_OK &&
			i915_ms_lcd.vblank_refs == 0 &&
			s.flip_event_ref == 0 &&
			s.events_cancelled == 1U &&
			i915_ms_lcd.events_cancelled == 1U &&
			i915_ms_lcd.power_underflows == underflows0,
			"J3: the stop settles that event too: every reference returned once");
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* E. A time-base fault is not a timeout. */
static void
i915_ms_test_time_base(void)
{
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	int result;
	int wait;
	int named;
	int refs;

	/* The PLL-lock wait meets a time-base fault. */
	i915_ms_bring_up();
	i915_ms_lcd.fault_time_base_reg = 0x46010U;
	config = i915_ms_config();
	result = i915_ms_prepare_enable(&config);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	wait = i915_ms_find(I915_LCD_T_WAIT, 0x46010U, NULL, 0U);
	printf("  E: enable rc=%d errors=%u first=%s", result, s.errors, i915_ms_first_error(&s, "-\n"));
	named = i915_ms_error_names(&s, "time base");
	i915_host_check(result == I915_LCD_MS_ERRORS &&
			named &&
			wait >= 0 &&
			i915_ms_trace.e[wait].rc == I915_LCD_EIO,
			"E: the wait returns -EIO (not -ETIMEDOUT) and the time-base fault is the FIRST anomaly, ahead of the reference's own PLL error");
	i915_host_check(i915_ms_lcd.plane_arms == 0U, "E: after that anomaly the plane was not armed");

	/* A transient fault: the stop itself runs on a sound time base and returns everything. */
	i915_ms_lcd.fault_time_base_reg = 0U;
	result = drv_i915_lcd_modeset_commit_disable(&i915_ms_display);
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	printf("  E: disable rc=%d retained=%d stop_unconfirmed=%d crtc_active=%d\n", result, s.retained, s.stop_unconfirmed, s.crtc_active);
	refs = drv_i915_lcd_fake_power_refs_total(&i915_ms_lcd);
	i915_host_check(result == I915_LCD_MS_OK && s.retained == 0 && s.crtc_active == 0 && refs == 0,
			"E: with the time base sound again, the reference stop path returns everything");
	drv_i915_lcd_modeset_plane_released(&i915_ms_display);
	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}

/* Dither is derived from the pipe bpp, not fixed. */
static void
i915_ms_test_dither(void)
{
	static struct i915_lcd_state state24;
	struct i915_lcd_modeset_cfg config;
	struct i915_lcd_modeset_status s;
	int error;

	/* An 18 bpp pipe dithers (intel_modeset_pipe_config). */
	i915_ms_bring_up();
	config = i915_ms_config();
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &i915_ms_state, &config, &i915_ms_trace.ops);
	i915_host_check(error == 0, "dither: prepare at 18 bpp");
	drv_i915_lcd_modeset_status(&i915_ms_display, &s);
	i915_host_check(s.dither == 1, "dither: an 18 bpp pipe dithers (intel_modeset_pipe_config)");

	/*
	 * This panel's EDID caps the pipe at 6 bpc, so the computed state stays
	 * 18 bpp; the derivation is checked on a state that carries 24 bpp (the
	 * rest of the state is this panel's).
	 */
	state24 = i915_ms_state;
	state24.link.bpp = 24;
	error = drv_i915_lcd_modeset_prepare(&i915_ms_display, &state24, &config, &i915_ms_trace.ops);
	if (error == 0) {
		drv_i915_lcd_modeset_status(&i915_ms_display, &s);
		i915_host_check(s.dither == 0, "dither: a 24 bpp pipe does not");
	} else {
		i915_host_check(0, "dither: prepare at 24 bpp");
	}

	(void)drv_i915_edp_end(i915_ms_display.dp_world, &i915_ms_res);
}
