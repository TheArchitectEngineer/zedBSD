#!/usr/bin/env python3
"""WS031 E-114 round 2: object members the modeset bodies use; disable-side steps and dispatchers.
usage: patch_compat2_e114.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = load(L + "lcd_compat.h")
c = rep(c, "		struct { struct { int nssc; } ref_clks; } dpll;" + NL,
        "		struct { struct { int nssc; } ref_clks; struct { int which; } lock; } dpll;" + NL)
c = rep(c, "	enum pipe hsw_workaround_pipe;" + NL,
        "	enum pipe hsw_workaround_pipe;" + NL +
        "	u16 linetime, ips_linetime;" + NL +
        "	bool double_wide, fec_enable, has_psr, has_audio, enhanced_framing;" + NL +
        "	struct { bool enable; u8 link_count; u8 pixel_overlap; } splitter;" + NL +
        "	struct { bool compression_enable; } dsc;" + NL +
        "	u8 active_planes;" + NL +
        "	int pixel_rate;" + NL)
c = rep(c, "	void (*set_signal_levels)(struct intel_encoder *, const struct intel_crtc_state *);" + NL + "};",
        "	void (*set_signal_levels)(struct intel_encoder *, const struct intel_crtc_state *);" + NL +
        "	void (*disable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);" + NL +
        "	void (*post_disable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);" + NL +
        "	void (*post_pll_disable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);" + NL +
        "	void (*enable_clock)(struct intel_encoder *, const struct intel_crtc_state *);" + NL +
        "	void (*disable_clock)(struct intel_encoder *);" + NL +
        "	const struct intel_ddi_buf_trans *(*get_buf_trans)(struct intel_encoder *, const struct intel_crtc_state *, int *n_entries);" + NL +
        "};")
c = rep(c, "struct intel_dp { u32 DP; };",
        "/* the panel's VBT data the modeset bodies read (filled from the explicit VBT by the caller) */" + NL +
        "struct intel_connector { struct { struct { struct { bool hobl, low_vswing; } edp; } vbt; } panel; };" + NL +
        "struct intel_dp {" + NL +
        "	u32 DP;" + NL +
        "	struct intel_connector *attached_connector;" + NL +
        "	bool hobl_failed, hobl_active, link_trained;" + NL +
        "	int link_rate;" + NL +
        "	u8 lane_count;" + NL +
        "	u8 train_set[4];" + NL +
        "	u8 dpcd[15];" + NL +
        "};")
c = rep(c, "	int ddi_io_wakeref, ddi_io_power_domain;" + NL,
        "	int ddi_io_wakeref, ddi_io_power_domain;" + NL +
        "	int aux_wakeref;" + NL +
        "	int aux_ch;                 /* enum aux_ch: 0 = A */" + NL +
        "	u8 max_lanes;" + NL)
c = rep(c, "#define enc_to_intel_dp(encoder) (&enc_to_dig_port(encoder)->dp)",
        "#define enc_to_intel_dp(encoder) (&enc_to_dig_port(encoder)->dp)" + NL +
        "#define dp_to_dig_port(intel_dp) container_of(intel_dp, struct intel_digital_port, dp)" + NL +
        "#define dp_to_i915(intel_dp) to_i915(dp_to_dig_port(intel_dp)->base.base.dev)")
save(L + "lcd_compat.h", c)

q = load(L + "lcd_seq_compat.h")
q = rep(q, "struct intel_atomic_state { struct { struct drm_device *dev; } base; const struct intel_crtc_state *crtc_state; };",
        "struct intel_atomic_state { struct { struct drm_device *dev; } base; const struct intel_crtc_state *crtc_state, *old_crtc_state; };" + NL +
        "#define intel_atomic_get_old_crtc_state(state, crtc) ((state)->old_crtc_state)")
gone = [l for l in q.split(NL) if l.startswith("#define intel_pps_on(")]
assert len(gone) == 1
q = q.replace(gone[0] + NL, "")
q = rep(q, "#endif /* PARITY_LCD_SEQ_COMPAT_H */", """/* ---- the disable side: callees that are not ported ---- */
void intel_encoders_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_post_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_post_pll_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
#define intel_dmc_disable_pipe(i915, pipe) PARITY_LCD_STEP(i915, "intel_dmc_disable_pipe")
#define intel_hdcp_disable(connector) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_hdcp_disable")
#define intel_psr_disable(intel_dp, cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_psr_disable")
#define intel_edp_backlight_off(conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_edp_backlight_off")
#define intel_dp_sink_disable_decompression(state, connector, cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_dp_sink_disable_decompression")
#define intel_dp_sink_set_msa_timing_par_ignore_state(intel_dp, cs, enable) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_dp_sink_set_msa_timing_par_ignore_state")
#define intel_disable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_disable_ddi_hdmi")
#define intel_crtc_vblank_off(cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_crtc_vblank_off")
#define intel_dsc_disable(cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_dsc_disable")
#define skl_scaler_disable(cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "skl_scaler_disable")
#define ilk_pfit_disable(cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "ilk_pfit_disable")
#define mtl_disable_ddi_buf(encoder, cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "mtl_disable_ddi_buf")
#define adlp_tbt_to_dp_alt_switch_wa(encoder) PARITY_LCD_STEP(parity_lcd_cur_i915, "adlp_tbt_to_dp_alt_switch_wa")
#define intel_tc_port_put_link(dig_port) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_tc_port_put_link")
#define intel_tc_port_link_cancel_reset_work(dig_port) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_tc_port_link_cancel_reset_work")
#define intel_crtc_max_vblank_count(cs) (0xffffffffu)             /* the hardware frame counter's range; used by a vblank-layer check only */

#endif /* PARITY_LCD_SEQ_COMPAT_H */""")
save(L + "lcd_seq_compat.h", q)
print("done")
