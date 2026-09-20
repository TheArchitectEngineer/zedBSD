#!/usr/bin/env python3
"""WS031 E-118 round 39: (1) the "no vblank after masking" check takes its baseline after the mask is read back and the
pipe's in-flight handler work has drained; (2) brightness is saved / restored in USER units (the backlight device's
brightness, kept like props.brightness) and the real-machine steps judge DUTY / FREQ / PWM enable against the reference
conversion.  usage: round39.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ---- modeset: the backlight device's user brightness
g = load(L + "parity_backlight_glue.inc")
g = g.rstrip(NL) + NL + """
/* the backlight device's brightness for the current hardware level (intel_backlight_device_register / __intel_backlight_enable) */
u32 parity_lcd_ms_user_level(struct parity_lcd_modeset *ms, u32 user_max)
{
	return scale_hw_to_user(&ms->connector, ms->connector.panel.backlight.level, user_max);
}
"""
save(L + "parity_backlight_glue.inc", g)
h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on);",
        "void parity_lcd_ms_backlight_power(struct parity_lcd_modeset *ms, int on);" + NL +
        "u32 parity_lcd_ms_user_level(struct parity_lcd_modeset *ms, u32 user_max);")
h = rep(h, "	int dc_off_held; int dc_off_wakeref;", "	int dc_off_held; int dc_off_wakeref;" + NL +
        "	u32 bl_user, bl_user_max;                /* the backlight device's props.brightness / max_brightness */")
save(L + "parity_lcd_modeset_int.h", h)
a = load(L + "parity_lcd_modeset.h")
a = rep(a, "	uint32_t backlight_max;", "	uint32_t backlight_max;" + NL + "	uint32_t backlight_user, backlight_user_max;   /* the user brightness (restore THIS, not the hw level) */")
save(L + "parity_lcd_modeset.h", a)
r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	out->backlight_max = ms.connector.panel.backlight.max;", "	out->backlight_max = ms.connector.panel.backlight.max;" + NL +
        "	out->backlight_user = ms.bl_user;" + NL + "	out->backlight_user_max = ms.bl_user_max;")
r = rep(r, "	parity_lcd_ms_crtc_enable(&ms);" + NL, "	parity_lcd_ms_crtc_enable(&ms);" + NL +
        "	/* intel_backlight_device_register(): max_brightness = backlight.max, brightness = the level scaled to it */" + NL +
        "	ms.bl_user_max = ms.connector.panel.backlight.max;" + NL +
        "	ms.bl_user = ms.bl_user_max != 0u ? parity_lcd_ms_user_level(&ms, ms.bl_user_max) : 0u;" + NL)
r = rep(r, "	parity_lcd_ms_set_brightness(&ms, user_level, user_max);" + NL,
        "	parity_lcd_ms_set_brightness(&ms, user_level, user_max);" + NL +
        "	if (user_max == ms.bl_user_max)" + NL + "		ms.bl_user = user_level;" + NL +
        "	else" + NL + "		ms.bl_user = parity_lcd_ms_user_level(&ms, ms.bl_user_max);" + NL)
r = rep(r, "	parity_lcd_ms_backlight_power(&ms, on);" + NL,
        "	{" + NL + "		u32 before_level = ms.connector.panel.backlight.level;" + NL + NL +
        "		parity_lcd_ms_backlight_power(&ms, on);" + NL +
        "		/* __intel_backlight_enable(): a level <= min comes back as max, and the device's brightness follows */" + NL +
        "		if (on && ms.connector.panel.backlight.level != before_level)" + NL +
        "			ms.bl_user = parity_lcd_ms_user_level(&ms, ms.bl_user_max);" + NL + "	}" + NL)
save(L + "parity_lcd_modeset.c", r)

# ---- host test: start from a non-max level, restore the USER level
t = load(T)
t = rep(t, "		rc = parity_lcd_modeset_brightness(orig, mx);" + NL + '		CHECK(rc == PARITY_LCD_MS_OK && lcd_fake_reg(&lcd, 0xc8258) == orig, "F: back to the original level");',
        """		rc = parity_lcd_modeset_brightness(orig, mx);
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
		}""")
save(T, t)

# ---- kernel: IRQ baseline, brightness judged
k = load(L + "parity_lcd_kernel.c")
k = rep(k, """	f1 = read_frame(k);
	raw1 = d->irq->de_vblank_count[0];
	parity_drm_vblank_put(d->irq, 0u);
	imr_off = osdep_mmio_read32(d->mmio, 0x44404u);
	step_sleep(k, 100u);
	raw2 = d->irq->de_vblank_count[0];""", """	f1 = read_frame(k);
	raw1 = d->irq->de_vblank_count[0];
	parity_drm_vblank_put(d->irq, 0u);
	/* baseline only after: the mask is read back set, and handler work already inside pipe A has drained -- a legitimate
	 * vblank handled between the last wait and the put is not counted as "after masking" */
	imr_off = osdep_mmio_read32(d->mmio, 0x44404u);
	drain_rc = parity_irq_drain_pipes(d->irq, 1u << 0, 10000u);
	raw1 = d->irq->de_vblank_count[0];
	step_sleep(k, 100u);
	raw2 = d->irq->de_vblank_count[0];""")
k = rep(k, "	int rc[3] = { -1, -1, -1 }, get_rc, ok;", "	int rc[3] = { -1, -1, -1 }, get_rc, ok, drain_rc = -1;")
k = rep(k, "(imr_on & 1u) == 0u && rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && raw1 - raw0 >= 3u && f1 != f0 &&",
        "(imr_on & 1u) == 0u && rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && raw1 - raw0 >= 3u && f1 != f0 && drain_rc == 0 &&")
# brightness steps judge the registers
k = rep(k, """static int brightness_step(struct lcd_kernel *k, unsigned step, const char *name, int op, uint32_t level, uint32_t max,
	unsigned hold_ms)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_lcd_modeset_status s;
	uint32_t f0, f1;
	int rc;
