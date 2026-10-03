#!/usr/bin/env python3
"""WS031 E-114 round 4.  usage: round4.py <repo root>"""
import sys, json, re
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
R = "plan/ws031/linux-parity/linux-reference/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

q = load(L + "lcd_seq_compat.h")
for n in ["intel_dp_set_link_params", "intel_dp_set_power", "intel_dp_start_link_train", "intel_dp_stop_link_train",
          "intel_edp_backlight_on", "intel_edp_backlight_off"]:
    lines = [l for l in q.split(NL) if l.startswith("#define " + n + "(")]
    assert len(lines) == 1, n
    q = q.replace(lines[0] + NL, "")
q = q.replace("#define DP_SET_POWER_D0 0x1" + NL, "")
save(L + "lcd_seq_compat.h", q)

c = load(L + "lcd_compat.h")
c = rep(c, "	struct { int unused; } aux;" + NL, "	struct drm_dp_aux aux;" + NL +
        "	u8 downstream_ports[16];            /* DP_MAX_DOWNSTREAM_PORTS */" + NL +
        "	u8 (*voltage_max)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);" + NL +
        "	u8 (*preemph_max)(struct intel_dp *intel_dp, u8 voltage_swing);" + NL)
c = rep(c, "/* the panel's VBT data the modeset bodies read", "struct drm_dp_aux { const char *name; struct drm_device *drm_dev; };" + NL +
        "/* the panel's VBT data the modeset bodies read")
save(L + "lcd_compat.h", c)

d = load(L + "lcd_dp_compat.h")
d = rep(d, "#undef EIO" + NL, """#include "lcd_mreg_link_training.h"     /* reference, extracted: the TRAIN_*_FMT / _ARGS log macros */
#include "lcd_dp_helper_inlines.h"      /* reference, extracted: drm_dp_tps3_supported, drm_dp_tps4_supported, drm_dp_is_branch */
#undef ERANGE
#define ERANGE 34                       /* Linux numbering, returned negative */
#define USEC_PER_MSEC 1000L
#define WARN_ON(cond) ({ int _w = !!(cond); if (_w) parity_lcd_error("WARN_ON(" #cond ")\\n"); _w; })
#define hweight8(x) ((unsigned int)__builtin_popcount((unsigned int)(x) & 0xffu))
#define ilog2(x) (31 - __builtin_clz((unsigned int)(x)))
/* jiffies: only intel_edp_init_source_oui() uses it, to remember WHEN the OUI was written (the wait that
 * consumes it, intel_dp_wait_source_oui(), belongs to the backlight / PPS side) */
#define jiffies (0ul)
#undef EIO
""")
d = rep(d, "#endif /* PARITY_LCD_DP_COMPAT_H */", """/* prototypes of kept non-static reference functions called across the generated files */
bool intel_dp_is_edp(struct intel_dp *intel_dp);
bool intel_dp_source_supports_tps3(struct drm_i915_private *i915);
bool intel_dp_source_supports_tps4(struct drm_i915_private *i915);
void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock, u8 *link_bw, u8 *rate_select);
void intel_dp_set_link_params(struct intel_dp *intel_dp, int link_rate, int lane_count);
void intel_dp_set_power(struct intel_dp *intel_dp, u8 mode);
void intel_dp_start_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
void intel_dp_stop_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
void intel_dp_program_link_training_pattern(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, enum drm_dp_phy dp_phy, u8 dp_train_pat);
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void intel_edp_backlight_off(const struct drm_connector_state *old_conn_state);
bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
u8 drm_dp_get_adjust_tx_ffe_preset(const u8 link_status[DP_LINK_STATUS_SIZE], int lane);
int drm_dp_read_clock_recovery_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, bool uhbr);
int drm_dp_read_channel_eq_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, bool uhbr);
int drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux, enum drm_dp_phy dp_phy, u8 link_status[DP_LINK_STATUS_SIZE]);
int drm_dp_lttpr_count(const u8 cap[DP_LTTPR_COMMON_CAP_SIZE]);
bool drm_dp_lttpr_voltage_swing_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
bool drm_dp_lttpr_pre_emphasis_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
int drm_dp_read_lttpr_common_caps(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 caps[DP_LTTPR_COMMON_CAP_SIZE]);
int drm_dp_read_lttpr_phy_caps(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE], enum drm_dp_phy dp_phy, u8 caps[DP_LTTPR_PHY_CAP_SIZE]);
const char *drm_dp_phy_name(enum drm_dp_phy dp_phy);
u8 drm_dp_link_rate_to_bw_code(int link_rate);

#endif /* PARITY_LCD_DP_COMPAT_H */""")
save(L + "lcd_dp_compat.h", d)

j = json.load(open(root + J))
lt = open(root + R + "ubu-i915-src/display/intel_dp_link_training.c").read()
roots = sorted(set(re.findall(r"(?m)^#define[ \t]+(_?TRAIN_\w+)", lt)))
j["macro_headers"].append({"out": "lcd_mreg_link_training.h", "dir": "i915", "source": "display/intel_dp_link_training.c",
                           "path": "drivers/gpu/drm/i915/display/intel_dp_link_training.c", "roots": roots, "exclude": j["macro_headers"][0]["exclude"]})
hh = open(root + R + "drm-v6.8.12/drm_dp_helper.h").read()
j["range_headers"].append({"out": "lcd_dp_helper_inlines.h", "dir": "drm", "source": "drm_dp_helper.h", "path": "include/drm/display/drm_dp_helper.h",
    "ranges": [["static inline bool\ndrm_dp_tps3_supported(", NL + "}" + NL, "drm_dp_tps3_supported"],
               ["static inline bool\ndrm_dp_tps4_supported(", NL + "}" + NL, "drm_dp_tps4_supported"],
               ["static inline bool\ndrm_dp_is_branch(", NL + "}" + NL, "drm_dp_is_branch"]]})
for f in j["new_files"]:
    if f["out"] == "drm_dp_link_port.c":
        f["functions"] = ["dp_lttpr_common_cap", "dp_lttpr_phy_cap", "drm_dp_read_lttpr_regs"] + f["functions"]
    if f["out"] == "intel_dp_link_training_port.c":
        f["includes"] = ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_dp_compat.h"]
j["extra"]["intel_dp.c"] = ["intel_dp_rate_index"] + j["extra"]["intel_dp.c"]
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated; TRAIN roots:", roots)
for n in ["drm_dp_tps3_supported", "drm_dp_tps4_supported", "drm_dp_is_branch"]:
    print(n, hh.count("static inline bool\n" + n + "("))
