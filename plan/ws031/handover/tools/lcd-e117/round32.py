#!/usr/bin/env python3
"""WS031 E-117 round 32: the power-well IRQ hooks (gen8_irq_power_well_post_enable / _pre_disable), the IRQ
synchronisation they need, the pipe's vblank enable / disable (bdw_update_pipe_irq) and a vblank delivery path from
the real handler to a waiter.  usage: round32.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ---------------------------------------------------------------- power wells
h = load(P + "power_domains.h")
h = rep(h, "struct parity_cdclk_dev;" + NL + NL + "/*" + NL + " * Context for the power-well operation bodies",
        """struct parity_cdclk_dev;

/*
 * gen8_irq_power_well_post_enable() / gen8_irq_power_well_pre_disable(): implemented by the IRQ layer (irq.c) and
 * bound here, so this file does not depend on the IRQ device.  Both check intel_irqs_enabled() themselves.
 */
struct parity_pw_irq_ops {
	void (*post_enable)(void *ctx, unsigned pipe_mask);
	void (*pre_disable)(void *ctx, unsigned pipe_mask);
};

/*
 * Context for the power-well operation bodies""")
h = rep(h, "	unsigned irq_post_enable_calls;", "	unsigned irq_post_enable_calls;" + NL + "	unsigned irq_pre_disable_calls;" + NL +
        "	const struct parity_pw_irq_ops *irq_ops;   /* NULL: the IRQ layer is not bound (GPU-free tests) */" + NL + "	void *irq_ctx;")
save(P + "power_domains.h", h)
c = load(P + "power_domains.c")
c = rep(c, """	if (w->irq_pipe_mask != 0u) {
		/*
		 * gen8_irq_power_well_post_enable(): the reference takes the IRQ lock and
		 * only acts when intel_irqs_enabled().  Before P4 the handler is not
		 * installed (irqs_enabled == 0), so this is a guarded no-op -- but the
		 * guarded entry is present; we do NOT front-load the P4 handler install.
		 */
		if (c->irqs_enabled)
			c->irq_post_enable_calls++;   /* would program the pipe IRQ registers */
	}""", """	if (w->irq_pipe_mask != 0u) {
		/*
		 * gen8_irq_power_well_post_enable(): restores the pipe's IMR / IER from the IRQ state once the well is on
		 * (the pipe's interrupt registers live in the well).  It only acts when intel_irqs_enabled(): before P4
		 * installs the handler this is a no-op.  Until E-117 this entry only counted.
		 */
		if (c->irqs_enabled) {
			c->irq_post_enable_calls++;
			if (c->irq_ops != 0)
				c->irq_ops->post_enable(c->irq_ctx, w->irq_pipe_mask);
		}
	}""")
c = rep(c, "	/* hsw_power_well_pre_disable(): pipe-IRQ pre-disable (guarded) would run here. */",
        """	/*
	 * hsw_power_well_pre_disable() -> gen8_irq_power_well_pre_disable(): stop the pipe's interrupt sources and wait for
	 * a handler already running, BEFORE the well goes off (afterwards the handler would read a powered-off register).
	 */
	if (w->irq_pipe_mask != 0u && c->irqs_enabled) {
		c->irq_pre_disable_calls++;
		if (c->irq_ops != 0)
			c->irq_ops->pre_disable(c->irq_ctx, w->irq_pipe_mask);
	}""")
save(P + "power_domains.c", c)

# ---------------------------------------------------------------- IRQ layer
ih = load(P + "irq.h")
ih = rep(ih, "#include <stdint.h>" + NL, "#include <stdint.h>" + NL + '#include "backend_sync.h"' + NL)
ih = rep(ih, "struct parity_irq_dev {", """/*
 * The pipes' interrupt control and vblank delivery (the reference's irq_lock-protected de_irq_mask updates and a
 * minimal stand-in for the DRM vblank core: a per-pipe count of vblank interrupts, a wake-up for waiters, and the
 * enable reference).  Device-owned; bound with parity_irq_vblank_init() before the handler is installed.
 */
struct parity_irq_vblank {
	struct spinlock lock;                    /* dev_priv->irq_lock */
	int inited;
	struct parity_kcompletion wake[PARITY_IRQ_MAX_PIPES];
	volatile uint32_t count[PARITY_IRQ_MAX_PIPES];     /* vblank interrupts handled while enabled */
	volatile int enabled[PARITY_IRQ_MAX_PIPES];        /* drm vblank->enabled */
	unsigned refs[PARITY_IRQ_MAX_PIPES];               /* drm_vblank_get / _put references */
	unsigned enable_calls[PARITY_IRQ_MAX_PIPES], disable_calls[PARITY_IRQ_MAX_PIPES];
	/* the hooks, observed */
	unsigned post_enable_calls, pre_disable_calls, skipped_irqs_disabled;
	uint32_t post_imr[PARITY_IRQ_MAX_PIPES], post_ier[PARITY_IRQ_MAX_PIPES];
	unsigned sync_calls, sync_timeouts;
};

struct parity_irq_dev {""")
ih = rep(ih, "	/* Diagnostics. */" + NL + "	unsigned reset_writes;",
         "	/* handler entry / exit counts: intel_synchronize_irq() waits for exits to reach the entries it saw */" + NL +
         "	volatile unsigned handler_entries, handler_exits;" + NL +
         "	struct parity_irq_vblank *vbl;           /* NULL: vblank delivery not bound (GPU-free tests of the old parts) */" + NL + NL +
         "	/* Diagnostics." + " */" + NL + "	unsigned reset_writes;")