""", """/* intel_backlight: user -> hw (scale_user_to_hw) -> PWM (intel_backlight_level_to_pwm, identity here: min/max = PWM min/max) */
static uint32_t expected_duty(const struct parity_lcd_modeset_status *s, uint32_t user)
{
	uint64_t span = (uint64_t)(s->backlight_max - s->backlight_min) * user;

	return s->backlight_min + (uint32_t)((span + s->backlight_user_max / 2u) / s->backlight_user_max);
}

static int brightness_step(struct lcd_kernel *k, unsigned step, const char *name, int op, uint32_t level, uint32_t max,
	unsigned hold_ms)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_lcd_modeset_status s;
	uint32_t f0, f1, ctl, freq, duty, want;
	int rc, regs_ok;
""")
k = rep(k, """	rc = op == 0 ? parity_lcd_modeset_brightness(level, max) : parity_lcd_modeset_backlight(op > 0);
	f0 = read_frame(k);
	parity_lcd_modeset_status(&s);""", """	rc = op == 0 ? parity_lcd_modeset_brightness(level, max) : parity_lcd_modeset_backlight(op > 0);
	f0 = read_frame(k);
	parity_lcd_modeset_status(&s);
	ctl = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_CTL"));
	freq = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_FREQ"));
	duty = osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY"));
	want = expected_duty(&s, s.backlight_user);
	/* the period never changes; off: PWM disabled and the device's backlight off; on / level: PWM enabled and the duty the
	 * reference conversion gives for the user brightness */
	regs_ok = freq == k->bl_freq0 && (op < 0 ? ((ctl & 0x80000000u) == 0u && !s.backlight_enabled) :
		((ctl & 0x80000000u) != 0u && s.backlight_enabled && duty == want));
	kern_logf("i915: parity LCD-R step=%u %s registers: user %u/%u -> expected DUTY %u, read DUTY %u FREQ 0x%08x (start 0x%08x) "
		"PWM_CTL 0x%08x -> %s\\n", step, name, s.backlight_user, s.backlight_user_max, op < 0 ? 0u : want, duty, freq, k->bl_freq0,
		ctl, regs_ok ? "OK" : "MISMATCH");""")
k = rep(k, "	return rc == PARITY_LCD_MS_OK && f1 != f0 && s.crtc_active && s.plane_armed ? 0 : -5;",
        "	return rc == PARITY_LCD_MS_OK && regs_ok && f1 != f0 && s.crtc_active && s.plane_armed ? 0 : -5;")
k = rep(k, "	mx = s.backlight_max;" + NL + "	orig = s.backlight_level;",
        "	mx = s.backlight_user_max;" + NL + "	orig = s.backlight_user;                /* the USER brightness is what gets restored */" + NL +
        "	k->bl_freq0 = osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name(\"BXT_BLC_PWM_FREQ\"));")
k = rep(k, "	unsigned pattern_id, window_ms, post_before, pre_before, vbt_min;", "	unsigned pattern_id, window_ms, post_before, pre_before, vbt_min;" + NL + "	uint32_t bl_freq0;")
save(L + "parity_lcd_kernel.c", k)
print("done")
