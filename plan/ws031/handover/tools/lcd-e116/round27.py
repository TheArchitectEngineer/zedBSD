#!/usr/bin/env python3
"""WS031 E-116 round 27: findings of the first LCD-B picture: the stop evidence must be taken while the pipe's power
well is still on (afterwards pipe A's registers read 0 and prove nothing); the binding's text about the interrupt mask
said something the existing power-well code does not do.  usage: round27.py <repo root>"""
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

h = load(L + "parity_lcd_observe.h")
h = rep(h, "	int vblank_unmasked_seen;", "	int vblank_unmasked_seen;" + NL +
        "	/* the stop evidence, taken at the disable's pipe-disabled point -- the last moment the pipe's power well is on */" + NL +
        "	int stop_checked; uint32_t stop_transconf, stop_frame0, stop_frame1;")
h = rep(h, "/* after the stop: 0 when the counter stood still over window_ms (the pipe no longer scans), -16 when it still moved */",
        "/* the stop evidence recorded at the pipe-disabled point (well still on): 0 when TRANSCONF's state bit was clear AND the" + NL +
        " * frame counter stood over 50 ms; -16 when not; -22 when that point was never reached.  window_ms is unused (kept for" + NL +
        " * callers): reading pipe registers LATER, with the well off, returns 0 and would prove nothing. */")
save(L + "parity_lcd_observe.h", h)
c = load(L + "parity_lcd_observe.c")
c = rep(c, "	if (point == PARITY_LCD_OBS_PIPE_DISABLED)" + NL + "		o->powered = 0;" + NL,
        "	if (point == PARITY_LCD_OBS_PIPE_DISABLED) {" + NL +
        "		o->stop_transconf = o->hw->read32(o->hw->ctx, i915_mmio_reg_offset(TRANSCONF((enum transcoder)o->pipe)));" + NL +
        "		o->stop_frame0 = o->hw->read32(o->hw->ctx, o->frame_reg);" + NL +
        "		o->hw->usleep(o->hw->ctx, 50000u);                /* three frame times at 60 Hz */" + NL +
        "		o->stop_frame1 = o->hw->read32(o->hw->ctx, o->frame_reg);" + NL +
        "		o->stop_checked = 1;" + NL +
        "		o->powered = 0;" + NL + "	}" + NL)
a = c.index("int parity_lcd_observer_stopped(")
b = c.index("int parity_lcd_observer_pipe_active(")
c = c[:a] + """int parity_lcd_observer_stopped(struct parity_lcd_observer *o, unsigned window_ms, uint32_t *first, uint32_t *last)
{
	(void)window_ms;
	if (first != 0)
		*first = o->stop_frame0;
	if (last != 0)
		*last = o->stop_frame1;
	if (!o->stop_checked)
		return -22;
	return (!(o->stop_transconf & TRANSCONF_STATE_ENABLE) && o->stop_frame0 == o->stop_frame1) ? 0 : -16;
}

""" + c[b:]
save(L + "parity_lcd_observe.c", c)

s = load(L + "parity_lcd_show.c")
s = rep(s, "		if (parity_lcd_observer_pipe_active(&r->obs, &r->transconf_after_stop))" + NL + "			r->stopped_rc = -EBUSY;" + NL,
        "		r->transconf_after_stop = r->obs.stop_transconf;        /* read at the pipe-disabled point, the well still on */" + NL)
save(L + "parity_lcd_show.c", s)

k = load(L + "parity_lcd_kernel.c")
a = k.index("	/*" + NL + "	 * Pipe A's registers (TRANSCONF, PLANE_CTL, GEN8_DE_PIPE_IMR(A)")
b = k.index("	if ((d->irq->de_irq_mask[0] & 0x80600001u) != 0x80600001u) {", a)
k = k[:a] + """	/*
	 * Pipe A's registers (TRANSCONF, PLANE_CTL, GEN8_DE_PIPE_IMR / IER(A), ICL_PIPESTATUS(A)) sit in power well A, which is
	 * off until the commit takes the pipe's power domain: they read 0 here and say nothing.  NOTE (found by the first
	 * LCD-B picture): the existing power-well code does NOT yet do the reference's gen8_irq_power_well_post_enable() /
	 * _pre_disable() -- the hook is a counted placeholder -- so when the well comes up the pipe's IMR / IER keep their
	 * hardware values (observed: IMR 0xfff9ffff, IER 0: no pipe interrupt can be delivered at all).  That suits this
	 * test, which uses none, but it is not the reference's state (IMR = de_irq_mask, IER = ~de_irq_mask | vblank |
	 * underrun | flip done) and must be implemented before anything relies on pipe interrupts.  Checked here: the IRQ
	 * state's mask, which the reference WOULD program, keeps vblank (bit 0) and underrun (bits 31 / 22 / 21) masked; the
	 * register itself is read by the observer at every sample while the well is on.
	 */
""" + k[b:]
save(L + "parity_lcd_kernel.c", k)
print("done")
