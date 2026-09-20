#!/usr/bin/env python3
"""WS031 E-120 round 52: kernel side.
 - flip event: cancel_event (the stop settles the armed event), the CPU's real IRQ state checked at every sleep entry.
 - TLB: an intentional fault is tagged (backend / test id / expected); the fake clears the done bit only after some
   reads (the wait really polls for 0); ktest section markers.
 - LCD-D: 8 draws A0 B1 A2 B3 A1 B2 A3 B0 (every variant in both buffers: cross-buffer hash compare), per-flip sleeps,
   then a bounded evasion probe: A/B flips triggered at a harness-chosen scanline before the evasion window, until one
   update really sleeps (or the attempts run out: recorded as NOT-REACHED, no success is invented).
 usage: round52.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (s.count(old), old[:100])
    return s.replace(old, new)

# ---------------- TLB tags ----------------
h = load(P + "gt_tlb.h")
_done_h = "expected_fault" in h
h = h if _done_h else rep(h, """	unsigned invalidations, engines_invalidated, timeouts, time_faults, fw_failures;
};""", """	unsigned invalidations, engines_invalidated, timeouts, time_faults, fw_failures;
	/* who is asking: a timeout log line carries these, so a test's intended fault is told apart mechanically from a
	 * hardware anomaly (backend 0 = "HW", test_id 0 = "-") */
	const char *backend, *test_id;
	int expected_fault;
};""")
h = h if _done_h else rep(h, "(intel_engine_pm_is_awake) and a GT\n * that is asleep", "(intel_engine_pm_is_awake) and a GT\n * that is asleep")
h = h if _done_h else rep(h, " * with a reset.  0; -ETIMEDOUT (an engine did not report done in 4 ms); -EIO (the time base / forcewake failed).",
        """ * with a reset.  Each engine's wait is for its done bit to read back 0 (wait_for_invalidate(): mask = done,
 * value = 0).  0; -ETIMEDOUT (an engine's bit did not clear in 4 ms); -EIO (the time base / forcewake failed).
 * ADAPTATIONS beyond the reference (recorded): the reference is void and logs a timeout; here the error is returned so
 * the caller keeps every page (release contract), and the seqno advances only on success, so a failed invalidation is
 * never taken as a completed generation.  No MCR lock is taken (the ADL-P engine registers are not MCR); the forcewake
 * is returned at once (the reference uses a delayed put); callers are serialised by the one owner (no invalidate_lock).""")
save(P + "gt_tlb.h", h)
c = load(P + "gt_tlb.c")
c = c if "expected_fault" in c else rep(c, """			kern_logf("i915: parity TLB invalidation did not complete in %ums (reg 0x%x done 0x%x)\\n",
				TLB_INVAL_TIMEOUT_MS, reg[i], done[i]);""", """			kern_logf("i915: parity TLB invalidation did not complete in %ums (reg 0x%x done bit 0x%x still set) "
				"backend=%s test=%s expected_fault=%d\\n", TLB_INVAL_TIMEOUT_MS, reg[i], done[i],
				tlb->backend != 0 ? tlb->backend : "HW", tlb->test_id != 0 ? tlb->test_id : "-", tlb->expected_fault);""")
save(P + "gt_tlb.c", c)

t = load(L + "lcdg_ktest.c")
t = rep(t, """	int stuck;
};""", """	int stuck;
	unsigned busy_reads, busy_left, polls;  /* after a request the done bit reads 1 this many times, then 0 */
};""")
t = rep(t, """	if (off >= 0xced8u && off <= 0xcf04u && f->stuck)
		for (i = f->n; i-- > 0u;)
			if (f->wr_off[i] == off)
				return f->wr_val[i] & 0xffffu;         /* the done bit never clears */
	return 0u;""", """	if (off >= 0xced8u && off <= 0xcf04u && off != 0xceecu) {
		f->polls++;
		for (i = f->n; i-- > 0u;) {
			if (f->wr_off[i] != off)
				continue;
			if (f->stuck)
				return f->wr_val[i] & 0xffffu;         /* the request bit never clears */
			if (f->busy_left != 0u) {
				f->busy_left--;
				return f->wr_val[i] & 0xffffu;         /* accepted, still in progress */
			}
			return 0u;                                      /* done: the bit cleared */
		}
	}
	return 0u;""")
t = rep(t, """	if (f->n < 32u) {
		f->wr_off[f->n] = off;""", """	if (off >= 0xced8u && off <= 0xcf04u && off != 0xceecu)
		f->busy_left = f->busy_reads;
	if (f->n < 32u) {
		f->wr_off[f->n] = off;""")
t = rep(t, """	int rc, rrc;

	/* ---- TLB: the reference's register table and request encodings ---- */""", """	int rc, rrc;

	kern_logf("i915: parity ktest section begin: lcdg (backend=MODEL; lines tagged expected_fault=1 below are intended)\\n");
	/* ---- TLB: the reference's register table and request encodings ---- */""")
# every stuck phase is tagged
_o = []; _n = 0
for _l in t.split(NL):
    _o.append(_l)
    if _l.strip() in ("tf.stuck = 1;", "tf.stuck = 0;"):
        _o.append(_l[:len(_l) - len(_l.lstrip())] + "tlb.expected_fault = " + _l.strip()[-2] + ";"); _n += 1
assert _n == 6, _n
t = NL.join(_o)
save(L + "lcdg_ktest.c", t)
# the tlb struct is memset before TLB-FULL: set tags after it, and a delayed clear for TLB-FULL
t = load(L + "lcdg_ktest.c")
i0 = t.index("	memset(&tlb, 0, sizeof(tlb));")
t = t[:i0] + "	memset(&tlb, 0, sizeof(tlb));\n	tlb.backend = \"MODEL\";\n	tlb.test_id = \"lcdg-ktest\";\n	tf.busy_reads = 3u;\n	tf.polls = 0u;\n" + \
    t[i0 + len("	memset(&tlb, 0, sizeof(tlb));\n"):]
t = rep(t, """		"lcdg: TLB-FULL every engine's request, then Wa_2207587034 (OA 0xceec), done bits back to 0, seqno += 2");""",
        """		"lcdg: TLB-FULL every engine's request, then Wa_2207587034 (OA 0xceec), done bits back to 0, seqno += 2");
	check(tf.polls >= 5u + 3u,
		"lcdg: TLB-POLL the done bit reads 1 after the request and clears later: the wait polls until it reads 0");""")
t = rep(t, """		"lcdg: FIN-DISCARD the latch is dropped only with its memory manager finalised (GPU-free model)");""",
        """		"lcdg: FIN-DISCARD the latch is dropped only with its memory manager finalised (GPU-free model)");
	kern_logf("i915: parity ktest section end: lcdg\\n");""")
save(L + "lcdg_ktest.c", t)

# ---------------- kernel flip ops: cancel, real IRQ state at sleep entry ----------------
k = load(L + "parity_lcd_kernel.c")
k = rep(k, """	struct lcd_kernel *k = ctx;
	int rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, (unsigned)ticks * 10u, k_frame, k, 0);

	k->vblank_sleeps++;""", """	struct lcd_kernel *k = ctx;
	int rc, on;

	/* the reference re-enables local IRQs before schedule_timeout(): read the CPU's real state (and restore it) */
	on = kern_irq_disable();
	if (on)
		kern_irq_enable();
	else
		k->sleep_irq_off++;
	rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, (unsigned)ticks * 10u, k_frame, k, 0);
	k->vblank_sleeps++;""")
k = rep(k, "static void bind_ops(struct lcd_kernel *k)", r"""/* drm_crtc_vblank_off() on the armed event: forget it -- a later wait is refused, a late vblank completes nothing.
 * The event record is this thread's own; the IRQ handler only advances the pipe's counters under the IRQ lock. */
