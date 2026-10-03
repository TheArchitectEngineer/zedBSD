#!/usr/bin/env python3
"""WS031 E-119 round 42: the E-118 review fixes.
  1 GPU use not confirmed -> the device-side retained state (not only the scanout's keep); every object the unfinished
    request may use is kept (keep = 1) and the probe's GT teardown is skipped; runner retained=1
  2 one release contract for the full-HD render: every mapping (state, batch, texture, RT) back to scratch, the GT TLB
    invalidation (mmio_invalidate_full, gt/intel_tlb.c) BEFORE any backing is freed; the result is re-verified from the
    tables on every call (no double counting); ownership kept until it succeeds; the scanout is freed only after it
  3 show_prepared: display_acquired apart from display_released (never handed to the display -> the owner may reclaim)
  4 vblank: count / state reads through one snapshot helper under the lock
  5 probe: the uninstall log only when it ran
usage: round42.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
I = "src/drivers/gpu/i915/"
P = I + "parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ================= GT TLB invalidation =================
open(root + P + "gt_tlb.h", "w").write("""/*
 * WS031 Linux-parity -- the GT TLB invalidation after PTEs were removed (gt/intel_tlb.c: mmio_invalidate_full for
 * graphics IP 12.0 / 12.10, the register table of intel_engine_init_tlb_invalidation()).  zedBSD project code.
 */
#ifndef PARITY_GT_TLB_H
#define PARITY_GT_TLB_H

#include <stdint.h>

struct parity_gt_engines;
struct osdep_mmio;
struct spinlock;

struct parity_gt_tlb {
	uint32_t seqno;                 /* gt->tlb.seqno: even; +2 after every completed full invalidation */
	unsigned invalidations, engines_invalidated, timeouts, time_faults, fw_failures;
};

/* the engine's invalidation register, its request word and its done bit (gen12 table); 0 = no such engine class */
int parity_gt_tlb_engine_reg(int class, int instance, uint32_t *reg, uint32_t *request, uint32_t *done);
/*
 * intel_gt_invalidate_tlb_full(): every engine's TLB, plus Wa_2207587034 (GEN12_OA_TLB_INV_CR) on ADL-P, then each
 * engine's done bit.  ADAPTATION: the reference skips engines that are not awake (intel_engine_pm_is_awake) and a GT
 * that is asleep, because their TLBs do not survive power-down.  Here "parked" is a software state that does not
 * power anything down, so no engine is skipped.  Forcewake is taken for the writes; the uncore lock serialises them
 * with a reset.  0; -ETIMEDOUT (an engine did not report done in 4 ms); -EIO (the time base / forcewake failed).
 */
int parity_gt_invalidate_tlb_full(struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m,
	struct spinlock *uncore_lock);

