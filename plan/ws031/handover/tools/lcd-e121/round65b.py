#!/usr/bin/env python3
"""WS031 E-122 round 65b: probe wiring for VBT_ONLY (the P2 acquisition; no stop at intel_opregion_register) and the
N0 condition text.  usage: round65b.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
BS = chr(92)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

pr = open(P + "probe.c").read()
old = '\t\tkern_logf("i915: parity P2 opregion: ASLS=0x%08x' + BS + 'n", asls);' + NL
pr = rep(pr, old, old +
    "\t\t/* E-122 VBT_ONLY: the OpRegion as data (read-only copy + VBT); the runtime protocol is not joined */" + NL +
    "\t\t{" + NL +
    "\t\t\tstatic struct parity_opregion_data opd;" + NL + NL +
    "\t\t\t(void)parity_opregion_read_data(asls, &opd);" + NL +
    "\t\t\tparity_opregion_log(&opd);" + NL +
    "\t\t\tif (opd.vbt_valid) {" + NL +
    "\t\t\t\tparity_bios_set_opregion_vbt(opd.vbt, opd.vbt_size);" + NL +
    "\t\t\t\topregion_vbt_present = 1;" + NL +
    "\t\t\t}" + NL +
    "\t\t}" + NL)
old = ("\tif (opregion_present) {" + NL +
       "\t\tosdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_UNIMPL," + NL +
       '\t\t\t"intel_opregion_register", 0u, 0u);' + NL +
       "\t\tres.outcome = PARITY_BLOCKED;" + NL +
       '\t\tres.where = "intel_opregion_register";' + NL +
       "\t\tgoto teardown;" + NL +
       "\t}" + NL +
       "\tparity_i915_driver_register(&dprobe, &dcore, &probe_pm, opregion_present);")
new = ("\t/*" + NL +
       "\t * intel_opregion_register(): E-122 VBT_ONLY -- the OpRegion runtime protocol is not joined (no ACPI notifier is" + NL +
       "\t * registered, drdy / ardy / csts / DIDL / CADL are not written, no ASLE service), as in the reference built without" + NL +
       "\t * ACPI where these calls are empty.  Recorded; the probe continues." + NL +
       "\t */" + NL +
       "\tif (opregion_present) {" + NL +
       "\t\tosdep_trace_emit(&trace, PARITY_STAGE_P3, OSDEP_TR_NOTE," + NL +
       '\t\t\t"intel_opregion_register:runtime_disabled(vbt_only)", 0u, 0u);' + NL +
       '\t\tkern_logf("i915: parity P7 intel_opregion_register: runtime DISABLED (VBT_ONLY, ACPI_RUNTIME_UNAVAILABLE): no "' + NL +
       '\t\t\t"notifier registered, no mailbox written, no ASLE service -- the probe continues' + BS + 'n");' + NL +
       "\t}" + NL +
       "\tparity_i915_driver_register(&dprobe, &dcore, &probe_pm, 0 /* opregion runtime not registered */);")
pr = rep(pr, old, new)
open(P + "probe.c", "w").write(pr)

pc = open(P + "native_precheck.c").read()
pc = rep(pc, '" OPREGION_REGISTER(later wall: not ported)"', '" OPREGION_PRESENT(data only; runtime disabled)"')
open(P + "native_precheck.c", "w").write(pc)
print("done")
