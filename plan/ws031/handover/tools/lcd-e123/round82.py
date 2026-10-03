#!/usr/bin/env python3
"""WS031 E-123 round 82: the modeset object carries an HDMI output (DDI B) beside the eDP one.
 - parity_lcd_modeset_cfg gains `output_hdmi`; prepare() fills the crtc state / digital port the way
   intel_hdmi_compute_config() + intel_ddi_init() leave them for an HDMI sink (8 bpc RGB, 4 lanes, TMDS clock in
   s->link.rate_khz) and skips the DP-only parts (link M/N, enhanced framing, eDP VBT, backlight);
 - the enable's evidence for HDMI is the crtc, not a DP link status;
 - parity_dpll_glue.inc exports the WRPLL calculation (icl_calc_wrpll + icl_calc_dpll_state) as
   parity_icl_hdmi_wrpll(), and parity_lcd_compute_hdmi() builds a parity_lcd_state from a mode with it.
Idempotent.  usage: round82.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)

def edit(path, pairs, marker):
    s = open(path).read()
    if marker in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90])
        s = s.replace(old, new)
    open(path, "w").write(s)

edit(L + "parity_lcd_modeset.h", [(
    "struct parity_lcd_modeset_cfg {" + NL + "	int port;",
    "struct parity_lcd_modeset_cfg {" + NL +
    "	int output_hdmi;                /* 0 = the eDP panel (DP SST), 1 = an HDMI sink on a combo-PHY DDI */" + NL +
    "	int port;")], "int output_hdmi;")

edit(L + "parity_dpll_glue.inc", [(
    "int parity_icl_dp_combo_pll(",
    "/*" + NL +
    " * The HDMI TMDS clock's PLL state: icl_calc_wrpll() + icl_calc_dpll_state(), the reference's own text." + NL +
    " * `port_clock` is the TMDS clock in kHz; `ref_nssc` the reference clock (38.4 MHz on this platform)." + NL +
    " */" + NL +
    "int parity_icl_hdmi_wrpll(int port_clock, int ref_nssc, u32 *cfgcr0, u32 *cfgcr1, u32 *div0)" + NL +
    "{" + NL +
    "	struct drm_i915_private i915;" + NL +
    "	struct intel_crtc crtc;" + NL +
    "	struct intel_crtc_state cs;" + NL +
    "	struct skl_wrpll_params params;" + NL +
    "	struct intel_dpll_hw_state hw;" + NL +
    "	int rc;" + NL + NL +
    "	memset(&i915, 0, sizeof(i915));" + NL +
    "	memset(&crtc, 0, sizeof(crtc));" + NL +
    "	memset(&cs, 0, sizeof(cs));" + NL +
    "	memset(&params, 0, sizeof(params));" + NL +
    "	memset(&hw, 0, sizeof(hw));" + NL +
    "	i915.display.dpll.ref_clks.nssc = ref_nssc;" + NL +
    "	crtc.base.dev = &i915.drm;" + NL +
    "	cs.uapi.crtc = &crtc.base;" + NL +
    "	cs.port_clock = port_clock;" + NL +
    "	rc = icl_calc_wrpll(&cs, &params);" + NL +
    "	if (rc != 0)" + NL +
    "		return rc;" + NL +
    "	icl_calc_dpll_state(&i915, &params, &hw);" + NL +
    "	*cfgcr0 = hw.cfgcr0; *cfgcr1 = hw.cfgcr1; *div0 = hw.div0;" + NL +
    "	return 0;" + NL +
    "}" + NL + NL +
    "int parity_icl_dp_combo_pll(")], "parity_icl_hdmi_wrpll(")

# prepare(): the HDMI crtc state / digital port
edit(L + "parity_lcd_modeset.c", [
    ("	if (cfg->port < 0 || cfg->port > 1 || cfg->pipe < 0 || cfg->pipe > 3 || cfg->cpu_transcoder < 0 || cfg->cpu_transcoder > 3 ||" + NL +
     "	    cfg->dpll_id < 0 || cfg->dpll_id > 1 || cfg->aux_ch != cfg->port || s->link.rate_khz <= 0 || s->link.rate_khz > 810000 ||" + NL +
     "	    (s->link.lanes != 1 && s->link.lanes != 2 && s->link.lanes != 4) || s->link.bpp <= 0)" + NL +
     "		return -EINVAL;",
     "	if (cfg->port < 0 || cfg->port > 1 || cfg->pipe < 0 || cfg->pipe > 3 || cfg->cpu_transcoder < 0 || cfg->cpu_transcoder > 3 ||" + NL +
     "	    cfg->dpll_id < 0 || cfg->dpll_id > 1 || s->link.rate_khz <= 0 || s->link.bpp <= 0)" + NL +
     "		return -EINVAL;" + NL +
     "	/* HDMI carries the TMDS clock in link.rate_khz (up to 600 MHz); DP carries the link rate and its lane count */" + NL +
     "	if (cfg->output_hdmi ? s->link.rate_khz > 600000 :" + NL +
     "	    (cfg->aux_ch != cfg->port || s->link.rate_khz > 810000 ||" + NL +
     "	     (s->link.lanes != 1 && s->link.lanes != 2 && s->link.lanes != 4)))" + NL +
     "		return -EINVAL;"),
    ("	ms.crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);",
     "	ms.crtc_state.output_types = cfg->output_hdmi ? BIT(INTEL_OUTPUT_HDMI) : BIT(INTEL_OUTPUT_EDP);"),
    ("	ms.crtc_state.enhanced_framing = (cfg->dpcd[2] & 0x80u) != 0u;      /* DP_ENHANCED_FRAME_CAP */",
     "	ms.crtc_state.enhanced_framing = !cfg->output_hdmi && (cfg->dpcd[2] & 0x80u) != 0u;   /* DP_ENHANCED_FRAME_CAP */" + NL +
     "	/*" + NL +
     "	 * What intel_hdmi_compute_config() leaves for a sink whose EDID could not be read: DVI mode (has_hdmi_sink" + NL +
     "	 * false, no infoframes, no scrambling), RGB 8 bpc, four lanes, the TMDS clock as the port clock." + NL +
     "	 * ADAPTATION: the reference derives has_hdmi_sink / bpc / colorimetry from the EDID; without one it would" + NL +
     "	 * not light the sink at all (the connector stays disconnected)." + NL +
     "	 */" + NL +
     "	if (cfg->output_hdmi) {" + NL +
     "		ms.crtc_state.has_hdmi_sink = false;" + NL +
     "		ms.crtc_state.has_infoframe = false;" + NL +
     "		ms.crtc_state.hdmi_scrambling = false;" + NL +
     "		ms.crtc_state.hdmi_high_tmds_clock_ratio = false;" + NL +
     "		ms.crtc_state.limited_color_range = false;" + NL +
     "		ms.crtc_state.lane_count = 4;" + NL +
     "		ms.crtc_state.dither = false;" + NL +
     "	}"),
    ("	ms.dig_port.base.type = INTEL_OUTPUT_EDP;",
     "	ms.dig_port.base.type = cfg->output_hdmi ? INTEL_OUTPUT_DDI : INTEL_OUTPUT_EDP;" + NL +
     "	if (cfg->output_hdmi) {" + NL +
     "		ms.dig_port.hdmi.attached_connector = &ms.connector;" + NL +
     "		ms.dig_port.hdmi.dp_dual_mode.type = DRM_DP_DUAL_MODE_NONE;   /* no adaptor detected (a step) */" + NL +
     "		ms.dig_port.set_infoframes = parity_lcd_hdmi_set_infoframes();" + NL +
     "	}"),
    ('	ms.connector.base.name = "eDP";',
     '	ms.connector.base.name = cfg->output_hdmi ? "HDMI" : "eDP";'),
    ("	/* connector-init work of the reference that touches the hardware (reads only): the backlight setup */" + NL +
     "	parity_lcd_cur_i915 = &ms.i915;" + NL +
     "	ms.backlight_setup_rc = parity_lcd_ms_backlight_setup(&ms);" + NL +
     "	if (ms.backlight_setup_rc != 0)" + NL +
     '		on_error(0, "intel_backlight_setup failed (no PWM frequency from the hardware or the VBT)\\n");',
     "	/* connector-init work of the reference that touches the hardware (reads only): the backlight setup (panel only) */" + NL +
     "	parity_lcd_cur_i915 = &ms.i915;" + NL +
     "	if (!ms.output_hdmi) {" + NL +
     "		ms.backlight_setup_rc = parity_lcd_ms_backlight_setup(&ms);" + NL +
     "		if (ms.backlight_setup_rc != 0)" + NL +
     '			on_error(0, "intel_backlight_setup failed (no PWM frequency from the hardware or the VBT)\\n");' + NL +
     "	}"),
    ("	/* the evidence that training worked is the sink's own status, not the flag the stop function sets */" + NL +
     "	parity_lcd_modeset_status(&st);" + NL +
     "	read_link_status(&st);" + NL +
     "	if (!ms.crtc.active || !st.cr_ok || !st.eq_ok)" + NL +
     "		return PARITY_LCD_MS_LINK_NOT_TRAINED;",
     "	/* the evidence that training worked is the sink's own status, not the flag the stop function sets" + NL +
     "	 * (HDMI has no link training: the crtc being active is the whole of it) */" + NL +
     "	parity_lcd_modeset_status(&st);" + NL +
     "	if (ms.output_hdmi)" + NL +
     "		return ms.crtc.active ? PARITY_LCD_MS_OK : PARITY_LCD_MS_LINK_NOT_TRAINED;" + NL +
     "	read_link_status(&st);" + NL +
     "	if (!ms.crtc.active || !st.cr_ok || !st.eq_ok)" + NL +
     "		return PARITY_LCD_MS_LINK_NOT_TRAINED;"),
    ("	memset(&ms, 0, sizeof(ms));" + NL + "	ms_ops = ops;",
     "	memset(&ms, 0, sizeof(ms));" + NL + "	ms.output_hdmi = cfg->output_hdmi;" + NL + "	ms_ops = ops;"),
], "cfg->output_hdmi")

edit(L + "parity_lcd_modeset_int.h", [(
    "struct parity_lcd_modeset {",
    "struct parity_lcd_modeset {" + NL +
    "	int output_hdmi;                /* the output this state drives: an HDMI sink instead of the eDP panel */")],
    "int output_hdmi;")

# the HDMI set_infoframes hook (parity_hdmi_mode_glue.inc) and the WRPLL entry
edit(L + "parity_lcd_modeset_int.h", [(
    "#endif",
    "/* intel_hdmi_mode_port.c (glue): the reference's hsw_set_infoframes, as dig_port->set_infoframes */" + NL +
    "void (*parity_lcd_hdmi_set_infoframes(void))(struct intel_encoder *, bool, const struct intel_crtc_state *," + NL +
    "	const struct drm_connector_state *);" + NL + NL + "#endif")], "parity_lcd_hdmi_set_infoframes")

edit(L + "parity_lcd_calc.h", [(
    "int parity_lcd_emit_transcoder(",
    "/*" + NL +
    " * The state for an HDMI sink: the mode is given (no EDID could be read from this port -- see the modeset's" + NL +
    " * ADAPTATION note), the TMDS clock is the mode's pixel clock at 8 bpc (intel_hdmi_tmds_clock()), and the PLL" + NL +
    " * state comes from the reference's WRPLL calculation." + NL +
    " */" + NL +
    "int parity_lcd_compute_hdmi(const struct parity_lcd_mode *mode, int ref_nssc_khz, struct parity_lcd_state *out);" + NL + NL +
    "int parity_lcd_emit_transcoder(")], "parity_lcd_compute_hdmi")

s = open(L + "parity_lcd_calc.c").read()
if "parity_lcd_compute_hdmi" not in s:
    anchor = "static void record_write(void *ctx, u32 reg, u32 value)"
    assert s.count(anchor) == 1
    s = s.replace(anchor,
        "int parity_icl_hdmi_wrpll(int port_clock, int ref_nssc, u32 *cfgcr0, u32 *cfgcr1, u32 *div0);" + NL + NL +
        "int parity_lcd_compute_hdmi(const struct parity_lcd_mode *mode, int ref_nssc_khz, struct parity_lcd_state *out)" + NL +
        "{" + NL +
        "	int rc;" + NL + NL +
        "	if (mode == 0 || out == 0 || mode->clock_khz <= 0)" + NL +
        "		return -EINVAL;" + NL +
        "	memset(out, 0, sizeof(*out));" + NL +
        "	out->mode = *mode;" + NL +
        "	/* intel_hdmi_tmds_clock(): the TMDS clock is the pixel clock scaled by bpc / 8 -- 8 bpc: the same value */" + NL +
        "	out->link.rate_khz = mode->clock_khz;" + NL +
        "	out->link.lanes = 4;" + NL +
        "	out->link.bpp = 24;" + NL +
        "	rc = parity_icl_hdmi_wrpll(out->link.rate_khz, ref_nssc_khz, &out->pll.cfgcr0, &out->pll.cfgcr1, &out->pll.div0);" + NL +
        "	if (rc != 0)" + NL +
        "		return rc;" + NL +
        "	return 0;" + NL +
        "}" + NL + NL + anchor)
    open(L + "parity_lcd_calc.c", "w").write(s)
print("done")
