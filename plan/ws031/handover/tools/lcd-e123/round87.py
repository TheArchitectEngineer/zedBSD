#!/usr/bin/env python3
"""WS031 E-123 round 87: the DPLLs belong to the device, not to one modeset.
The two combo PLLs are one pool that every modeset object sees (display.dpll.shared_dplls); which one a crtc gets is
the reference's own rule: share an enabled PLL whose hardware state matches, otherwise take a free one
(intel_find_shared_dpll), and the reference's reference counting marks the pipes that use it
(intel_reference_shared_dpll / intel_unreference_shared_dpll).  Single-screen runs keep working: with no other pipe
active the rule picks DPLL 0 exactly as the fixed configuration did.
Idempotent.  usage: round87.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
TAB = chr(9)

spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))
add = ["intel_get_shared_dpll_by_id", "intel_dpll_mask_all", "intel_find_shared_dpll",
       "intel_reference_shared_dpll_crtc", "intel_reference_shared_dpll",
       "intel_unreference_shared_dpll_crtc", "intel_unreference_shared_dpll"]
have = j["extra"]["intel_dpll_mgr.c"]
j["extra"]["intel_dpll_mgr.c"] = have + [n for n in add if n not in have]
open(spec, "w").write(json.dumps(j, indent=1) + NL)

def edit(path, pairs, marker):
    s = open(path).read()
    if marker in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90])
        s = s.replace(old, new)
    open(path, "w").write(s)

# the device's PLL pool lives in the compat device (a pointer: every modeset object sees the same objects)
edit(L + "lcd_compat.h", [(
    "		struct { struct { int nssc; } ref_clks; struct { int which; } lock; } dpll;",
    "		/* the device's shared DPLLs: ONE pool, seen by every modeset object (intel_get_shared_dpll_by_id) */" + NL +
    "		struct { struct { int nssc; } ref_clks; struct { int which; } lock;" + NL +
    "			struct intel_shared_dpll *shared_dplls; int num_shared_dpll; } dpll;")],
    "num_shared_dpll")

# the crtc state points at a typed PLL object (it was an opaque pointer while only one object existed)
edit(L + "lcd_compat.h", [(
    TAB + "void *shared_dpll;",
    TAB + "struct intel_shared_dpll *shared_dpll;")], "struct intel_shared_dpll *shared_dpll;")
edit(L + "lcd_compat.h", [(
    "struct intel_crtc_state {",
    "struct intel_shared_dpll;" + NL + "struct intel_crtc_state {")], "struct intel_shared_dpll;")

# the PLL object gains what the allocation text reads: a named state type and the pool index
edit(L + "lcd_modeset_compat.h", [(
    "struct intel_shared_dpll {" + NL +
    TAB + "struct { u8 pipe_mask; struct intel_dpll_hw_state hw_state; } state;" + NL +
    TAB + "u8 active_mask;" + NL +
    TAB + "bool on;" + NL +
    TAB + "const struct dpll_info *info;" + NL +
    TAB + "intel_wakeref_t wakeref;" + NL +
    "};",
    "/* intel_dpll_mgr.h: the per-PLL part of the atomic state, and the object the device keeps */" + NL +
    "struct intel_shared_dpll_state { u8 pipe_mask; struct intel_dpll_hw_state hw_state; };" + NL +
    "struct intel_shared_dpll {" + NL +
    TAB + "struct intel_shared_dpll_state state;" + NL +
    TAB + "u8 active_mask;" + NL +
    TAB + "bool on;" + NL +
    TAB + "const struct dpll_info *info;" + NL +
    TAB + "intel_wakeref_t wakeref;" + NL +
    TAB + "enum intel_dpll_id index;       /* its place in the device's pool (shared_dplls[]) */" + NL +
    "};")], "struct intel_shared_dpll_state {")

# what the allocation text needs beyond the modeset compat
edit(L + "lcd_modeset_compat.h", [(
    "/* ---- E-123: what the generated HDMI text",
    "/* ---- E-123: the shared-DPLL allocation (intel_find_shared_dpll and its reference counting) ---- */" + NL +
    "struct intel_shared_dpll_state *parity_lcd_shared_dpll_state(void);   /* the atomic state's shared_dpll[] */" + NL +
    "#define intel_atomic_get_shared_dpll_state(state) parity_lcd_shared_dpll_state()" + NL +
    "#define for_each_set_bit(bit, addr, size) " +
    "for ((bit) = 0; (bit) < (int)(size); (bit)++) for_each_if(*(addr) & (1ul << (bit)))" + NL +
    "#ifndef fls" + NL + "#define fls(x) ((x) ? 32 - __builtin_clz((unsigned int)(x)) : 0)" + NL + "#endif" + NL +
    "/* intel_dpll_mgr.h: the walk over the device's pool */" + NL +
    "#define for_each_shared_dpll(i915, pll, id) " +
    "for ((id) = 0; (id) < (i915)->display.dpll.num_shared_dpll && " +
    "((pll) = &(i915)->display.dpll.shared_dplls[(id)]); (id)++)" + NL + NL +
    "/* ---- E-123: what the generated HDMI text")], "parity_lcd_shared_dpll_state")

# the pool and the allocation entry point
s = open(L + "parity_dpll_glue.inc").read()
if "parity_lcd_ms_alloc_pll" not in s:
    old = "void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id)"
    assert s.count(old) == 1
    pool = ("/*" + NL +
            " * The device's two combo PLLs (intel_dpll_init_clock_hook / intel_shared_dpll_init for ADL-P: DPLL 0 and" + NL +
            " * DPLL 1).  One pool for the whole device: two modeset objects that want the same hardware state share the" + NL +
            " * same object, and its active_mask counts the pipes that drive it -- which is what keeps one screen's stop" + NL +
            " * from turning off a PLL the other screen still uses." + NL +
            " */" + NL +
            "static struct intel_shared_dpll parity_lcd_dpll_pool[2];" + NL +
            "static struct dpll_info parity_lcd_dpll_pool_info[2];" + NL +
            "static struct intel_shared_dpll_state parity_lcd_dpll_pool_state[2];" + NL +
            "static int parity_lcd_dpll_pool_inited;" + NL + NL +
            "struct intel_shared_dpll_state *parity_lcd_shared_dpll_state(void)" + NL +
            "{" + NL + TAB + "return parity_lcd_dpll_pool_state;" + NL + "}" + NL + NL +
            "static void parity_lcd_dpll_pool_init(struct parity_lcd_modeset *ms)" + NL +
            "{" + NL +
            TAB + "int i;" + NL + NL +
            TAB + "if (!parity_lcd_dpll_pool_inited) {" + NL +
            TAB * 2 + "for (i = 0; i < 2; i++) {" + NL +
            TAB * 3 + "memset(&parity_lcd_dpll_pool[i], 0, sizeof(parity_lcd_dpll_pool[i]));" + NL +
            TAB * 3 + "memset(&parity_lcd_dpll_pool_state[i], 0, sizeof(parity_lcd_dpll_pool_state[i]));" + NL +
            TAB * 3 + 'parity_lcd_dpll_pool_info[i].name = i == DPLL_ID_ICL_DPLL1 ? "DPLL 1" : "DPLL 0";' + NL +
            TAB * 3 + "parity_lcd_dpll_pool_info[i].funcs = &parity_combo_pll_funcs;" + NL +
            TAB * 3 + "parity_lcd_dpll_pool_info[i].id = i == DPLL_ID_ICL_DPLL1 ? DPLL_ID_ICL_DPLL1 : DPLL_ID_ICL_DPLL0;" + NL +
            TAB * 3 + "parity_lcd_dpll_pool_info[i].power_domain = 0;" + NL +
            TAB * 3 + "parity_lcd_dpll_pool[i].info = &parity_lcd_dpll_pool_info[i];" + NL +
            TAB * 3 + "parity_lcd_dpll_pool[i].index = (enum intel_dpll_id)i;" + NL +
            TAB * 2 + "}" + NL +
            TAB * 2 + "parity_lcd_dpll_pool_inited = 1;" + NL +
            TAB + "}" + NL +
            TAB + "ms->i915.display.dpll.shared_dplls = parity_lcd_dpll_pool;" + NL +
            TAB + "ms->i915.display.dpll.num_shared_dpll = 2;" + NL +
            "}" + NL + NL +
            "/*" + NL +
            " * icl_get_combo_phy_dpll()'s allocation half: the PLL state was computed by the caller" + NL +
            " * (parity_lcd_compute / parity_icl_hdmi_wrpll); here the reference picks the object that carries it." + NL +
            " * Returns the DPLL id, or a negative errno when both PLLs are taken by other pipes." + NL +
            " */" + NL +
            "int parity_lcd_ms_alloc_pll(struct parity_lcd_modeset *ms, const struct intel_dpll_hw_state *hw_state)" + NL +
            "{" + NL +
            TAB + "struct intel_shared_dpll *pll;" + NL + NL +
            TAB + "parity_lcd_dpll_pool_init(ms);" + NL +
            TAB + "/* intel_release_shared_dplls(): whatever this pipe still holds from an earlier state goes back first */" + NL +
            TAB + "parity_lcd_ms_release_pipe(ms->crtc.pipe);" + NL +
            TAB + "pll = intel_find_shared_dpll(&ms->state, &ms->crtc, hw_state," + NL +
            TAB * 2 + "BIT(DPLL_ID_ICL_DPLL0) | BIT(DPLL_ID_ICL_DPLL1));" + NL +
            TAB + "if (pll == NULL)" + NL +
            TAB * 2 + "return -EINVAL;" + NL +
            TAB + "intel_reference_shared_dpll(&ms->state, &ms->crtc, pll, hw_state);" + NL +
            TAB + "ms->crtc_state.shared_dpll = pll;" + NL +
            TAB + "pll->state = parity_lcd_dpll_pool_state[pll->index];   /* the object carries the new state */" + NL +
            TAB + "return (int)pll->info->id;" + NL +
            "}" + NL + NL +
            "/* the device is (re)created: the pool starts empty, as intel_shared_dpll_init() leaves it */" + NL +
            "void parity_lcd_dplls_reset(void)" + NL +
            "{" + NL +
            TAB + "int i;" + NL + NL +
            TAB + "parity_lcd_dpll_pool_inited = 0;" + NL +
            TAB + "for (i = 0; i < 2; i++) {" + NL +
            TAB * 2 + "memset(&parity_lcd_dpll_pool[i], 0, sizeof(parity_lcd_dpll_pool[i]));" + NL +
            TAB * 2 + "memset(&parity_lcd_dpll_pool_state[i], 0, sizeof(parity_lcd_dpll_pool_state[i]));" + NL +
            TAB + "}" + NL +
            "}" + NL + NL +
            "/* the pipe's reference on every PLL of the pool goes back (intel_release_shared_dplls) */" + NL +
            "void parity_lcd_ms_release_pipe(enum pipe pipe)" + NL +
            "{" + NL +
            TAB + "int i;" + NL + NL +
            TAB + "for (i = 0; i < 2; i++) {" + NL +
            TAB * 2 + "parity_lcd_dpll_pool_state[i].pipe_mask &= (u8)~BIT(pipe);" + NL +
            TAB * 2 + "parity_lcd_dpll_pool[i].state.pipe_mask &= (u8)~BIT(pipe);" + NL +
            TAB + "}" + NL +
            "}" + NL + NL +
            "/* intel_release_shared_dplls(): the crtc gives its reference back (the object stays for the other pipe) */" + NL +
            "void parity_lcd_ms_release_pll(struct parity_lcd_modeset *ms)" + NL +
            "{" + NL +
            TAB + "struct intel_shared_dpll *pll = ms->crtc_state.shared_dpll;" + NL + NL +
            TAB + "if (pll == NULL)" + NL +
            TAB * 2 + "return;" + NL +
            TAB + "intel_unreference_shared_dpll(&ms->state, &ms->crtc, pll);" + NL +
            TAB + "pll->state.pipe_mask = parity_lcd_dpll_pool_state[pll->index].pipe_mask;" + NL +
            "}" + NL + NL)
    s = s.replace(old, pool + old)
    open(L + "parity_dpll_glue.inc", "w").write(s)

edit(L + "parity_lcd_modeset_int.h", [(
    "void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id);              /* intel_dpll_port.c */",
    "void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id);              /* intel_dpll_port.c */" + NL +
    "int parity_lcd_ms_alloc_pll(struct parity_lcd_modeset *ms, const struct intel_dpll_hw_state *hw_state);" + NL +
    "void parity_lcd_ms_release_pll(struct parity_lcd_modeset *ms);" + NL +
    "void parity_lcd_ms_release_pipe(enum pipe pipe);" + NL +
    "void parity_lcd_dplls_reset(void);")], "parity_lcd_ms_alloc_pll")

# prepare(): allocate from the pool instead of binding a fixed id
edit(L + "parity_lcd_modeset.c", [(
    TAB + "/* the PLL state computed by icl_calc_dpll_state() (parity_lcd_compute) */" + NL +
    TAB + "parity_lcd_ms_bind_pll(&ms, cfg->dpll_id);" + NL +
    TAB + "ms.pll.state.hw_state.cfgcr0 = s->pll.cfgcr0;" + NL +
    TAB + "ms.pll.state.hw_state.cfgcr1 = s->pll.cfgcr1;" + NL +
    TAB + "ms.pll.state.hw_state.div0 = s->pll.div0;",
    TAB + "/*" + NL +
    TAB + " * The PLL state computed by icl_calc_dpll_state() (parity_lcd_compute) / icl_calc_wrpll, and the object" + NL +
    TAB + " * that carries it: the reference's rule over the device's pool, not a fixed id (cfg->dpll_id is only" + NL +
    TAB + " * the caller's expectation, logged by the caller)." + NL +
    TAB + " */" + NL +
    TAB + "{" + NL +
    TAB * 2 + "struct intel_dpll_hw_state want;" + NL + NL +
    TAB * 2 + "memset(&want, 0, sizeof(want));" + NL +
    TAB * 2 + "want.cfgcr0 = s->pll.cfgcr0;" + NL +
    TAB * 2 + "want.cfgcr1 = s->pll.cfgcr1;" + NL +
    TAB * 2 + "want.div0 = s->pll.div0;" + NL +
    TAB * 2 + "ms.dpll_id = parity_lcd_ms_alloc_pll(&ms, &want);" + NL +
    TAB * 2 + "if (ms.dpll_id < 0) {" + NL +
    TAB * 3 + 'on_error(0, "no shared DPLL is free for this pipe (both are used by other pipes with other states)' +
    chr(92) + 'n");' + NL +
    TAB * 3 + "return -EBUSY;" + NL +
    TAB * 2 + "}" + NL +
    TAB + "}")], "parity_lcd_ms_alloc_pll(&ms")

edit(L + "parity_lcd_modeset_int.h", [(
    TAB + "int hdmi_level_shift;           /* intel_bios_hdmi_level_shift() of this port (< 0 = not in the VBT) */",
    TAB + "int hdmi_level_shift;           /* intel_bios_hdmi_level_shift() of this port (< 0 = not in the VBT) */" + NL +
    TAB + "int dpll_id;                    /* the shared DPLL the reference's rule gave this crtc */")], "int dpll_id;")

# the status reports which PLL object the crtc got
edit(L + "parity_lcd_modeset.h", [(
    TAB + "int pll_on, pll_active_mask, pll_wakeref;",
    TAB + "int pll_on, pll_active_mask, pll_wakeref;" + NL +
    TAB + "int pll_id;                     /* the shared DPLL this crtc was given (0 / 1) */" + NL +
    TAB + "int pll_pipe_mask;              /* the pipes that hold a reference on it */")], "int pll_id;")
edit(L + "parity_lcd_modeset.c", [(
    TAB + "out->pll_on = ms.pll.on;" + NL +
    TAB + "out->pll_active_mask = ms.pll.active_mask;" + NL +
    TAB + "out->pll_wakeref = ms.pll.wakeref;",
    TAB + "out->pll_on = ms.crtc_state.shared_dpll != 0 ? ms.crtc_state.shared_dpll->on : 0;" + NL +
    TAB + "out->pll_active_mask = ms.crtc_state.shared_dpll != 0 ? ms.crtc_state.shared_dpll->active_mask : 0;" + NL +
    TAB + "out->pll_wakeref = ms.crtc_state.shared_dpll != 0 ? ms.crtc_state.shared_dpll->wakeref : 0;" + NL +
    TAB + "out->pll_id = ms.dpll_id;" + NL +
    TAB + "out->pll_pipe_mask = ms.crtc_state.shared_dpll != 0 ? ms.crtc_state.shared_dpll->state.pipe_mask : 0;")],
    "out->pll_id = ms.dpll_id;")
edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;" + NL +
    TAB + "observe(PARITY_LCD_OBS_COMMIT_END);" + NL +
    TAB + "if (ms_errors != before) {",
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;" + NL +
    TAB + "/* the crtc is off: its reference on the shared DPLL goes back (the object stays for any other pipe) */" + NL +
    TAB + "parity_lcd_ms_release_pll(&ms);" + NL +
    TAB + "observe(PARITY_LCD_OBS_COMMIT_END);" + NL +
    TAB + "if (ms_errors != before) {")], "parity_lcd_ms_release_pll(&ms);")
# the device-facing header exports the pool reset (the caller resets it when the device is created)
edit(L + "parity_lcd_modeset.h", [(
    "int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops);",
    "int parity_lcd_modeset_discard_model(const struct parity_lcd_emit *ops);" + NL + NL +
    "/* the device's shared DPLLs start empty (intel_shared_dpll_init): called when the device / backend is created */" + NL +
    "void parity_lcd_dplls_reset(void);")], "void parity_lcd_dplls_reset(void);")

# every run starts from a device whose PLL pool is empty (the kernel path), and the host test's bring_up() does the same
edit(L + "parity_lcd_kernel.c", [(
    TAB + "bind_ops(k);" + NL + TAB + "k->p = p;",
    TAB + "bind_ops(k);" + NL + TAB + "k->p = p;" + NL +
    TAB + "if (!p->output_hdmi || p->reset_dplls)" + NL +
    TAB * 2 + "parity_lcd_dplls_reset();       /* this run owns the device's PLL pool */")], "parity_lcd_dplls_reset();")
edit(L + "parity_lcd_kernel.c", [(
    TAB + "const char *tag;" + NL + "};",
    TAB + "const char *tag;" + NL +
    TAB + "int reset_dplls;                /* the run starts from an empty DPLL pool (a single-screen run) */" + NL + "};")],
    "int reset_dplls;")
edit(L + "parity_lcd_kernel.c", [(
    TAB + "p.output_hdmi = 1; p.port = 1; p.pipe = 1; p.cpu_transcoder = 1; p.dpll_id = 0;",
    TAB + "p.output_hdmi = 1; p.port = 1; p.pipe = 1; p.cpu_transcoder = 1; p.dpll_id = 0;" + NL +
    TAB + "p.reset_dplls = 1;              /* HDMI-B is a single-screen run: the pool starts empty */")], "p.reset_dplls = 1;")

t = root + "plan/ws031/tests/lcd-modeset-host-test.c"
s2 = open(t).read()
if "parity_lcd_dplls_reset();" not in s2:
    anchor = "static void bring_up(void)" + NL + "{" + NL
    assert s2.count(anchor) == 1
    s2 = s2.replace(anchor, anchor + TAB + "parity_lcd_dplls_reset();       /* a fresh device: its shared DPLLs are unused */" + NL)
    open(t, "w").write(s2)
print("done")
