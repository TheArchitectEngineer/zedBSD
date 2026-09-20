#!/usr/bin/env python3
"""WS031 E-123 round 78: the HDMI hotplug receive path (slice a) wired in.
 - the zedBSD files of the unit (hpd_compat.h, glue, parity_hotplug.h, parity_hpd_test.c, hpd_ktest.c) are copied in
   from <src dir> (the generated units come from port_lcd_calc.py, round 77);
 - driver_probe.c exports intel_hpd_irq_setup's register programming (parity_intel_hpd_irq_setup);
 - irq.c: gen8_de_irq_handler's PCH branch hands the acked SDEIIR to icp_irq_handler (parity_hpd_pch_irq);
 - probe.c: the hotplug path starts after intel_display_driver_probe (hpd_init), -DPARITY_HDMI_HPD_TEST=1 runs the
   HPD-TEST window, the teardown stops it before the interrupts are uninstalled;
 - ktest.c runs the HPD model tests; vmunix.mk builds the units; the host tests leave the kernel-only units out.
Idempotent.  usage: round78.py <repo root> <src dir>"""
import sys, os, shutil
root = sys.argv[1].rstrip("/") + "/"
src = sys.argv[2].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)

def rep(s, old, new, done_marker):
    if done_marker in s:
        return s
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

for f in ["hpd_compat.h", "parity_hotplug_glue.inc", "parity_ddi_hotplug_glue.inc", "parity_hdmi_detect_glue.inc",
          "parity_hotplug.h", "parity_hpd_test.c", "hpd_ktest.c"]:
    shutil.copyfile(src + f, L + f)

# driver_probe: export the register programming of intel_hpd_irq_setup()
c = open(P + "driver_probe.c").read()
c = rep(c, "void" + NL + "parity_intel_hpd_init(struct parity_hotplug *hp,",
        "/* intel_hpd_irq_setup() for the hotplug path (storm masking / re-enable): the pins' state is in hp->state */" + NL +
        "void" + NL + "parity_intel_hpd_irq_setup(struct parity_hotplug *hp, struct osdep_mmio *m, int pch_type," + NL +
        "	int intel_irqs_enabled)" + NL + "{" + NL + "	hp->irq_setups++;" + NL +
        "	gen11_hpd_irq_setup(hp, m, pch_type, intel_irqs_enabled);" + NL + "}" + NL + NL +
        "void" + NL + "parity_intel_hpd_init(struct parity_hotplug *hp,", "parity_intel_hpd_irq_setup(struct")
open(P + "driver_probe.c", "w").write(c)
h = open(P + "driver_probe.h").read()
h = rep(h, "int parity_intel_ddi_hpd_pin(int display_ver, int port);",
        "int parity_intel_ddi_hpd_pin(int display_ver, int port);" + NL +
        "/* intel_hpd_irq_setup(): gen11 + icp programming from hp->state (the hotplug path's storm masking / re-enable) */" + NL +
        "void parity_intel_hpd_irq_setup(struct parity_hotplug *hp, struct osdep_mmio *m, int pch_type, int intel_irqs_enabled);",
        "parity_intel_hpd_irq_setup(")
open(P + "driver_probe.h", "w").write(h)

# irq.c: the PCH branch -> icp_irq_handler
c = open(P + "irq.c").read()
c = rep(c, '#include "lcd/parity_opregion.h"' + NL,
        '#include "lcd/parity_opregion.h"' + NL + '#include "lcd/parity_hotplug.h"' + NL, "lcd/parity_hotplug.h")
c = rep(c, """		if (iir != 0u) {
			osdep_mmio_write32(d->m, SDEIIR, iir);
			d->de_pch_acks++;
		} else {""", """		if (iir != 0u) {
			osdep_mmio_write32(d->m, SDEIIR, iir);
			d->de_pch_acks++;
			/* INTEL_PCH_TYPE >= PCH_ICP: icp_irq_handler() (the hotplug path; dropped until it is started) */
			parity_hpd_pch_irq(iir);
		} else {""", "parity_hpd_pch_irq(iir)")
open(P + "irq.c", "w").write(c)

# bios.h: the test flag
h = open(P + "bios.h").read()
h = rep(h, "#ifndef PARITY_LCDD_TEST" + NL,
        "#ifndef PARITY_HDMI_HPD_TEST" + NL +
        "#define PARITY_HDMI_HPD_TEST 0        /* E-123: HPD-TEST window, the HDMI cable plugged / unplugged (no GPU submission) */" + NL +
        "#endif" + NL + "#ifndef PARITY_LCDD_TEST" + NL, "PARITY_HDMI_HPD_TEST 0")
open(P + "bios.h", "w").write(h)

