#!/usr/bin/env python3
"""WS031 E-114 round 3: DP link layer -- ops hook, intel_dp members, generator include, spec.  usage: round3.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
G = "plan/ws031/handover/tools/port_lcd_calc.py"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

o = load(L + "parity_lcd_ops.h")
o = rep(o, "	int (*panel)(void *ctx, int op);", "	/* drm_dp_read_dpcd_caps(): receiver caps incl. the extended field; 0, or a negative errno */" + NL +
        "	int (*read_dpcd_caps)(void *ctx, uint8_t dpcd[15]);" + NL + "	int (*panel)(void *ctx, int op);")
save(L + "parity_lcd_ops.h", o)

c = load(L + "lcd_compat.h")
c = rep(c, "	u8 dpcd[15];" + NL + "};",
        "	u8 dpcd[15];" + NL +
        "	u8 edp_dpcd[3];" + NL +
        "	struct { int unused; } aux;" + NL +
        "	u8 lttpr_common_caps[8];            /* DP_LTTPR_COMMON_CAP_SIZE */" + NL +
        "	u8 lttpr_phy_caps[8][3];            /* DP_MAX_LTTPR_COUNT x DP_LTTPR_PHY_CAP_SIZE */" + NL +
        "	bool use_rate_select, reset_link_params;" + NL +
        "	int num_sink_rates, sink_rates[8];" + NL +
        "	int max_link_rate; u8 max_link_lane_count;" + NL +
        "	unsigned long last_oui_write;" + NL +
        "	void (*prepare_link_retrain)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);" + NL +
        "	void (*set_link_train)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, u8 dp_train_pat);" + NL +
        "	void (*set_idle_link_train)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);" + NL +
        "};")
c = rep(c, "struct intel_connector { struct { struct { struct { bool hobl, low_vswing; } edp; } vbt; } panel; };",
        "struct intel_connector {" + NL +
        "	struct { struct { int id; } base; const char *name; } base;" + NL +
        "	struct { struct { struct { bool hobl, low_vswing; } edp; } vbt; } panel;" + NL +
        "	int modeset_retry_work;" + NL + "};")
c = rep(c, "struct intel_encoder {" + NL + "	struct { struct drm_device *dev; } base;",
        "struct intel_encoder {" + NL + "	struct { struct drm_device *dev; struct { int id; } base; const char *name; } base;" + NL +
        "	int type;                   /* enum intel_output_type */")
save(L + "lcd_compat.h", c)

g = load(G)
g = rep(g, """   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL + NL + body)""",
        """   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_dp_compat.h"' + NL + NL + body)""")
save(G, g)

j = json.load(open(root + J))
j["extra"]["intel_dp.c"] = ["intel_dp_is_edp", "intel_dp_source_supports_tps3", "intel_dp_source_supports_tps4", "intel_dp_rate_select",
                            "intel_dp_compute_rate", "intel_dp_set_link_params", "downstream_hpd_needs_d0",
                            "intel_edp_init_source_oui", "intel_dp_set_power", "intel_edp_backlight_on", "intel_edp_backlight_off"]
j["range_headers"].append({"out": "lcd_dp_phy_enum.h", "dir": "drm", "source": "drm_dp.h", "path": "include/drm/display/drm_dp.h",
                           "ranges": [["enum drm_dp_phy {", NL + "};" + NL, "enum drm_dp_phy"]]})
j["new_files"].append({"out": "drm_dp_link_port.c", "dir": "drm", "source": "drm_dp_helper.c",
    "path": "drivers/gpu/drm/display/drm_dp_helper.c", "ranges": [],
    "functions": ["dp_link_status", "dp_get_lane_status", "drm_dp_channel_eq_ok", "drm_dp_clock_recovery_ok",
                  "drm_dp_get_adjust_request_voltage", "drm_dp_get_adjust_request_pre_emphasis", "drm_dp_get_adjust_tx_ffe_preset",
                  "__8b10b_clock_recovery_delay_us", "__8b10b_channel_eq_delay_us", "__128b132b_channel_eq_delay_us",
                  "__read_delay", "drm_dp_read_clock_recovery_delay", "drm_dp_read_channel_eq_delay",
                  "drm_dp_dpcd_read_phy_link_status", "drm_dp_lttpr_count",
                  "drm_dp_lttpr_voltage_swing_level_3_supported", "drm_dp_lttpr_pre_emphasis_level_3_supported",
                  "drm_dp_read_lttpr_common_caps", "drm_dp_read_lttpr_phy_caps", "drm_dp_phy_name", "drm_dp_link_rate_to_bw_code"],
    "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_dp_compat.h"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated")
