#!/usr/bin/env python3
"""WS031 E-116 round 24: LCD-B body, kernel binding, show ktest: integration into the tree.
usage: round24.py <repo root> <dir with the new files>"""
import sys, shutil
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
src = sys.argv[2].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

for f in ("parity_lcd_show.h", "parity_lcd_show.c", "parity_lcd_regs.c", "parity_lcd_kernel.c", "lcd_show_ktest.c"):
    shutil.copy(src + f, root + L + f); print("copied", f)

open(root + L + "parity_lcd_kernel.h", "w").write("""/*
 * WS031 Linux-parity -- LCD-B on the real GPU (parity_lcd_kernel.c): one known picture on the panel, a finite
 * observation window, the reference's stop path, everything given back.  Build: -DPARITY_LCDB_TEST=1.
 * zedBSD project code.
 */
#ifndef PARITY_LCD_KERNEL_H
#define PARITY_LCD_KERNEL_H

#include <stdint.h>

struct parity_edp_device;
struct osdep_mmio;
struct parity_power_domains;
struct parity_pw_ctx;
struct parity_display_core;
struct parity_cdclk_dev;
struct parity_display_nogem;
struct parity_display_state;
struct parity_bw_state;
struct parity_dmc_dev;
struct parity_irq_dev;
struct parity_gt_mem;

#define PARITY_LCDB_PATTERN_ID  110u                     /* the asymmetric LCD-B picture (lcd_pattern.c) */
#define PARITY_LCDB_PATTERN_FNV 0xce63f20b23f91f85ull    /* its pinned hash at 1920x1080 (lcd-pattern-host.c) */
#ifndef PARITY_LCDB_WINDOW_MS
#define PARITY_LCDB_WINDOW_MS   20000u                   /* finite: long enough for the camera, then the stop path runs */
#endif

/* the objects of the normal initialisation this run reads and uses; none is copied, none is re-created */
struct parity_lcd_kernel_deps {
	struct parity_edp_device *edp;          /* resident panel: DPCD / EDID / LCD-A state, PPS, AUX, tick sleeps, VBT */
	struct osdep_mmio *mmio;
	struct parity_power_domains *pd;
	struct parity_pw_ctx *pwc;
	struct parity_display_core *dcore;      /* DBUF slices */
	struct parity_cdclk_dev *cdclk;
	struct parity_display_nogem *nogem;     /* watermark latencies, SAGV block time */
	struct parity_display_state *dstate;    /* the bandwidth object: QGV mask */
	struct parity_bw_state *bw;             /* QGV / PSF bandwidth table */
	struct parity_dmc_dev *dmc;
	struct parity_irq_dev *irq;
	struct parity_gt_mem *gm;
	int ipc_enabled;                        /* skl_watermark_ipc_init()'s result */
};

/* 0 = PASS (software state + hardware observation; the photograph is judged outside) */
int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);
/* 1 when a run left a buffer the display may still read: the outer teardown must keep DMA, bus mastering and scratch */
int parity_lcd_kernel_abandoned(void);

#endif /* PARITY_LCD_KERNEL_H */
""")
open(root + L + "lcd_show_ktest.h", "w").write("""/* WS031 Linux-parity -- GPU-free kernel checks of LCD-B's production body with a real scanout object (lcd_show_ktest.c). */
#ifndef PARITY_LCD_SHOW_KTEST_H
#define PARITY_LCD_SHOW_KTEST_H

#include <stdint.h>

struct drv_dma_device;
typedef void (*parity_lcd_show_ktest_check)(int ok, const char *msg);
void parity_lcd_show_ktest(parity_lcd_show_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask);

#endif /* PARITY_LCD_SHOW_KTEST_H */
""")

