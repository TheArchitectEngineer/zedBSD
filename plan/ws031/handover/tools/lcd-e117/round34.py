#!/usr/bin/env python3
"""WS031 E-117 round 34: brightness through the reference (intel_panel_set_backlight: user level -> [backlight.min,
backlight.max] -> PWM), backlight off / on without stopping the display (intel_edp_backlight_off / _on), and host
tests: brightness steps, backlight off with the scanout continuing, repeated display cycles on one resident eDP.
usage: round34.py <repo root>"""
import sys, json
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

j = json.load(open(root + J))
for f in j["new_files"]:
    if f["out"] == "intel_backlight_port.c":
        for n in ("intel_panel_actually_set_backlight", "scale_user_to_hw", "intel_panel_set_backlight"):
            if n not in f["functions"]:
                f["functions"].append(n)
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

g = load(L + "parity_backlight_glue.inc")
g = g.rstrip(NL) + NL + """
/*
 * The user-visible brightness: intel_backlight_device_update_status() -> intel_panel_set_backlight(conn_state,
 * brightness, max_brightness), with the backlight device's range [0, backlight.max] (intel_backlight_device_register).
 * The level is scaled to [backlight.min, backlight.max] -- backlight.min comes from the VBT's min_brightness, a
 * coefficient in 0..255 of the PWM range (get_backlight_min_vbt), not a count and not a percentage.
 */
void parity_lcd_ms_set_brightness(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max)
{
	parity_lcd_cur_i915 = &ms->i915;
	intel_panel_set_backlight(&ms->conn_state, user_level, user_max);
}

/* intel_edp_backlight_on() / _off(): the PWM and the panel's backlight-enable only -- the pipe keeps scanning out */
void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on)
{
	parity_lcd_cur_i915 = &ms->i915;
	if (on)
		intel_edp_backlight_on(&ms->crtc_state, &ms->conn_state);
	else
		intel_edp_backlight_off(&ms->conn_state);
}
"""
save(L + "parity_backlight_glue.inc", g)
h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);",
        "void parity_lcd_ms_set_brightness(struct parity_lcd_modeset *ms, u32 user_level, u32 user_max);     /* intel_backlight_port.c */" + NL +
        "void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on);" + NL +
        "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);")
save(L + "parity_lcd_modeset_int.h", h)
a = load(L + "parity_lcd_modeset.h")
a = rep(a, "int parity_lcd_modeset_commit_enable(void);", """int parity_lcd_modeset_commit_enable(void);
/*
 * While the picture is up (crtc active):
 *   _brightness(user_level, user_max)   the backlight device's update (user range [0, user_max])
 *   _backlight(on)                      backlight PWM + panel backlight-enable off / on; the pipe, the plane and the
 *                                       buffer stay in use -- this is NOT a display stop
 * PARITY_LCD_MS_OK, _NOT_PREPARED (no active crtc / retained), or _ERRORS (a reference error during the call).
 */
int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max);
int parity_lcd_modeset_backlight(int on);""")
a = rep(a, "	int dither;", "	int dither;" + NL + "	uint32_t backlight_min;         /* panel->backlight.min (hw units) */" + NL + "	uint32_t backlight_max;")
save(L + "parity_lcd_modeset.h", a)
r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	out->dither = ms.crtc_state.dither;", "	out->dither = ms.crtc_state.dither;" + NL +
        "	out->backlight_min = ms.connector.panel.backlight.min;" + NL + "	out->backlight_max = ms.connector.panel.backlight.max;")
r = r.rstrip(NL) + NL + """
int parity_lcd_modeset_brightness(uint32_t user_level, uint32_t user_max)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active || parity_lcd_modeset_retained() || user_max == 0u || user_level > user_max)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_ms_set_brightness(&ms, user_level, user_max);
	return ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}

int parity_lcd_modeset_backlight(int on)
{
	unsigned before = ms_errors;

	if (!ms.prepared || !ms.crtc.active || parity_lcd_modeset_retained())
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_ms_backlight_power(&ms, on);
	return ms_errors != before ? PARITY_LCD_MS_ERRORS : PARITY_LCD_MS_OK;
}
"""
save(L + "parity_lcd_modeset.c", r)

t = load(T)
t = rep(t, "	/* ================= E. a time-base fault is not a timeout ================= */",
        """	/* ================= F. brightness and backlight off / on while the picture stays up ================= */
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
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mn + (mx / 2u) * (mx - mn) / mx + ((((mx / 2u) * (mx - mn)) % mx) * 2u >= mx ? 1u : 0u),
		      "F: user half -> duty = min + half of (max - min) (scale_user_to_hw, rounded to closest)");
		rc = parity_lcd_modeset_brightness(0u, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == mn && lcd_fake_reg(&lcd, 0xc8254) == mx,
		      "F: user 0 -> duty = the VBT-derived minimum, never 0; the PWM period is unchanged");
		f0 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		rc = parity_lcd_modeset_backlight(0);
		lcd.ops.usleep(lcd.ops.ctx, 200000);
		f1 = lcd.ops.read32(lcd.ops.ctx, 0x70040);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) == 0u && (dpf.pp_control & 4u) == 0u &&
		      s.backlight_enabled == 0 && s.crtc_active == 1 && s.plane_armed == 1 && f1 > f0 && lcd.plane_disarms == disarms,
		      "F: backlight off = PWM disabled + PPS backlight bit clear, while the pipe keeps scanning out the SAME buffer (still in use)");
		rc = parity_lcd_modeset_backlight(1);
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && (lcd_fake_reg(&lcd, 0xc8250) & 0x80000000u) != 0u && (dpf.pp_control & 4u) == 4u &&
		      s.backlight_enabled == 1 && lcd_fake_reg(&lcd, 0xc8258) == mn && lcd.plane_arms == arms,
		      "F: backlight on again restores the last level; no new modeset, no plane re-arm");
		rc = parity_lcd_modeset_brightness(orig, mx);
		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == orig, "F: back to the original level");
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

	/* ================= E. a time-base fault is not a timeout ================= */""")
save(T, t)
print("done")
