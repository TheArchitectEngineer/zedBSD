#!/usr/bin/env python3
"""WS031 E-118 round 37: IRQ safety of the power-well hooks.
  - per-pipe admission gate in the handler (close -> stop source -> drain the pipe's in-flight count)
  - drain / synchronize: -ETIMEDOUT (a handler did not finish) apart from -EIO (the time base failed)
  - a failed drain refuses the well disable BEFORE POWER_REQUEST is cleared; the well stays owned; a latch refuses every
    later well disable (the handler may still need display power); the probe keeps the IRQ handler attached then
  - vblank references / enabled / count under the IRQ lock; one waiter per pipe
usage: round37.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ---------------------------------------------------------------- power wells
h = load(P + "power_domains.h")
h = rep(h, "	void (*pre_disable)(void *ctx, unsigned pipe_mask);", "	int (*pre_disable)(void *ctx, unsigned pipe_mask);     /* 0, or the drain failed: the well must NOT go off */")
h = rep(h, "	unsigned irq_pre_disable_calls;", """	unsigned irq_pre_disable_calls;
	/*
	 * Set when a pipe's interrupt drain failed before its well would have gone off.  From then on every well disable
	 * is refused (the handler that did not finish may still touch display registers), the well that was about to go
	 * off stays owned, and the probe's teardown keeps the IRQ handler attached and the device resources in place.
	 */
	int irq_sync_failed;
	unsigned disable_refusals;             /* well disables refused because of it */
	unsigned kept_wells;                   /* wells kept on by a refused disable */""")
save(P + "power_domains.h", h)
c = load(P + "power_domains.c")
c = rep(c, """	if (w->irq_pipe_mask != 0u && c->irqs_enabled) {
		c->irq_pre_disable_calls++;
		if (c->irq_ops != 0)
			c->irq_ops->pre_disable(c->irq_ctx, w->irq_pipe_mask);
	}""", """	if (w->irq_pipe_mask != 0u && c->irqs_enabled && !c->irq_sync_failed) {
		c->irq_pre_disable_calls++;
		if (c->irq_ops != 0 && c->irq_ops->pre_disable(c->irq_ctx, w->irq_pipe_mask) != 0) {
			/* the handler was not shown to have left the pipe: the well stays on, and so does everything else */
			c->irq_sync_failed = 1;
			kern_logf("i915: parity power well %s: pipe interrupt drain FAILED; the well is kept on and every later "
				"well disable is refused\\n", w->name);
		}
	}
	if (c->irq_sync_failed) {
		c->disable_refusals++;
		return -EBUSY;                  /* POWER_REQUEST is not touched */
	}""")
c = rep(c, "parity_power_well_disable(struct parity_power_well *w, struct parity_pw_ctx *c)" + NL + "{" + NL + "	unsigned reg;" + NL + "	uint32_t req, st, v;" + NL,
        "parity_power_well_disable(struct parity_power_well *w, struct parity_pw_ctx *c)" + NL + "{" + NL + "	unsigned reg;" + NL + "	uint32_t req, st, v;" + NL + NL +
        "	if (c->irq_sync_failed && w->ops != PARITY_PW_OPS_ALWAYS_ON) {" + NL + "		c->disable_refusals++;" + NL + "		return -EBUSY;" + NL + "	}" + NL)
c = rep(c, "	w->refcount--;" + NL + "	if (w->refcount == 0u)" + NL + "		(void)parity_power_well_disable(w, c);",
        "	w->refcount--;" + NL + "	if (w->refcount == 0u && parity_power_well_disable(w, c) == -EBUSY) {" + NL +
        "		/* the disable was refused: the well is still on and now owned by that failed stop (never re-enabled on a later get) */" + NL +
        "		w->refcount = 1u;" + NL + "		c->kept_wells++;" + NL + "	}")
save(P + "power_domains.c", c)

# ---------------------------------------------------------------- IRQ layer
ih = load(P + "irq.h")
ih = rep(ih, "	volatile unsigned handler_entries, handler_exits;",
         "	volatile unsigned handler_entries, handler_exits;" + NL +
         "	/* per-pipe admission: the handler enters a pipe's registers only while it is open, counted in pipe_inflight */" + NL +
         "	volatile int pipe_closed[PARITY_IRQ_MAX_PIPES];" + NL +
         "	volatile unsigned pipe_inflight[PARITY_IRQ_MAX_PIPES];" + NL +
         "	volatile unsigned pipe_refused[PARITY_IRQ_MAX_PIPES];     /* master bit seen while the pipe was closed */")
ih = rep(ih, "	unsigned sync_calls, sync_timeouts;", "	unsigned sync_calls, sync_timeouts, sync_time_faults, drain_timeouts, drain_time_faults;" + NL +
         "	int waiting[PARITY_IRQ_MAX_PIPES];                 /* one waiter per pipe (the wake-up is re-armed by it) */" + NL +
         "	unsigned second_waiter_refusals;")
ih = rep(ih, "void parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask);",
         "/* 0, or the pipes' in-flight handler work did not drain: -ETIMEDOUT (it did not finish) / -EIO (the time base failed) */" + NL +
         "int parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask);" + NL +
         "/* wait until no handler is inside the given pipes' registers (pipes stay as they are: open or closed) */" + NL +
         "int parity_irq_drain_pipes(struct parity_irq_dev *d, unsigned pipe_mask, unsigned timeout_us);")
ih = rep(ih, """ * intel_synchronize_irq(): returns once every handler invocation that had started when it was called has finished.
 * The HAL offers no synchronize without detach (hal_irq_detach_msi_sync detaches); this is the same guarantee from the
 * handler's own entry / exit counts -- no HAL change.  0, or -ETIMEDOUT after 100 ms (a handler that never ends).""",
         """ * intel_synchronize_irq() over the handler's own entry / exit counts: waits until the exits reach the entries seen at the
 * call.  This equals "every invocation started before the call has finished" ONLY if invocations of this handler never
 * overlap; the HAL (amd64) fixes an MSI's destination CPU at allocation and records one in_handler flag per vector, but
 * nothing in it states that an invocation cannot begin after the EOI while the previous one is still returning -- so the
 * power-well path does NOT rely on this function: it closes the pipe's admission and drains the pipe's own in-flight
 * count (parity_irq_drain_pipes).  0; -ETIMEDOUT after 100 ms; -EIO when the time base fails while waiting.""")
