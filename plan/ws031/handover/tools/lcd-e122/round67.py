#!/usr/bin/env python3
"""WS031 E-122 round 67: OP-NOTIFY unit 1 -- generator spec for intel_opregion_port.c (mailbox layout, struct
intel_opregion, intel_opregion_video_event), the notifier chain, the glue, the ktest; build wiring.
usage: round67.py <repo root> <dir with the new files>"""
import sys, json, shutil
root = sys.argv[1].rstrip("/") + "/"
src = sys.argv[2].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
J = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
NL = chr(10)
TAB = chr(9)

shutil.copy(src + "opregion_service.h", P + "opregion_service.h")
shutil.copy(src + "opregion_service.c", P + "opregion_service.c")
for f in ("opregion_compat.h", "parity_opregion_glue.inc", "parity_opregion.h", "opregion_ktest.c", "opregion_ktest.h"):
    shutil.copy(src + f, L + f)

j = json.load(open(J))
if not any(x["out"] == "opreg_struct.h" for x in j["range_headers"]):
    j["range_headers"].append({"dir": "i915", "out": "opreg_struct.h", "path": "drivers/gpu/drm/i915/display/intel_opregion.h",
        "source": "display/intel_opregion.h",
        "ranges": [["struct intel_opregion {", NL + "};" + NL, "struct intel_opregion"],
                   ["#define OPREGION_SIZE", NL, "OPREGION_SIZE"]]})
if not any(x["out"] == "intel_opregion_port.c" for x in j["new_files"]):
    j["new_files"].append({"dir": "i915", "out": "intel_opregion_port.c",
        "path": "drivers/gpu/drm/i915/display/intel_opregion.c", "source": "display/intel_opregion.c",
        "includes": ["opregion_compat.h"], "glue": "parity_opregion_glue.inc",
        "functions": ["intel_opregion_video_event"],
        "ranges": [["#define OPREGION_HEADER_OFFSET 0", "#define MAX_DSLP" + TAB + "1500" + NL, "mailbox layout and encodings"],
                   ["#define ACPI_EV_DISPLAY_SWITCH (1<<0)", "#define ACPI_EV_DOCK            (1<<2)" + NL, "ACPI_EV_*"]]})
json.dump(j, open(J, "w"), indent=1, sort_keys=True)

mk = open(root + "platform/amd64/vmunix.mk").read()
if "intel_opregion_port.c" not in mk:
    old = "src/drivers/gpu/i915/parity/lcd/lcdg_ktest.c"
    assert mk.count(old) == 1
    mk = mk.replace(old, old + " src/drivers/gpu/i915/parity/lcd/intel_opregion_port.c src/drivers/gpu/i915/parity/lcd/opregion_ktest.c"
                    " src/drivers/gpu/i915/parity/opregion_service.c")
    open(root + "platform/amd64/vmunix.mk", "w").write(mk)

k = open(P + "ktest.c").read()
if "parity_opregion_ktest" not in k:
    old = TAB * 3 + "parity_lcdg_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL
    assert k.count(old) == 1
    k = k.replace(old, old + TAB * 3 + "/* E-122 OP-NOTIFY: the OpRegion receive side on a SHADOW mailbox, synthetic ACPI video events */" + NL +
                  TAB * 3 + "parity_opregion_ktest(edp_ktest_check);" + NL)
    old = '#include "lcd/lcdg_ktest.h"' + NL
    assert k.count(old) == 1
    k = k.replace(old, old + '#include "lcd/opregion_ktest.h"' + NL)
    open(P + "ktest.c", "w").write(k)
print("done")
