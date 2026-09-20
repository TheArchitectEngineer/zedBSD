#!/usr/bin/env python3
"""WS031 E-120 round 51: the flip event's lifetime and the evasion sleep.
 - intel_crtc_vblank_off() settles the pending flip event (drm_crtc_vblank_off(): pending events are sent / their
   vblank reference dropped): ops->cancel_event + ONE vblank_put when the event still holds its reference.
 - parity_lcd_modeset_evade_window(): the reference's evasion window of the running mode (for tests / harness).
 - model: scanline hold (to start inside the window), IRQ state at sleep entry, cancel_event, late-event check.
 - host section J.  usage: round51.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (s.count(old), old[:100])
    return s.replace(old, new)

o = load(L + "parity_lcd_ops.h")
o = rep(o, "	int (*wait_event)(void *ctx, int pipe, unsigned timeout_ms);",
        "	int (*wait_event)(void *ctx, int pipe, unsigned timeout_ms);" + NL +
        "	/* drm_crtc_vblank_off() on a pending event: it will never be waited for again (no completion after this) */" + NL +
        "	void (*cancel_event)(void *ctx, int pipe);")
save(L + "parity_lcd_ops.h", o)

t = load(L + "parity_lcd_trace.c")
t = rep(t, "static int t_wait_event(void *ctx, int pipe, unsigned timeout_ms) { return TB(ctx)->wait_event(TB(ctx)->ctx, pipe, timeout_ms); }",
        "static int t_wait_event(void *ctx, int pipe, unsigned timeout_ms) { return TB(ctx)->wait_event(TB(ctx)->ctx, pipe, timeout_ms); }" + NL +
        "static void t_cancel_event(void *ctx, int pipe) { TB(ctx)->cancel_event(TB(ctx)->ctx, pipe); }")
t = rep(t, "		t->ops.wait_event = t_wait_event;", "		t->ops.wait_event = t_wait_event;" + NL +
        "		if (backend->cancel_event != 0)" + NL + "			t->ops.cancel_event = t_cancel_event;")
save(L + "parity_lcd_trace.c", t)

# the modeset object: the event's reference is tracked; vblank_off settles it
i = load(L + "parity_lcd_modeset_int.h")
i = rep(i, "	u32 cur_surf, pend_surf, old_surf; int flip_pending, flip_stuck; unsigned flip_gen;",
        "	u32 cur_surf, pend_surf, old_surf; int flip_pending, flip_stuck; unsigned flip_gen;" + NL +
        "	int flip_event_ref;                      /* the armed event still holds its vblank reference (taken in update_end) */" + NL +
        "	unsigned events_cancelled;")
i = i.rstrip(NL) + NL + NL + "/* intel_crtc_vblank_off() of the flip path: settle the pending event (parity_lcd_modeset.c) */" + NL + \
    "void parity_lcd_ms_vblank_off(void);" + NL + \
    "int parity_lcd_ms_evade_window(struct parity_lcd_modeset *ms, int *min, int *max, int *vblank_start);" + NL
save(L + "parity_lcd_modeset_int.h", i)

c = load(L + "parity_lcd_modeset.c")
c = rep(c, """	parity_lcd_ms_plane_update_flip(&ms);
	res.update_errors = (int)(ms_errors - before);""", """	parity_lcd_ms_plane_update_flip(&ms);
	ms.flip_event_ref = 1;                   /* intel_pipe_update_end(): drm_crtc_vblank_get() for the armed event */
	res.update_errors = (int)(ms_errors - before);""")
c = rep(c, """		ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
		if (res.live_after == new_surf) {""", """		ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
		ms.flip_event_ref = 0;
		if (res.live_after == new_surf) {""")
c = rep(c, "/* the caller confirmed (frame counter / scanline) that the plane is off: the buffer is its own again */",
        r"""/*
 * intel_crtc_vblank_off() -> drm_crtc_vblank_off(): pending events of the crtc are sent (with the current count) and
 * the reference each one held is dropped; nobody waits for them afterwards.  Here: the flip's one event.  The event
 * record is the waiting thread's own (the IRQ side only advances the pipe's counters under the IRQ lock), so settling
 * it is: the backend forgets the armed event (a later wait is refused, a late vblank completes nothing), then the
 * reference goes -- exactly once, whatever happened to the flip (done / timed out / not latched).
 */
void parity_lcd_ms_vblank_off(void)
{
	if (!ms.flip_event_ref || ms_ops == 0)
		return;
	if (ms_ops->cancel_event != 0)
		ms_ops->cancel_event(ms_ops->ctx, ms.crtc.pipe);
	ms_ops->vblank_put(ms_ops->ctx, ms.crtc.pipe);
	ms.flip_event_ref = 0;
	ms.events_cancelled++;
}

int parity_lcd_modeset_evade_window(int *min, int *max, int *vblank_start)
{
	if (!ms.prepared || !ms.crtc.active)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	return parity_lcd_ms_evade_window(&ms, min, max, vblank_start) == 0 ? PARITY_LCD_MS_OK : PARITY_LCD_MS_ERRORS;
}

/* the caller confirmed (frame counter / scanline) that the plane is off: the buffer is its own again */""")
c = rep(c, "	out->flip_stuck = ms.flip_stuck;", "	out->flip_stuck = ms.flip_stuck;" + NL +
        "	out->flip_event_ref = ms.flip_event_ref;" + NL + "	out->events_cancelled = ms.events_cancelled;")
save(L + "parity_lcd_modeset.c", c)
h = load(L + "parity_lcd_modeset.h")
h = rep(h, "int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out);",
        "int parity_lcd_modeset_flip(uint32_t new_surf, struct parity_lcd_flip_result *out);" + NL +
        "/* the reference's vblank-evasion window of the running mode (intel_crtc_vblank_evade_scanlines): scanlines [min, max] */" + NL +
        "int parity_lcd_modeset_evade_window(int *min, int *max, int *vblank_start);")
import re
m = re.search(r"int flip_pending, flip_stuck;[^\n]*\n", h)
assert m, "status fields"
h = h[:m.end()] + "	int flip_event_ref;                     /* the pending event still holds its vblank reference */" + NL + \
    "	unsigned events_cancelled;              /* events settled by the stop path (drm_crtc_vblank_off) */" + NL + h[m.end():]
save(L + "parity_lcd_modeset.h", h)

g = load(L + "parity_flip_glue.inc")
g = g.rstrip(NL) + NL + r"""
/* intel_crtc_vblank_evade_scanlines() of the running mode: the window intel_pipe_update_start() sleeps out of */
int parity_lcd_ms_evade_window(struct parity_lcd_modeset *ms, int *min, int *max, int *vblank_start)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = &ms->crtc_state;
	intel_crtc_vblank_evade_scanlines(&ms->state, &ms->crtc, min, max, vblank_start);
	return *min > 0 && *max > 0 ? 0 : -1;
}
"""
save(L + "parity_flip_glue.inc", g)

s = load(L + "lcd_seq_compat.h")
s = rep(s, """#define intel_crtc_vblank_off(cs) PARITY_LCD_DECIDED(parity_lcd_cur_i915, "intel_crtc_vblank_off: no vblank waiters / events / work exist in this test")""",
        """/* _off(): the flip path has one pending event with a reference: it is settled here (E-120; the single-buffer
 * statement above no longer covers it).  No vblank work / other waiters exist. */
void parity_lcd_ms_vblank_off(void);
#define intel_crtc_vblank_off(cs) do { (void)(cs); parity_lcd_ms_vblank_off(); } while (0)""")
save(L + "lcd_seq_compat.h", s)