save(P + "irq.h", ih)

ic = load(P + "irq.c")
# handler: admission per pipe
ic = rep(ic, """		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;

		iir = rd(d, GEN8_DE_PIPE_IIR(pipe));""", """		if (!(master_ctl & GEN8_DE_PIPE_IRQ(pipe)))
			continue;
		/* admission: counted in first, then the gate is checked (the closing side sets the gate, then reads the count) */
		(void)__atomic_add_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
		if (__atomic_load_n(&d->pipe_closed[pipe], __ATOMIC_SEQ_CST)) {
			(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
			d->pipe_refused[pipe]++;
			continue;
		}

		iir = rd(d, GEN8_DE_PIPE_IIR(pipe));""")
ic = rep(ic, """		iir = rd(d, GEN8_DE_PIPE_IIR(pipe));
		if (iir == 0u) {
			d->de_lied_count++;   /* (DE PIPE) */
			continue;
		}""", """		iir = rd(d, GEN8_DE_PIPE_IIR(pipe));
		if (iir == 0u) {
			d->de_lied_count++;   /* (DE PIPE) */
			(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);
			continue;
		}""")
ic = rep(ic, """		fault_errors = iir & parity_gen8_de_pipe_fault_mask(d->display_ver);
		if (fault_errors != 0u) {
			d->de_fault_count++;
			kern_logf("i915: parity Fault errors on pipe %c: 0x%08x\\n",
				(char)('A' + pipe), fault_errors);
		}""", """		fault_errors = iir & parity_gen8_de_pipe_fault_mask(d->display_ver);
		if (fault_errors != 0u) {
			d->de_fault_count++;
			kern_logf("i915: parity Fault errors on pipe %c: 0x%08x\\n",
				(char)('A' + pipe), fault_errors);
		}
		(void)__atomic_sub_fetch(&d->pipe_inflight[pipe], 1u, __ATOMIC_SEQ_CST);""")
# vblank delivery under the lock
ic = rep(ic, """			if (d->vbl != 0 && d->vbl->inited && d->vbl->enabled[pipe]) {
				d->vbl->count[pipe]++;
				parity_kcomplete(&d->vbl->wake[pipe]);
			}""", """			if (d->vbl != 0 && d->vbl->inited) {
				unsigned long vf = spin_lock_irqsave(&d->vbl->lock);

				if (d->vbl->enabled[pipe]) {
					d->vbl->count[pipe]++;
					parity_kcomplete(&d->vbl->wake[pipe]);
				}
				spin_unlock_irqrestore(&d->vbl->lock, vf);
			}""")