ih = rep(ih, "/* The pieces, exposed so the GPU-free tests can drive them individually. */",
         """/* bind the vblank state (before install) and the power-well hooks (pwc->irq_ops) */
void parity_irq_vblank_init(struct parity_irq_dev *d, struct parity_irq_vblank *v);
/* gen8_irq_power_well_post_enable() / gen8_irq_power_well_pre_disable() */
void parity_gen8_irq_power_well_post_enable(struct parity_irq_dev *d, unsigned pipe_mask);
void parity_gen8_irq_power_well_pre_disable(struct parity_irq_dev *d, unsigned pipe_mask);
/*
 * intel_synchronize_irq(): returns once every handler invocation that had started when it was called has finished.
 * The HAL offers no synchronize without detach (hal_irq_detach_msi_sync detaches); this is the same guarantee from the
 * handler's own entry / exit counts -- no HAL change.  0, or -ETIMEDOUT after 100 ms (a handler that never ends).
 */
int parity_intel_synchronize_irq(struct parity_irq_dev *d);
/* drm_vblank_get() / drm_vblank_put() with the reference's bdw_enable_vblank() / bdw_disable_vblank() and i915's
 * vblank_disable_immediate (the last put disables at once).  get: 0, or -EINVAL when interrupts are not enabled. */
int parity_drm_vblank_get(struct parity_irq_dev *d, unsigned pipe);
void parity_drm_vblank_put(struct parity_irq_dev *d, unsigned pipe);
/*
 * Wait for `n` NEW vblanks of `pipe`: the pipe's vblank interrupt count must advance by n from the moment of the call
 * AND read_frame (the pipe's hardware frame counter, may be NULL) must have advanced -- a stale pending bit delivered
 * at unmask, another pipe's interrupt or mere elapsed time never satisfy it.  Needs a vblank reference.
 * 0; -EINVAL no reference held; -ETIMEDOUT.
 */
int parity_wait_vblank(struct parity_irq_dev *d, unsigned pipe, unsigned n, unsigned timeout_ms,
	uint32_t (*read_frame)(void *ctx), void *frame_ctx, uint32_t *count_seen);

/* The pieces, exposed so the GPU-free tests can drive them individually. */""")
save(P + "irq.h", ih)

ic = load(P + "irq.c")
ic = rep(ic, "#include <errno.h>" + NL, "#include <errno.h>" + NL + "#include <kern/lock.h>" + NL + "#include <kern/sched.h>" + NL + '#include "wait.h"' + NL, )
# vblank delivery from the handler
ic = rep(ic, "		if (iir & GEN8_PIPE_VBLANK)" + NL + "			d->de_vblank_count[pipe]++;",
         "		if (iir & GEN8_PIPE_VBLANK) {" + NL + "			d->de_vblank_count[pipe]++;" + NL +
         "			/* intel_handle_vblank() -> drm_handle_vblank(): counted and waiters woken only while enabled */" + NL +
         "			if (d->vbl != 0 && d->vbl->inited && d->vbl->enabled[pipe]) {" + NL +
         "				d->vbl->count[pipe]++;" + NL + "				parity_kcomplete(&d->vbl->wake[pipe]);" + NL + "			}" + NL + "		}")
# handler entry / exit accounting
ic = rep(ic, "static void" + NL + "gen11_irq_handler(int irq, hal_irq_ack_t ack, void *arg)" + NL + "{",
         "static void" + NL + "gen11_irq_handler_body(int irq, hal_irq_ack_t ack, void *arg)" + NL + "{")
