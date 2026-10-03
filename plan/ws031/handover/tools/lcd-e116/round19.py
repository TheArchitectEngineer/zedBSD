#!/usr/bin/env python3
"""WS031 E-116 round 19: the commit's outer part -- generator spec: CRTC power domains, DBUF pre/post + MBUS, CDCLK requirement.
usage: round19.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
END = NL + "};" + NL
STD = ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_plane_compat.h", "lcd_wm_compat.h"]

j = json.load(open(root + J))
for n in ("get_crtc_power_domains", "intel_modeset_get_crtc_power_domains", "intel_modeset_put_crtc_power_domains"):
    if n not in j["extra"]["intel_display.c"]:
        j["extra"]["intel_display.c"].append(n)
for n in ("tgl_ddi_min_voltage_level", "intel_ddi_compute_min_voltage_level"):
    if n not in j["extra"]["intel_ddi.c"]:
        j["extra"]["intel_ddi.c"].append(n)
for f in j["new_files"]:
    if f["out"] == "skl_watermark_port.c":
        for n in ("intel_dbuf_mdclk_cdclk_ratio_update", "update_mbus_pre_enable", "intel_dbuf_pre_plane_update",
                  "intel_dbuf_post_plane_update", "xelpdp_is_only_pipe_per_dbuf_bank", "intel_mbus_dbox_update"):
            if n not in f["functions"]:
                f["functions"].append(n)
have = [f["out"] for f in j["new_files"]]
if "intel_display_power_set_port.c" not in have:
    j["new_files"].append({"dir": "i915", "out": "intel_display_power_set_port.c", "source": "display/intel_display_power.c",
        "path": "drivers/gpu/drm/i915/display/intel_display_power.c", "ranges": [],
        "functions": ["intel_display_power_get_in_set", "intel_display_power_put_mask_in_set"],
        "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h"]})
if "intel_cdclk_port.c" not in have:
    j["new_files"].append({"dir": "i915", "out": "intel_cdclk_port.c", "source": "display/intel_cdclk.c",
        "path": "drivers/gpu/drm/i915/display/intel_cdclk.c",
        "ranges": [["struct intel_cdclk_vals {", END, "struct intel_cdclk_vals"],
                   ["static const struct intel_cdclk_vals adlp_cdclk_table[] = {", END, "adlp_cdclk_table[]"]],
        "functions": ["bxt_calc_cdclk", "bxt_calc_cdclk_pll_vco", "calc_voltage_level", "tgl_calc_voltage_level",
                      "intel_pixel_rate_to_cdclk", "intel_planes_min_cdclk", "intel_crtc_compute_min_cdclk"],
        "includes": STD, "glue": "parity_cdclk_glue.inc"})
if "intel_bw_port.c" not in have:
    j["new_files"].append({"dir": "i915", "out": "intel_bw_port.c", "source": "display/intel_bw.c",
        "path": "drivers/gpu/drm/i915/display/intel_bw.c", "ranges": [],
        "functions": ["intel_bw_crtc_data_rate", "intel_bw_crtc_min_cdclk"],
        "includes": STD, "glue": "parity_bw_glue.inc"})
heads = [h["out"] for h in j["range_headers"]]
if "lcd_power_domain_set_types.h" not in heads:
    j["range_headers"].append({"dir": "i915", "out": "lcd_power_domain_set_types.h", "source": "display/intel_display_power.h",
        "path": "drivers/gpu/drm/i915/display/intel_display_power.h",
        "ranges": [["struct intel_power_domain_mask {", END, "struct intel_power_domain_mask"],
                   ["struct intel_display_power_domain_set {", END, "struct intel_display_power_domain_set"],
                   ["#define for_each_power_domain(__domain, __mask)", "for_each_if(test_bit((__domain), (__mask)->bits))" + NL, "for_each_power_domain()"]]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated")