static void k_cancel_event(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	(void)pipe;
	if (k->event_armed)
		k->events_cancelled++;
	k->event_armed = 0;
}

static void bind_ops(struct lcd_kernel *k)""")
k = rep(k, "	k->ops.wait_event = k_wait_event;", "	k->ops.wait_event = k_wait_event;" + NL + "	k->ops.cancel_event = k_cancel_event;")
k = rep(k, "	int irq_was_on, event_armed, event_pipe; uint32_t event_frame; unsigned vblank_sleeps;",
        "	int irq_was_on, event_armed, event_pipe; uint32_t event_frame; unsigned vblank_sleeps, sleep_irq_off, events_cancelled;")

# ---------------- LCD-D: the variant order, cross-buffer compare, evasion probe ----------------
k = rep(k, "#define LCDD_ROUNDS 8u", "#define LCDD_ROUNDS 7u                        /* draws 0..7: A0 B1 A2 B3 A1 B2 A3 B0 */")
k = rep(k, "static int lcdd_gpu_kept;", r"""static int lcdd_gpu_kept;
/* E-120: every variant into both buffers; a buffer's next variant always differs from what it last held */
static const unsigned lcdd_seq[LCDD_ROUNDS + 1u] = { 0u, 1u, 2u, 3u, 1u, 2u, 3u, 0u };
static uint64_t lcdd_hash[2][4];
static unsigned lcdd_hash_set[2][4];
#define LCDD_PROBE_MAX 16u""")
k = rep(k, """	lcdd_variant[i] = variant;
	k->draws_ok++;""", """	lcdd_variant[i] = variant;
	lcdd_hash[i][variant & 3u] = x->image_hash;
	lcdd_hash_set[i][variant & 3u]++;
	k->draws_ok++;""")