ic = rep(ic, "int" + NL + "parity_intel_irq_install(struct parity_irq_dev *d)",
         """/* the handler the HAL calls: the reference's body, bracketed by the counts intel_synchronize_irq() waits on */
static void
gen11_irq_handler(int irq, hal_irq_ack_t ack, void *arg)
{
	struct parity_irq_dev *d = (struct parity_irq_dev *)arg;

	(void)__atomic_add_fetch(&d->handler_entries, 1u, __ATOMIC_SEQ_CST);
	gen11_irq_handler_body(irq, ack, arg);
	(void)__atomic_add_fetch(&d->handler_exits, 1u, __ATOMIC_SEQ_CST);
}

/* ---------------- display/intel_display_irq.c: power-well hooks, pipe IRQ mask, vblank ---------------- */

void
parity_irq_vblank_init(struct parity_irq_dev *d, struct parity_irq_vblank *v)
{
	unsigned p;

	for (p = 0u; p < sizeof(*v); p++)
		((char *)v)[p] = 0;
	spin_init(&v->lock, LOCK_RANK_DEVICE, "parity-irq-lock");
	for (p = 0u; p < PARITY_IRQ_MAX_PIPES; p++)
		parity_kcompletion_init(&v->wake[p], "parity-vblank");
	v->inited = 1;
	d->vbl = v;
}

int
parity_intel_synchronize_irq(struct parity_irq_dev *d)
{
	unsigned seen = __atomic_load_n(&d->handler_entries, __ATOMIC_SEQ_CST);
	unsigned waited;

	if (d->vbl != 0)
		d->vbl->sync_calls++;
	for (waited = 0u; waited < 100000u; waited += 10u) {
		if ((int)(__atomic_load_n(&d->handler_exits, __ATOMIC_SEQ_CST) - seen) >= 0)
			return 0;
		if (parity_udelay(10u) != 0)
			break;
	}
	if (d->vbl != 0)
		d->vbl->sync_timeouts++;
	kern_logf("i915: parity intel_synchronize_irq: a handler invocation did not finish (entries=%u exits=%u)\\n",
		seen, d->handler_exits);
	return -ETIMEDOUT;
}

/* gen8_irq_power_well_post_enable() */
void
parity_gen8_irq_power_well_post_enable(struct parity_irq_dev *d, unsigned pipe_mask)
{
	uint32_t extra_ier = GEN8_PIPE_VBLANK | parity_gen8_de_pipe_underrun_mask(d->display_ver) |
		parity_gen8_de_pipe_flip_done_mask(d->display_ver);
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
	d->vbl->post_enable_calls++;
	for (pipe = 0u; pipe < PARITY_IRQ_MAX_PIPES; pipe++) {
		if ((d->pipe_mask & pipe_mask & (1u << pipe)) == 0u)
			continue;
		/* GEN8_IRQ_INIT_NDX(uncore, DE_PIPE, pipe, de_irq_mask[pipe], ~de_irq_mask[pipe] | extra_ier) */
		gen3_irq_init(d, GEN8_DE_PIPE_IMR(pipe), d->de_irq_mask[pipe],
			GEN8_DE_PIPE_IER(pipe), ~d->de_irq_mask[pipe] | extra_ier, GEN8_DE_PIPE_IIR(pipe));
		d->vbl->post_imr[pipe] = d->de_irq_mask[pipe];
		d->vbl->post_ier[pipe] = ~d->de_irq_mask[pipe] | extra_ier;
	}
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

/* gen8_irq_power_well_pre_disable() */
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
}

/* bdw_update_pipe_irq(): called with the IRQ lock held */
static void
bdw_update_pipe_irq(struct parity_irq_dev *d, unsigned pipe, uint32_t interrupt_mask, uint32_t enabled_irq_mask)
{
	uint32_t new_val;

	if (enabled_irq_mask & ~interrupt_mask)
		kern_logf("i915: parity WARN bdw_update_pipe_irq: enabled bits outside the mask\\n");
	if (!d->irqs_enabled) {
		kern_logf("i915: parity WARN bdw_update_pipe_irq: interrupts are not enabled\\n");
		return;
	}
	new_val = d->de_irq_mask[pipe];
	new_val &= ~interrupt_mask;
	new_val |= (~enabled_irq_mask & interrupt_mask);
	if (new_val != d->de_irq_mask[pipe]) {
		d->de_irq_mask[pipe] = new_val;
		osdep_mmio_write32(d->m, GEN8_DE_PIPE_IMR(pipe), d->de_irq_mask[pipe]);
		osdep_mmio_posting_read32(d->m, GEN8_DE_PIPE_IMR(pipe));
	}
}

/*
 * bdw_enable_vblank() / bdw_disable_vblank().  gen11_dsi_configure_te() returns false unless the crtc drives a DSI
 * command-mode panel (not on this path).  HAS_PSR(): the reference then calls drm_crtc_vblank_restore(), which
 * re-estimates the DRM vblank count from time across PSR self-refresh; there is no DRM vblank count here (the count
 * is of handled interrupts, and waits also require the hardware frame counter to move), and PSR is not enabled on
 * this path -- an explicit adaptation, not reached with PSR.
 */
static int
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
}

int
parity_wait_vblank(struct parity_irq_dev *d, unsigned pipe, unsigned n, unsigned timeout_ms,
	uint32_t (*read_frame)(void *ctx), void *frame_ctx, uint32_t *count_seen)
{
	uint64_t deadline;
	uint32_t start, frame0 = 0u;
	int ok = 0;

	if (d->vbl == 0 || !d->vbl->inited || pipe >= PARITY_IRQ_MAX_PIPES || d->vbl->refs[pipe] == 0u || n == 0u)
		return -EINVAL;
	parity_kreinit_completion(&d->vbl->wake[pipe]);
	start = d->vbl->count[pipe];
	if (read_frame != 0)
		frame0 = read_frame(frame_ctx);
	deadline = sched_ticks() + (timeout_ms + 9u) / 10u + 1u;
	for (;;) {
		if ((uint32_t)(d->vbl->count[pipe] - start) >= n &&
		    (read_frame == 0 || read_frame(frame_ctx) != frame0)) {
			ok = 1;
			break;
		}
		if (!parity_kwait(&d->vbl->wake[pipe], deadline))
			break;
	}
	if (count_seen != 0)
		*count_seen = d->vbl->count[pipe] - start;
	return ok ? 0 : -ETIMEDOUT;
}

static void
pw_irq_post_enable(void *ctx, unsigned pipe_mask)
{
	parity_gen8_irq_power_well_post_enable(ctx, pipe_mask);
}

static void
pw_irq_pre_disable(void *ctx, unsigned pipe_mask)
{
	parity_gen8_irq_power_well_pre_disable(ctx, pipe_mask);
}

const struct parity_pw_irq_ops parity_pw_irq_ops = { pw_irq_post_enable, pw_irq_pre_disable };

int
parity_intel_irq_install(struct parity_irq_dev *d)""")
# uninstall: no vblank reference may outlive the handler
ic = rep(ic, "	/*" + NL + "	 * Mask and disable every source FIRST -- detaching the handler does not" + NL + "	 * stop the device from sending messages." + NL + "	 */",
         "	if (d->vbl != 0 && d->vbl->inited) {" + NL + "		unsigned p;" + NL + NL +
         "		for (p = 0u; p < PARITY_IRQ_MAX_PIPES; p++)" + NL + "			if (d->vbl->refs[p] != 0u)" + NL +
         '				kern_logf("i915: parity WARN intel_irq_uninstall: pipe %u still holds %u vblank reference(s)\\n", p, d->vbl->refs[p]);' + NL +
         "	}" + NL +
         "	/*" + NL + "	 * Mask and disable every source FIRST -- detaching the handler does not" + NL + "	 * stop the device from sending messages." + NL + "	 */")
save(P + "irq.c", ic)
ih = load(P + "irq.h")
ih = rep(ih, "int parity_intel_synchronize_irq(struct parity_irq_dev *d);", "int parity_intel_synchronize_irq(struct parity_irq_dev *d);" + NL +
         "struct parity_pw_irq_ops;" + NL + "extern const struct parity_pw_irq_ops parity_pw_irq_ops;   /* for pwc->irq_ops, ctx = the irq device */")
save(P + "irq.h", ih)

# ---------------------------------------------------------------- probe: bind
pc = load(P + "probe.c")
pc = rep(pc, "		irqdev.pwc = &pwc;" + NL, "		irqdev.pwc = &pwc;" + NL +
         "		/* the pipes' interrupt control + vblank delivery, and the power-well hooks that restore / stop it */" + NL +
         "		parity_irq_vblank_init(&irqdev, &irq_vblank);" + NL +
         "		pwc.irq_ops = &parity_pw_irq_ops;" + NL + "		pwc.irq_ctx = &irqdev;" + NL)
pc = rep(pc, "	static struct parity_irq_dev irqdev;", "	static struct parity_irq_dev irqdev;" + NL + "	static struct parity_irq_vblank irq_vblank;")
save(P + "probe.c", pc)
print("done")