#endif /* PARITY_GT_TLB_H */
""")
open(root + P + "gt_tlb.c", "w").write("""/*
 * WS031 Linux-parity -- the GT TLB invalidation (see gt_tlb.h).  zedBSD project code; register numbers and the
 * request / done encodings are those of the reference (gt/intel_gt_regs.h, i915_perf_oa_regs.h,
 * gt/intel_engine_cs.c intel_engine_init_tlb_invalidation(), gt/intel_tlb.c mmio_invalidate_full()).
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <errno.h>
#include "osdep/mmio.h"
#include "wait.h"
#include "gt_mmio.h"
#include "gt_resume.h"
#include "gt_tlb.h"

#define GEN12_GFX_TLB_INV_CR      0xced8u
#define GEN12_VD_TLB_INV_CR       0xcedcu
#define GEN12_VE_TLB_INV_CR       0xcee0u
#define GEN12_BLT_TLB_INV_CR      0xcee4u
#define GEN12_COMPCTX_TLB_INV_CR  0xcf04u
#define GEN12_OA_TLB_INV_CR       0xceecu
#define TLB_INVAL_TIMEOUT_US      100u
#define TLB_INVAL_TIMEOUT_MS      4u
#define COMPUTE_CLASS             5

int
parity_gt_tlb_engine_reg(int class, int instance, uint32_t *reg, uint32_t *request, uint32_t *done)
{
	uint32_t r, val;

	switch (class) {
	case PARITY_RENDER_CLASS:            r = GEN12_GFX_TLB_INV_CR; break;
	case PARITY_VIDEO_DECODE_CLASS:      r = GEN12_VD_TLB_INV_CR; break;
	case PARITY_VIDEO_ENHANCEMENT_CLASS: r = GEN12_VE_TLB_INV_CR; break;
	case PARITY_COPY_ENGINE_CLASS:       r = GEN12_BLT_TLB_INV_CR; break;
	case COMPUTE_CLASS:                  r = GEN12_COMPCTX_TLB_INV_CR; break;
	default:                             return -ERANGE;
	}
	if (instance < 0 || instance > 15)
		return -ERANGE;
	val = 1u << (unsigned)instance;
	*reg = r;
	*done = val;
	/* GRAPHICS_VER >= 12: VD / VE / compute use a masked register */
	*request = (class == PARITY_VIDEO_DECODE_CLASS || class == PARITY_VIDEO_ENHANCEMENT_CLASS || class == COMPUTE_CLASS) ?
		(val << 16) | val : val;
	return 0;
}

int
parity_gt_invalidate_tlb_full(struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m,
	struct spinlock *uncore_lock)
{
	static const int fwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT, OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2,
		OSDEP_FW_MEDIA_VEBOX0 };
	uint32_t reg[16], done[16], request;
	unsigned i, n = 0u, held = 0u;
	unsigned long flags;
	int rc = 0;

	if (tlb == 0 || es == 0 || m == 0 || uncore_lock == 0 || es->n > 16u)
		return -EINVAL;
	/* intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL) */
	while (held < 5u && osdep_fw_get(m, fwd[held]) == 0)
		held++;
	if (held != 5u) {
		tlb->fw_failures++;
		rc = -EIO;
		goto out;
	}
	/* serialise invalidate with GT reset */
	flags = spin_lock_irqsave(uncore_lock);
	for (i = 0u; i < es->n; i++) {
		if (parity_gt_tlb_engine_reg(es->ge[i].info->class, es->ge[i].info->instance, &reg[n], &request, &done[n]) != 0)
			continue;
		osdep_mmio_raw_write32(m, reg[n], request);
		n++;
	}
	/* Wa_2207587034:tgl,dg1,rkl,adl-s,adl-p */
	if (n != 0u)
		osdep_mmio_raw_write32(m, GEN12_OA_TLB_INV_CR, 1u);
	spin_unlock_irqrestore(uncore_lock, flags);

	for (i = 0u; i < n; i++) {
		int w = parity_wait_reg(m, reg[i], done[i], 0u, TLB_INVAL_TIMEOUT_US, TLB_INVAL_TIMEOUT_MS, 0);

		if (w == -ETIMEDOUT) {
			tlb->timeouts++;
			kern_logf("i915: parity TLB invalidation did not complete in %ums (reg 0x%x done 0x%x)\\n",
				TLB_INVAL_TIMEOUT_MS, reg[i], done[i]);
			if (rc == 0)
				rc = -ETIMEDOUT;
		} else if (w != 0) {
			tlb->time_faults++;
			rc = -EIO;
		}
	}
	tlb->engines_invalidated = n;
	if (rc == 0) {
		tlb->invalidations++;
		tlb->seqno += 2u;       /* write_seqcount_invalidate(&gt->tlb.seqno) */
	}