k = rep(k, """	for (r = 1u; r <= LCDD_ROUNDS; r++) {
		unsigned back = front ^ 1u, variant = r % 4u;""", """	for (r = 1u; r <= LCDD_ROUNDS; r++) {
		unsigned back = front ^ 1u, variant = lcdd_seq[r], sl0 = k->vblank_sleeps;""")
k = rep(k, """		kern_logf("i915: parity LCD-D flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event rc=%d | "
			"update errors %d | result %s\\n", r, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after, fr.frame_before,
			fr.frame_after, fr.event_rc, fr.update_errors,""", """		kern_logf("i915: parity LCD-D flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event_rc=%d | "
			"update errors %d | evasion sleeps %u | result %s\\n", r, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after,
			fr.frame_before, fr.frame_after, fr.event_rc, fr.update_errors, k->vblank_sleeps - sl0,""")
k = rep(k, """		step_sleep(k, LCDD_HOLD_MS);
	}
	return 0;
}

int parity_lcd_kernel_lcdd_run(""", r"""		step_sleep(k, LCDD_HOLD_MS);
	}
	return lcdd_evasion_probe(k, front);
}

int parity_lcd_kernel_lcdd_run(""")
k = rep(k, "static int lcdd_rounds(void *ctx, struct parity_lcd_observer *o)", r"""/*
 * The evasion sleep on the real pipe: the ordinary flip (the same update body) is started when the harness has seen
 * the scanline `lead` lines before the evasion window, so that intel_pipe_update_start() finds itself inside it and
 * sleeps to the next vblank.  The harness only chooses WHEN to call; nothing is faked.  Bounded: LCDD_PROBE_MAX
 * flips (A and B alternate, both hold valid pictures); the first flip that slept and completed ends it.  If none did,
 * the result is NOT-REACHED -- recorded, not a failure of the display test.
 */
static int lcdd_probe_result;               /* 1 reached, 0 not reached, -1 error */
static unsigned lcdd_probe_tries, lcdd_probe_lead;

static int lcdd_evasion_probe(struct lcd_kernel *k, unsigned front)
{
	struct parity_lcd_flip_result fr;
	uint32_t dsl_reg = 0x70000u, sl = 0u;       /* PIPEDSL(pipe A) */
	int emin = 0, emax = 0, evbs = 0, rc;
	unsigned a, polls;

	lcdd_probe_result = 0;
	if (parity_lcd_modeset_evade_window(&emin, &emax, &evbs) != PARITY_LCD_MS_OK) {
		lcdd_probe_result = -1;
		return 0;
	}
	kern_logf("i915: parity LCD-D evasion probe: window scanlines %d..%d (vblank start %d); up to %u flips\n", emin, emax, evbs,
		LCDD_PROBE_MAX);
	for (a = 0u; a < LCDD_PROBE_MAX; a++) {
		unsigned back = front ^ 1u, lead = 2u * a, sl0 = k->vblank_sleeps, target = (unsigned)emin > lead ? (unsigned)emin - lead : 0u;

		/* wait (bounded, ~2 frames of polls) for the scanline to reach [target, emin) */
		for (polls = 0u; polls < 200000u; polls++) {
			sl = osdep_mmio_read32(k->d->mmio, dsl_reg) & 0x1fffu;
			if (sl >= target && sl < (unsigned)emin)
				break;
		}
		if (polls == 200000u) {
			kern_logf("i915: parity LCD-D evasion probe %u: scanline never seen in [%u, %d)\n", a, target, emin);
			continue;
		}
		if (parity_scanout_begin(&lcdd_buf[back]) != 0)
			return -22;
		rc = parity_lcd_modeset_flip((uint32_t)lcdd_buf[back].surf, &fr);
		kern_logf("i915: parity LCD-D evasion probe %u: lead %u lines, trigger scanline %u, gen %u %s, evasion sleeps %u, "
			"event_rc=%d, update errors %d, IRQs off at a sleep entry %u\n", a, lead, sl, fr.gen,
			fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : "NOT-DONE", k->vblank_sleeps - sl0, fr.event_rc, fr.update_errors,
			k->sleep_irq_off);
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;
		parity_scanout_end(&lcdd_buf[front]);
		front = back;
		k->probe_flips++;
		lcdd_probe_tries = a + 1u;
		if (k->vblank_sleeps != sl0) {
			lcdd_probe_result = 1;
			lcdd_probe_lead = lead;
			break;
		}
	}
	kern_logf("i915: parity LCD-D evasion probe: %s after %u flip(s)%s\n", lcdd_probe_result == 1 ? "REACHED (an update slept "
		"out of the window, then armed and completed)" : "NOT-REACHED (recorded; the sleep path stays unverified on this pipe)",
		lcdd_probe_tries, lcdd_probe_result == 1 ? "" : "");
	return 0;
}

static int lcdd_rounds(void *ctx, struct parity_lcd_observer *o)""")
k = rep(k, "	struct parity_scanout *flip_a, *flip_b; unsigned flips_done, draws_ok;",
        "	struct parity_scanout *flip_a, *flip_b; unsigned flips_done, draws_ok, probe_flips;")
