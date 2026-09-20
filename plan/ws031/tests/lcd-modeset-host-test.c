/*
 * WS031 — host integration test of the one-screen LCD modeset: the reference's callers and callees
 * (parity/lcd generated files) + the resident eDP (parity/dp: PPS, AUX, DPCD) on the register / sink
 * models, from "prepare" to "everything given back".  zedBSD project code.
 *
 * Three families (the ownership boundaries of the first light-up):
 *   A. normal: prepare -> enable -> plane update -> frames advance -> plane off -> disable -> released
 *   B. an early failure (PLL does not lock / the sink never reports clock recovery / EQ): no success is
 *      invented, the normal disable gives back what the enable took
 *   C. trouble after the plane was armed (the pipe does not stop): the buffer stays protected
 * The pass criteria are read back from the models and the production objects, never from the trace.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parity_edp.h"
#include "dp_fake_hw.h"
#include "lcd_fake_hw.h"
#include "parity_lcd_trace.h"
#include "parity_lcd_modeset.h"
#include "parity_lcd_observe.h"
#include "lcd_power_domain_enum.h"

void parity_lcd_modeset_plane_released(void);
void parity_lcd_modeset_link_status(struct parity_lcd_modeset_status *out);

void parity_vbt_emit(const char *text) { fputs(text, stdout); }
int parity_vbt_fmtcheck(const char *fmt, ...) { (void)fmt; return 0; }

static unsigned checks, failures;
static int verbose;
#define CHECK(c, what) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); } else if (verbose) printf("ok   %s\n", what); } while (0)

static unsigned char d000[256], d100[256], d700[256], edid[128];
static struct dp_fake_hw dpf;
static struct parity_dp_env env;
static struct parity_edp_result res;
static struct lcd_fake_hw lcd;
static struct parity_lcd_trace trace;
static struct parity_lcd_state st;

static void slurp(const char *dir, const char *name, unsigned char *buf, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (!f || fread(buf, 1, n, f) != n) { perror(path); exit(2); }
	fclose(f);
}

static struct parity_edp_config vbt_cfg(void)
{
	struct parity_edp_config c;

	memset(&c, 0, sizeof(c));
	c.port = 0; c.aux_ch = 0; c.rawclk_khz = 19200;
	c.t1_t3 = 2000; c.t8 = 800; c.t9 = 2000; c.t10 = 1100; c.t11_t12 = 5000;
	c.log_level = -1;
	return c;
}

static struct parity_lcd_modeset_cfg ms_cfg(void)
{
	struct parity_lcd_modeset_cfg c;

	memset(&c, 0, sizeof(c));
	memcpy(c.dpcd, res.dpcd, 15);
	memcpy(c.edp_dpcd, res.edp_dpcd, 3);
	c.fb_fourcc = 0x34325258u;              /* XR24 */
	/* watermark inputs as the target's normal initialisation logs them (P5a: levels 6, latency 3/54/83/102/147/147/144/144,
	 * SAGV block time 35 us; XE_LPD: DBUF 4096 blocks in 4 slices, IPC; slice 1 enabled after power-domain init) */
	{ static const uint16_t lat[8] = { 3, 54, 83, 102, 147, 147, 144, 144 }; memcpy(c.wm_latency, lat, sizeof(lat)); }
	c.wm_num_levels = 6; c.wm_ipc_enabled = 1; c.sagv_block_time_us = 35; c.dbuf_size = 4096; c.dbuf_slice_mask = 0x0f; c.dbuf_enabled_slices = 0x01;
	/* the state the target's normal initialisation leaves (real-machine log): CDCLK 179200 kHz from VCO 537600 on a 38.4 MHz
	 * reference, voltage level 0; MBUS not joined; SAGV off with QGV point 2 allowed (derated bandwidth 11707 MB/s) */
	c.cdclk_khz = 179200; c.cdclk_vco_khz = 537600; c.cdclk_ref_khz = 38400; c.cdclk_bypass_khz = 19200; c.cdclk_max_khz = 652800;
	c.cdclk_voltage_level = 0; c.mbus_joined = 0; c.qgv_allowed_bw = 11707;
	c.dmc_fw_mask = 1u << 1;
	/* the target's VBT backlight block (display-ref/vbt-decode.txt): PWM 200 Hz, min 6, controller 0; rawclk 19.2 MHz */
	c.vbt_backlight_present = 1; c.vbt_backlight_pwm_freq_hz = 200; c.vbt_backlight_min_brightness = 6; c.rawclk_khz = 19200;                /* DMC_FW_PIPEA */
	c.fb_width = 1920; c.fb_height = 1080; c.fb_pitch = 7680; c.fb_surf = 0xfdfc0000u;
	return c;
}

/* resident eDP up (as the probe leaves it), LCD state computed from what it read, models fresh */
static void bring_up(void)
{
	parity_lcd_dplls_reset();       /* a fresh device: its shared DPLLs are unused */
	parity_lcd_dbuf_forget();       /* and its DBUF / MBUS state is read from the hardware again */
	struct parity_edp_config cfg = vbt_cfg();
	int rc;

	dp_fake_init(&dpf, d000, d100, d700, edid, sizeof(edid));
	dp_fake_bind_env(&dpf, &env);
	rc = parity_edp_begin(&env, &cfg, &res);
	rc = rc == 0 ? parity_edp_init_late(&cfg, &res) : rc;
	if (rc != 0) { printf("eDP bring-up failed rc=%d\n", rc); exit(2); }
	rc = parity_lcd_compute(res.edid, res.dpcd, res.edp_dpcd, 18, 38400, &st);
	if (rc != 0) { printf("lcd compute failed rc=%d\n", rc); exit(2); }
	lcd_fake_init(&lcd, &dpf, 0, 0, 0, st.mode.vtotal);
	lcd.dbuf_size = 4096;
	parity_lcd_trace_init(&trace, &lcd.ops);
}

static const char *kind_name[] = { "?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put", "STEP", "ERROR", "PHASE", "decide", "dbuf", "observ" };

static void dump_trace(const char *title)
{
	unsigned i;

	printf("  ---- %s: %u entries (%u writes, %u rmw, %u waits [%u timed out], %u steps not ported, %u errors, dropped %u)\n",
	       title, trace.n, trace.writes, trace.rmws, trace.waits, trace.wait_timeouts, trace.steps, trace.errors, trace.dropped);
	for (i = 0; i < trace.n; i++) {
		const struct parity_lcd_trace_entry *e = &trace.e[i];

		if (e->kind == PARITY_LCD_T_READ && !verbose)
			continue;
		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->kind <= PARITY_LCD_T_DECIDED)
			printf("  t[%3u] %-6s %s", i, kind_name[e->kind], e->name);
		else
			printf("  t[%3u] %-6s 0x%05x b=0x%08x c=0x%08x d=0x%08x rc=%d n=%u%s%s\n", i, kind_name[e->kind], e->a, e->b, e->c, e->d,
			       e->rc, e->n, e->name ? " " : "", e->name ? e->name : "");
		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->kind <= PARITY_LCD_T_DECIDED && e->name[strlen(e->name) - 1] != '\n')
			printf("\n");
	}
}

