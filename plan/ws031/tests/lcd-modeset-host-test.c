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
	c.dmc_fw_mask = 1u << 1;
	/* the target's VBT backlight block (display-ref/vbt-decode.txt): PWM 200 Hz, min 6, controller 0; rawclk 19.2 MHz */
	c.vbt_backlight_present = 1; c.vbt_backlight_pwm_freq_hz = 200; c.vbt_backlight_min_brightness = 6; c.rawclk_khz = 19200;                /* DMC_FW_PIPEA */
	c.fb_width = 1920; c.fb_height = 1080; c.fb_pitch = 7680; c.fb_surf = 0xfdfc0000u;
	return c;
}

/* resident eDP up (as the probe leaves it), LCD state computed from what it read, models fresh */
static void bring_up(void)
{
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
	parity_lcd_trace_init(&trace, &lcd.ops);
}

static const char *kind_name[] = { "?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put", "STEP", "ERROR", "PHASE", "decide" };

static void dump_trace(const char *title)
{
	unsigned i;

	printf("  ---- %s: %u entries (%u writes, %u rmw, %u waits [%u timed out], %u steps not ported, %u errors, dropped %u)\n",
	       title, trace.n, trace.writes, trace.rmws, trace.waits, trace.wait_timeouts, trace.steps, trace.errors, trace.dropped);
	for (i = 0; i < trace.n; i++) {
		const struct parity_lcd_trace_entry *e = &trace.e[i];

		if (e->kind == PARITY_LCD_T_READ && !verbose)
			continue;
		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP)
			printf("  t[%3u] %-6s %s", i, kind_name[e->kind], e->name);
		else
			printf("  t[%3u] %-6s 0x%05x b=0x%08x c=0x%08x d=0x%08x rc=%d n=%u%s%s\n", i, kind_name[e->kind], e->a, e->b, e->c, e->d,
			       e->rc, e->n, e->name ? " " : "", e->name ? e->name : "");
		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->name[strlen(e->name) - 1] != '\n')
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
	parity_lcd_trace_phase(&trace, "crtc enable");
	rc = parity_lcd_modeset_enable();
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
	CHECK(s.pll_on == 1 && s.pll_active_mask == 1 && s.ddi_io_wakeref != 0 && s.aux_wakeref != 0 && lcd_fake_power_refs_total(&lcd) == 2,
	      "A: the enable owns: the PLL (pipe A), the DDI IO and the AUX power references");
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
	parity_lcd_trace_init(&trace, &lcd.ops);        /* a fresh log for the plane phase (the backend keeps its state) */
	rc = parity_lcd_modeset_plane_update();
	parity_lcd_modeset_status(&s);
	CHECK(rc == PARITY_LCD_MS_OK && s.plane_armed == 1 && lcd.plane_arms == 1 && lcd.plane_ctl_at_arm == 0x94000000u &&
	      lcd.plane_surf_at_arm == 0xfdfc0000u && lcd.plane_armed_without_pipe == 0,
	      "A: plane armed once on the running pipe with PLANE_CTL 0x94000000 and THIS buffer's GGTT address");
	CHECK(lcd_fake_reg(&lcd, 0x70188) == 0x78u && lcd_fake_reg(&lcd, 0x70190) == 0x0437077fu, "A: PLANE_STRIDE / PLANE_SIZE = Linux's dump");
	{
		uint32_t f0 = lcd.ops.read32(lcd.ops.ctx, 0x70040), f1;

		lcd.ops.usleep(lcd.ops.ctx, 500000);
		f1 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		CHECK(f1 >= f0 + 29u, "A: the pipe's frame counter advances while the picture is up (the observation window)");
	}
	rc = parity_lcd_modeset_plane_disable();
	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_disarms == 1 && lcd_fake_reg(&lcd, 0x70180) == 0u, "A: plane disabled (PLANE_CTL 0, then the arming PLANE_SURF write)");
	parity_lcd_modeset_status(&s);
	CHECK(s.plane_armed == 1, "A: the modeset does NOT declare the buffer free on its own");
	parity_lcd_modeset_plane_released();            /* the caller saw a further frame pass with the plane off */
	parity_lcd_trace_phase(&trace, "crtc disable");
	rc = parity_lcd_modeset_disable();
	parity_lcd_modeset_status(&s);
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
	rc = rc == 0 ? parity_lcd_modeset_enable() : -99;
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	printf("  B1: enable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && strstr(s.first_error, "not locked") != 0,
	      "B1: a PLL that does not lock is reported (first error names it); enable is NOT a success");
	CHECK(s.cr_ok == 0 && s.eq_ok == 0, "B1: and the sink indeed has no link");
	CHECK(s.link_trained_flag == 1, "B1: ... although intel_dp->link_trained was set by the stop function (why it is not used as evidence)");
	CHECK(s.first_error != 0 && trace.first_error_at >= 0 && trace.e[trace.first_error_at].name == s.first_error,
	      "B1: the run log's first ERROR entry is that error, at the position it happened");
	rc = parity_lcd_modeset_disable();
	parity_lcd_modeset_status(&s);
	CHECK(s.crtc_active == 0 && s.pll_on == 0 && s.ddi_io_wakeref == 0 && s.aux_wakeref == 0 && lcd_fake_power_refs_total(&lcd) == 0,
	      "B1: the reference's disable gives back what the failed enable had taken");
	CHECK(all_released("B1"), "B1: nothing held after the eDP ends");

	bring_up();
	lcd.fault_cr_never = 1;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_enable() : -99;
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
	rc = parity_lcd_modeset_disable();
	parity_lcd_modeset_status(&s);
	CHECK(s.crtc_active == 0 && s.pll_on == 0 && lcd_fake_power_refs_total(&lcd) == 0 && (lcd_fake_reg(&lcd, 0x70008) & 0xc0000000u) == 0u,
	      "B2: the disable stops the pipe and gives everything back");
	CHECK(all_released("B2"), "B2: nothing held after the eDP ends");

	bring_up();
	lcd.sink_want_vswing = 2; lcd.sink_want_preemph = 1;
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_enable() : -99;
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	CHECK(rc == PARITY_LCD_MS_OK && s.cr_ok && s.eq_ok && (s.train_set[0] & 3u) == 2u && ((s.train_set[0] >> 3) & 3u) == 1u,
	      "B3: a sink that asks for swing 2 / pre-emphasis 1 gets them (the levels come from the sink's answers at run time)");
	CHECK(lcd_fake_violations(&lcd) == 0, "B3: no violation");
	rc = parity_lcd_modeset_disable();
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

	/* ================= C. trouble after the plane was armed ================= */
	bring_up();
	cfg = ms_cfg();
	rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
	rc = rc == 0 ? parity_lcd_modeset_enable() : -99;
	rc = rc == 0 ? parity_lcd_modeset_plane_update() : rc;
	CHECK(rc == PARITY_LCD_MS_OK, "C: picture up");
	lcd.fault_pipe_stuck_on = 1;
	(void)parity_lcd_modeset_plane_disable();
	rc = parity_lcd_modeset_disable();
	parity_lcd_modeset_status(&s);
	printf("  C: disable rc=%d errors=%u first=%s", rc, s.errors, s.first_error ? s.first_error : "-\n");
	CHECK(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0, "C: a pipe that does not stop is reported by the reference's wait; the disable is NOT a success");
	CHECK(s.plane_armed == 1, "C: the framebuffer stays marked as possibly scanned out: the caller must keep it (no forged cleanup)");
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "C: and a new modeset is refused while that is so");
	(void)parity_edp_end(&res);

	printf("lcd_modeset_host_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