out:
	while (held-- > 0u)
		osdep_fw_put(m, fwd[held]);
	return rc;
}
""")
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, "src/drivers/gpu/i915/parity/gt_mem.c ", "src/drivers/gpu/i915/parity/gt_mem.c src/drivers/gpu/i915/parity/gt_tlb.c ")
save("platform/amd64/vmunix.mk", mk)

# ================= the full-HD render: one release contract =================
eh = load(P + "eu_test.h")
eh = rep(eh, "	int gpu_done;                          /* request retired and engine parked: the GPU no longer uses the target */",
         "	int gpu_done;                          /* request retired and engine parked: the GPU no longer uses the target */" + NL +
         "	/* release: every mapping, the TLB, then the objects -- re-verified from the tables on every call */" + NL +
         "	unsigned maps_total, maps_scratch;     /* PTEs this draw wrote / found back at scratch by the LAST release call */" + NL +
         "	int tlb_rc, released;                  /* released: mappings gone, TLB invalidated, own objects freed */" + NL +
         "	uint64_t first_unreleased_va;          /* the first PTE still pointing at a page, 0 = none */" + NL +
         "	unsigned release_calls;")
eh = rep(eh, "/* PTEs of the target back to scratch, own objects freed -- only when gpu_done; the target itself is never touched */" + NL +
         "int parity_fhd_render_release(struct parity_fhd_render *x, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm);",
         """/*
 * The release, in the reference's order: (the request already retired) every PTE the draw wrote -- state, batch, texture
 * and the render target -- back to scratch; the GT TLB invalidated; only then the draw's own objects are freed.  May be
 * called again after a failure: the PTEs are re-read and counted afresh each time, the ownership stays until everything
 * is done.  -EBUSY: the GPU is not shown to be done (nothing is touched).  The render target is never freed here: its
 * owner may free it once this returned 0.
 */
struct parity_gt_tlb;
int parity_fhd_render_release(struct parity_fhd_render *x, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock);
/* after a GPU hang that was not shown to be over: every object the request may use is kept for ever (keep = 1) */
void parity_fhd_render_keep(struct parity_fhd_render *x);""")
save(P + "eu_test.h", eh)
e = load(P + "eu_test.c")
a = e.index("int" + NL + "parity_fhd_render_release(")
b = e.index("/* ---------------- T3: texture update, binding switch, redraw, new context ---------------- */")
e = e[:a] + r'''/* every PTE the draw wrote, in the order it wrote them */
static unsigned
fhd_maps(const struct parity_fhd_render *x, uint64_t *va, unsigned cap)
{
	unsigned n = 0u, p;

	if (n < cap) va[n++] = PARITY_EU_SHARED_VA;
	if (n < cap) va[n++] = PARITY_EU_BATCH_VA;
	if (n < cap) va[n++] = I915_TEX_FIXTURE_TEX_VA;
	for (p = 0u; p < x->rt_pages_mapped && n < cap; p++)
		va[n++] = I915_TEX_FHD_RT_VA + (uint64_t)p * 4096u;
	return n;
}

int
parity_fhd_render_release(struct parity_fhd_render *x, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_tlb *tlb, struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock)
{
	static uint64_t va[3u + 2048u];
	struct parity_gt_ppgtt_walk w;
	unsigned n, i;

	if (x == 0 || gm == 0 || vm == 0 || tlb == 0 || es == 0 || m == 0)
		return -EINVAL;
	if (x->released)
		return 0;
	x->release_calls++;
	if (!x->gpu_done && x->t.submitted)
		return -EBUSY;                  /* the GPU may still use the target and the state: nothing is released */
	/* 1. every mapping back to scratch; 2. re-read the tables: the count is THIS call's verification, never a sum */
	n = fhd_maps(x, va, (unsigned)(sizeof(va) / sizeof(va[0])));
	for (i = 0u; i < n; i++)
		(void)parity_gt_ppgtt_insert_scratch(vm, va[i]);
	x->maps_total = n;
	x->maps_scratch = 0u;
	x->first_unreleased_va = 0u;
	for (i = 0u; i < n; i++) {
		if (parity_gt_ppgtt_walk(vm, va[i], &w) == 0 && w.levels == 4 && w.scratch[3])
			x->maps_scratch++;
		else if (x->first_unreleased_va == 0u)
			x->first_unreleased_va = va[i];
	}
	x->rt_pages_cleared = x->maps_scratch >= 3u ? x->maps_scratch - 3u : 0u;
	if (x->maps_scratch != n)
		return -EIO;                    /* some PTE still names a page: nothing is freed, ownership stays */
	/* 3. the TLB, before any page these entries named may be reused */
	x->tlb_rc = parity_gt_invalidate_tlb_full(tlb, es, m, uncore_lock);
	if (x->tlb_rc != 0)
		return x->tlb_rc;               /* the old translations may survive: nothing is freed */
	/* 4. only now the draw's own objects */
	if (x->tex != 0) {
		parity_gt_object_destroy(gm, x->tex);
		x->tex = 0;
	}
	parity_eu_test_release(&x->t, gm);
	x->rt = 0;
	x->released = 1;
	return 0;
}

void
parity_fhd_render_keep(struct parity_fhd_render *x)
{
	if (x == 0)
		return;
	if (x->tex != 0) x->tex->keep = 1;
	if (x->t.shared != 0) x->t.shared->keep = 1;
	if (x->t.batch != 0) x->t.batch->keep = 1;
	if (x->t.tl_page != 0) x->t.tl_page->keep = 1;
	if (x->rt != 0) x->rt->keep = 1;
	parity_lrc_keep(&x->t.ce);
}

''' + e[b:]
e = rep(e, '#include "eu_test.h"', '#include "eu_test.h"' + NL + '#include "gt_tlb.h"') if '#include "eu_test.h"' in e else e
save(P + "eu_test.c", e)

# ================= the device latch for a GPU that is not shown to be done =================
sh = load(L + "parity_lcd_show.h")
sh = rep(sh, "	int display_released;", "	int display_acquired;                   /* the buffer was handed to the display (the enable commit was attempted) */" + NL + "	int display_released;")
sh = rep(sh, "int parity_lcd_show_retained(void);", """int parity_lcd_show_retained(void);
/*
 * A buffer's OTHER user (the GPU) is not shown to have stopped using it: the same device-side retained state as an
 * unconfirmed display stop.  `gm` identifies the memory manager that owns the kept objects; the latch can only be
 * dropped for a manager that has since been finalised (its objects are gone with it: GPU-free tests).
 */