static int all_released(const char *what)
{
	int ok, end;

	end = parity_edp_end(&res);
	dp_fake_flush_async(&dpf);
	ok = end == 0 && lcd_fake_power_refs_total(&lcd) == 0 && lcd.lock_held[0] == 0 && lcd.lock_held[1] == 0 &&
		dpf.refs_core == 0 && dpf.refs_aux == 0 && (dpf.pp_control & 9u) == 0;
	if (!ok)
		printf("  [%s] end=%d lcd refs=%d locks=%d/%d dp refs core=%d aux=%d pp_control=0x%x\n", what, end,
		       lcd_fake_power_refs_total(&lcd), lcd.lock_held[0], lcd.lock_held[1], dpf.refs_core, dpf.refs_aux, dpf.pp_control);
	return ok;
}

/* E-124: the OpRegion unit is kernel-only in this build; the boundary the readout calls (notify of a
 * sanitized encoder) answers as a firmware without the SWSCI mailbox does. */
int parity_opregion_notify_encoder(int port, int output_type, int enable);
int parity_opregion_notify_encoder(int port, int output_type, int enable)
{
	(void)port; (void)output_type; (void)enable;
	return 0;
}

/* E-124: the kernel log of the driver; the host tests print nothing */
void kern_logf(const char *fmt, ...);
void kern_logf(const char *fmt, ...)
{
	(void)fmt;
}

/* E-124: the diagnostic trace flag of the N1 readout (the kernel build owns it) */
int parity_lcd_reg_trace;