# ---- kernel binding: small corrections to the drafted file
k = load(L + "parity_lcd_kernel.c")
a = k.index("static void at_stage(void *ctx, int stage)")
b = k.index("static const char *kind_name(int kind)")
k = k[:a] + """static void at_stage(void *ctx, int stage)
{
	struct lcd_kernel *k = ctx;

	if (stage == PARITY_LCD_SHOW_ENABLE_RETURNED) {
		/* whatever the reference reports from here on belongs to the way down: counted apart */
		k->phase_cleanup = 1;
	} else if (stage == PARITY_LCD_SHOW_PICTURE_UP) {
		log_regs(k, "picture-up", 1);
		kern_logf("i915: parity LCD-B PICTURE UP: pattern id=%u on the panel; observation window %u ms starts now "
			"(take the photograph)\\n", PARITY_LCDB_PATTERN_ID, PARITY_LCDB_WINDOW_MS);
	} else if (stage == PARITY_LCD_SHOW_WINDOW_DONE) {
		kern_logf("i915: parity LCD-B observation window over; stopping through the reference's disable path\\n");
	} else if (stage == PARITY_LCD_SHOW_DISABLE_RETURNED) {
		log_regs(k, "after-disable", 0);
	}
}

""" + k[b:]
k = rep(k, "	c->wm_ipc_enabled = d->nogem->ipc_enabled;", "	c->wm_ipc_enabled = d->ipc_enabled;")
k = rep(k, "	c->cdclk_max_khz = d->cdclk->max_cdclk_freq;", "	c->cdclk_max_khz = parity_intel_max_cdclk_freq(d->cdclk);      /* intel_update_max_cdclk(), display version 11+ */")
k = rep(k, "	(void)rc;" + NL + "	return rep.pass ? 0 : -1;" + NL + "}", "	(void)rc;" + NL + "	lcdb_abandoned = rep.abandoned;" + NL + "	return rep.pass ? 0 : -1;" + NL + "}" + NL + NL +
        "int parity_lcd_kernel_abandoned(void)" + NL + "{" + NL + "	return lcdb_abandoned;" + NL + "}")
k = rep(k, "static struct parity_scanout lcdb_scanout;", "static int lcdb_abandoned;" + NL + "static struct parity_scanout lcdb_scanout;")
save(L + "parity_lcd_kernel.c", k)

# ---- observer: named registers, pipe state; more samples (a 20 s window has 40 rounds)
o = load(L + "parity_lcd_observe.h")
o = rep(o, "#define PARITY_LCD_OBS_MAX_SAMPLES 32u", "#define PARITY_LCD_OBS_MAX_SAMPLES 96u")
o = rep(o, "#endif /* PARITY_LCD_OBSERVE_H */", """/* TRANSCONF of the pipe's transcoder: returns whether its state bit says "active" */
int parity_lcd_observer_pipe_active(struct parity_lcd_observer *o, uint32_t *transconf);

/* the registers the run log reads back, by the reference's names (parity_lcd_regs.c) */
struct parity_lcd_named_reg {
	const char *name;
	uint32_t reg;
	int has_linux;                  /* Linux's dump of this machine has a value for it */
	uint32_t linux_value, compare_mask;
};
const struct parity_lcd_named_reg *parity_lcd_reg_table(int pipe, int port, int dpll_id, unsigned *n);
uint32_t parity_lcd_reg_by_name(const char *name);      /* pipe A / port A / DPLL 0; 0 = unknown name */

#endif /* PARITY_LCD_OBSERVE_H */""")
save(L + "parity_lcd_observe.h", o)
oc = load(L + "parity_lcd_observe.c")
oc = oc.rstrip(NL) + NL + """
int parity_lcd_observer_pipe_active(struct parity_lcd_observer *o, uint32_t *transconf)
{
	uint32_t v = o->hw->read32(o->hw->ctx, i915_mmio_reg_offset(TRANSCONF((enum transcoder)o->pipe)));

	if (transconf != 0)
		*transconf = v;
	return (v & TRANSCONF_STATE_ENABLE) != 0u;
}
"""
save(L + "parity_lcd_observe.c", oc)
rg = load(L + "parity_lcd_regs.c")
rg = rg.rstrip(NL) + NL + """
uint32_t parity_lcd_reg_by_name(const char *name)
{
	const struct parity_lcd_named_reg *t;
	unsigned n = 0, i;

	t = parity_lcd_reg_table(0, 0, 0, &n);
	for (i = 0; i < n; i++)
		if (strcmp(t[i].name, name) == 0)
			return t[i].reg;
	return 0u;
}
"""
save(L + "parity_lcd_regs.c", rg)

sh = load(L + "parity_lcd_show.c")
sh = rep(sh, "#define TRANSCONF_REG(pipe)     (0x70008u + 0x1000u * (unsigned)(pipe))" + NL + "#define TRANSCONF_STATE_BIT     (1u << 30)" + NL + NL, "")
sh = rep(sh, "		r->transconf_after_stop = env->hw->read32(env->hw->ctx, TRANSCONF_REG(env->pipe));" + NL +
         "		if (r->transconf_after_stop & TRANSCONF_STATE_BIT)" + NL,
         "		if (parity_lcd_observer_pipe_active(&r->obs, &r->transconf_after_stop))" + NL)
