#!/usr/bin/env python3
"""WS031 E-123 round 79: HDMI hotplug slice (b) -- the EDID over GMBUS as reference text.
 - intel_gmbus_port.c: struct intel_gmbus and the GMBUS transfer (gmbus_wait / _idle, read / write chunks, the index
   transfer, do_gmbus_xfer with its NAK retry and the bit-banging fallback signal, gmbus_xfer, force_bit,
   intel_gmbus_irq_handler) from display/intel_gmbus.c;
 - intel_hdmi_detect_port.c gains intel_hdmi_set_edid (the adapted glue version goes);
 - hpd_mreg_gmbus.h / hpd_mreg_gmbus_pins.h: the GMBUS registers and pin numbers.
Idempotent.  usage: round79.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
p = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(p))
NL = chr(10)
TAB = chr(9)

gmbus = {"dir": "i915", "source": "display/intel_gmbus.c", "path": "drivers/gpu/drm/i915/display/intel_gmbus.c",
         "out": "intel_gmbus_port.c", "includes": ["hpd_compat.h"], "glue": "parity_gmbus_glue.inc",
         "ranges": [["struct intel_gmbus {", TAB + "struct drm_i915_private *i915;" + NL + "};" + NL, "struct intel_gmbus"],
                    ["#define INTEL_GMBUS_BURST_READ_MAX_LEN", "767U" + NL, "INTEL_GMBUS_BURST_READ_MAX_LEN"]],
         "functions": ["to_intel_gmbus", "intel_gmbus_reset", "has_gmbus_irq", "gmbus_wait", "gmbus_wait_idle",
                       "gmbus_max_xfer_size", "gmbus_xfer_read_chunk", "gmbus_xfer_read", "gmbus_xfer_write_chunk",
                       "gmbus_xfer_write", "gmbus_is_index_xfer", "gmbus_index_xfer", "do_gmbus_xfer", "gmbus_xfer",
                       "intel_gmbus_force_bit", "intel_gmbus_is_forced_bit", "intel_gmbus_irq_handler"]}
for e in j["new_files"]:
    if e["out"] == "intel_hdmi_detect_port.c":
        e["functions"] = ["intel_hdmi_unset_edid", "intel_hdmi_set_edid", "intel_hdmi_detect"]
j["new_files"] = [e for e in j["new_files"] if e["out"] != gmbus["out"]] + [gmbus]

mh = [
    {"dir": "i915", "source": "display/intel_gmbus_regs.h", "path": "drivers/gpu/drm/i915/display/intel_gmbus_regs.h",
     "out": "hpd_mreg_gmbus.h", "exclude": ["_MMIO"],
     "roots": ["GMBUS0", "GMBUS1", "GMBUS2", "GMBUS3", "GMBUS4", "GMBUS5", "GMBUS_RATE_100KHZ", "GMBUS_BYTE_CNT_OVERRIDE",
               "GMBUS_SW_CLR_INT", "GMBUS_SW_RDY", "GMBUS_CYCLE_WAIT", "GMBUS_CYCLE_INDEX", "GMBUS_CYCLE_STOP",
               "GMBUS_BYTE_COUNT_SHIFT", "GMBUS_BYTE_COUNT_MAX", "GEN9_GMBUS_BYTE_COUNT_MAX", "GMBUS_SLAVE_INDEX_SHIFT",
               "GMBUS_SLAVE_ADDR_SHIFT", "GMBUS_SLAVE_READ", "GMBUS_SLAVE_WRITE", "GMBUS_HW_WAIT_PHASE", "GMBUS_HW_RDY",
               "GMBUS_SATOER", "GMBUS_ACTIVE", "GMBUS_IDLE_EN", "GMBUS_HW_WAIT_EN", "GMBUS_HW_RDY_EN",
               "GMBUS_2BYTE_INDEX_EN"]},
    {"dir": "i915", "source": "display/intel_gmbus.h", "path": "drivers/gpu/drm/i915/display/intel_gmbus.h",
     "out": "hpd_mreg_gmbus_pins.h", "roots": ["GMBUS_PIN_2_BXT", "GMBUS_NUM_PINS"]},
]
outs = {m["out"] for m in mh}
j["macro_headers"] = [m for m in j["macro_headers"] if m["out"] not in outs] + mh
open(p, "w").write(json.dumps(j, indent=1) + NL)
print("spec updated")
