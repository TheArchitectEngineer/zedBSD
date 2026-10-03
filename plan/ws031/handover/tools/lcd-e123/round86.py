#!/usr/bin/env python3
"""WS031 E-123 round 86: the review's four points.
 1. the model tests never share the device's hotplug state: a model instance is refused once a real one has run in
    this boot (the hardware's own interrupts are still installed then), and the ktest says so instead of running;
 2. what "connected" came from is in the record: the EDID read's result (blocks / errno) next to the live status;
 3. the VBT's HDMI level shift is read from the parser (the reference falls back to the table's default entry only
    when the VBT has no value -- 0 is a valid index);
 4. the GMBUS transfer takes the reference's bus mutex (gmbus_lock_bus), so the hotplug worker and a test thread
    cannot drive the controller at the same time.
Idempotent.  usage: round86.py <repo root> <src dir>"""
import sys, shutil
root = sys.argv[1].rstrip("/") + "/"
src = sys.argv[2].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
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

for f in ["parity_hotplug_glue.inc", "parity_hotplug.h", "hpd_ktest.c", "parity_gmbus_glue.inc"]:
    shutil.copyfile(src + f, L + f)

# ---- 3: the VBT level shift travels with the modeset configuration ----
edit(L + "parity_lcd_modeset.h", [(
    TAB + "int output_hdmi;                /* 0 = the eDP panel (DP SST), 1 = an HDMI sink on a combo-PHY DDI */",
    TAB + "int output_hdmi;                /* 0 = the eDP panel (DP SST), 1 = an HDMI sink on a combo-PHY DDI */" + NL +
    TAB + "int vbt_hdmi_level_shift;       /* the VBT child's HDMI level shift for this port; < 0 = the VBT has none */")],
    "vbt_hdmi_level_shift")

edit(L + "parity_lcd_modeset_int.h", [(
    TAB + "int output_hdmi;                /* the output this state drives: an HDMI sink instead of the eDP panel */",
    TAB + "int output_hdmi;                /* the output this state drives: an HDMI sink instead of the eDP panel */" + NL +
    TAB + "int hdmi_level_shift;           /* intel_bios_hdmi_level_shift() of this port (< 0 = not in the VBT) */")],
    "int hdmi_level_shift;")

edit(L + "parity_lcd_modeset.c", [(
    TAB + "ms.output_hdmi = cfg->output_hdmi;",
    TAB + "ms.output_hdmi = cfg->output_hdmi;" + NL +
    TAB + "ms.hdmi_level_shift = cfg->vbt_hdmi_level_shift;")], "ms.hdmi_level_shift = cfg")

edit(L + "lcd_modeset_compat.h", [(
    "#define intel_bios_hdmi_level_shift(devdata) (-1)",
    "int parity_lcd_hdmi_level_shift(void);          /* the VBT value of this port (intel_ddi_port.c glue) */" + NL +
    "#define intel_bios_hdmi_level_shift(devdata) parity_lcd_hdmi_level_shift()")],
    "parity_lcd_hdmi_level_shift()")

edit(L + "parity_ddi_emit_glue.inc", [(
    "void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms)",
    "/*" + NL +
    " * intel_bios_hdmi_level_shift(): the VBT child's value for this port, as the parser read it (vbt/parity_vbt.h)." + NL +
    " * A value of 0 is a valid index; only a VBT without the field (< 0) makes intel_ddi_hdmi_level() fall back to" + NL +
    " * the buffer-translation table's own default entry." + NL +
    " */" + NL +
    "int parity_lcd_hdmi_level_shift(void)" + NL +
    "{" + NL +
    TAB + "return ddi_ms != 0 ? ddi_ms->hdmi_level_shift : -1;" + NL +
    "}" + NL + NL +
    "void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms)")], "parity_lcd_hdmi_level_shift(void)")

# the HDMI-B run fills it from the VBT the probe parsed
edit(L + "parity_lcd_kernel.c", [(
    TAB * 2 + "env.cfg.vbt_backlight_present = 0;",
    TAB * 2 + "env.cfg.vbt_backlight_present = 0;" + NL +
    TAB * 2 + "{" + NL +
    TAB * 3 + "const struct parity_vbt_encoder *ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, p->port);" + NL + NL +
    TAB * 3 + "env.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;" + NL +
    TAB * 3 + 'kern_logf("i915: parity HDMI-B input: VBT child for port %d: hdmi_level_shift=%d (< 0 = not in the VBT: '
    'the buffer-translation table\'s default entry is used) hdmi_boost=%d ddc_pin=%d\\n", p->port,' + NL +
    TAB * 4 + "env.cfg.vbt_hdmi_level_shift, ve != 0 ? ve->hdmi_boost_level : -1, ve != 0 ? ve->ddc_pin : -1);" + NL +
    TAB * 2 + "}")], "vbt_hdmi_level_shift = ve")
print("done")