# cross-buffer compare + verdict
k = rep(k, """verdict:
	lcdb_summary.pass = rc == 0 && k->draws_ok == LCDD_ROUNDS + 1u && k->flips_done == LCDD_ROUNDS && released && held == 0 &&
		bad[0] == 0u && bad[1] == 0u && k->unresolved_steps == 0u && k->time_faults == 0u;""", """verdict:
	{
		unsigned v, cross = 0u;

		for (v = 0u; v < 4u; v++) {
			int same = lcdd_hash_set[0][v] != 0u && lcdd_hash_set[1][v] != 0u && lcdd_hash[0][v] == lcdd_hash[1][v];

			cross += same ? 1u : 0u;
			kern_logf("i915: parity LCD-D cross-buffer variant %u: A hash %016llx (%u draw(s)) | B hash %016llx (%u draw(s)) | %s\\n", v,
				(unsigned long long)lcdd_hash[0][v], lcdd_hash_set[0][v], (unsigned long long)lcdd_hash[1][v], lcdd_hash_set[1][v],
				same ? "SAME" : "DIFFERENT / MISSING");
		}
		k->cross_ok = cross;
	}
	kern_logf("i915: parity LCD-D evasion: %s (probe flips %u, lead %u lines, IRQs off at a sleep entry %u, events cancelled by "
		"the stop %u)\\n", lcdd_probe_result == 1 ? "REACHED" : lcdd_probe_result == 0 ? "NOT-REACHED" : "ERROR", k->probe_flips,
		lcdd_probe_lead, k->sleep_irq_off, k->events_cancelled);
	lcdb_summary.pass = rc == 0 && k->draws_ok == LCDD_ROUNDS + 1u && k->flips_done == LCDD_ROUNDS && released && held == 0 &&
		bad[0] == 0u && bad[1] == 0u && k->unresolved_steps == 0u && k->time_faults == 0u && k->cross_ok == 4u &&
		k->sleep_irq_off == 0u && lcdd_probe_result >= 0;""")
k = rep(k, "unsigned flips_done, draws_ok, probe_flips;", "unsigned flips_done, draws_ok, probe_flips, cross_ok;")
k = rep(k, """	kern_logf("i915: parity LCD-D verdict: %s (GPU draws %u/%u, flips done %u/%u, one modeset, stop %s, targets unmapped=%d, \"""",
        """	kern_logf("i915: parity LCD-D verdict: %s (GPU draws %u/%u, flips done %u/%u (+%u probe), cross-buffer variants %u/4, one modeset, stop %s, targets unmapped=%d, \"""")
k = rep(k, """		lcdb_summary.pass ? "PASS" : "FAIL", k->draws_ok, LCDD_ROUNDS + 1u, k->flips_done, LCDD_ROUNDS,""",
        """		lcdb_summary.pass ? "PASS" : "FAIL", k->draws_ok, LCDD_ROUNDS + 1u, k->flips_done, LCDD_ROUNDS, k->probe_flips, k->cross_ok,""")
save(L + "parity_lcd_kernel.c", k)
print("done")