save(L + "parity_lcd_show.c", sh)

# ---- recorder: a tap on the observe points
t = load(L + "parity_lcd_trace.h")
t = rep(t, "	int first_error_at;                     /* index of the first ERROR entry, or -1 */",
        "	int first_error_at;                     /* index of the first ERROR entry, or -1 */" + NL +
        "	void (*tap)(void *ctx, int point);      /* optional: called at every observe() point, before the backend */" + NL +
        "	void *tap_ctx;")
save(L + "parity_lcd_trace.h", t)
tc = load(L + "parity_lcd_trace.c")
tc = rep(tc, "	if (e) e->a = (uint32_t)point;" + NL, "	if (e) e->a = (uint32_t)point;" + NL + "	if (t->tap != 0)" + NL + "		t->tap(t->tap_ctx, point);" + NL)
save(L + "parity_lcd_trace.c", tc)

# ---- existing bodies made reachable (no behaviour change)
dc = load(P + "display_core.c")
dc = rep(dc, "static void" + NL + "gen9_dbuf_enable(struct parity_display_core *dc)",
         "/* the same body for the modeset's intel_dbuf_pre/post_plane_update() */" + NL + "void" + NL +
         "parity_gen9_dbuf_slices_update(struct parity_display_core *dc, uint8_t req_slices)" + NL + "{" + NL +
         "	gen9_dbuf_slices_update(dc, req_slices & DBUF_SLICE_MASK);" + NL + "}" + NL + NL +
         "static void" + NL + "gen9_dbuf_enable(struct parity_display_core *dc)")
save(P + "display_core.c", dc)
dh = load(P + "display_core.h")
dh = rep(dh, "/* Exposed for tests. */", "/* gen9_dbuf_slices_update(): request exactly these slices; updates dbuf_enabled_slices under the power-domains lock. */" + NL +
         "void parity_gen9_dbuf_slices_update(struct parity_display_core *dc, uint8_t req_slices);" + NL + NL + "/* Exposed for tests. */")
save(P + "display_core.h", dh)
ds = load(P + "display_state.c")
ds = rep(ds, "unsigned" + NL + "parity_icl_max_bw_qgv_point_mask(", "/* icl_qgv_bw() for callers outside this file (the modeset's bandwidth check) */" + NL + "unsigned" + NL +
         "parity_icl_qgv_bw(const struct parity_bw_state *bw, int display_ver, int num_active_planes, int qgv_point)" + NL + "{" + NL +
         "	return icl_qgv_bw(bw, display_ver, num_active_planes, qgv_point);" + NL + "}" + NL + NL + "unsigned" + NL + "parity_icl_max_bw_qgv_point_mask(")
save(P + "display_state.c", ds)
dsh = load(P + "display_state.h")
dsh = rep(dsh, "uint16_t parity_icl_qgv_points_mask(const struct parity_bw_state *bw);",
          "uint16_t parity_icl_qgv_points_mask(const struct parity_bw_state *bw);" + NL +
          "unsigned parity_icl_qgv_bw(const struct parity_bw_state *bw, int display_ver, int num_active_planes, int qgv_point);")
save(P + "display_state.h", dsh)
ch = load(P + "cdclk.h")
ch = rep(ch, "uint8_t parity_tgl_calc_voltage_level(int cdclk);", "uint8_t parity_tgl_calc_voltage_level(int cdclk);" + NL +
         "/* intel_update_max_cdclk(), the display version 11+ branch: 648000 kHz on a 24 MHz reference, else 652800 kHz */" + NL +
         "uint32_t parity_intel_max_cdclk_freq(const struct parity_cdclk_dev *cd);")
save(P + "cdclk.h", ch)
cc = load(P + "cdclk.c")
cc = cc.rstrip(NL) + NL + """
/* intel_update_max_cdclk(): DISPLAY_VER >= 11 (not JSL / EHL) */
uint32_t
parity_intel_max_cdclk_freq(const struct parity_cdclk_dev *cd)
{
	return cd->hw.ref == 24000u ? 648000u : 652800u;
}
"""
save(P + "cdclk.c", cc)
vh = load(P + "vbt/parity_vbt.h")
vh = rep(vh, "	int edp_low_vswing;" + NL, "	int edp_low_vswing;" + NL + "	int edp_hobl;                     /* panel->vbt.edp.hobl */" + NL +
         "	int override_afc_startup;         /* display.vbt.override_afc_startup (general features, BDB 249+) */" + NL)