# synchronize: time-base fault apart
ic = rep(ic, """	for (waited = 0u; waited < 100000u; waited += 10u) {
		if ((int)(__atomic_load_n(&d->handler_exits, __ATOMIC_SEQ_CST) - seen) >= 0)
			return 0;
		if (parity_udelay(10u) != 0)
			break;
	}
	if (d->vbl != 0)
		d->vbl->sync_timeouts++;""", """	for (waited = 0u; waited < 100000u; waited += 10u) {
		if ((int)(__atomic_load_n(&d->handler_exits, __ATOMIC_SEQ_CST) - seen) >= 0)
			return 0;
		if (parity_udelay(10u) != 0) {
			if (d->vbl != 0)
				d->vbl->sync_time_faults++;
			kern_logf("i915: parity intel_synchronize_irq: the time base failed while waiting (not a timeout)\\n");
			return -EIO;
		}
	}
	if (d->vbl != 0)
		d->vbl->sync_timeouts++;""")
# pre-disable: close, stop, drain, report
ic = rep(ic, """/* gen8_irq_power_well_pre_disable() */
void
parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask)
{
	unsigned long flags;
	unsigned pipe;

	if (d->vbl == 0 || !d->vbl->inited)
		return;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		d->vbl->skipped_irqs_disabled++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return;
	}
	d->vbl->pre_disable_calls++;
	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & pipe_mask & (1u << pipe)) == 0u)
			continue;
		/* GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe) */
		gen3_irq_reset(d, GEN8_DE_PIPE_IMR(pipe), GEN8_DE_PIPE_IIR(pipe), GEN8_DE_PIPE_IER(pipe));
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);

	/* make sure we're done processing display irqs */
	(void)parity_intel_synchronize_irq(d);
}""", """int
parity_irq_drain_pipes(struct parity_irq_dev *d, unsigned pipe_mask, unsigned timeout_us)
{
	unsigned waited, pipe, busy;

	for (waited = 0u;; waited += 10u) {
		busy = 0u;
		for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++)
			if ((pipe_mask & (1u << pipe)) != 0u && __atomic_load_n(&d->pipe_inflight[pipe], __ATOMIC_SEQ_CST) != 0u)
				busy |= 1u << pipe;
		if (busy == 0u)
			return 0;
		if (waited >= timeout_us) {
			if (d->vbl != 0)
				d->vbl->drain_timeouts++;
			kern_logf("i915: parity IRQ drain: the handler did not leave pipe(s) 0x%x within %u us\\n", busy, timeout_us);
			return -ETIMEDOUT;
		}
		if (parity_udelay(10u) != 0) {
			if (d->vbl != 0)
				d->vbl->drain_time_faults++;
			kern_logf("i915: parity IRQ drain: the time base failed while waiting (not a timeout)\\n");
			return -EIO;
		}
	}
}

/*
 * gen8_irq_power_well_pre_disable(): the reference resets the pipe's interrupt registers and then completes
 * intel_synchronize_irq() before the well goes off.  Here: close the pipe's admission (no new handler work enters its
 * registers), reset the source, then drain what had already entered.  The pipe stays closed until the next
 * post-enable.  A drain that does not complete is returned, so the caller keeps the well on.
 */
int
parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask)
{
	unsigned long flags;
	unsigned pipe, closing = 0u;

	if (d->vbl == 0 || !d->vbl->inited)
		return 0;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		d->vbl->skipped_irqs_disabled++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return 0;
	}
	d->vbl->pre_disable_calls++;
	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & pipe_mask & (1u << pipe)) == 0u)
			continue;
		__atomic_store_n(&d->pipe_closed[pipe], 1, __ATOMIC_SEQ_CST);
		closing |= 1u << pipe;
		/* GEN8_IRQ_RESET_NDX(uncore, DE_PIPE, pipe) */
		gen3_irq_reset(d, GEN8_DE_PIPE_IMR(pipe), GEN8_DE_PIPE_IIR(pipe), GEN8_DE_PIPE_IER(pipe));
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);

	/* make sure we're done processing display irqs (of these pipes) */
	d->vbl->sync_calls++;
	return parity_irq_drain_pipes(d, closing, 100000u);
}""")
# post-enable reopens the pipe
ic = rep(ic, """		d->vbl->post_imr[pipe] = d->de_irq_mask[pipe];
		d->vbl->post_ier[pipe] = ~d->de_irq_mask[pipe] | extra_ier;""", """		d->vbl->post_imr[pipe] = d->de_irq_mask[pipe];
		d->vbl->post_ier[pipe] = ~d->de_irq_mask[pipe] | extra_ier;
		__atomic_store_n(&d->pipe_closed[pipe], 0, __ATOMIC_SEQ_CST);     /* the handler may enter it again */""")
