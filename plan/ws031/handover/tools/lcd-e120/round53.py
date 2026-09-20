#!/usr/bin/env python3
"""WS031 E-120 round 53: the evasion probe measures its own latency.
 - k_read32 notes the first PIPEDSL value the update body reads after a probe trigger (observation only);
 - each attempt aims the trigger so that this first read lands at the window's first line, using the latency the
   previous attempt measured (bounded attempts; NOT-REACHED is recorded, nothing is faked);
 - the run log (trace) holds 2048 entries so the probe flips do not truncate the record the verdict requires.
 usage: round53.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (s.count(old), old[:100])
    return s.replace(old, new)

tr = load(L + "parity_lcd_trace.h")
tr = rep(tr, "#define PARITY_LCD_TRACE_MAX 768u", "#define PARITY_LCD_TRACE_MAX 2048u")
save(L + "parity_lcd_trace.h", tr)

k = load(L + "parity_lcd_kernel.c")
k = rep(k, """static uint32_t k_read32(void *ctx, uint32_t reg)
{
	return osdep_mmio_read32(((struct lcd_kernel *)ctx)->d->mmio, reg);
}""", """static uint32_t k_read32(void *ctx, uint32_t reg)
{
	struct lcd_kernel *k = ctx;
	uint32_t v = osdep_mmio_read32(k->d->mmio, reg);

	/* evasion probe (observation only): the first scanline the update body reads after the trigger */
	if (k->probe_watch && reg == 0x70000u) {
		k->probe_first_dsl = v & 0x1fffu;
		k->probe_watch = 0;
	}
	return v;
}""")
k = rep(k, "unsigned flips_done, draws_ok, probe_flips, cross_ok;",
        "unsigned flips_done, draws_ok, probe_flips, cross_ok;" + "\n" + "	int probe_watch; uint32_t probe_first_dsl;")
old_start = k.index("static int lcdd_evasion_probe(struct lcd_kernel *k, unsigned front)")
old_end = k.index("static int lcdd_rounds(void *ctx, struct parity_lcd_observer *o)")
k = k[:old_start] + r"""static int lcdd_evasion_probe(struct lcd_kernel *k, unsigned front)
{
	struct parity_lcd_flip_result fr;
	uint32_t dsl_reg = 0x70000u, sl = 0u, vtotal = 0u;       /* PIPEDSL(pipe A) */
	int emin = 0, emax = 0, evbs = 0, rc;
	unsigned a, polls, lat = 0u;                             /* measured trigger -> first update read, in lines */

	lcdd_probe_result = 0;
	if (parity_lcd_modeset_evade_window(&emin, &emax, &evbs) != PARITY_LCD_MS_OK) {
		lcdd_probe_result = -1;
		return 0;
	}
	/* one frame's line count, from the counter itself (the highest scanline seen over > 1 frame of polling) */
	for (polls = 0u; polls < 400000u; polls++) {
		sl = osdep_mmio_read32(k->d->mmio, dsl_reg) & 0x1fffu;
		if (sl + 1u > vtotal)
			vtotal = sl + 1u;
	}
	kern_logf("i915: parity LCD-D evasion probe: window scanlines %d..%d (vblank start %d), lines per frame seen %u; up to %u flips\n",
		emin, emax, evbs, vtotal, LCDD_PROBE_MAX);
	if (vtotal <= (uint32_t)emax) {
		lcdd_probe_result = -1;
		return 0;
	}
	for (a = 0u; a < LCDD_PROBE_MAX; a++) {
		unsigned back = front ^ 1u, sl0 = k->vblank_sleeps, target, want = (unsigned)emin + 1u;
		uint32_t d;

		/* aim: trigger + latency = the window's second line (first attempt: latency unknown, assume 0) */
		target = (want + vtotal - (lat % vtotal)) % vtotal;
		for (polls = 0u; polls < 400000u; polls++) {
			sl = osdep_mmio_read32(k->d->mmio, dsl_reg) & 0x1fffu;
			if (sl == target || sl == (target + 1u) % vtotal)
				break;
		}
		if (polls == 400000u) {
			kern_logf("i915: parity LCD-D evasion probe %u: scanline %u never seen\n", a, target);
			continue;
		}
		if (parity_scanout_begin(&lcdd_buf[back]) != 0)
			return -22;
		k->probe_first_dsl = 0xffffffffu;
		k->probe_watch = 1;
		rc = parity_lcd_modeset_flip((uint32_t)lcdd_buf[back].surf, &fr);
		k->probe_watch = 0;
		d = k->probe_first_dsl != 0xffffffffu ? (k->probe_first_dsl + vtotal - sl) % vtotal : 0u;
		kern_logf("i915: parity LCD-D evasion probe %u: trigger scanline %u (aimed %u), first update read %u (latency %u lines), "
			"gen %u %s, evasion sleeps %u, event_rc=%d, update errors %d, IRQs off at a sleep entry %u\n", a, sl, target,
			k->probe_first_dsl, d, fr.gen, fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : "NOT-DONE", k->vblank_sleeps - sl0,
			fr.event_rc, fr.update_errors, k->sleep_irq_off);
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;
		parity_scanout_end(&lcdd_buf[front]);
		front = back;
		k->probe_flips++;
		lcdd_probe_tries = a + 1u;
		if (k->vblank_sleeps != sl0) {
			lcdd_probe_result = 1;
			lcdd_probe_lead = d;
			break;
		}
		if (k->probe_first_dsl != 0xffffffffu)
			lat = d;                        /* the next attempt aims with what this one measured */
	}
	kern_logf("i915: parity LCD-D evasion probe: %s after %u flip(s)\n", lcdd_probe_result == 1 ? "REACHED (an update slept out of "
		"the window, then armed and completed)" : "NOT-REACHED (recorded; the sleep path stays unverified on this pipe)",
		lcdd_probe_tries);
	return 0;
}

""" + k[old_end:]
k = rep(k, "lead %u lines, IRQs off at a sleep entry %u", "measured latency %u lines, IRQs off at a sleep entry %u")
save(L + "parity_lcd_kernel.c", k)
print("done")