void parity_lcd_show_retain_gpu(const void *gm, const char *why);
int parity_lcd_show_gpu_retained(void);
int parity_lcd_show_discard_gpu_model(const void *gm, int gm_finalised);""")
save(L + "parity_lcd_show.h", sh)
s = load(L + "parity_lcd_show.c")
s = rep(s, "int parity_lcd_show_retained(void)" + NL + "{" + NL + "	return show_retained;" + NL + "}",
        """static int show_gpu_retained;
static const void *show_gpu_retained_gm;
static const char *show_gpu_retained_why;

int parity_lcd_show_retained(void)
{
	return show_retained || show_gpu_retained;
}

void parity_lcd_show_retain_gpu(const void *gm, const char *why)
{
	show_gpu_retained = 1;
	show_gpu_retained_gm = gm;
	show_gpu_retained_why = why;
}

int parity_lcd_show_gpu_retained(void)
{
	return show_gpu_retained;
}

int parity_lcd_show_discard_gpu_model(const void *gm, int gm_finalised)
{
	if (!show_gpu_retained)
		return 0;
	if (gm == 0 || gm != show_gpu_retained_gm || !gm_finalised)
		return -1;
	show_gpu_retained = 0;
	show_gpu_retained_gm = 0;
	show_gpu_retained_why = 0;
	return 0;
}""")
s = rep(s, "	if (show_retained || parity_lcd_modeset_retained())" + NL + "		return -EBUSY;                  /* an earlier run left resources the display may still read: nothing is touched */" + NL + "	memset(r, 0, sizeof(*r));",
        "	if (parity_lcd_show_retained() || parity_lcd_modeset_retained())" + NL + "		return -EBUSY;                  /* an earlier run left resources the display or the GPU may still use: nothing is touched */" + NL + "	memset(r, 0, sizeof(*r));")
s = rep(s, "	parity_lcd_trace_phase(&show_trace, \"commit: enable\");" + NL + "	r->enable_rc = parity_lcd_modeset_commit_enable();",
        "	r->display_acquired = 1;" + NL + "	parity_lcd_trace_phase(&show_trace, \"commit: enable\");" + NL + "	r->enable_rc = parity_lcd_modeset_commit_enable();")
s = rep(s, "	if (show_retained == 0)" + NL, "	if (show_retained == 0)" + NL) if "	if (show_retained == 0)" + NL in s else s
save(L + "parity_lcd_show.c", s)

# ================= LCD-G: the failure paths through the same contract =================
k = load(L + "parity_lcd_kernel.c")
k = rep(k, '#include "../eu_test.h"', '#include "../eu_test.h"' + NL + '#include "../gt_tlb.h"')
k = rep(k, "static struct parity_fhd_render lcdg_render;", "static struct parity_fhd_render lcdg_render;" + NL + "static struct parity_gt_tlb lcdg_tlb;" + NL + NL +
        """/*
 * The buffer's two users are done?  display: never acquired, or its stop confirmed; GPU: never submitted, or retired
 * and parked.  Then (and only then) the render mappings, the TLB, the draw's objects and finally the buffer go -- each
 * step only after the previous one succeeded.  Otherwise everything the unfinished user may touch is kept and the
 * device-side retained state is set.  Returns 1 when the buffer was freed.
 */