# vblank get / put / wait under the lock
ic = rep(ic, """static int
bdw_enable_vblank(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags = spin_lock_irqsave(&d->vbl->lock);

	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, GEN8_PIPE_VBLANK);
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	return 0;
}

static void
bdw_disable_vblank(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags = spin_lock_irqsave(&d->vbl->lock);

	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, 0u);
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

int
parity_drm_vblank_get(struct parity_irq_dev *d, unsigned pipe)
{
	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || !d->irqs_enabled)
		return -EINVAL;
	if (d->vbl->refs[pipe]++ == 0u) {
		d->vbl->enable_calls[pipe]++;
		d->vbl->enabled[pipe] = 1;
		(void)bdw_enable_vblank(d, pipe);
	}
	return 0;
}

void
parity_drm_vblank_put(struct parity_irq_dev *d, unsigned pipe)
{
	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || d->vbl->refs[pipe] == 0u)
		return;
	if (--d->vbl->refs[pipe] == 0u) {        /* vblank_disable_immediate */
		d->vbl->disable_calls[pipe]++;
		bdw_disable_vblank(d, pipe);
		d->vbl->enabled[pipe] = 0;
	}
}""", """/* bdw_enable_vblank() / bdw_disable_vblank() without their lock: the callers below hold it (one lock, one rule) */
static int
bdw_enable_vblank_locked(struct parity_irq_dev *d, unsigned pipe)
{
	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, GEN8_PIPE_VBLANK);
	return 0;
}

static void
bdw_disable_vblank_locked(struct parity_irq_dev *d, unsigned pipe)
{
	bdw_update_pipe_irq(d, pipe, GEN8_PIPE_VBLANK, 0u);
}

/*
 * The reference counts, the enabled state, the IMR bit and the handler's count all change under the IRQ lock.
 * ADAPTATION: the last put masks at once.  Linux's vblank_disable_immediate still disables through the vblank core
 * after the pending vblank / event processing; this limited path has no DRM events or workers, so it masks inside
 * put().  Not to be carried over to a present / flip path as it is.
 */
int
parity_drm_vblank_get(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES)
		return -EINVAL;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (!d->irqs_enabled) {
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EINVAL;
	}
	if (d->vbl->refs[pipe]++ == 0u) {
		d->vbl->enable_calls[pipe]++;
		d->vbl->enabled[pipe] = 1;
		(void)bdw_enable_vblank_locked(d, pipe);
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	return 0;
}

void
parity_drm_vblank_put(struct parity_irq_dev *d, unsigned pipe)
{
	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES)
		return;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (d->vbl->refs[pipe] != 0u && --d->vbl->refs[pipe] == 0u) {
		d->vbl->disable_calls[pipe]++;
		bdw_disable_vblank_locked(d, pipe);
		d->vbl->enabled[pipe] = 0;
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}""")
ic = rep(ic, """	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || d->vbl->refs[pipe] == 0u || n == 0u)
		return -EINVAL;
	parity_kreinit_completion(&d->vbl->wake[pipe]);
	start = d->vbl->count[pipe];""", """	unsigned long flags;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || n == 0u)
		return -EINVAL;
	flags = spin_lock_irqsave(&d->vbl->lock);
	if (d->vbl->refs[pipe] == 0u) {
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EINVAL;
	}
	if (d->vbl->waiting[pipe]) {            /* a second waiter would re-arm the first one's wake-up */
		d->vbl->second_waiter_refusals++;
		spin_unlock_irqrestore(&d->vbl->lock, flags);
		return -EBUSY;
	}
	d->vbl->waiting[pipe] = 1;
	parity_kreinit_completion(&d->vbl->wake[pipe]);
	start = d->vbl->count[pipe];
	spin_unlock_irqrestore(&d->vbl->lock, flags);""")
ic = rep(ic, """	if (count_seen != 0)
		*count_seen = d->vbl->count[pipe] - start;
	return ok ? 0 : -ETIMEDOUT;""", """	if (count_seen != 0)
		*count_seen = d->vbl->count[pipe] - start;
	flags = spin_lock_irqsave(&d->vbl->lock);
	d->vbl->waiting[pipe] = 0;
	spin_unlock_irqrestore(&d->vbl->lock, flags);
	return ok ? 0 : -ETIMEDOUT;""")
