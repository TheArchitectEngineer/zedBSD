#!/usr/bin/env python3
"""WS031 E-123 round 77: the HDMI hotplug receive path (slice a) as reference text.  port_lcd_modeset.json gains the
units of the reference chain  icp_irq_handler -> intel_get_hpd_pins -> intel_hpd_irq_handler -> i915_hotplug_work_func
-> intel_ddi_hotplug -> drm_helper_probe_detect -> intel_hdmi_detect -> intel_digital_port_connected
(lpt_digital_port_connected: SDEISR & pch_hpd[pin]), plus the types and registers they use.  Idempotent.
usage: round77.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
p = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(p))
NL = chr(10)
TAB = chr(9)

new_files = [
    {"dir": "i915", "source": "display/intel_hotplug.c", "path": "drivers/gpu/drm/i915/display/intel_hotplug.c",
     "out": "intel_hotplug_port.c", "includes": ["hpd_compat.h"], "glue": "parity_hotplug_glue.inc",
     "ranges": [["#define HPD_STORM_DEFAULT_THRESHOLD", "#define HPD_RETRY_DELAY" + TAB * 3 + "1000" + NL,
                 "HPD storm / retry timing"]],
     "functions": ["intel_connector_hpd_pin", "intel_hpd_irq_storm_detect", "intel_hpd_irq_storm_switch_to_polling",
                   "intel_hpd_irq_storm_reenable_work", "intel_hotplug_detect_connector", "intel_encoder_hotplug",
                   "intel_encoder_has_hpd_pulse", "i915_digport_work_func", "i915_hotplug_work_func",
                   "intel_hpd_irq_handler", "intel_hpd_init_early", "intel_hpd_cancel_work"]},
    {"dir": "i915", "source": "display/intel_hotplug_irq.c", "path": "drivers/gpu/drm/i915/display/intel_hotplug_irq.c",
     "out": "intel_hotplug_irq_port.c", "includes": ["hpd_compat.h"],
     "functions": ["icp_ddi_port_hotplug_long_detect", "icp_tc_port_hotplug_long_detect", "intel_get_hpd_pins",
                   "icp_irq_handler"]},
    {"dir": "i915", "source": "display/intel_ddi.c", "path": "drivers/gpu/drm/i915/display/intel_ddi.c",
     "out": "intel_ddi_hotplug_port.c", "includes": ["hpd_compat.h"], "glue": "parity_ddi_hotplug_glue.inc",
     "functions": ["intel_ddi_hotplug", "lpt_digital_port_connected"]},
    {"dir": "i915", "source": "display/intel_dp.c", "path": "drivers/gpu/drm/i915/display/intel_dp.c",
     "out": "intel_dp_connected_port.c", "includes": ["hpd_compat.h"],
     "functions": ["intel_digital_port_connected"]},
    {"dir": "i915", "source": "display/intel_hdmi.c", "path": "drivers/gpu/drm/i915/display/intel_hdmi.c",
     "out": "intel_hdmi_detect_port.c", "includes": ["hpd_compat.h"], "glue": "parity_hdmi_detect_glue.inc",
     "functions": ["intel_hdmi_unset_edid", "intel_hdmi_detect"]},
    {"dir": "drm", "source": "drm_probe_helper.c", "path": "drivers/gpu/drm/drm_probe_helper.c",
     "out": "drm_probe_detect_port.c", "includes": ["hpd_compat.h"],
     "functions": ["drm_helper_probe_detect_ctx", "drm_helper_probe_detect"]},
    {"dir": "drm", "source": "drm_connector.c", "path": "drivers/gpu/drm/drm_connector.c",
     "out": "drm_connector_status_port.c", "includes": ["hpd_compat.h"],
     "functions": ["drm_get_connector_status_name"]},
]
range_headers = [
    {"dir": "i915", "source": "display/intel_display_limits.h", "path": "drivers/gpu/drm/i915/display/intel_display_limits.h",
     "out": "hpd_pin_enum.h", "ranges": [["enum hpd_pin {", NL + "};" + NL, "enum hpd_pin"]]},
    {"dir": "i915", "source": "display/intel_display.h", "path": "drivers/gpu/drm/i915/display/intel_display.h",
     "out": "hpd_for_each_pin.h", "ranges": [["#define for_each_hpd_pin(__pin)", "(__pin)++)" + NL, "for_each_hpd_pin"]]},
    {"dir": "i915", "source": "display/intel_display_types.h", "path": "drivers/gpu/drm/i915/display/intel_display_types.h",
     "out": "hpd_hotplug_state.h", "ranges": [["enum intel_hotplug_state {", NL + "};" + NL, "enum intel_hotplug_state"]]},
    {"dir": "i915", "source": "display/intel_display_core.h", "path": "drivers/gpu/drm/i915/display/intel_display_core.h",
     "out": "hpd_hotplug_types.h", "ranges": [["struct intel_hotplug {", TAB + "bool ignore_long_hpd;" + NL + "};" + NL,
                                             "struct intel_hotplug"]]},
    {"dir": "drm", "source": "drm_connector.h", "path": "include/drm/drm_connector.h",
     "out": "hpd_drm_connector_status.h",
     "ranges": [["enum drm_connector_status {", NL + "};" + NL, "enum drm_connector_status"],
                ["#define DRM_CONNECTOR_POLL_HPD", "#define DRM_CONNECTOR_POLL_DISCONNECT (1 << 2)" + NL,
                 "DRM_CONNECTOR_POLL_*"]]},
]
macro_headers = [
    {"dir": "i915", "source": "i915_reg.h", "path": "drivers/gpu/drm/i915/i915_reg.h", "out": "hpd_mreg_i915_reg.h",
     "roots": ["SDEISR", "SHOTPLUG_CTL_DDI", "SHOTPLUG_CTL_DDI_HPD_LONG_DETECT", "SHOTPLUG_CTL_TC",
               "ICP_TC_HPD_LONG_DETECT", "SDE_DDI_HOTPLUG_MASK_ICP", "SDE_TC_HOTPLUG_MASK_ICP", "SDE_GMBUS_ICP"],
     "exclude": ["REG_BIT", "_MMIO"]},
    {"dir": "drm", "source": "drm_dp.h", "path": "include/drm/display/drm_dp.h", "out": "hpd_mreg_drm_dp.h",
     "roots": ["DP_TEST_LINK_PHY_TEST_PATTERN"]},
]

def upsert(lst, items):
    outs = {e["out"] for e in items}
    kept = [e for e in lst if e["out"] not in outs]
    return kept + items

j["new_files"] = upsert(j["new_files"], new_files)
j["range_headers"] = upsert(j.get("range_headers", []), range_headers)
j["macro_headers"] = upsert(j["macro_headers"], macro_headers)
open(p, "w").write(json.dumps(j, indent=1) + NL)
print("spec: new_files", len(j["new_files"]), "range_headers", len(j["range_headers"]), "macro_headers", len(j["macro_headers"]))
