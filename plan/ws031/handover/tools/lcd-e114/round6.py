#!/usr/bin/env python3
"""WS031 E-114 round 6: callees whose reference body starts with an early return that holds in this configuration
become GUARDS (the reference's condition is kept; reaching the unported rest is an ERROR, not a silent pass);
DMC pipe enable / disable and intel_dp_set_infoframes are extracted.  usage: round6.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

q = load(L + "lcd_seq_compat.h")
def drop(name):
    global q
    lines = [l for l in q.split(NL) if l.startswith("#define " + name + "(")]
    assert len(lines) == 1, name
    q = q.replace(lines[0] + NL, "")
for n in ["intel_dmc_enable_pipe", "intel_dmc_disable_pipe", "icl_program_mg_dp_mode", "intel_dp_configure_protocol_converter",
          "intel_dp_sink_enable_decompression", "intel_dp_sink_set_fec_ready", "intel_dp_check_frl_training",
          "intel_dp_pcon_dsc_configure", "intel_ddi_enable_fec", "intel_dsc_dp_pps_write", "intel_dsc_enable",
          "intel_uncompressed_joiner_enable", "skl_pfit_enable", "intel_audio_sdp_split_update", "intel_ddi_wait_for_fec_status",
          "intel_dp_set_infoframes", "trans_port_sync_stop_link_train", "intel_hdcp_enable", "intel_dsc_disable",
          "intel_dp_sink_disable_decompression", "skl_scaler_disable", "drm_connector_update_privacy_screen", "intel_hdcp_disable",
          "intel_psr_disable"]:
    drop(n)
q = rep(q, "/* ---- hsw_crtc_enable(): callees that are not ported ---- */", """/*
 * ---- GUARDS: callees whose reference body begins with an early return that holds in the supported
 * configuration.  The reference's condition is kept here word for word in meaning (the function and the
 * condition are named); if it ever does NOT hold, the unported rest is reported as an ERROR -- the run is
 * then not a success -- instead of being passed over in silence.
 */
#define PARITY_LCD_GUARD(holds, what) do { if (!(holds)) parity_lcd_error("UNPORTED callee body reached: " what "\\n"); } while (0)
/* icl_program_mg_dp_mode(): returns unless the PHY is Type-C (and not in TBT-alt mode) */
#define icl_program_mg_dp_mode(dig_port, cs) PARITY_LCD_GUARD(!intel_phy_is_tc(SEQ_I915_ENCODER(&(dig_port)->base), intel_port_to_phy(SEQ_I915_ENCODER(&(dig_port)->base), (dig_port)->base.port)), "icl_program_mg_dp_mode (Type-C PHY)")
/* intel_dp_configure_protocol_converter(): returns when DPCD_REV < 1.3, or the sink is not a branch device */
#define intel_dp_configure_protocol_converter(intel_dp, cs) PARITY_LCD_GUARD((intel_dp)->dpcd[DP_DPCD_REV] < 0x13 || !drm_dp_is_branch((intel_dp)->dpcd), "intel_dp_configure_protocol_converter (DPCD >= 1.3 branch device)")
/* DSC: every one of these returns at once when crtc_state->dsc.compression_enable is false */
#define intel_dp_sink_enable_decompression(state, connector, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dp_sink_enable_decompression (DSC)")
#define intel_dp_sink_disable_decompression(state, connector, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dp_sink_disable_decompression (DSC)")
#define intel_dsc_dp_pps_write(encoder, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_dp_pps_write (DSC)")
#define intel_dsc_enable(cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_enable (DSC)")
#define intel_dsc_disable(cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_disable (DSC)")
/* intel_uncompressed_joiner_enable(): acts only with bigjoiner_pipes set */
#define intel_uncompressed_joiner_enable(cs) PARITY_LCD_GUARD((cs)->bigjoiner_pipes == 0, "intel_uncompressed_joiner_enable (big joiner)")
/* FEC: all three return at once when crtc_state->fec_enable is false */
#define intel_dp_sink_set_fec_ready(intel_dp, cs, enable) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_dp_sink_set_fec_ready (FEC)")
#define intel_ddi_enable_fec(encoder, cs) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_ddi_enable_fec (FEC)")
#define intel_ddi_wait_for_fec_status(encoder, cs, enabled) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_ddi_wait_for_fec_status (FEC)")
/* intel_dp_check_frl_training(): returns unless downstream_ports[2] has DP_PCON_SOURCE_CTL_MODE (a PCON) */
#define intel_dp_check_frl_training(intel_dp) PARITY_LCD_GUARD(!((intel_dp)->downstream_ports[2] & 0x20), "intel_dp_check_frl_training (PCON, DP_PCON_SOURCE_CTL_MODE = bit 5)")
/* intel_dp_pcon_dsc_configure(): returns unless the sink is an HDMI 2.1 PCON (intel_dp_is_hdmi_2_1_sink: the PCON caps) */
#define intel_dp_pcon_dsc_configure(intel_dp, cs) PARITY_LCD_GUARD(!drm_dp_is_branch((intel_dp)->dpcd), "intel_dp_pcon_dsc_configure (HDMI 2.1 PCON)")
/* skl_pfit_enable() / skl_scaler_disable(): nothing without the pipe scaler (pch_pfit.enabled / a scaler in use) */
#define skl_pfit_enable(cs) PARITY_LCD_GUARD(!(cs)->pch_pfit.enabled, "skl_pfit_enable (panel fitter)")
#define skl_scaler_disable(cs) PARITY_LCD_GUARD(!(cs)->pch_pfit.enabled, "skl_scaler_disable (panel fitter)")
/* intel_audio_sdp_split_update(): a register write only with HAS_DP20 */
#define intel_audio_sdp_split_update(cs) PARITY_LCD_GUARD(!HAS_DP20(SEQ_I915_CRTC_STATE(cs)), "intel_audio_sdp_split_update (DP 2.0)")
/* trans_port_sync_stop_link_train(): returns when crtc_state->sync_mode_slaves_mask is 0 */
#define trans_port_sync_stop_link_train(state, encoder, cs) PARITY_LCD_GUARD((cs)->sync_mode_slaves_mask == 0, "trans_port_sync_stop_link_train (port sync)")
/* intel_hdcp_enable() / _disable(): act only when the connector state asks for content protection */
#define intel_hdcp_enable(state, encoder, cs, conn) PARITY_LCD_GUARD((conn)->content_protection == 0, "intel_hdcp_enable (content protection requested)")
#define intel_hdcp_disable(connector) ((void)0)       /* returns at once when the connector has no HDCP shim (hdcp->shim == NULL): eDP registers none */
/* intel_psr_disable(): returns when crtc_state->has_psr is false */
#define intel_psr_disable(intel_dp, cs) PARITY_LCD_GUARD(!(cs)->has_psr, "intel_psr_disable (PSR)")
/* drm_connector_update_privacy_screen(): returns when the connector has no privacy screen object */
#define drm_connector_update_privacy_screen(conn) ((void)0)
/* intel_write_dp_sdp(): returns unless the SDP type is set in crtc_state->infoframes.enable */
#define intel_write_dp_sdp(encoder, cs, type) PARITY_LCD_GUARD((cs)->infoframes.enable == 0, "intel_write_dp_sdp (an enabled SDP)")
#define HAS_DSC(i915) 1                               /* display version >= 11: only selects a bit to clear in intel_dp_set_infoframes */
#define DP_SDP_VSC 0x07
#define HDMI_PACKET_TYPE_GAMUT_METADATA 0x0a

