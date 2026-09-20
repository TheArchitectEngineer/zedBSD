#!/usr/bin/env python3
"""WS031 E-116 round 26: findings of the first real-machine attempt (stopped in preflight, nothing written):
pipe A's registers (GEN8_DE_PIPE_IMR(A), ICL_PIPESTATUS(A)) sit in power well A and read 0 until the pipe's power
domain is held; kern_logf has no '-' flag.  usage: round26.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---- observer: the interrupt mask is judged only while the pipe's power well is held by the commit
h = load(L + "parity_lcd_observe.h")
h = rep(h, "	int begun, steady;", "	int begun, steady;" + NL +
        "	int powered;                    /* between the enable's underrun-arm point and the disable's pipe-disabled point the crtc holds" + NL +
        "	                                 * the pipe's power domain; outside, pipe A's registers (power well A) read 0 and say nothing */")
h = rep(h, " * The pipe's vblank interrupt must stay masked for the whole run (nothing here could service it): every sample also" + NL +
        " * reads GEN8_DE_PIPE_IMR and remembers if the vblank bit was ever found unmasked.",
        " * The pipe's vblank interrupt must stay masked for the whole run (nothing here could service it): every sample also" + NL +
        " * reads GEN8_DE_PIPE_IMR and remembers if the vblank bit was ever found unmasked -- judged only while the pipe's" + NL +
        " * power well is held (the register lives in that well: before, it reads 0; the power-well enable programs the mask).")
save(L + "parity_lcd_observe.h", h)
c = load(L + "parity_lcd_observe.c")
c = rep(c, "	if (!(imr & o->vblank_bit))" + NL + "		o->vblank_unmasked_seen = 1;" + NL,
        "	if (point == PARITY_LCD_OBS_UNDERRUN_ARM)" + NL + "		o->powered = 1;" + NL +
        "	if (o->powered && !(imr & o->vblank_bit))" + NL + "		o->vblank_unmasked_seen = 1;" + NL +
        "	if (point == PARITY_LCD_OBS_PIPE_DISABLED)" + NL + "		o->powered = 0;" + NL)
c = rep(c, "	if (point == PARITY_LCD_OBS_UNDERRUN_ARM) {" + NL,
        "	if (point == PARITY_LCD_OBS_UNDERRUN_ARM) {" + NL +
        "		o->saved_before = status;       /* the first read with the pipe's well on: what was really left there */" + NL)
save(L + "parity_lcd_observe.c", c)

# ---- model: the same hardware fact
m = load(L + "lcd_fake_hw.c")
m = rep(m, "	return lcd_fake_reg(hw, reg);" + NL + "}" + NL + NL + "static void f_write32(",
        "	/* pipe A..D interrupt registers live in the pipe's power well: off = reads 0 (the well's enable programs the mask) */" + NL +
        "	if (reg == REG_DE_PIPE_IMR(hw->pipe) && hw->power_refs[POWER_DOMAIN_PIPE_A + hw->pipe] <= 0)" + NL + "		return 0u;" + NL +
        "	return lcd_fake_reg(hw, reg);" + NL + "}" + NL + NL + "static void f_write32(")
save(L + "lcd_fake_hw.c", m)
t = load("plan/ws031/tests/lcd-modeset-host-test.c")
t = rep(t, '		      "D: no underrun in either period; the pipe\'s vblank interrupt was masked at every sample");',
        '		      "D: no underrun in either period; the pipe\'s vblank interrupt was masked at every sample");' + NL +
        "		CHECK(obs.s[0].point == PARITY_LCD_OBS_COMMIT_BEGIN && obs.s[0].imr == 0u && obs.s[1].imr == 0xffffffffu," + NL +
        '		      "D: before the crtc holds the pipe\'s power domain its interrupt mask reads 0 (well off): recorded, not judged");')
save("plan/ws031/tests/lcd-modeset-host-test.c", t)

# ---- binding: preflight judges the interrupt STATE; formats kern_logf understands
k = load(L + "parity_lcd_kernel.c")
a = k.index("	/* the pipe's vblank (bit 0) and underrun")
b = k.index("	return ok ? 0 : -1;" + NL + "}" + NL, a)
k = k[:a] + """	/*
	 * Pipe A's registers (TRANSCONF, PLANE_CTL, GEN8_DE_PIPE_IMR(A), ICL_PIPESTATUS(A)) sit in power well A, which is
	 * off until the commit takes the pipe's power domain: they read 0 here and say nothing.  What the power-well enable
	 * will program into the pipe's interrupt mask (gen8_irq_power_well_post_enable) is the IRQ state's de_irq_mask: THAT
	 * must keep vblank (bit 0) and underrun (bit 31; XELPD soft / hard: bits 22 / 21) masked -- nothing services them in
	 * this path.  The register itself is checked by the observer at every sample once the well is on.
	 */
	if ((d->irq->de_irq_mask[0] & 0x80600001u) != 0x80600001u) {
		kern_logf("i915: parity LCD-B preflight: the IRQ state's pipe A mask 0x%08x would leave vblank / underrun unmasked\\n",
			d->irq->de_irq_mask[0]);
		ok = 0;
	}
	kern_logf("i915: parity LCD-B preflight: DDI / PLL idle=%d (pipe A registers read TRANSCONF 0x%08x PLANE_CTL 0x%08x IMR 0x%08x with "
		"power well A off) | IRQ state pipe A mask 0x%08x: vblank + underrun masked=%d | no code of this path waits for a software "
		"vblank count; no worker / callback / waiter is created by it\\n", (pll & 0x80000000u) == 0u && (ddi & 0x80000000u) == 0u,
		transconf, plane_ctl, imr, d->irq->de_irq_mask[0], (d->irq->de_irq_mask[0] & 0x80600001u) == 0x80600001u);
""" + k[b:]
k = k.replace("%-20s", "%s").replace("%-7s", "%s")
save(L + "parity_lcd_kernel.c", k)
print("done")