save(P + "vbt/parity_vbt.h", vh)
vg = load(P + "vbt/parity_vbt_glue.inc")
vg = rep(vg, "	out->edp_low_vswing = panel.vbt.edp.low_vswing;" + NL, "	out->edp_low_vswing = panel.vbt.edp.low_vswing;" + NL +
         "	out->edp_hobl = panel.vbt.edp.hobl;" + NL + "	out->override_afc_startup = vbt_i915.display.vbt.override_afc_startup;" + NL)
save(P + "vbt/parity_vbt_glue.inc", vg)

# ---- the test mode
bh = load(P + "bios.h")
bh = rep(bh, "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT PARITY_AUX_TEST",
         "#ifndef PARITY_LCDB_TEST" + NL + "#define PARITY_LCDB_TEST 0            /* LCD-B: one picture on the panel (implies the explicit VBT) */" + NL + "#endif" + NL +
         "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST)")
save(P + "bios.h", bh)
pc = load(P + "probe.c")
pc = rep(pc, '#include "lcd/lcd_hw_check.h"', '#include "lcd/lcd_hw_check.h"' + NL + '#include "lcd/parity_lcd_kernel.h"')
pc = rep(pc, "	if (PARITY_T3_TEST || PARITY_BL_TEST) {", """	/*
	 * LCD-B: one known picture on the panel through the reference's modeset, a finite observation window, the
	 * reference's stop path.  No GPU submission happens in this mode; the AUX diagnostics are not repeated.
	 */
	if (PARITY_LCDB_TEST) {
		static struct parity_lcd_kernel_deps lcdb;

		lcdb.edp = &edp_dev; lcdb.mmio = &mmio; lcdb.pd = &power_domains; lcdb.pwc = &pwc; lcdb.dcore = &dcore;
		lcdb.cdclk = &cdclk; lcdb.nogem = &nogem; lcdb.dstate = &dstate; lcdb.bw = &bw_state; lcdb.dmc = &dmc_dev;
		lcdb.irq = &irqdev; lcdb.gm = &gtmem; lcdb.ipc_enabled = dprobe.ipc_enabled;
		if (gtmem_inited)
			(void)parity_lcd_kernel_lcdb_run(&lcdb);
		else
			kern_logf("i915: parity LCD-B verdict: FAIL (no GT memory)\\n");
	}

	if (PARITY_T3_TEST || PARITY_BL_TEST) {""")
pc = rep(pc, "	if (scratch_created) {" + NL, "	if (parity_lcd_kernel_abandoned()) {" + NL +
         "		/* a scanout buffer may still be read by the display: its pages, the scratch page behind its guard PTEs, the DMA" + NL +
         "		 * device (IOMMU mappings), the GGTT and bus mastering all stay as they are -- a leak is the safe side */" + NL +
         '		kern_logf("i915: parity teardown: a scanout buffer was ABANDONED -- DMA device, scratch page, BARs and bus mastering are kept\\n");' + NL +
         "		goto lcdb_kept;" + NL + "	}" + NL + "	if (scratch_created) {" + NL)
pc = rep(pc, "	parity_dump_trace(&trace);" + NL + '	kern_logf("i915: parity attach end:', "lcdb_kept:" + NL + "	parity_dump_trace(&trace);" + NL + '	kern_logf("i915: parity attach end:')
save(P + "probe.c", pc)
kt = load(P + "ktest.c")
kt = rep(kt, '#include "lcd/lcd_modeset_ktest.h"', '#include "lcd/lcd_modeset_ktest.h"' + NL + '#include "lcd/lcd_show_ktest.h"')
kt = rep(kt, "			parity_scanout_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL, "			parity_scanout_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL +
         "			/* LCD-B's production body: the modeset commits + a real scanout object on the models */" + NL +
         "			parity_lcd_show_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL)
save(P + "ktest.c", kt)
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, "src/drivers/gpu/i915/parity/lcd/parity_lcd_observe.c ", "src/drivers/gpu/i915/parity/lcd/parity_lcd_observe.c src/drivers/gpu/i915/parity/lcd/parity_lcd_regs.c " +
         "src/drivers/gpu/i915/parity/lcd/parity_lcd_show.c src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c src/drivers/gpu/i915/parity/lcd/lcd_show_ktest.c ")
save("platform/amd64/vmunix.mk", mk)
print("done")