int main(int argc, char **argv)
{
	struct parity_lcd_modeset_cfg cfg;
	struct parity_lcd_modeset_status s;
	int rc;

	if (argc < 2) { fprintf(stderr, "usage: %s <display-ref dir> [-v]\n", argv[0]); return 2; }
	verbose = argc > 2;
	slurp(argv[1], "dpcd-drm_dp_aux0-000.bin", d000, 256);
	slurp(argv[1], "dpcd-drm_dp_aux0-100.bin", d100, 256);
	slurp(argv[1], "dpcd-drm_dp_aux0-700.bin", d700, 256);
	slurp(argv[1], "edid-eDP-1.bin", edid, 128);

	/* ================= A. normal ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	CHECK(rc == 0, "A: prepare");
	CHECK(trace.n == 0, "A: prepare touches nothing");
	parity_lcd_trace_phase(&trace, "commit: enable");
	rc = parity_lcd_modeset_commit_enable();
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	dump_trace("A: enable");
	printf("  A: enable rc=%d errors=%u first=%s | link %d x%d trained_flag=%d status %02x %02x %02x cr=%d eq=%d train_set %02x %02x | DP=0x%08x | pll on=%d mask=%d | wakerefs io=%d aux=%d\n",
	       rc, s.errors, s.first_error ? s.first_error : "-", s.link_rate, s.lane_count, s.link_trained_flag, s.link_status[0],
	       s.link_status[1], s.link_status[2], s.cr_ok, s.eq_ok, s.train_set[0], s.train_set[1], s.ddi_buf_ctl_value, s.pll_on,
	       s.pll_active_mask, s.ddi_io_wakeref, s.aux_wakeref);
	CHECK(rc == PARITY_LCD_MS_OK && s.errors == 0, "A: enable succeeds without a reference error");
	CHECK(s.crtc_active == 1 && s.cr_ok == 1 && s.eq_ok == 1 && (s.link_status[0] & 0x77) == 0x77 && (s.link_status[2] & 1) == 1,
	      "A: the SINK reports CR + EQ + symbol lock on both lanes and inter-lane alignment (not just the driver's flag)");
	CHECK(s.link_rate == 270000 && s.lane_count == 2 && s.ddi_buf_ctl_value == 0x80000002u,
	      "A: link 270000 x2; intel_dp->DP = 0x80000002 = Linux's DDI_BUF_CTL_A dump");
	CHECK(lcd_fake_reg(&lcd, 0x64000) == 0x80000002u && lcd_fake_reg(&lcd, 0x60400) == 0x8a210002u &&
	      lcd_fake_reg(&lcd, 0x46140) == 0x10000000u && (lcd_fake_reg(&lcd, 0x70008) & 0xc0000000u) == 0xc0000000u,
	      "A: registers as Linux left them: DDI_BUF_CTL_A, TRANS_DDI_FUNC_CTL_A, TRANS_CLK_SEL_A 0x10000000, TRANSCONF enabled + active");
	CHECK((lcd_fake_reg(&lcd, 0x46010) & 0xcc000000u) == 0xcc000000u && lcd_fake_reg(&lcd, 0x164284) == 0x00e001a5u &&
	      lcd_fake_reg(&lcd, 0x164288) == 0x00000088u,
	      "A: DPLL0 enable = power + enable + lock (dump 0xcc000000), CFGCR0 / CFGCR1 = Linux's dump");
	CHECK((lcd_fake_reg(&lcd, 0x164280) & 0x403u) == 0u, "A: DPCLKA_CFGCR0: DDI A takes DPLL0 and its clock is ungated");
	printf("  A: PIPE_MISC_A=0x%08x\n", lcd_fake_reg(&lcd, 0x70030));
	CHECK((lcd_fake_reg(&lcd, 0x70030) & 0xf0u) == 0x50u,
	      "A: PIPE_MISC: 6 bpc WITH dithering (the reference dithers exactly the 18 bpp pipes; Linux reports dither=yes for this one)");
	CHECK(s.pll_on == 1 && s.pll_active_mask == 1 && s.ddi_io_wakeref != 0 && s.aux_wakeref != 0 && lcd_fake_power_refs_total(&lcd) == 6,
	      "A: the enable owns: the PLL (pipe A), the DDI IO and the AUX power references, and the crtc its four domains");
	CHECK((dpf.pp_control & 1u) == 1u && (dpf.pp_control & 4u) == 4u, "A: panel power on and the PPS backlight-enable bit set (resident eDP's PPS code)");
	CHECK(lcd_fake_violations(&lcd) == 0, "A: no ordering violation seen by the model (PLL before DDI, DDI before pipe, panel power before training)");
	CHECK(s.backlight_present == 1 && s.backlight_enabled == 1 && s.backlight_pwm_max == 0x17700u && s.backlight_level == 0x17700u &&
	      lcd_fake_reg(&lcd, 0xc8254) == 0x17700u && lcd_fake_reg(&lcd, 0xc8258) == 0x17700u && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) != 0u,
	      "A: PWM backlight: 19.2 MHz / 200 Hz = 96000 = Linux's BLC_PWM_PCH_CTL2 dump (0x17700), duty = max, PWM enabled");
	{
		int pps = parity_lcd_trace_find(&trace, PARITY_LCD_T_PANEL, 0, 0, 0);
		int pll = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x46010, 0, 0);
		int clk = parity_lcd_trace_find(&trace, PARITY_LCD_T_RMW, 0x164280, 0, 0);
		int tp1 = parity_lcd_trace_find(&trace, PARITY_LCD_T_DPCD_WRITE, 0x102, 0, 0);
		int src = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x6001c, 0, 0);
		int mn = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x60030, 0, 0);
		int conf = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x70008, 0, (unsigned)(mn > 0 ? mn + 20 : 0));
		int bl = parity_lcd_trace_find(&trace, PARITY_LCD_T_PANEL, 4, 0, 0);

		CHECK(pll >= 0 && pll < pps && pps < clk && clk < tp1 && tp1 < src && src < mn && mn < conf && conf < bl,
		      "A: order from the run: PLL lock < panel power < DDI clock < training < PIPESRC < M/N < transcoder enable < PPS backlight");
	}
	CHECK(trace.dropped == 0, "A: the run log holds the whole commit");
	parity_lcd_modeset_status(&s);
	CHECK(rc == PARITY_LCD_MS_OK && s.plane_armed == 1 && lcd.plane_arms == 1 && lcd.plane_ctl_at_arm == 0x94000000u &&
	      lcd.plane_surf_at_arm == 0xfdfc0000u && lcd.plane_armed_without_pipe == 0,
	      "A: plane armed once on the running pipe with PLANE_CTL 0x94000000 and THIS buffer's GGTT address");
	CHECK(lcd_fake_reg(&lcd, 0x70188) == 0x78u && lcd_fake_reg(&lcd, 0x70190) == 0x0437077fu, "A: PLANE_STRIDE / PLANE_SIZE = Linux's dump");
	printf("  A: wm rc=%d ddb=[%u,%u) wm0 en=%d blocks=%u lines=%u slices=0x%x mbus_joined=%d | PLANE_WM_1_A_0=0x%08x BUF_CFG=0x%08x WM_TRANS=0x%08x WM_SAGV=0x%08x\n",
	       s.wm_rc, s.ddb_start, s.ddb_end, s.wm0_enable, s.wm0_blocks, s.wm0_lines, s.dbuf_slices_wanted, s.mbus_joined,
	       lcd_fake_reg(&lcd, 0x70240), lcd_fake_reg(&lcd, 0x7027c), lcd_fake_reg(&lcd, 0x70268), lcd_fake_reg(&lcd, 0x70258));
	CHECK(s.wm_rc == 0 && s.ddb_start == 0 && s.ddb_end == 4060 && lcd_fake_reg(&lcd, 0x7027c) == 0x0fdb0000u,
	      "A: DDB [0, 4060) of the joined 4096-block DBUF (the rest is the cursor's reserve); PLANE_BUF_CFG = end - 1 encoded = Linux's dump 0x0fdb0000");
	CHECK(s.wm0_enable == 1 && lcd_fake_reg(&lcd, 0x70240) == 0x80004010u,
	      "A: watermark level 0 from the target's latencies = Linux's dump 0x80004010 (enable, 1 line, 16 blocks)");
	CHECK(lcd.plane_armed_without_ddb == 0, "A: the plane was armed with a valid DDB range and an enabled level-0 watermark");
	{
		uint32_t f0 = lcd.ops.read32(lcd.ops.ctx, 0x70040), f1;

		lcd.ops.usleep(lcd.ops.ctx, 500000);
		f1 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		CHECK(f1 >= f0 + 29u, "A: the pipe's frame counter advances while the picture is up (the observation window)");
	}
	parity_lcd_trace_init(&trace, &lcd.ops);        /* a fresh log for the second commit (the backend keeps its state) */
	parity_lcd_trace_phase(&trace, "commit: disable");
	rc = parity_lcd_modeset_commit_disable();
	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_disarms == 1 && lcd_fake_reg(&lcd, 0x70180) == 0u, "A: plane disabled (PLANE_CTL 0, then the arming PLANE_SURF write)");
	parity_lcd_modeset_status(&s);
	CHECK(s.plane_armed == 1, "A: the modeset does NOT declare the buffer free on its own");
	parity_lcd_modeset_plane_released();            /* the caller saw the pipe stand still */
	dump_trace("A: plane update / disable / crtc disable");
	CHECK(rc == PARITY_LCD_MS_OK && s.errors == 0 && s.crtc_active == 0, "A: disable succeeds");
	CHECK((lcd_fake_reg(&lcd, 0x70008) & 0xc0000000u) == 0u && (lcd_fake_reg(&lcd, 0x64000) & 0x80000080u) == 0x80u &&
	      (lcd_fake_reg(&lcd, 0x46010) & 0xcc000000u) == 0u && (lcd_fake_reg(&lcd, 0x164280) & 0x400u) == 0x400u &&
	      (lcd_fake_reg(&lcd, 0x60400) & 0x80000000u) == 0u,
	      "A: read back: pipe off, DDI buffer off and idle, PLL off and unpowered, DDI clock gated, transcoder function off");
	CHECK(s.pll_on == 0 && s.pll_active_mask == 0 && s.ddi_io_wakeref == 0 && s.aux_wakeref == 0 && s.link_trained_flag == 0,
	      "A: ownership given back: PLL, DDI IO and AUX references; link_trained cleared");
	CHECK((dpf.pp_control & 5u) == 0u && dpf.dpcd[0x600] == 2u, "A: panel power and PPS backlight off; the sink was put to D3");
	CHECK((lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) == 0u && lcd_fake_reg(&lcd, 0xc8258) == 0u && s.backlight_enabled == 0,
	      "A: PWM backlight off: duty 0, PWM disabled");
	CHECK(lcd_fake_violations(&lcd) == 0, "A: no ordering violation on the way down (backlight, plane, pipe, DDI, PLL)");
	CHECK(all_released("A"), "A: after the eDP ends nothing is held anywhere (power refs, locks, VDD)");

	/* ================= B. early failures ================= */
	bring_up();
	lcd.fault_pll_no_lock = 1;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	printf("  B1: enable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && strstr(s.first_error, "not locked") != 0,
	      "B1: a PLL that does not lock is reported (first error names it); enable is NOT a success");
	CHECK(s.cr_ok == 0 && s.eq_ok == 0, "B1: and the sink indeed has no link");
	CHECK(s.link_trained_flag == 1, "B1: ... although intel_dp->link_trained was set by the stop function (why it is not used as evidence)");
	CHECK(s.first_error != 0 && trace.first_error_at >= 0 && trace.e[trace.first_error_at].name == s.first_error,
	      "B1: the run log's first ERROR entry is that error, at the position it happened");
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_status(&s);
	CHECK(s.crtc_active == 0 && s.pll_on == 0 && s.ddi_io_wakeref == 0 && s.aux_wakeref == 0 && lcd_fake_power_refs_total(&lcd) == 0,
	      "B1: the reference's disable gives back what the failed enable had taken");
	CHECK(all_released("B1"), "B1: nothing held after the eDP ends");

	bring_up();
	lcd.fault_cr_never = 1;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	printf("  B2: enable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.cr_ok == 0, "B2: the sink never reports clock recovery: enable fails");
	CHECK(parity_lcd_trace_find(&trace, PARITY_LCD_T_ERROR, 0, "FALLBACK", 0) >= 0,
	      "B2: the reference asked for the (unported) fallback: recorded as an error, no other rate / lane count was tried");
	printf("  B2: training-pattern writes=%u train_set=%02x %02x\n", lcd.training_pattern_writes, s.train_set[0], s.train_set[1]);
	CHECK(lcd.training_pattern_writes >= 2u && lcd.training_pattern_writes <= 12u && (s.train_set[0] & 3u) == 0u,
	      "B2: the reference re-tried the level the sink kept requesting a bounded number of times, then gave up (no endless loop, no invented level)");
	CHECK(parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x70008, 0, 0) >= 0 && (lcd_fake_reg(&lcd, 0x70008) & 0x80000000u) != 0u,
	      "B2: as in the reference, the pipe is still enabled after a failed training (the panel stays black) -- so the disable must run");
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_status(&s);
	CHECK(s.crtc_active == 0 && s.pll_on == 0 && lcd_fake_power_refs_total(&lcd) == 0 && (lcd_fake_reg(&lcd, 0x70008) & 0xc0000000u) == 0u,
	      "B2: the disable stops the pipe and gives everything back");
	CHECK(all_released("B2"), "B2: nothing held after the eDP ends");

	bring_up();
	lcd.sink_want_vswing = 2; lcd.sink_want_preemph = 1;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	CHECK(rc == PARITY_LCD_MS_OK && s.cr_ok && s.eq_ok && (s.train_set[0] & 3u) == 2u && ((s.train_set[0] >> 3) & 3u) == 1u,
	      "B3: a sink that asks for swing 2 / pre-emphasis 1 gets them (the levels come from the sink's answers at run time)");
	CHECK(lcd_fake_violations(&lcd) == 0, "B3: no violation");
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_plane_released();
	CHECK(rc == PARITY_LCD_MS_OK && all_released("B3"), "B3: disable + released");

	/* ================= what this path does not cover is refused before anything is touched ================= */
	bring_up();
	cfg = ms_cfg(); cfg.port = 3; cfg.aux_ch = 3;
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22 && trace.n == 0, "a Type-C port is refused, nothing touched");
	cfg = ms_cfg(); cfg.fb_modifier = 0x0100000000000002ull;
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22 && trace.n == 0, "a tiled framebuffer is refused, nothing touched");
	cfg = ms_cfg(); trace.ops.panel = 0;
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22, "a backend without the panel hook is refused (no half-connected run)");
	(void)parity_edp_end(&res);

	/* ================= the model really checks the DDB: lose the PLANE_BUF_CFG write ================= */
	bring_up();
	lcd.fault_drop_write_reg = 0x7027c;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_without_ddb == 1 && lcd_fake_violations(&lcd) == 1,
	      "model self-test: with the PLANE_BUF_CFG write lost, arming the plane is counted as a violation");
	(void)parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_plane_released();
	(void)parity_edp_end(&res);

	/* ================= C. trouble after the plane was armed ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	CHECK(rc == PARITY_LCD_MS_OK, "C: picture up");
	lcd.fault_pipe_stuck_on = 1;
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_status(&s);
	printf("  C: disable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0, "C: a pipe that does not stop is reported by the reference's wait; the disable is NOT a success");
	CHECK(s.plane_armed == 1, "C: the framebuffer stays marked as possibly scanned out: the caller must keep it (no forged cleanup)");
	CHECK(s.stop_unconfirmed == 1 && s.dc_off_held == 1 && s.crtc_domains_held == 4 && lcd.dbuf_enabled == 0x0f &&
	      (lcd_fake_reg(&lcd, 0x4438c) & 0x80000000u) != 0u && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 &&
	      lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && lcd.power_dropped_with_pipe_on == 0 && lcd.dbuf_shrunk_under_plane == 0,
	      "C: nothing was taken away from under the pipe: DC_OFF, the crtc's power domains, the DBUF slices and the MBUS joining all stay");
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "C: and a new modeset is refused while that is so");
	parity_lcd_modeset_abandoned();
	{
		struct parity_lcd_emit not_model = trace.ops;

		not_model.model = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16 && parity_lcd_modeset_commit_enable() == PARITY_LCD_MS_NOT_PREPARED,
		      "C: after abandon, a second call of the same entry is refused (prepare and commit)");
		parity_lcd_modeset_status(&s);
		CHECK(s.retained == 1 && s.stop_unconfirmed == 1 && s.dc_off_held == 1 && s.crtc_domains_held == 4 && s.plane_armed == 1 &&
		      lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 && lcd.dbuf_enabled == 0x0f,
		      "C: ... and the retained state is still recorded: nothing was forgotten by the refused calls");
		CHECK(parity_lcd_modeset_discard_model(&not_model) == -1 && parity_lcd_modeset_retained() == 1,
		      "C: a backend that is not a model (real hardware) cannot release it");
		CHECK(parity_lcd_modeset_discard_model(&trace.ops) == 0 && parity_lcd_modeset_retained() == 0,
		      "C: discarding the MODEL that holds it is the isolation: only then is the object free again");
	}
	(void)parity_edp_end(&res);

	/* ================= D. the commit's outer part ================= */
	{
		static struct parity_lcd_observer obs;
		uint32_t f0 = 0, f1 = 0;
		int dc, dom, mbus, dbuf1, dbox, pll, surf, dbuf2, put;
		unsigned i, n_dbuf = 0;

		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		parity_lcd_modeset_status(&s);
		printf("  D: cdclk: crtc min %d, bw min %d -> required %d kHz vco %d level %d, change needed %d | data rate %u MB/s" "\n", s.cdclk_crtc_min,
		       s.cdclk_bw_min, s.cdclk_required_khz, s.cdclk_required_vco, s.cdclk_required_level, s.cdclk_change_needed, s.bw_data_rate);
		CHECK(rc == 0 && s.cdclk_crtc_min == 70400 && s.cdclk_bw_min == 11000 && s.cdclk_required_khz == 179200 && s.cdclk_required_vco == 537600 &&
		      s.cdclk_required_level == 0 && s.cdclk_change_needed == 0,
		      "D: CDCLK the reference computes for this state (pixel rate / 2, plane, bandwidth -> first table entry at 38.4 MHz) = the current 179200 kHz: its no-change path");
		CHECK(s.bw_data_rate == 564, "D: memory bandwidth of the one XRGB plane: 140.8 MHz x 4 bytes = 564 MB/s, below the allowed QGV point");
		rc = parity_lcd_modeset_commit_enable();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.errors == 0 && lcd_fake_violations(&lcd) == 0, "D: the enable commit succeeds without a model violation");
		CHECK(s.dc_off_held == 0 && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 0 && lcd.async_puts == 1 && lcd.last_async_delay_ms == 17,
		      "D: DC_OFF was held around the whole commit and dropped at its end the reference's way (asynchronous, 17 ms)");
		CHECK(s.crtc_domains_held == 4 && lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && lcd.power_refs[POWER_DOMAIN_TRANSCODER_A] == 1 &&
		      lcd.power_refs[POWER_DOMAIN_PORT_DDI_LANES_A] == 1 && lcd.power_refs[POWER_DOMAIN_DISPLAY_CORE] == 1,
		      "D: get_crtc_power_domains(): pipe A, transcoder A, the encoder's DDI lanes, display core (shared DPLL) -- each held once");
		CHECK(lcd.power_refs[POWER_DOMAIN_PORT_DDI_IO_A] == 1 && lcd.power_refs[POWER_DOMAIN_AUX_IO_A] + lcd.power_refs[POWER_DOMAIN_AUX_A] == 1,
		      "D: next to them the encoder's own references (DDI IO, AUX): different owners, no double acquisition");
		CHECK(lcd.dbuf_enabled == 0x0f && s.dbuf_slices_now == 0x0f && s.mbus_joined_now == 1 &&
		      (lcd_fake_reg(&lcd, 0x4438c) & 0xfc000000u) == 0xdc000000u && (lcd_fake_reg(&lcd, 0x45008) & 0xc0070000u) == 0xc0030000u,
		      "D: DBUF: four slices on, MBUS joined (MBUS_CTL upper bits 0xdc = Linux's dump), tracker state service 3 (= dump's DBUF_CTL_S1)");
		printf("  D: PIPE_MBUS_DBOX_CTL_A=0x%08x MBUS_CTL=0x%08x DBUF_CTL_S1..4=0x%08x 0x%08x 0x%08x 0x%08x" "\n", lcd_fake_reg(&lcd, 0x7003c),
		       lcd_fake_reg(&lcd, 0x4438c), lcd_fake_reg(&lcd, 0x45008), lcd_fake_reg(&lcd, 0x44fe8), lcd_fake_reg(&lcd, 0x44300), lcd_fake_reg(&lcd, 0x44304));
		CHECK(lcd_fake_reg(&lcd, 0x7003c) == 0x01038806u, "D: MBUS DBOX credits of ADL-P with joined MBUS (A 6, BW 2, B 8, B2B 16 / delay 1 / regulate)");
		dc = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_GET, POWER_DOMAIN_DC_OFF, 0, 0);
		dom = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_GET, POWER_DOMAIN_PIPE_A, 0, 0);
		mbus = parity_lcd_trace_find(&trace, PARITY_LCD_T_RMW, 0x4438c, 0, 0);
		dbuf1 = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, 0);
		dbox = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x7003c, 0, 0);
		pll = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x46010, 0, 0);
		surf = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x7019c, 0, 0);
		dbuf2 = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, (unsigned)(surf > 0 ? surf : 0));
		put = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_PUT, POWER_DOMAIN_DC_OFF, 0, 0);
		for (i = 0; i < trace.n; i++)
			n_dbuf += trace.e[i].kind == PARITY_LCD_T_DBUF;
		CHECK(dc >= 0 && dc < dom && dom < mbus && mbus < dbuf1 && dbuf1 < dbox && dbox < pll && pll < surf && surf < dbuf2 && dbuf2 < put &&
		      n_dbuf == 2 && trace.e[put].d == 1u && trace.dropped == 0,
		      "D: order from the run: DC_OFF < crtc domains < MBUS_CTL < DBUF slices (old|new) < DBOX < PLL ... plane arm < DBUF slices (new) < DC_OFF put");
		CHECK(lcd.nobs == 5 && lcd.obs[0] == PARITY_LCD_OBS_COMMIT_BEGIN && lcd.obs[1] == PARITY_LCD_OBS_UNDERRUN_ARM &&
		      lcd.obs[2] == PARITY_LCD_OBS_PIPE_ENABLED && lcd.obs[3] == PARITY_LCD_OBS_PLANE_ARMED && lcd.obs[4] == PARITY_LCD_OBS_COMMIT_END,
		      "D: the observer is called at the commit's points, the underrun clear at the reference's position inside the crtc enable");
		rc = parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1);
		parity_lcd_observer_steady_begin(&obs);
		CHECK(rc == 0 && f1 - f0 >= 10u, "D: frames advance (from the frame counter)");
		rc = parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		CHECK(rc == 0 && obs.seen_transition == 0u && obs.seen_steady == 0u && obs.vblank_unmasked_seen == 0 && obs.dropped == 0,
		      "D: no underrun in either period; the pipe's vblank interrupt was masked at every sample");
		CHECK(obs.s[0].point == PARITY_LCD_OBS_COMMIT_BEGIN && obs.s[0].imr == 0u && obs.s[1].imr == 0xffffffffu,
		      "D: before the crtc holds the pipe's power domain its interrupt mask reads 0 (well off): recorded, not judged");
		parity_lcd_trace_init(&trace, &lcd.ops);
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.crtc_domains_held == 0 && s.dc_off_held == 0 && lcd_fake_power_refs_total(&lcd) == 0 &&
		      lcd.dbuf_enabled == 0x01 && s.dbuf_slices_now == 0x01 && s.mbus_joined_now == 0 && (lcd_fake_reg(&lcd, 0x4438c) & 0x80000000u) == 0u &&
		      lcd_fake_violations(&lcd) == 0,
		      "D: the disable commit: domains returned, DBUF back to slice 1 only AFTER the pipe stopped, MBUS un-joined, no violation");
		{
			int off = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x70008, 0, 0);
			int shrink = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x01, 0, 0);

			CHECK(off >= 0 && shrink > off && parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, 0) > off,
			      "D: order from the run: the pipe-off wait comes before both DBUF updates (old|new, then new)");
		}
		CHECK(parity_lcd_observer_stopped(&obs, 100, &f0, &f1) == 0, "D: after the stop the frame counter stands still");
		parity_lcd_modeset_plane_released();
		CHECK(all_released("D"), "D: nothing held after the eDP ends");

		/* a missing setting is noticed (the model really depends on the new parts) */
		bring_up();
		lcd.fault_drop_dbuf_update = 1;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_outside_slices == 1 && lcd_fake_violations(&lcd) == 1,
		      "model self-test: DBUF slice requests lost -> the plane's DDB lies in slices that are off: counted");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
		bring_up();
		lcd.fault_drop_write_reg = 0x4438c;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_outside_slices == 1 && lcd_fake_violations(&lcd) == 1,
		      "model self-test: MBUS_CTL write lost -> a DDB beyond the un-joined half: counted");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* what the commit cannot do is refused in the check phase */
		bring_up();
		cfg = ms_cfg(); cfg.cdclk_khz = 307200; cfg.cdclk_vco_khz = 614400;
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		parity_lcd_modeset_status(&s);
		CHECK(rc == -22 && trace.writes + trace.rmws == 0 && s.cdclk_change_needed == 1 && s.first_error != 0 && strstr(s.first_error, "CDCLK") != 0,
		      "a current CDCLK other than the required one (the reference would reprogram it) is refused with its reason, nothing touched");
		cfg = ms_cfg(); cfg.qgv_allowed_bw = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22 && trace.writes + trace.rmws == 0, "an unknown memory bandwidth is refused, nothing touched");
		cfg = ms_cfg(); trace.ops.dbuf_slices_update = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22, "a backend without the DBUF hook is refused");
		(void)parity_edp_end(&res);

		/* observation: progress is what the frame counter says, never elapsed time */
		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		lcd.fault_frame_counter_frozen = 1;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		{
			uint64_t t0 = dpf.now_us;

			CHECK(rc == PARITY_LCD_MS_OK && parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1) == -110 && f0 == f1 && dpf.now_us - t0 >= 500000u,
			      "observation: half a second passed, the frame counter did not move -> NOT progress");
		}
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* observation: an underrun while the pipe starts is recorded as such, and is not lost by the clear */
		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		lcd.fault_underrun_at_arm = 1;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		(void)parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1);
		parity_lcd_observer_steady_begin(&obs);
		(void)parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		CHECK(rc == PARITY_LCD_MS_OK && (obs.seen_transition & 0x80000000u) != 0u && obs.seen_steady == 0u && lcd_fake_reg(&lcd, 0x70058) == 0u,
		      "observation: an underrun at the plane arm is kept in the START record; the steady record stays clean; the status was cleared after recording");
		/* ... and one while the picture stands goes to the steady record */
		lcd.fault_underrun_steady_after = (int)lcd.frame_reads_after_arm + 2;
		(void)parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		(void)parity_lcd_modeset_commit_disable();
		CHECK((obs.seen_steady & 0x80000000u) != 0u && obs.n >= 8u && obs.dropped == 0,
		      "observation: an underrun during the steady picture is kept in the STEADY record (and survives the stop's samples)");
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
	}

	/* ================= F. brightness and backlight off / on while the picture stays up ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	CHECK(rc == PARITY_LCD_MS_OK && s.backlight_max == 96000u && s.backlight_min == (6u * 96000u + 127u) / 255u,
	      "F: backlight range from the reference: max = the PWM period (96000), min = VBT min_brightness 6 as a 0..255 coefficient of it (2259)");
	{
		uint32_t mx = s.backlight_max, mn = s.backlight_min, orig = s.backlight_level, f0, f1;
		unsigned arms = lcd.plane_arms, disarms = lcd.plane_disarms;

		rc = parity_lcd_modeset_brightness(mx, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mx, "F: user max -> duty = max");
		rc = parity_lcd_modeset_brightness(mx / 2u, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mn + (uint32_t)(((uint64_t)(mx / 2u) * (mx - mn) + mx / 2u) / mx),
		      "F: user half -> duty = min + half of (max - min) (scale_user_to_hw, rounded to closest)");
		rc = parity_lcd_modeset_brightness(0u, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mn && lcd_fake_reg(&lcd, 0xc8254) == mx,
		      "F: user 0 -> duty = the VBT-derived minimum, never 0; the PWM period is unchanged");
		{
			uint32_t half = lcd_fake_reg(&lcd, 0xc8258);

			(void)half;
		}
		rc = parity_lcd_modeset_brightness(mx / 2u, mx);
		f0 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_backlight(0) : rc;
		lcd.ops.usleep(lcd.ops.ctx, 200000);
		f1 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) == 0u && (dpf.pp_control & 4u) == 0u &&
		      s.backlight_enabled == 0 && s.crtc_active == 1 && s.plane_armed == 1 && f1 > f0 && lcd.plane_disarms == disarms,
		      "F: backlight off = PWM disabled + PPS backlight bit clear, while the pipe keeps scanning out the SAME buffer (still in use)");
		rc = parity_lcd_modeset_backlight(1);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) != 0u && (dpf.pp_control & 4u) == 4u &&
		      s.backlight_enabled == 1 && lcd_fake_reg(&lcd, 0xc8258) == mn + (uint32_t)(((uint64_t)(mx / 2u) * (mx - mn) + mx / 2u) / mx) &&
		      lcd.plane_arms == arms,
		      "F: backlight on again restores the last level (half); no new modeset, no plane re-arm");
		rc = parity_lcd_modeset_brightness(0u, mx);
		rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_backlight(0) : rc;
		rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_backlight(1) : rc;
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mx,
		      "F: ... but from the minimum it comes back at MAX (__intel_backlight_enable: level <= min -> max)");
		rc = parity_lcd_modeset_brightness(orig, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == orig, "F: back to the original level");
		{
			uint32_t d0, u0;

			rc = parity_lcd_modeset_brightness(30000u, mx);                /* start from a level that is not max */
			d0 = lcd_fake_reg(&lcd, 0xc8258);
			parity_lcd_modeset_status(&s);
			u0 = s.backlight_user;
			rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_brightness(mx, mx) : rc;
			rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_brightness(0u, mx) : rc;
			rc = rc == PARITY_LCD_MS_OK ? parity_lcd_modeset_brightness(u0, s.backlight_user_max) : rc;
			CHECK(rc == PARITY_LCD_MS_OK && u0 == 30000u && lcd_fake_reg(&lcd, 0xc8258) == d0 && d0 != 30000u,
			      "F: restore uses the saved USER level (30000 -> hw duty again), not the hw level fed back as a user level");
		}
		CHECK(lcd_fake_violations(&lcd) == 0, "F: no model violation");
	}
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_plane_released();
	CHECK(rc == PARITY_LCD_MS_OK && parity_lcd_modeset_brightness(1u, 2u) == PARITY_LCD_MS_NOT_PREPARED,
	      "F: after the stop, brightness requests are refused (no active crtc)");

	/* ================= G. the same resident eDP shows, stops, shows again (three cycles) ================= */
	{
		unsigned cyc, ok = 0;

		for (cyc = 0; cyc < 3u; cyc++) {
			cfg = ms_cfg();
			cfg.dbuf_enabled_slices = lcd.dbuf_enabled;             /* the shared state as the previous cycle left it */
			rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
			rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
			parity_lcd_modeset_status(&s);
			parity_lcd_modeset_link_status(&s);
			if (rc == PARITY_LCD_MS_OK && s.cr_ok && s.eq_ok && lcd.plane_surf_at_arm == 0xfdfc0000u)
				ok++;
			rc = parity_lcd_modeset_commit_disable();
			parity_lcd_modeset_plane_released();
			parity_lcd_modeset_status(&s);
			if (rc != PARITY_LCD_MS_OK || s.crtc_domains_held != 0u || lcd.dbuf_enabled != 0x01u || (dpf.pp_control & 1u) != 0u)
				break;
		}
		CHECK(ok == 3u && lcd_fake_violations(&lcd) == 0 && lcd.power_underflows == 0 && lcd_fake_power_refs_total(&lcd) == 0,
		      "G: three show / stop cycles on one resident eDP, each trained and armed, each stop returns everything, no violation");
		CHECK(all_released("G"), "G: nothing held after the eDP ends");
	}

	/* ================= H. the pipe's power cannot be released (interrupt drain failed): the stop is not confirmed ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	lcd.fault_put_refused_domain = POWER_DOMAIN_PIPE_A + 1;
	rc = rc == 0 ? parity_lcd_modeset_commit_disable() : -99;
	parity_lcd_modeset_status(&s);
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.stop_unconfirmed == 1 && s.dc_off_held == 1 && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 &&
	      lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && s.first_error != 0 && strstr(s.first_error, "kept") != 0,
	      "H: a pipe power release refused in the commit tail -> the disable is NOT a success, DC_OFF and the pipe's power stay held");
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "H: and nothing new starts on top of it");
	parity_lcd_modeset_abandoned();
	CHECK(parity_lcd_modeset_discard_model(&trace.ops) == 0, "H: (the model is discarded)");
	(void)parity_edp_end(&res);

	/* ================= I. the synchronous flip of the running picture ================= */
	{
		static const uint32_t seq[4] = { 0xfd000000u, 0xfdfc0000u, 0xfd000000u, 0xfdfc0000u };   /* B A B A */
		struct parity_lcd_flip_result fr;
		unsigned k, ok = 0u, arms0, outside0, ev0;

		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.surf_live == 0xfdfc0000u, "I: the picture is up on buffer A (live surface = A)");
		arms0 = lcd.plane_arms;
		outside0 = lcd.arm_outside_section;
		ev0 = lcd.events_done;
		for (k = 0u; k < 4u; k++) {
			uint32_t old = k == 0u ? 0xfdfc0000u : seq[k - 1u];

			rc = parity_lcd_modeset_flip(seq[k], &fr);
			parity_lcd_modeset_status(&s);
			if (rc == PARITY_LCD_MS_OK && fr.result == PARITY_LCD_FLIP_DONE && fr.gen == k + 1u && fr.old_surf == old &&
			    fr.live_before == old && fr.live_after == seq[k] && fr.frame_after > fr.frame_before && fr.event_rc == 0 &&
			    fr.update_errors == 0 && s.cur_surf == seq[k] && s.flip_pending == 0 && lcd.vblank_refs == 0 &&
			    lcd.plane_surf_at_arm == seq[k] && !lcd.irq_off)
				ok++;
			printf("  I: flip %u gen %u %08x -> %08x live %08x -> %08x frames %u -> %u event %d result %d" "\n", k + 1u, fr.gen,
			       fr.old_surf, fr.new_surf, fr.live_before, fr.live_after, fr.frame_before, fr.frame_after, fr.event_rc, fr.result);
		}
		CHECK(ok == 4u, "I: B, A, B, A: each flip completes only with its event AND the live surface = the new buffer; the old one is released");
		printf("  I: arms %u->%u outside %u->%u events %u->%u lock_errors %u irq_off_calls %u\n", arms0, lcd.plane_arms, outside0, lcd.arm_outside_section, ev0, lcd.events_done, lcd.lock_errors, lcd.irq_off_calls);
		CHECK(lcd.plane_arms == arms0 + 4u && lcd.arm_outside_section == outside0 && lcd.events_done == ev0 + 4u && lcd.lock_errors == 0u &&
		      lcd.irq_off_calls >= 4u,
		      "I: every arm happened inside the update section (interrupts off, balanced), one event per flip, vblank references returned");
		{
			unsigned arms1 = lcd.plane_arms;

			rc = parity_lcd_modeset_flip(0xfdfc0000u, &fr);
			CHECK(rc == PARITY_LCD_MS_NOT_PREPARED && fr.result == PARITY_LCD_FLIP_REFUSED && lcd.plane_arms == arms1,
			      "I: a flip to the buffer already shown is refused, nothing written");
		}
		/* an early (stale) completion: the event reports before any vblank -- the live surface is still the old buffer */
		lcd.fault_early_event = 1;
		rc = parity_lcd_modeset_flip(0xfd000000u, &fr);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_NOT_LATCHED && fr.live_after == 0xfdfc0000u &&
		      s.flip_pending == 1 && s.flip_stuck == 1 && s.cur_surf == 0xfdfc0000u && s.pend_surf == 0xfd000000u,
		      "I: STALE an event without the new surface live is NOT a completion: both buffers stay protected");
		lcd.fault_early_event = 0;
		{
			unsigned arms1 = lcd.plane_arms;

			CHECK(parity_lcd_modeset_flip(0xfdfc0000u, &fr) == PARITY_LCD_MS_NOT_PREPARED && lcd.plane_arms == arms1,
			      "I: STALE and no further flip is submitted while one is unresolved");
		}
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		parity_lcd_modeset_status(&s);
		printf("  I: stop rc=%d pending=%d stuck=%d violations=%u\n", rc, s.flip_pending, s.flip_stuck, lcd_fake_violations(&lcd));
		CHECK(rc == PARITY_LCD_MS_OK && s.flip_pending == 0 && s.flip_stuck == 0 && lcd_fake_violations(&lcd) == 0,
		      "I: the stop path still runs; once the display is stopped neither buffer is read any more");
		CHECK(all_released("I"), "I: nothing held after the eDP ends");

		/* the arm never reaches the live surface */
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		lcd.fault_flip_never_latch = 1;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_NOT_LATCHED && fr.event_rc == 0 && fr.live_after == 0xfdfc0000u,
		      "I: NOT-LIVE the register write alone (live surface unchanged after the vblank) is not a completion");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* no vblank arrives */
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		lcd.fault_no_vblank = 1;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_TIMEOUT && fr.event_rc == -110 && s.flip_stuck == 1 &&
		      lcd.vblank_refs == 1,
		      "I: TIMEOUT no completion within the limit: both kept, the event's vblank reference stays (it may still complete)");
		lcd.fault_no_vblank = 0;
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
	}

	/* ================= J. the event after a timeout, the stop, the relight; the evasion sleep ================= */
	{
		struct parity_lcd_flip_result fr;
		unsigned ev0, under0, refused0;
		int emin = 0, emax = 0, evbs = 0;

		/* J1: a flip times out; the stop settles its event once; after the relight the old event completes nothing */
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		under0 = lcd.power_underflows;
		lcd.fault_no_vblank = 1;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		parity_lcd_modeset_status(&s);
		CHECK(fr.result == PARITY_LCD_FLIP_TIMEOUT && s.flip_event_ref == 1 && lcd.vblank_refs == 1 && lcd.event_armed == 1,
		      "J1: TIMEOUT the armed event keeps its vblank reference (it may still complete)");
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.flip_event_ref == 0 && s.events_cancelled == 1u && lcd.events_cancelled >= 1u &&
		      lcd.vblank_refs == 0 && lcd.power_underflows == under0 && lcd.event_armed == 0,
		      "J1: the stop (intel_crtc_vblank_off) settles the event: cancelled, its reference returned exactly once (no underflow)");
		lcd.fault_no_vblank = 0;                        /* the late vblank arrives now */
		refused0 = lcd.waits_refused;
		CHECK(lcd.ops.wait_event(lcd.ops.ctx, 0, 100u) == -22 && lcd.waits_refused == refused0 + 1u,
		      "J1: a late completion of the cancelled event is refused (no event is armed any more)");
		(void)parity_edp_end(&res);
		bring_up();
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		ev0 = lcd.events_done;
		rc = rc == 0 ? parity_lcd_modeset_flip(0xfd000000u, &fr) : -99;
		parity_lcd_modeset_status(&s);
		printf("  J1: relight rc=%d result=%d events %u->%u ref=%d refs=%d cancelled ms=%u model=%u\n", rc, fr.result, ev0, lcd.events_done,
		       s.flip_event_ref, lcd.vblank_refs, s.events_cancelled, lcd.events_cancelled);
		CHECK(rc == PARITY_LCD_MS_OK && fr.result == PARITY_LCD_FLIP_DONE && lcd.events_done == ev0 + 1u && s.flip_event_ref == 0 &&
		      lcd.vblank_refs == 0 && s.events_cancelled == 0u,
		      "J1: after the relight the next flip needs (and gets) its own new event; the cancelled one was not counted twice");

		/* J2: the update starts inside the evasion window: it sleeps (IRQs on), wakes after the vblank, re-reads, arms */
		rc = parity_lcd_modeset_evade_window(&emin, &emax, &evbs);
		CHECK(rc == PARITY_LCD_MS_OK && emin > 0 && emax >= emin && emax < evbs,
		      "J2: the reference's evasion window of the running mode: scanlines [min, max] before vblank start");
		printf("  J: evasion window %d..%d, vblank start %d" "\n", emin, emax, evbs);
		lcd.vblank_sleeps = 0u;
		lcd.sleep_irq_off = 0u;
		lcd.scanline_hold = (uint32_t)emin;
		lcd.scanline_hold_reads = 1u;
		rc = parity_lcd_modeset_flip(0xfdfc0000u, &fr);
		CHECK(rc == PARITY_LCD_MS_OK && fr.result == PARITY_LCD_FLIP_DONE && lcd.vblank_sleeps == 1u && lcd.sleep_irq_off == 0u &&
		      !lcd.irq_off && lcd.lock_errors == 0u && lcd.vblank_refs == 0 && fr.update_errors == 0,
		      "J2: SLEEP inside the window: one sleep entered with IRQs ON, woke after the vblank, re-read the scanline, armed; "
		      "IRQ state and references restored");

		/* J3: inside the window and no vblank ever comes: a finite end, no fabricated completion, state restored */
		lcd.vblank_sleeps = 0u;
		lcd.scanline_hold = (uint32_t)emin;
		lcd.scanline_hold_reads = 1000u;
		lcd.fault_no_vblank = 1;
		rc = parity_lcd_modeset_flip(0xfd000000u, &fr);
		lcd.scanline_hold_reads = 0u;
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_ERRORS && fr.result == PARITY_LCD_FLIP_TIMEOUT && lcd.vblank_sleeps >= 1u && lcd.vblank_sleeps <= 3u &&
		      fr.update_errors > 0 && !lcd.irq_off && lcd.lock_errors == 0u && lcd.sleep_irq_off == 0u && lcd.vblank_refs == 1 &&
		      s.flip_event_ref == 1,
		      "J3: NO-VBLANK the evasion gives up after its timeout (reported as an update error), the flip is not completed, "
		      "IRQs restored; only the event's own reference is held");
		lcd.fault_no_vblank = 0;
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		parity_lcd_modeset_status(&s);
		printf("  J3: stop rc=%d refs=%d ref=%d cancelled ms=%u model=%u under %u/%u\n", rc, lcd.vblank_refs, s.flip_event_ref,
		       s.events_cancelled, lcd.events_cancelled, lcd.power_underflows, under0);
		CHECK(rc == PARITY_LCD_MS_OK && lcd.vblank_refs == 0 && s.flip_event_ref == 0 && s.events_cancelled == 1u && lcd.events_cancelled == 1u &&
		      lcd.power_underflows == under0,
		      "J3: the stop settles that event too: every reference returned once");
		(void)parity_edp_end(&res);
	}

	/* ================= E. a time-base fault is not a timeout ================= */
	bring_up();
	lcd.fault_time_base_reg = 0x46010;              /* the PLL-lock wait */
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
	parity_lcd_modeset_status(&s);
	{
		int w = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x46010, 0, 0);

		printf("  E: enable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
		CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && strstr(s.first_error, "time base") != 0 && w >= 0 && trace.e[w].rc == -5,
		      "E: the wait returns -EIO (not -ETIMEDOUT) and the time-base fault is the FIRST anomaly, ahead of the reference's own PLL error");
		CHECK(lcd.plane_arms == 0, "E: after that anomaly the plane was not armed");
	}
	lcd.fault_time_base_reg = 0u;                  /* a transient fault: the stop itself runs on a sound time base */
	rc = parity_lcd_modeset_commit_disable();
	parity_lcd_modeset_status(&s);
	printf("  E: disable rc=%d retained=%d stop_unconfirmed=%d crtc_active=%d\n", rc, s.retained, s.stop_unconfirmed, s.crtc_active);
	CHECK(rc == PARITY_LCD_MS_OK && s.retained == 0 && s.crtc_active == 0 && lcd_fake_power_refs_total(&lcd) == 0,
	      "E: with the time base sound again, the reference stop path returns everything");
	parity_lcd_modeset_plane_released();
	(void)parity_edp_end(&res);

	/* ================= dither is derived from the pipe bpp, not fixed ================= */
	{
		static struct parity_lcd_state st24;

		bring_up();
		cfg = ms_cfg();
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == 0, "dither: prepare at 18 bpp");
		parity_lcd_modeset_status(&s);
		CHECK(s.dither == 1, "dither: an 18 bpp pipe dithers (intel_modeset_pipe_config)");
		/* this panel's EDID caps the pipe at 6 bpc, so the computed state stays 18 bpp; the derivation is checked on a
		 * state that carries 24 bpp (the rest of the state is this panel's) */
		st24 = st;
		st24.link.bpp = 24;
		if (parity_lcd_modeset_prepare(&st24, &cfg, &trace.ops) == 0) {
			parity_lcd_modeset_status(&s);
			CHECK(s.dither == 0, "dither: a 24 bpp pipe does not");
		} else {
			CHECK(0, "dither: prepare at 24 bpp");
		}
		(void)parity_edp_end(&res);
	}

	printf("lcd_modeset_host_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
