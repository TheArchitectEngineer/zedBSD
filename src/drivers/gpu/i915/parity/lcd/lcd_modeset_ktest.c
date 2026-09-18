/*
 * WS031 Linux-parity — GPU-free kernel test of the one-screen LCD modeset: the reference's enable /
 * plane / disable callers and callees on the register / sink models, with the resident eDP's own PPS
 * and AUX code underneath (the same production objects as on hardware; only the ops backend differs).
 * zedBSD project code.  The host fixture (plan/ws031/tests/lcd-modeset-host-test.c) has the long
 * version; this one keeps the two ownership stories in the kernel regression:
 *   A. picture up -> frames advance -> plane off -> disable -> everything given back
 *   C. the pipe does not stop after the plane was armed -> no success, the buffer stays protected
 */
#include "lcd_modeset_ktest.h"
#include "parity_lcd_modeset.h"
#include "parity_lcd_trace.h"
#include "lcd_fake_hw.h"
#include "../dp/parity_edp.h"
#include "../dp/dp_fake_hw.h"
#include "../dp/dp_fixture_latitude5330.h"
#include <string.h>

static struct dp_fake_hw dpf;
static struct parity_dp_env env;
static struct parity_edp_result res;
static struct lcd_fake_hw lcd;
static struct parity_lcd_trace trace;
static struct parity_lcd_state st;
static struct parity_lcd_modeset_cfg cfg;
static struct parity_lcd_modeset_status s;

static int bring_up(void)
{
	struct parity_edp_config c;
	int rc;

	memset(&c, 0, sizeof(c));
	c.rawclk_khz = 19200;
	c.t1_t3 = 2000; c.t8 = 800; c.t9 = 2000; c.t10 = 1100; c.t11_t12 = 5000;
	c.log_level = -1;
	dp_fake_init(&dpf, dp_fixture_dpcd_000, dp_fixture_dpcd_100, dp_fixture_dpcd_700, dp_fixture_edid, 128u);
	dp_fake_bind_env(&dpf, &env);
	rc = parity_edp_begin(&env, &c, &res);
	if (rc == 0)
		rc = parity_edp_init_late(&c, &res);
	if (rc == 0)
		rc = parity_lcd_compute(res.edid, res.dpcd, res.edp_dpcd, 18, 38400, &st);
	if (rc != 0)
		return rc;
	lcd_fake_init(&lcd, &dpf, 0, 0, 0, st.mode.vtotal);
	parity_lcd_trace_init(&trace, &lcd.ops);
	memset(&cfg, 0, sizeof(cfg));
	memcpy(cfg.dpcd, res.dpcd, sizeof(cfg.dpcd));
	memcpy(cfg.edp_dpcd, res.edp_dpcd, sizeof(cfg.edp_dpcd));
	cfg.fb_fourcc = 0x34325258u;
	cfg.fb_width = 1920u; cfg.fb_height = 1080u; cfg.fb_pitch = 7680u; cfg.fb_surf = 0xfdfc0000u;
	cfg.dmc_fw_mask = 1u << 1;
	cfg.vbt_backlight_present = 1; cfg.vbt_backlight_pwm_freq_hz = 200; cfg.vbt_backlight_min_brightness = 6;
	cfg.rawclk_khz = 19200u;
	return 0;
}

static int released(void)
{
	int end = parity_edp_end(&res);

	dp_fake_flush_async(&dpf);
	return end == 0 && lcd_fake_power_refs_total(&lcd) == 0 && lcd.lock_held[0] == 0 && lcd.lock_held[1] == 0 &&
		dpf.refs_core == 0 && dpf.refs_aux == 0 && (dpf.pp_control & 9u) == 0u;
}

