#!/usr/bin/env python3
"""WS031 E-123 round 92: nothing may still be scanned out when the guest ends.

The host froze twice while this VM exited: its log shows vfio_iommu_type1_detach_group() stuck, which is what
happens when the device keeps reading memory that is being unmapped -- a pipe that is still scanning out.  The
driver's own stop path is the reference's; this is the LAST RESORT for the test environment, run at the end of the
teardown: any pipe that still reports activity is stopped bluntly (plane off, transcoder off, DDI buffer off) with
bounded waits, and every step is logged.  ADAPTATION, recorded: not a reference path -- a safety net so that a failed
stop cannot take the host down with it.

Also: the dual test logs which screen's objects the stop was bound to (the earlier failure was the encoder hooks
running against the other screen).
Idempotent.  usage: round92.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
TAB = chr(9)

# ---- the last-resort stop ----
guard = "parity_lcd_last_resort_stop"
p = L + "parity_lcd_regs.c"
s = open(p).read()
if guard not in s:
    s += (NL +
          "/*" + NL +
          " * LAST RESORT (zedBSD, not a reference path): at the end of the run, any pipe that still scans out is" + NL +
          " * stopped here.  A pipe left running would keep reading memory after the driver let it go, which on this" + NL +
          " * test host (VFIO) hangs the IOMMU unmap when the guest ends.  Bounded: each wait is a fixed number of" + NL +
          " * polls; everything is logged." + NL +
          " */" + NL +
          "unsigned parity_lcd_last_resort_stop(struct osdep_mmio *m)" + NL +
          "{" + NL +
          TAB + "unsigned stopped = 0u, pipe, i;" + NL + NL +
          TAB + "for (pipe = 0u; pipe < 4u; pipe++) {" + NL +
          TAB * 2 + "u32 base = 0x1000u * pipe;" + NL +
          TAB * 2 + "u32 transconf = osdep_mmio_read32(m, 0x70008u + base);" + NL +
          TAB * 2 + "u32 plane_ctl = osdep_mmio_read32(m, 0x70180u + base);" + NL + NL +
          TAB * 2 + "if ((transconf & 0x80000000u) == 0u && (plane_ctl & 0x80000000u) == 0u)" + NL +
          TAB * 3 + "continue;" + NL +
          TAB * 2 + 'kern_logf("i915: parity LAST-RESORT pipe %u is still on (TRANSCONF=0x%08x PLANE_CTL=0x%08x): '
          'stopping it so that nothing is scanned out when the driver is gone' + chr(92) + 'n", pipe, transconf, plane_ctl);' + NL +
          TAB * 2 + "osdep_mmio_write32(m, 0x70180u + base, plane_ctl & ~0x80000000u);   /* PLANE_CTL: plane off */" + NL +
          TAB * 2 + "osdep_mmio_write32(m, 0x7019cu + base, 0u);                         /* PLANE_SURF: arm it */" + NL +
          TAB * 2 + "osdep_mmio_write32(m, 0x70008u + base, transconf & ~0x80000000u);   /* TRANSCONF: transcoder off */" + NL +
          TAB * 2 + "for (i = 0u; i < 100u; i++) {" + NL +
          TAB * 3 + "if ((osdep_mmio_read32(m, 0x70008u + base) & 0x40000000u) == 0u)   /* TRANSCONF_STATE */" + NL +
          TAB * 4 + "break;" + NL +
          TAB * 3 + "kern_usleep_range(1000u, 2000u);" + NL +
          TAB * 2 + "}" + NL +
          TAB * 2 + 'kern_logf("i915: parity LAST-RESORT pipe %u after %u ms: TRANSCONF=0x%08x PLANE_CTL=0x%08x' +
          chr(92) + 'n", pipe, i, osdep_mmio_read32(m, 0x70008u + base), osdep_mmio_read32(m, 0x70180u + base));' + NL +
          TAB * 2 + "stopped++;" + NL +
          TAB + "}" + NL +
          TAB + "for (pipe = 0u; pipe < 2u; pipe++) {                 /* DDI A / B buffers */" + NL +
          TAB * 2 + "u32 reg = 0x64000u + 0x100u * pipe;" + NL +
          TAB * 2 + "u32 buf = osdep_mmio_read32(m, reg);" + NL + NL +
          TAB * 2 + "if ((buf & 0x80000000u) == 0u)" + NL +
          TAB * 3 + "continue;" + NL +
          TAB * 2 + "osdep_mmio_write32(m, reg, buf & ~0x80000000u);" + NL +
          TAB * 2 + 'kern_logf("i915: parity LAST-RESORT DDI %c buffer was enabled (0x%08x): disabled' + chr(92) +
          'n", (char)(65 + pipe), buf);' + NL +
          TAB * 2 + "stopped++;" + NL +
          TAB + "}" + NL +
          TAB + "return stopped;" + NL +
          "}" + NL)
    open(p, "w").write(s)

h = open(L + "parity_lcd_show.h").read()
if guard not in h:
    h = h.replace("#endif", "/* LAST RESORT (zedBSD): stop anything still scanned out; returns how many things it had to stop */" + NL +
                  "struct osdep_mmio;" + NL +
                  "unsigned parity_lcd_last_resort_stop(struct osdep_mmio *m);" + NL + NL + "#endif", 1)
    open(L + "parity_lcd_show.h", "w").write(h)

c = open(P + "probe.c").read()
if guard not in c:
    old = "teardown:" + NL
    assert c.count(old) == 1
    c = c.replace(old, old +
        TAB + "/*" + NL +
        TAB + " * Before anything else is given back: nothing may still be scanned out.  The driver's own stop path is" + NL +
        TAB + " * the reference's; this is the test environment's last resort (a pipe left running keeps reading memory" + NL +
        TAB + " * after the driver is gone, which hangs the host's IOMMU unmap when the guest ends)." + NL +
        TAB + " */" + NL +
        TAB + "{" + NL +
        TAB * 2 + "unsigned forced = parity_lcd_last_resort_stop(&mmio);" + NL + NL +
        TAB * 2 + "if (forced != 0u)" + NL +
        TAB * 3 + 'kern_logf("i915: parity teardown: LAST-RESORT stopped %u display element(s) the driver had not' + NL +
        TAB * 3 + '	" released' + chr(92) + 'n", forced);' + NL +
        TAB + "}" + NL)
    open(P + "probe.c", "w").write(c)

# ---- the dual test says which screen the stop was bound to ----
d = open(root + "plan/ws031/handover/tools/lcd-e123/dual_run.c").read()
if "bound port" not in d:
    old = TAB + "(void)parity_lcd_modeset_select(sc->idx);" + NL + TAB + "prc = parity_lcd_modeset_plane_disable();"
    assert d.count(old) == 1
    d = d.replace(old, TAB + "(void)parity_lcd_modeset_select(sc->idx);" + NL +
        TAB + 'kern_logf("i915: parity DUAL %s: stopping screen %u (bound port %d, pipe %d)' + chr(92) + 'n", sc->name,' + NL +
        TAB * 2 + "parity_lcd_modeset_selected(), parity_lcd_ms_bound_port(), sc->pipe);" + NL +
        TAB + "prc = parity_lcd_modeset_plane_disable();")
    open(root + "plan/ws031/handover/tools/lcd-e123/dual_run.c", "w").write(d)

g = open(L + "parity_ddi_emit_glue.inc").read()
if "parity_lcd_ms_bound_port" not in g:
    anchor = "void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms)"
    assert g.count(anchor) == 1
    g = g.replace(anchor, "/* which screen's encoder the DDI callers are bound to right now (diagnostics) */" + NL +
                  "int parity_lcd_ms_bound_port(void)" + NL + "{" + NL +
                  TAB + "return ddi_ms != 0 ? (int)ddi_ms->dig_port.base.port : -1;" + NL + "}" + NL + NL + anchor)
    open(L + "parity_ddi_emit_glue.inc", "w").write(g)

i = open(L + "parity_lcd_modeset.h").read()
if "parity_lcd_ms_bound_port" not in i:
    i = i.replace("unsigned parity_lcd_modeset_selected(void);",
                  "unsigned parity_lcd_modeset_selected(void);" + NL +
                  "int parity_lcd_ms_bound_port(void);     /* the port the generated DDI callers are bound to */")
    open(L + "parity_lcd_modeset.h", "w").write(i)
print("done")