# probe.c
c = open(P + "probe.c").read()
c = rep(c, '#include "native_precheck.h"' + NL,
        '#include "native_precheck.h"' + NL + '#include "lcd/parity_hotplug.h"' + NL +
        "/* E-123: the hotplug path is running (started after intel_display_driver_probe) */" + NL +
        "static int parity_hpd_started;" + NL, "lcd/parity_hotplug.h")
c = rep(c, """	if (dprobe.initial_commit_unimplemented) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_UNIMPL,
			"intel_initial_commit", dprobe.active_crtcs, 0u);""", """	/*
	 * The hotplug path (E-123): intel_hpd_init_early() + the connectors of the encoders intel_setup_outputs() made;
	 * from here on gen8_de_irq_handler() hands SDEIIR to icp_irq_handler().  (The reference makes the connectors in
	 * intel_ddi_init; adaptation: they are made here, once the hotplug registers are programmed.)
	 */
	if (rc == 0 && parity_hpd_start(&dprobe.hp, &mmio, &nogem, &power_domains, &pwc, pch.type,
			irqdev.irqs_enabled, NULL) == 0)
		parity_hpd_started = 1;
	kern_logf("i915: parity P7 hotplug path: %s\\n", parity_hpd_started ? "started" : "NOT started");
	if (dprobe.initial_commit_unimplemented) {
		osdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_UNIMPL,
			"intel_initial_commit", dprobe.active_crtcs, 0u);""", "parity_hpd_started = 1;")
c = rep(c, """	if (PARITY_T3_TEST || PARITY_BL_TEST) {
		const char *t3tag""", """	/* HPD-TEST: the HDMI cable of DDI B is plugged / unplugged in a finite window (no GPU submission) */
	if (PARITY_HDMI_HPD_TEST) {
		if (parity_hpd_started)
			(void)parity_hpd_test_run(&mmio, 150u);
		else
			kern_logf("i915: parity HPD-TEST verdict: FAIL (the hotplug path did not start)\\n");
	}

	if (PARITY_T3_TEST || PARITY_BL_TEST) {
		const char *t3tag""", "(void)parity_hpd_test_run(")
c = rep(c, """teardown:
	/*
	 * i915_driver_remove() -> i915_driver_unregister(): runtime PM back to""", """teardown:
	/* the hotplug path first: its IRQ entry closed, the works cancelled (intel_hpd_cancel_work) */
	if (parity_hpd_started) {
		parity_hpd_stop();
		parity_hpd_started = 0;
	}
	/*
	 * i915_driver_remove() -> i915_driver_unregister(): runtime PM back to""", "parity_hpd_stop();")
open(P + "probe.c", "w").write(c)

# ktest.c
c = open(P + "ktest.c").read()
c = rep(c, '#include "irq.h"' + NL, '#include "irq.h"' + NL + '#include "lcd/parity_hotplug.h"' + NL, "lcd/parity_hotplug.h")
c = rep(c, """			parity_opregion_ktest(edp_ktest_check);
""", """			parity_opregion_ktest(edp_ktest_check);
			/* E-123 HPD: the HDMI hotplug receive path (generated reference chain) on fake SHOTPLUG / SDEISR */
			parity_hpd_ktest(edp_ktest_check);
""", "parity_hpd_ktest(edp_ktest_check)")
open(P + "ktest.c", "w").write(c)

# vmunix.mk
mk = root + "platform/amd64/vmunix.mk"
c = open(mk).read()
units = ["intel_hotplug_port.c", "intel_hotplug_irq_port.c", "intel_ddi_hotplug_port.c", "intel_dp_connected_port.c",
         "intel_hdmi_detect_port.c", "drm_probe_detect_port.c", "drm_connector_status_port.c", "parity_hpd_test.c",
         "hpd_ktest.c"]
anchor = "src/drivers/gpu/i915/parity/lcd/opregion_fwtest.c"
add = " ".join("src/drivers/gpu/i915/parity/lcd/" + u for u in units)
c = rep(c, anchor + " ", anchor + " " + add + " ", "lcd/intel_hotplug_port.c")
open(mk, "w").write(c)

# host tests: the hotplug units are kernel-only (kern locks, work queue): tested by the ktest
for t in ["plan/ws031/tests/run-lcd-modeset-host-test.sh", "plan/ws031/tests/run-lcd-host-test.sh"]:
    c = open(root + t).read()
    c = rep(c, "grep -v -e intel_opregion_port.c -e intel_acpi_port.c)",
            "grep -v -e intel_opregion_port.c -e intel_acpi_port.c -e hotplug -e intel_dp_connected_port.c "
            "-e intel_hdmi_detect_port.c -e drm_probe_detect_port.c -e drm_connector_status_port.c)", "-e hotplug")
    open(root + t, "w").write(c)
print("done")
