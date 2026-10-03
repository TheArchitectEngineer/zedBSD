#!/usr/bin/env python3
"""WS031 E-115 round 10: backlight glue + modeset cfg / runner hookup.  usage: round10.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = load(L + "lcd_compat.h")
c = rep(c, "struct drm_device { int unused; };", "struct drm_device { int unused; int switch_power_state; };")
save(L + "lcd_compat.h", c)

open(root + L + "parity_backlight_glue.inc", "w").write("""/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_backlight_port.c.
 *
 * What intel_backlight_setup() / intel_backlight_init_funcs() do for a native-PWM eDP panel on a CNP+ PCH:
 * pwm_funcs = cnp_pwm_funcs, funcs = pwm_bl_funcs (the two tables below repeat the reference's member
 * assignments for the members this path uses), then funcs->setup() -- which READS the PWM registers and
 * derives max / min / level -- and backlight.present = true.  Without a VBT backlight block
 * (vbt.backlight.present == false) the reference sets nothing up and enable / disable return at once.
 */
#include "parity_lcd_modeset_int.h"

static const struct intel_panel_bl_funcs parity_cnp_pwm_funcs = {
	.setup = cnp_setup_backlight,
	.enable = cnp_enable_backlight,
	.disable = cnp_disable_backlight,
	.set = bxt_set_backlight,
	.get = bxt_get_backlight,
	.hz_to_pwm = cnp_hz_to_pwm,
};

static const struct intel_panel_bl_funcs parity_pwm_bl_funcs = {
	.setup = intel_pwm_setup_backlight,
	.enable = intel_pwm_enable_backlight,
	.disable = intel_pwm_disable_backlight,
	.set = intel_pwm_set_backlight,
	.get = intel_pwm_get_backlight,
};

