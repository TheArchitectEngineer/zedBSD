#!/usr/bin/env python3
"""WS031 E-123 round 81: the HDMI modeset's reference text (the external display on DDI B).
 - intel_dpll_port.c gains the WRPLL calculation (HDMI's TMDS clock): icl_wrpll_ref_clock, icl_wrpll_get_multipliers,
   icl_wrpll_params_populate, icl_calc_wrpll;
 - intel_ddi_port.c gains the HDMI branches the DDI callers reach: intel_ddi_hdmi_level, intel_ddi_pre_enable_hdmi,
   intel_enable_ddi_hdmi, intel_disable_ddi_hdmi, intel_ddi_post_disable_hdmi;
 - a new unit intel_hdmi_mode_port.c (display/intel_hdmi.c): assert_hdmi_transcoder_func_disabled, hsw_set_infoframes
   (the disable half is what this path runs), intel_dp_dual_mode_set_tmds_output, intel_hdmi_handle_sink_scrambling;
 - the placeholders these replace go from lcd_seq_compat.h / lcd_modeset_compat.h, and what the new text needs is
   added there instead.
Idempotent.  usage: round81.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
TAB = chr(9)

spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))
dpll_add = ["icl_wrpll_ref_clock", "icl_wrpll_get_multipliers", "icl_wrpll_params_populate", "icl_calc_wrpll"]
ddi_add = ["intel_ddi_hdmi_level", "intel_ddi_pre_enable_hdmi", "intel_enable_ddi_hdmi", "intel_disable_ddi_hdmi",
           "intel_ddi_post_disable_hdmi"]
for key, add in (("intel_dpll_mgr.c", dpll_add), ("intel_ddi.c", ddi_add)):
    have = j["extra"][key]
    j["extra"][key] = have + [n for n in add if n not in have]
hdmi = {"dir": "i915", "source": "display/intel_hdmi.c", "path": "drivers/gpu/drm/i915/display/intel_hdmi.c",
        "out": "intel_hdmi_mode_port.c", "includes": ["lcd_compat.h", "lcd_ddi_regs.h", "lcd_mreg_hdmi_dip.h", "lcd_seq_compat.h", "lcd_modeset_compat.h"],
        "glue": "parity_hdmi_mode_glue.inc",
        "functions": ["assert_hdmi_transcoder_func_disabled", "hsw_set_infoframes", "intel_dp_dual_mode_set_tmds_output",
                      "intel_hdmi_handle_sink_scrambling"]}
j["new_files"] = [e for e in j["new_files"] if e["out"] != hdmi["out"]] + [hdmi]
mh = {"dir": "i915", "source": "i915_reg.h", "path": "drivers/gpu/drm/i915/i915_reg.h", "out": "lcd_mreg_hdmi_dip.h",
      "exclude": ["REG_BIT", "_MMIO", "_MMIO_TRANS2", "_MMIO_PIPE2", "_MMIO_PORT", "_MMIO_TRANS", "_PICK", "_PICK_EVEN",
                  "_TRANS", "_PORT", "DISPLAY_VER", "DISPLAY_INFO", "DISPLAY_RUNTIME_INFO", "REG_FIELD_PREP",
                  "REG_FIELD_GET", "REG_GENMASK", "IS_DISPLAY_VER", "HAS_DP20", "HAS_GMCH", "HAS_VRR"],
      "roots": ["VIDEO_DIP_ENABLE_VSC_HSW", "VIDEO_DIP_ENABLE_AVI_HSW", "VIDEO_DIP_ENABLE_GCP_HSW",
                "VIDEO_DIP_ENABLE_VS_HSW", "VIDEO_DIP_ENABLE_GMP_HSW", "VIDEO_DIP_ENABLE_SPD_HSW",
                "VIDEO_DIP_ENABLE_DRM_GLK"]}
j["macro_headers"] = [m for m in j["macro_headers"] if m["out"] != mh["out"]] + [mh]
open(spec, "w").write(json.dumps(j, indent=1) + NL)

def rep(path, old, new, marker):
    s = open(path).read()
    if marker in s:
        return
    assert s.count(old) == 1, (path, old[:80])
    open(path, "w").write(s.replace(old, new))

# the placeholders the generated HDMI text replaces
rep(L + "lcd_seq_compat.h",
    '#define intel_ddi_pre_enable_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_pre_enable_hdmi")' + NL +
    '#define intel_enable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_enable_ddi_hdmi")' + NL,
    "/* intel_ddi_pre_enable_hdmi / intel_enable_ddi_hdmi are the reference's own text now (intel_ddi_port.c) */" + NL,
    "are the reference's own text now")
rep(L + "lcd_seq_compat.h",
    '#define intel_ddi_post_disable_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_ddi_post_disable_hdmi")' + NL +
    '#define intel_disable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_disable_ddi_hdmi")' + NL,
    "/* intel_ddi_post_disable_hdmi / intel_disable_ddi_hdmi are the reference's own text now (intel_ddi_port.c) */" + NL,
    "intel_ddi_post_disable_hdmi / intel_disable_ddi_hdmi are the reference")
rep(L + "lcd_modeset_compat.h",
    "#define intel_ddi_hdmi_level(encoder, trans) (0)                   /* HDMI branch of intel_ddi_level() */" + NL,
    "/* intel_ddi_hdmi_level() is the reference's own text now (intel_ddi_port.c); the VBT level shift it asks for:" + NL +
    " * ADAPTATION -- the VBT child's hdmi_level_shifter_value is not read here, so the buffer-translation table's" + NL +
    " * own default entry is used, which is what the reference does when the VBT has no level shift. */" + NL +
    "#define intel_bios_hdmi_level_shift(devdata) (-1)" + NL,
    "intel_ddi_hdmi_level() is the reference's own text now")

# what the new HDMI text needs
add = (NL + "/* ---- E-123: what the generated HDMI text (intel_ddi_port.c, intel_hdmi_mode_port.c) needs ---- */" + NL +
       "/* the platform branches of the HDMI enable that DISPLAY_VER 13 never takes */" + NL +
       "#define has_buf_trans_select(i915) (0)" + NL +
       "#define hsw_prepare_hdmi_ddi_buffers(encoder, cs) ((void)0)" + NL +
       "#define mtl_ddi_enable_d2d(encoder) ((void)0)" + NL +
       "#define gen9_chicken_trans_reg_by_port(i915, port) CHICKEN_TRANS(0)" + NL +
       "/* the sink-side halves that need DDC: recorded steps (the SCDC path is only reached when the sink supports it," + NL +
       " * the dual-mode adaptor path only with an adaptor present -- neither is the case here) */" + NL +
       "#define drm_scdc_set_high_tmds_clock_ratio(connector, set) " +
       "(PARITY_LCD_STEP(parity_lcd_cur_i915, \"drm_scdc_set_high_tmds_clock_ratio\"), false)" + NL +
       "#define drm_scdc_set_scrambling(connector, enable) " +
       "(PARITY_LCD_STEP(parity_lcd_cur_i915, \"drm_scdc_set_scrambling\"), false)" + NL +
       "#define drm_dp_dual_mode_set_tmds_output(drm, type, ddc, enable) " +
       "PARITY_LCD_STEP(parity_lcd_cur_i915, \"drm_dp_dual_mode_set_tmds_output\")" + NL +
       "/* the infoframe writes: this path runs with has_infoframe false, so the reference returns before them */" + NL +
       "#define intel_hdmi_set_gcp_infoframe(encoder, cs, conn) " +
       "(PARITY_LCD_STEP(parity_lcd_cur_i915, \"intel_hdmi_set_gcp_infoframe\"), false)" + NL +
       "#define intel_write_infoframe(encoder, cs, type, frame) " +
       "PARITY_LCD_STEP(parity_lcd_cur_i915, \"intel_write_infoframe\")" + NL +
       "#define HDMI_INFOFRAME_TYPE_AVI 0x82" + NL +
       "#define HDMI_INFOFRAME_TYPE_SPD 0x83" + NL +
       "#define HDMI_INFOFRAME_TYPE_VENDOR 0x81" + NL +
       "#define HDMI_INFOFRAME_TYPE_DRM 0x87" + NL +
       "#define str_yes_no(v) ((v) ? \"yes\" : \"no\")" + NL +
       "/* the WRPLL calculation's own diagnostics (i915_utils.h / linux/kernel.h) */" + NL +
       "#define WARN(cond, fmt) ({ int _w = !!(cond); if (_w) parity_lcd_error(fmt); _w; })" + NL +
       "#define abs(x) ((x) < 0 ? -(x) : (x))" + NL +
       "#define intel_hdmi_to_i915(hdmi) parity_lcd_cur_i915" + NL +
       "/* the two the HDMI text of intel_ddi_port.c calls (intel_hdmi_mode_port.c) */" + NL +
       "void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);" + NL +
       "bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder, struct drm_connector *connector," + NL +
       "	bool high_tmds_clock_ratio, bool scrambling);" + NL +
       "/* the DISPLAY_VER >= 14 (MTL) half of intel_enable_ddi_hdmi, which this platform never takes */" + NL +
       "#define mtl_get_port_width(lane_count) (0u)" + NL +
       "#define XELPDP_PORT_WIDTH(width) (0u)" + NL +
       "#define XELPDP_PORT_WIDTH_MASK 0u" + NL +
       "#define XELPDP_PORT_REVERSAL 0u" + NL)
c = open(L + "lcd_compat.h").read()
if "struct intel_hdmi {" not in c:
    anchor = "struct intel_digital_port {" + NL
    assert c.count(anchor) == 1
    c = c.replace(anchor,
        "/* the HDMI half of a digital port (intel_display_types.h): what the HDMI enable / disable text reads */" + NL +
        "enum drm_dp_dual_mode_type { DRM_DP_DUAL_MODE_NONE, DRM_DP_DUAL_MODE_TYPE1_DVI, DRM_DP_DUAL_MODE_TYPE1_HDMI," + NL +
        "	DRM_DP_DUAL_MODE_TYPE2_DVI, DRM_DP_DUAL_MODE_TYPE2_HDMI, DRM_DP_DUAL_MODE_LSPCON };" + NL +
        "struct intel_hdmi { struct intel_connector *attached_connector;" + NL +
        "	struct { enum drm_dp_dual_mode_type type; int max_tmds_clock; } dp_dual_mode; };" + NL + anchor)
    c = c.replace("	struct intel_dp dp;" + NL, "	struct intel_dp dp;" + NL + "	struct intel_hdmi hdmi;" + NL, 1)
    # the connector's DDC adapter and the sink's SCDC capability (drm_connector.h / drm_edid.h)
    c = c.replace("struct drm_display_info { u32 quirks; };",
        "struct drm_scrambling { bool supported, low_rates; };" + NL +
        "struct drm_scdc { bool supported, read_request; struct drm_scrambling scrambling; };" + NL +
        "struct drm_hdmi_info { struct drm_scdc scdc; };" + NL +
        "struct drm_display_info { u32 quirks; struct drm_hdmi_info hdmi; };", 1)
    c = c.replace("struct intel_connector {" + NL +
        "	struct { struct drm_device *dev; struct { int id; } base; const char *name; } base;",
        "struct i2c_adapter;" + NL + "struct intel_connector {" + NL +
        "	struct { struct drm_device *dev; struct { int id; } base; const char *name; struct i2c_adapter *ddc;" + NL +
        "		struct drm_display_info display_info; } base;", 1)
    open(L + "lcd_compat.h", "w").write(c)

s = open(L + "lcd_modeset_compat.h").read()
if "E-123: what the generated HDMI text" not in s:
    at = s.rindex("#endif")
    open(L + "lcd_modeset_compat.h", "w").write(s[:at] + add + NL + s[at:])
print("done")