/* ---- hsw_crtc_enable(): callees that are not ported ---- */""")
save(L + "lcd_seq_compat.h", q)

c = load(L + "lcd_compat.h")
c = rep(c, "struct drm_connector_state { enum drm_colorspace colorspace; void *connector; void *best_encoder; };",
        "struct drm_connector_state { enum drm_colorspace colorspace; void *connector; void *best_encoder; int content_protection; };")
c = rep(c, "	u8 active_planes;" + NL, "	u8 active_planes;" + NL + "	u8 sync_mode_slaves_mask;" + NL + "	struct { u32 enable; } infoframes;" + NL)
c = rep(c, "		struct { bool ignore_long_hpd; } hotplug;", "		struct { bool ignore_long_hpd; } hotplug;" + NL +
        "		struct { u32 fw_mask; } dmc;        /* bit n: firmware for enum intel_dmc_id n is loaded (from the DMC loader) */")
save(L + "lcd_compat.h", c)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "/* ---- state checkers [check] ---- */", """/* DMC: which firmware ids are loaded is the DMC loader's knowledge, handed over in i915->display.dmc.fw_mask [ops-like] */
#define has_dmc_id_fw(i915, dmc_id) ((((i915)->display.dmc.fw_mask) >> (dmc_id)) & 1u)

/* ---- state checkers [check] ---- */""")
m = rep(m, "bool intel_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy);",
        "bool intel_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy);" + NL +
        "void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);" + NL +
        "void intel_dmc_disable_pipe(struct drm_i915_private *i915, enum pipe pipe);" + NL +
        "void intel_dp_set_infoframes(struct intel_encoder *encoder, bool enable, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);")
save(L + "lcd_modeset_compat.h", m)

j = json.load(open(root + J))
j["new_files"].append({"out": "intel_dmc_port.c", "dir": "i915", "source": "display/intel_dmc.c",
    "path": "drivers/gpu/drm/i915/display/intel_dmc.c",
    "ranges": [["enum intel_dmc_id {", NL + "};" + NL, "enum intel_dmc_id"]],
    "functions": ["is_valid_dmc_id", "intel_dmc_enable_pipe", "intel_dmc_disable_pipe"],
    "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_mreg_dmc.h", "lcd_mreg_dmc_c.h"]})
j["macro_headers"].append({"out": "lcd_mreg_dmc.h", "dir": "i915", "source": "display/intel_dmc_regs.h",
    "path": "drivers/gpu/drm/i915/display/intel_dmc_regs.h", "roots": [], "exclude": j["macro_headers"][0]["exclude"]})
j["macro_headers"].append({"out": "lcd_mreg_dmc_c.h", "dir": "i915", "source": "display/intel_dmc.c",
    "path": "drivers/gpu/drm/i915/display/intel_dmc.c", "roots": ["PIPE_TO_DMC_ID"], "exclude": j["macro_headers"][0]["exclude"]})
j["extra"]["intel_dp.c"].append("intel_dp_set_infoframes")
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated")
