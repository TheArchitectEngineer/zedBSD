#!/usr/bin/env python3
"""WS031 E-123 round 89: the DBUF / MBUS state belongs to the device, and a commit knows which pipes the whole
configuration will light.
The reference computes the DDB of an atomic state that holds EVERY crtc, so a second pipe changes the first pipe's
slices and watermarks in the same check.  Here the two screens commit one after the other, so each one computes its
DDB for the FINAL set of active pipes (cfg->also_active_pipes) against the device's current DBUF state.
ADAPTATION, recorded: two serial commits instead of one atomic commit over both crtcs; the computed state is the one
the reference's check would have produced for that configuration.
Idempotent.  usage: round89.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

def edit(path, pairs, marker):
    s = open(path).read()
    if marker in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90])
        s = s.replace(old, new)
    open(path, "w").write(s)

edit(L + "parity_lcd_modeset.h", [(
    TAB + "int vbt_hdmi_level_shift;       /* the VBT child's HDMI level shift for this port; < 0 = the VBT has none */",
    TAB + "int vbt_hdmi_level_shift;       /* the VBT child's HDMI level shift for this port; < 0 = the VBT has none */" + NL +
    TAB + "unsigned also_active_pipes;     /* the other pipes this configuration lights (BIT(pipe)): the DDB is" + NL +
    TAB + "                                 * computed for the whole set, as the reference's atomic check would */")],
    "also_active_pipes")

edit(L + "parity_lcd_modeset_int.h", [(
    TAB + "int dpll_id;                    /* the shared DPLL the reference's rule gave this crtc */",
    TAB + "int dpll_id;                    /* the shared DPLL the reference's rule gave this crtc */" + NL +
    TAB + "unsigned also_active_pipes;     /* the other pipes of this configuration (cfg) */")], "unsigned also_active_pipes;")

edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.hdmi_level_shift = cfg->vbt_hdmi_level_shift;",
    TAB + "ms.hdmi_level_shift = cfg->vbt_hdmi_level_shift;" + NL +
    TAB + "ms.also_active_pipes = cfg->also_active_pipes;")], "ms.also_active_pipes = cfg")

# the old global DBUF state: the device's, when a screen has already set it
edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;" + NL +
    TAB + "ms.wm.old_dbuf.joined_mbus = cfg->mbus_joined != 0;",
    TAB + "/* the global DBUF state as it is NOW: the device's own, or what the caller read from the hardware */" + NL +
    TAB + "if (parity_lcd_dbuf_current(&ms.wm.old_dbuf) != 0) {" + NL +
    TAB * 2 + "ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;" + NL +
    TAB * 2 + "ms.wm.old_dbuf.joined_mbus = cfg->mbus_joined != 0;" + NL +
    TAB + "}")], "parity_lcd_dbuf_current(&ms.wm.old_dbuf)")

# both commits publish the new global state to the device
edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;        /* the new global state is the current one from here on */",
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;        /* the new global state is the current one from here on */" + NL +
    TAB + "parity_lcd_dbuf_publish(&ms.wm.new_dbuf);")], "parity_lcd_dbuf_publish(&ms.wm.new_dbuf);")
edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;" + NL +
    TAB + "/* the crtc is off: its reference on the shared DPLL goes back (the object stays for any other pipe) */",
    TAB + "ms.wm.old_dbuf = ms.wm.new_dbuf;" + NL +
    TAB + "parity_lcd_dbuf_publish(&ms.wm.new_dbuf);" + NL +
    TAB + "/* the crtc is off: its reference on the shared DPLL goes back (the object stays for any other pipe) */")],
    "parity_lcd_dbuf_publish(&ms.wm.new_dbuf);" + NL + TAB + "/* the crtc is off")

# the device's DBUF state and the planned active pipes live with the watermark code
s = open(L + "parity_wm_glue.inc").read()
if "parity_lcd_dbuf_current" not in s:
    anchor = "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms)"
    assert s.count(anchor) == 1
    dev = ("/*" + NL +
           " * The device's DBUF / MBUS state.  The reference keeps it in the global atomic state; here the screens" + NL +
           " * commit one after the other, so the state the last commit left is what the next one starts from." + NL +
           " */" + NL +
           "static struct intel_dbuf_state parity_lcd_dbuf_dev;" + NL +
           "static int parity_lcd_dbuf_dev_valid;" + NL + NL +
           "int parity_lcd_dbuf_current(struct intel_dbuf_state *out)" + NL +
           "{" + NL +
           TAB + "if (!parity_lcd_dbuf_dev_valid)" + NL +
           TAB * 2 + "return -1;" + NL +
           TAB + "*out = parity_lcd_dbuf_dev;" + NL +
           TAB + "return 0;" + NL +
           "}" + NL + NL +
           "void parity_lcd_dbuf_publish(const struct intel_dbuf_state *now)" + NL +
           "{" + NL +
           TAB + "parity_lcd_dbuf_dev = *now;" + NL +
           TAB + "parity_lcd_dbuf_dev_valid = 1;" + NL +
           "}" + NL + NL +
           "/* a fresh device: the next prepare takes the DBUF state the caller read from the hardware */" + NL +
           "void parity_lcd_dbuf_forget(void)" + NL +
           "{" + NL +
           TAB + "memset(&parity_lcd_dbuf_dev, 0, sizeof(parity_lcd_dbuf_dev));" + NL +
           TAB + "parity_lcd_dbuf_dev_valid = 0;" + NL +
           "}" + NL + NL)
    s = s.replace(anchor, dev + anchor)
    old = (TAB + "new_dbuf->active_pipes = ms->wm.old_dbuf.active_pipes | (u8)BIT(pipe);")
    assert s.count(old) == 1
    s = s.replace(old, TAB + "/* the whole configuration's pipes: this one and the others it lights (the reference's" + NL +
                  TAB + " * atomic state would hold every crtc) */" + NL +
                  TAB + "new_dbuf->active_pipes = (u8)(ms->wm.old_dbuf.active_pipes | BIT(pipe) | ms->also_active_pipes);")
    open(L + "parity_wm_glue.inc", "w").write(s)

edit(L + "parity_lcd_modeset_int.h", [(
    "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);",
    "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);" + NL +
    "int parity_lcd_dbuf_current(struct intel_dbuf_state *out);        /* the device's DBUF state (-1 = none yet) */" + NL +
    "void parity_lcd_dbuf_publish(const struct intel_dbuf_state *now);" + NL +
    "void parity_lcd_dbuf_forget(void);")], "parity_lcd_dbuf_current(struct intel_dbuf_state *out)")

edit(L + "parity_lcd_modeset.h", [(
    "void parity_lcd_dplls_reset(void);",
    "void parity_lcd_dplls_reset(void);" + NL +
    "/* the device's DBUF / MBUS state is forgotten with it (the next prepare reads the hardware's own) */" + NL +
    "void parity_lcd_dbuf_forget(void);")], "void parity_lcd_dbuf_forget(void);" + NL)

edit(L + "parity_lcd_kernel.c", [(
    TAB * 2 + "parity_lcd_dplls_reset();       /* this run owns the device's PLL pool */",
    TAB * 2 + "parity_lcd_dplls_reset();       /* this run owns the device's PLL pool */" + NL +
    TAB * 2 + "parity_lcd_dbuf_forget();")], "parity_lcd_dbuf_forget();")
print("done")