int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms)
{
	struct intel_panel *panel = &ms->connector.panel;
	int ret;

	if (!panel->vbt.backlight.present)
		return 0;
	panel->backlight.pwm_funcs = &parity_cnp_pwm_funcs;
	panel->backlight.funcs = &parity_pwm_bl_funcs;
	mutex_lock(&ms->i915.display.backlight.lock);
	ret = panel->backlight.funcs->setup(&ms->connector, ms->crtc.pipe);
	mutex_unlock(&ms->i915.display.backlight.lock);
	if (ret != 0)
		return ret;
	panel->backlight.present = true;
	return 0;
}
""")
print("wrote parity_backlight_glue.inc")

h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms);",
        "int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms);                      /* intel_backlight_port.c */" + NL +
        "void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms);")
h = rep(h, "	int plane_armed;", "	int backlight_setup_rc;                 /* intel_backlight_setup(): 0, or a negative errno */" + NL + "	int plane_armed;")
save(L + "parity_lcd_modeset_int.h", h)

a = load(L + "parity_lcd_modeset.h")
a = rep(a, "	uint32_t dmc_fw_mask;", """	/* the VBT backlight block of the panel, and the raw clock the PCH PWM divides (kHz, read out by the caller) */
	int vbt_backlight_present, vbt_backlight_active_low, vbt_backlight_controller;
	uint16_t vbt_backlight_pwm_freq_hz;
	uint8_t vbt_backlight_min_brightness;
	uint32_t rawclk_khz;
	uint32_t dmc_fw_mask;""")
a = rep(a, "	/* ownership */", "	/* backlight (PWM): what intel_backlight_setup() derived and whether it is on */" + NL +
        "	int backlight_present, backlight_enabled, backlight_setup_rc;" + NL +
        "	uint32_t backlight_pwm_max, backlight_level;" + NL + "	/* ownership */")
save(L + "parity_lcd_modeset.h", a)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	ms.connector.panel.vbt.edp.hobl = cfg->vbt_hobl != 0;" + NL,
        "	ms.connector.panel.vbt.edp.hobl = cfg->vbt_hobl != 0;" + NL +
        "	ms.connector.base.dev = &ms.i915.drm;" + NL +
        "	ms.connector.panel.vbt.backlight.present = cfg->vbt_backlight_present != 0;" + NL +
        "	ms.connector.panel.vbt.backlight.active_low_pwm = cfg->vbt_backlight_active_low != 0;" + NL +
        "	ms.connector.panel.vbt.backlight.controller = (s8)cfg->vbt_backlight_controller;" + NL +
        "	ms.connector.panel.vbt.backlight.pwm_freq_hz = cfg->vbt_backlight_pwm_freq_hz;" + NL +
        "	ms.connector.panel.vbt.backlight.min_brightness = cfg->vbt_backlight_min_brightness;" + NL +
        "	ms.i915.display.runtime.rawclk_freq = cfg->rawclk_khz;" + NL)
r = rep(r, "	parity_lcd_ms_crtc_enable(&ms);" + NL + "	if (ms_errors != 0u)",
        "	/* connector-init work of the reference that touches the hardware (reads only): the backlight setup */" + NL +
        "	parity_lcd_cur_i915 = &ms.i915;" + NL +
        "	ms.backlight_setup_rc = parity_lcd_ms_backlight_setup(&ms);" + NL +
        "	if (ms.backlight_setup_rc != 0)" + NL +
        "		on_error(0, \"intel_backlight_setup failed (no PWM frequency from the hardware or the VBT)\\n\");" + NL +
        "	parity_lcd_ms_crtc_enable(&ms);" + NL + "	if (ms_errors != 0u)")
r = rep(r, "	out->errors = ms_errors;", "	out->backlight_present = ms.connector.panel.backlight.present;" + NL +
        "	out->backlight_enabled = ms.connector.panel.backlight.enabled;" + NL +
        "	out->backlight_setup_rc = ms.backlight_setup_rc;" + NL +
        "	out->backlight_pwm_max = ms.connector.panel.backlight.pwm_level_max;" + NL +
        "	out->backlight_level = ms.connector.panel.backlight.level;" + NL + "	out->errors = ms_errors;")
save(L + "parity_lcd_modeset.c", r)

t = load("plan/ws031/tests/lcd-modeset-host-test.c")
t = rep(t, "	c.dmc_fw_mask = 1u << 1;", "	c.dmc_fw_mask = 1u << 1;" + NL +
        "	/* the target's VBT backlight block (display-ref/vbt-decode.txt): PWM 200 Hz, min 6, controller 0; rawclk 19.2 MHz */" + NL +
        "	c.vbt_backlight_present = 1; c.vbt_backlight_pwm_freq_hz = 200; c.vbt_backlight_min_brightness = 6; c.rawclk_khz = 19200;")
t = rep(t, '	CHECK(lcd_fake_violations(&lcd) == 0, "A: no ordering violation seen by the model (PLL before DDI, DDI before pipe, panel power before training)");',
        '	CHECK(lcd_fake_violations(&lcd) == 0, "A: no ordering violation seen by the model (PLL before DDI, DDI before pipe, panel power before training)");' + NL +
        '	CHECK(s.backlight_present == 1 && s.backlight_enabled == 1 && s.backlight_pwm_max == 0x17700u && s.backlight_level == 0x17700u &&' + NL +
        '	      lcd_fake_reg(&lcd, 0xc8254) == 0x17700u && lcd_fake_reg(&lcd, 0xc8258) == 0x17700u && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) != 0u,' + NL +
        '	      "A: PWM backlight: 19.2 MHz / 200 Hz = 96000 = Linux\'s BLC_PWM_PCH_CTL2 dump (0x17700), duty = max, PWM enabled");')
t = rep(t, '	CHECK((dpf.pp_control & 5u) == 0u && dpf.dpcd[0x600] == 2u, "A: panel power and PPS backlight off; the sink was put to D3");',
        '	CHECK((dpf.pp_control & 5u) == 0u && dpf.dpcd[0x600] == 2u, "A: panel power and PPS backlight off; the sink was put to D3");' + NL +
        '	CHECK((lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) == 0u && lcd_fake_reg(&lcd, 0xc8258) == 0u && s.backlight_enabled == 0,' + NL +
        '	      "A: PWM backlight off: duty 0, PWM disabled");')
save("plan/ws031/tests/lcd-modeset-host-test.c", t)
print("done")