void parity_lcd_modeset_ktest(parity_lcd_modeset_ktest_check check)
{
	uint32_t f0, f1;
	int rc;

	/* ---- A ---- */
	rc = bring_up();
	rc = rc == 0 ? parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) : rc;
	check(rc == 0 && trace.n == 0u, "lcd-ms: A-PREPARE the state is built without touching anything");
	rc = parity_lcd_modeset_enable();
	parity_lcd_modeset_status(&s);
	parity_lcd_modeset_link_status(&s);
	check(rc == PARITY_LCD_MS_OK && s.errors == 0u && s.cr_ok && s.eq_ok && (s.link_status[0] & 0x77u) == 0x77u &&
		s.ddi_buf_ctl_value == 0x80000002u && lcd_fake_reg(&lcd, 0x60400u) == 0x8a210002u &&
		lcd_fake_reg(&lcd, 0x46140u) == 0x10000000u && lcd_fake_reg(&lcd, 0xc8254u) == 0x17700u,
		"lcd-ms: A-ENABLE the sink reports CR + EQ + lock; DDI_BUF_CTL / TRANS_DDI_FUNC_CTL / TRANS_CLK_SEL / PWM = Linux's dump");
	check(lcd_fake_violations(&lcd) == 0u && s.pll_on && s.ddi_io_wakeref != 0 && s.aux_wakeref != 0 &&
		lcd_fake_power_refs_total(&lcd) == 2,
		"lcd-ms: A-ORDER no ordering violation seen by the model; the enable owns the PLL and two power references");
	rc = parity_lcd_modeset_plane_update();
	f0 = lcd.ops.read32(lcd.ops.ctx, 0x70040u);
	lcd.ops.usleep(lcd.ops.ctx, 500000u);
	f1 = lcd.ops.read32(lcd.ops.ctx, 0x70040u);
	check(rc == PARITY_LCD_MS_OK && lcd.plane_arms == 1u && lcd.plane_ctl_at_arm == 0x94000000u &&
		lcd.plane_surf_at_arm == 0xfdfc0000u && f1 >= f0 + 29u,
		"lcd-ms: A-PLANE armed once on the running pipe with this buffer's address; frames advance");
	rc = parity_lcd_modeset_plane_disable();
	parity_lcd_modeset_status(&s);
	check(rc == PARITY_LCD_MS_OK && s.plane_armed == 1, "lcd-ms: A-PLANE-OFF the modeset does not declare the buffer free on its own");
	parity_lcd_modeset_plane_released();
	rc = parity_lcd_modeset_disable();
	parity_lcd_modeset_status(&s);
	check(rc == PARITY_LCD_MS_OK && s.errors == 0u && !s.crtc_active && !s.pll_on && s.ddi_io_wakeref == 0 &&
		s.aux_wakeref == 0 && (lcd_fake_reg(&lcd, 0x70008u) & 0xc0000000u) == 0u &&
		(lcd_fake_reg(&lcd, 0x46010u) & 0xcc000000u) == 0u && (dpf.pp_control & 5u) == 0u && lcd_fake_violations(&lcd) == 0u,
		"lcd-ms: A-DISABLE the reference's disable: pipe / DDI / PLL / panel / backlight off, references returned, no violation");
	check(released(), "lcd-ms: A-RELEASED nothing is held after the eDP ends");

	/* ---- C ---- */
	rc = bring_up();
	rc = rc == 0 ? parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) : rc;
	rc = rc == 0 ? parity_lcd_modeset_enable() : rc;
	rc = rc == 0 ? parity_lcd_modeset_plane_update() : rc;
	lcd.fault_pipe_stuck_on = 1;
	(void)parity_lcd_modeset_plane_disable();
	rc = rc == 0 ? parity_lcd_modeset_disable() : -99;
	parity_lcd_modeset_status(&s);
	check(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && s.plane_armed == 1 &&
		parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16,
		"lcd-ms: C-STUCK a pipe that does not stop: no success, the buffer stays marked in use, a new modeset is refused");
	(void)parity_edp_end(&res);
	/* the caller decided what to do with that buffer (kept for ever): the object may be used again */
	parity_lcd_modeset_plane_released();
}