# ---- the model ----
fh = load(L + "lcd_fake_hw.h")
fh = rep(fh, "	int irq_off, vblank_refs, event_armed; unsigned irq_off_calls, vblank_sleeps, events_armed, events_done;",
         "	int irq_off, vblank_refs, event_armed; unsigned irq_off_calls, vblank_sleeps, events_armed, events_done;" + NL +
         "	unsigned events_cancelled, sleep_irq_off, waits_refused;   /* sleep entered with IRQs off = a violation */" + NL +
         "	uint32_t scanline_hold; unsigned scanline_hold_reads;     /* PIPEDSL reads this value this many times (test knob) */")
save(L + "lcd_fake_hw.h", fh)
f = load(L + "lcd_fake_hw.c")
f = rep(f, """	if (reg == REG_PIPEDSL(hw->pipe)) {
		if (hw->pipe_on_since_us != 0u)""", """	if (reg == REG_PIPEDSL(hw->pipe)) {
		if (hw->scanline_hold_reads != 0u) {
			hw->scanline_hold_reads--;
			hw->scanline = hw->scanline_hold;
			return hw->scanline;
		}
		if (hw->pipe_on_since_us != 0u)""")
f = rep(f, """	(void)pipe;
	hw->vblank_sleeps++;
	if (hw->fault_no_vblank) {""", """	(void)pipe;
	hw->vblank_sleeps++;
	if (hw->irq_off)
		hw->sleep_irq_off++;            /* the reference enables IRQs before schedule_timeout() */
	if (hw->fault_no_vblank) {""")
f = rep(f, """	to_next_frame(hw);
	hw->scanline = 0u;                      /* just after the vblank: far from the evasion window */""",
        """	to_next_frame(hw);
	hw->scanline = 0u;                      /* just after the vblank: far from the evasion window */
	hw->scanline_hold_reads = 0u;""")
f = rep(f, """	(void)pipe;
	if (!hw->event_armed)
		return -22;""", """	(void)pipe;
	if (!hw->event_armed) {
		hw->waits_refused++;
		return -22;
	}""")
f = rep(f, "static void f_observe(void *ctx, int point)", r"""static void f_cancel_event(void *ctx, int pipe)
{
	struct lcd_fake_hw *hw = ctx;

	(void)pipe;
	if (hw->event_armed)
		hw->events_cancelled++;
	hw->event_armed = 0;
}

static void f_observe(void *ctx, int point)""")
f = rep(f, "	hw->ops.wait_event = f_wait_event;", "	hw->ops.wait_event = f_wait_event;" + NL + "	hw->ops.cancel_event = f_cancel_event;")
save(L + "lcd_fake_hw.c", f)

# ---- host section J ----
ht = load(T)
ht = rep(ht, "	/* ================= E. a time-base fault is not a timeout ================= */", r"""	/* ================= J. the event after a timeout, the stop, the relight; the evasion sleep ================= */
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
		CHECK(rc == PARITY_LCD_MS_OK && fr.result == PARITY_LCD_FLIP_DONE && lcd.events_done == ev0 + 1u && s.flip_event_ref == 0 &&
		      lcd.vblank_refs == 0 && s.events_cancelled == 1u,
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
		CHECK(rc == PARITY_LCD_MS_OK && lcd.vblank_refs == 0 && s.flip_event_ref == 0 && s.events_cancelled == 2u &&
		      lcd.power_underflows == under0,
		      "J3: the stop settles that event too: every reference returned once");
		(void)parity_edp_end(&res);
	}

	/* ================= E. a time-base fault is not a timeout ================= */""")
save(T, ht)
print("done")