int parity_lcdg_finish(struct parity_fhd_render *fr, struct parity_scanout *so, int display_acquired, int display_released,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb, struct parity_gt_engines *es,
	struct osdep_mmio *m, struct spinlock *uncore_lock, int *render_rc)
{
	int rc;

	*render_rc = -EBUSY;
	if (fr->t.submitted && !fr->gpu_done) {
		/* the GPU is not shown to have let go: the buffer, the state, the batch, the texture, the context stay */
		parity_fhd_render_keep(fr);
		if (so->state >= PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(so);
		parity_lcd_show_retain_gpu(gm, "the GPU request using the buffer did not finish");
		return 0;
	}
	if (display_acquired && !display_released)
		return 0;                       /* show_prepared already abandoned it and set the latch */
	rc = parity_fhd_render_release(fr, gm, vm, tlb, es, m, uncore_lock);
	*render_rc = rc;
	if (rc != 0) {
		/* mappings or the TLB not shown to be gone: the pages may still be translated -- keep the buffer */
		if (so->state >= PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(so);
		parity_lcd_show_retain_gpu(gm, "the render mappings / TLB could not be shown released");
		return 0;
	}
	if (parity_scanout_unpin(so) != 0 || parity_scanout_destroy(so) != 0)
		return 0;
	return 1;
}
""")
# the render failure branch
a = k.index("	if (rc != 0 || fr->t.outcome != PARITY_EU_PASS || !same_backing) {")
b = k.index("	/* ---- show THAT object: no CPU write since the draw ---- */")
k = k[:a] + """	if (rc != 0 || fr->t.outcome != PARITY_EU_PASS || !same_backing) {
		int rrc;

		released = parity_lcdg_finish(fr, so, 0, 0, d->gm, d->vm, &lcdg_tlb, d->es, d->mmio, d->uncore_lock, &rrc);
		lcdb_summary.first_anomaly = "the GPU image is not the expected one (not shown)";
		lcdb_summary.first_anomaly_stage = "render";
		lcdb_summary.retained = parity_lcd_show_retained();
		kern_logf("i915: parity LCD-G verdict: FAIL (the GPU draw did not produce the expected image; nothing was shown; "
			"gpu_done=%d render release rc=%d buffer freed=%d retained=%d)\\n", fr->gpu_done, rrc, released,
			lcdb_summary.retained);
		return -1;
	}

""" + k[b:]
k = rep(k, """	if (rep.display_released) {
		render_released = parity_fhd_render_release(fr, d->gm, d->vm) == 0;
		if (render_released && parity_scanout_unpin(so) == 0 && parity_scanout_destroy(so) == 0)
			released = 1;
	}""", """	{
		int rrc;

		released = parity_lcdg_finish(fr, so, rep.display_acquired, rep.display_released, d->gm, d->vm, &lcdg_tlb, d->es,
			d->mmio, d->uncore_lock, &rrc);
		render_released = rrc == 0;
	}""")
k = rep(k, """	kern_logf("i915: parity LCD-G stop / release: display released=%d abandoned=%d | render PTEs cleared %u/%u released=%d | \"""",
        """	kern_logf("i915: parity LCD-G release: mappings back to scratch %u/%u (first left 0x%llx) TLB rc=%d (invalidations %u, engines %u, "
		"timeouts %u) release calls %u\\n", fr->maps_scratch, fr->maps_total, (unsigned long long)fr->first_unreleased_va, fr->tlb_rc,
		lcdg_tlb.invalidations, lcdg_tlb.engines_invalidated, lcdg_tlb.timeouts, fr->release_calls);
	kern_logf("i915: parity LCD-G stop / release: display released=%d abandoned=%d | render PTEs cleared %u/%u released=%d | \"""")
k = rep(k, "	lcdb_summary.retained = parity_lcd_show_retained();" + NL + "	kern_logf(\"i915: parity LCD-G verdict: %s",
        "	lcdb_summary.retained = parity_lcd_show_retained();" + NL + "	if (rep.first_anomaly == 0 && !rep.display_acquired)" + NL +
        '		lcdb_summary.first_anomaly = "the display part did not start";' + NL + "	kern_logf(\"i915: parity LCD-G verdict: %s")
save(L + "parity_lcd_kernel.c", k)
kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);", """int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);
/* the release decision after a draw and (maybe) a show -- exported for the GPU-free test */
struct parity_fhd_render;
struct parity_scanout;
struct parity_gt_tlb;
struct parity_gt_engines;
int parity_lcdg_finish(struct parity_fhd_render *fr, struct parity_scanout *so, int display_acquired, int display_released,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb, struct parity_gt_engines *es,
	struct osdep_mmio *m, struct spinlock *uncore_lock, int *render_rc);