ic = rep(ic, """static void
pw_irq_pre_disable(void *ctx, unsigned pipe_mask)
{
	parity_gen8_irq_power_well_pre_disable(ctx, pipe_mask);
}""", """static int
pw_irq_pre_disable(void *ctx, unsigned pipe_mask)
{
	return parity_gen8_irq_power_well_pre_disable(ctx, pipe_mask);
}""")
save(P + "irq.c", ic)

# ---------------------------------------------------------------- probe: keep the handler when a drain failed
pc = load(P + "probe.c")
pc = rep(pc, "		parity_intel_irq_uninstall(&irqdev);",
         "		if (pwc.irq_sync_failed)" + NL +
         '			kern_logf("i915: parity teardown: a pipe interrupt drain failed earlier -- the IRQ handler stays attached, "' + NL +
         '				"display power and device resources are kept\\n");' + NL +
         "		else" + NL + "			parity_intel_irq_uninstall(&irqdev);")
pc = rep(pc, "	if (parity_lcd_kernel_abandoned()) {", "	if (parity_lcd_kernel_abandoned() || pwc.irq_sync_failed) {")
save(P + "probe.c", pc)

# ---------------------------------------------------------------- LCD binding: a refused well disable is a stop failure
k = load(L + "parity_lcd_kernel.c")
k = rep(k, """	k->power_refs[domain]--;
	parity_display_power_put(k->d->pd, (enum parity_power_domain)domain, k->d->pwc);
}""", """	k->power_refs[domain]--;
	{
		unsigned before = k->d->pwc->disable_refusals;

		parity_display_power_put(k->d->pd, (enum parity_power_domain)domain, k->d->pwc);
		if (k->d->pwc->disable_refusals != before)
			parity_lcd_backend_fault("a power well was NOT turned off: the pipe interrupt drain failed (the stop is not "
				"confirmed; display power is kept)\\n");
	}
}""")
k = rep(k, "int parity_lcd_kernel_abandoned(void)" + NL + "{" + NL + "	return parity_lcd_show_retained() || parity_lcd_modeset_retained();",
        "int parity_lcd_kernel_abandoned(void)" + NL + "{" + NL + "	return parity_lcd_show_retained() || parity_lcd_modeset_retained();")
save(L + "parity_lcd_kernel.c", k)

# the disable commit: errors in its tail (power put refused) make the stop unconfirmed
r = load(L + "parity_lcd_modeset.c")
r = rep(r, """	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it");
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);
	/* skl_commit_modeset_enables(): nothing is enabled by this commit */
	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	ms.wm.old_dbuf = ms.wm.new_dbuf;
	observe(PARITY_LCD_OBS_COMMIT_END);
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return PARITY_LCD_MS_OK;""", """	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it");
	before = ms_errors;
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);
	/* skl_commit_modeset_enables(): nothing is enabled by this commit */
	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	ms.wm.old_dbuf = ms.wm.new_dbuf;
	observe(PARITY_LCD_OBS_COMMIT_END);
	if (ms_errors != before) {
		/* e.g. the pipe's power well could not be turned off (its interrupt drain failed): the stop is not confirmed;
		 * DC_OFF stays held and the caller keeps the buffer */
		ms.stop_unconfirmed = 1;
		PARITY_LCD_DECIDED(&ms.i915, "commit tail reported an error (power not released): DC_OFF kept, stop unconfirmed");
		return PARITY_LCD_MS_ERRORS;
	}
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return PARITY_LCD_MS_OK;""")
save(L + "parity_lcd_modeset.c", r)

# model: a domain whose release is refused (as the binding reports a refused well disable)
mh = load(L + "lcd_fake_hw.h")
mh = rep(mh, "	int fault_power_get;", "	int fault_put_refused_domain;           /* domain + 1 whose put is refused like a kept power well; 0 = none */" + NL + "	int fault_power_get;")
save(L + "lcd_fake_hw.h", mh)
mc = load(L + "lcd_fake_hw.c")
mc = rep(mc, "	(void)wakeref;" + NL + "	hw->power_puts++;", "	(void)wakeref;" + NL + "	hw->power_puts++;" + NL +
         "	if (hw->fault_put_refused_domain == domain + 1) {" + NL +
         '		parity_lcd_backend_fault("model: power well kept on (pipe interrupt drain failed)" "' + BS + 'n");' + NL +
         "		return;                         /* the reference stays held */" + NL + "	}")
save(L + "lcd_fake_hw.c", mc)
print("done")