/* 1 while the GPU is not shown to be done with retained objects: the probe skips the GT teardown too */
int parity_lcd_kernel_gpu_retained(void);""")
save(L + "parity_lcd_kernel.h", kh)
k = load(L + "parity_lcd_kernel.c")
k = rep(k, "void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out)", "int parity_lcd_kernel_gpu_retained(void)" + NL + "{" + NL +
        "	return parity_lcd_show_gpu_retained();" + NL + "}" + NL + NL + "void parity_lcd_kernel_summary(struct parity_lcd_test_summary *out)")
save(L + "parity_lcd_kernel.c", k)

# ================= probe: GT teardown skipped while the GPU may still use kept objects; honest IRQ log =================
pc = load(P + "probe.c")
pc = rep(pc, "	if (gtmem_inited) {" + NL + "		parity_gt_ppgtt_destroy(&gtmem, &gtpp);" + NL + "		parity_gt_mem_fini(&gtmem);",
         "	if (gtmem_inited && parity_lcd_kernel_gpu_retained()) {" + NL +
         '		kern_logf("i915: parity teardown: the GPU was not shown to be done with a kept buffer -- the vm, its tables "' + NL +
         '			"and every GT object stay (resources_retained=1)\\n");' + NL +
         "	} else if (gtmem_inited) {" + NL + "		parity_gt_ppgtt_destroy(&gtmem, &gtpp);" + NL + "		parity_gt_mem_fini(&gtmem);")
save(P + "probe.c", pc)
print("done")
