#!/usr/bin/env python3
"""WS031 E-113: the reference's enable-sequence CALLERS (hsw_crtc_enable + the DDI DP pre_pll_enable / pre_enable /
enable chain); unported callees become named steps.  usage: patch_e113.py <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
G = "plan/ws031/handover/tools/port_lcd_calc.py"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ------------------------------------------------------------------ generator
g = load(G)
g = rep(g, '''          "hsw_set_frame_start_delay", "hsw_set_transconf", "hsw_configure_cpu_transcoder"):''',
        '''          "hsw_set_frame_start_delay", "hsw_set_transconf", "hsw_configure_cpu_transcoder", "hsw_crtc_enable"):''')
g = rep(g, '''    "   hsw_set_frame_start_delay, hsw_set_transconf, and their caller hsw_configure_cpu_transcoder (so the",
    "   order BETWEEN the writers is the reference's as well);",''',
        '''    "   hsw_set_frame_start_delay, hsw_set_transconf, their caller hsw_configure_cpu_transcoder, and ITS caller",
    "   hsw_crtc_enable (so the order between the writers and the steps around them is the reference's);",
    " - callees of hsw_crtc_enable that are not ported are named steps (lcd_seq_compat.h), never dropped;",''')
g = rep(g, """   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + NL + body + NL +
   '#include "parity_display_emit_glue.inc"'""",
        """   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_seq_compat.h"' + NL + NL + body + NL +
   '#include "parity_display_emit_glue.inc"'""")
g = rep(g, '''for n in ("ddi_buf_phy_link_rate", "intel_ddi_init_dp_buf_reg", "intel_ddi_set_dp_msa", "bdw_trans_port_sync_master_select",
          "intel_ddi_transcoder_func_reg_val_get", "intel_ddi_enable_transcoder_func", "hsw_chicken_trans_reg"):''',
        '''for n in ("ddi_buf_phy_link_rate", "intel_ddi_init_dp_buf_reg", "intel_ddi_set_dp_msa", "bdw_trans_port_sync_master_select",
          "intel_ddi_transcoder_func_reg_val_get", "intel_ddi_enable_transcoder_func", "intel_ddi_config_transcoder_func",
          "hsw_chicken_trans_reg", "tgl_ddi_pre_enable_dp", "intel_ddi_pre_enable_dp", "intel_ddi_pre_enable",
          "intel_enable_ddi_dp", "intel_enable_ddi", "intel_ddi_pre_pll_enable"):''')
g = rep(g, '''    "   intel_ddi_enable_transcoder_func, hsw_chicken_trans_reg;",''',
        '''    "   intel_ddi_enable_transcoder_func, intel_ddi_config_transcoder_func, hsw_chicken_trans_reg, and the DP",
    "   enable-sequence callers tgl_ddi_pre_enable_dp, intel_ddi_pre_enable_dp, intel_ddi_pre_enable,",
    "   intel_enable_ddi_dp, intel_enable_ddi, intel_ddi_pre_pll_enable (their unported callees are named steps,",
    "   lcd_seq_compat.h);",''')
g = rep(g, """   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_dp_msa.h"' + NL + NL + body + NL +""",
        """   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_dp_msa.h"' + NL + '#include "lcd_seq_compat.h"' + NL + NL + body + NL +""")
save(G, g)

# ------------------------------------------------------------------ lcd_compat.h
c = load(L + "lcd_compat.h")
c = rep(c, "struct intel_crtc { struct drm_crtc base; enum pipe pipe; };", "struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; };")
c = rep(c, "	bool gamma_enable, csc_enable, enable_psr2_sel_fetch;" + NL,
        "	bool gamma_enable, csc_enable, enable_psr2_sel_fetch;" + NL +
        "	bool has_infoframe, has_panel_replay;" + NL +
        "	u8 bigjoiner_pipes, lane_lat_optim_mask;" + NL +
        "	void *shared_dpll;" + NL +
        "	enum pipe hsw_workaround_pipe;" + NL)
c = rep(c, "	struct { bool force_thru; } pch_pfit;" + NL, "	struct { bool force_thru, enabled; } pch_pfit;" + NL)
c = rep(c, "struct drm_connector_state { enum drm_colorspace colorspace; };" + NL,
        "struct drm_connector_state { enum drm_colorspace colorspace; void *connector; };" + NL)
c = rep(c, """struct intel_encoder { struct { struct drm_device *dev; } base; enum port port; };
struct intel_dp { u32 DP; };
struct intel_digital_port { struct intel_encoder base; struct intel_dp dp; u32 saved_port_bits; };
""", """struct intel_atomic_state;
struct intel_crtc_state;
struct intel_encoder {
	struct { struct drm_device *dev; } base;
	enum port port;
	/* the hooks hsw_crtc_enable() reaches through intel_encoders_*(); bound as intel_ddi_init() binds them */
	void (*pre_pll_enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*pre_enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*enable)(struct intel_atomic_state *, struct intel_encoder *, const struct intel_crtc_state *, const struct drm_connector_state *);
	void (*set_signal_levels)(struct intel_encoder *, const struct intel_crtc_state *);
};
struct intel_dp { u32 DP; };
struct intel_digital_port {
	struct intel_encoder base;
	struct intel_dp dp;
	u32 saved_port_bits;
	int ddi_io_wakeref, ddi_io_power_domain;
	struct { bool active; } lspcon;
	void (*set_infoframes)(struct intel_encoder *, bool, const struct intel_crtc_state *, const struct drm_connector_state *);
};
""")
save(L + "lcd_compat.h", c)

# ------------------------------------------------------------------ calc API
h = load(L + "parity_lcd_calc.h")
h = rep(h, "#define PARITY_LCD_MAX_REGWRITES 32u", "#define PARITY_LCD_MAX_REGWRITES 96u")
h = rep(h, "/* how many plain writes to `reg` the list holds; the last one's value in *value */",
        """/*
 * The modeset ENABLE sequence for an eDP / DP SST output on a combo-PHY `port`, as the reference's own
 * callers issue it: hsw_crtc_enable() -> encoder pre_pll_enable / shared DPLL / encoder pre_enable
 * (tgl_ddi_pre_enable_dp: panel power, clocks, signal levels, link training, ...; then MSA) -> pipe
 * source, cpu transcoder, colour, watermarks -> encoder enable (transcoder function, transcoder on,
 * backlight).  Ported callees contribute their register operations; every callee that is not ported
 * appears as a named step at its position.  The list is a SEQUENCE DESCRIPTION: nothing is written.
 */
int parity_lcd_emit_enable_sequence(const struct parity_lcd_state *s, int port, int pipe, int cpu_transcoder,
	uint32_t src_width, uint32_t src_height, uint32_t saved_port_bits, struct parity_lcd_words *out);

/* index of the first entry that is the named step `name` at or after `from`, or -1 */
int parity_lcd_words_step(const struct parity_lcd_words *w, const char *name, unsigned from);

/* how many plain writes to `reg` the list holds; the last one's value in *value */""")
save(L + "parity_lcd_calc.h", h)

k = load(L + "parity_lcd_calc.c")
k = rep(k, "unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)" + NL + "{",
        """int parity_lcd_words_step(const struct parity_lcd_words *w, const char *name, unsigned from)
{
	unsigned i;

	for (i = from; w != 0 && name != 0 && i < w->n; i++)
		if (w->w[i].step != 0 && strcmp(w->w[i].step, name) == 0)
			return (int)i;
	return -1;
}

int parity_display_emit_crtc_enable(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int port, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp,
	int src_w, int src_h, u32 saved_port_bits);

int parity_lcd_emit_enable_sequence(const struct parity_lcd_state *s, int port, int pipe, int cpu_transcoder,
	uint32_t src_width, uint32_t src_height, uint32_t saved_port_bits, struct parity_lcd_words *out)
{
	struct parity_lcd_emit emit;
	struct drm_display_mode mode;
	struct intel_link_m_n m_n;
	int rc;

	if (s == 0 || out == 0 || port < 0 || port > 1 || pipe < 0 || pipe > 3 || cpu_transcoder < 0 || cpu_transcoder > 3 ||
	    src_width == 0u || src_height == 0u)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	memset(&emit, 0, sizeof(emit));
	state_to_mode(s, &mode, &m_n);
	emit.ctx = out;
	emit.write32 = record_write;
	emit.rmw32 = record_rmw;
	emit.step = record_step;
	rc = parity_display_emit_crtc_enable(&emit, &mode, &m_n, port, pipe, cpu_transcoder, s->link.rate_khz, s->link.lanes,
		s->link.bpp, (int)src_width, (int)src_height, saved_port_bits);
	if (rc == 0 && out->overflow != 0u)
		rc = -EINVAL;
	return rc;
}

unsigned parity_lcd_words_find(const struct parity_lcd_words *w, uint32_t reg, uint32_t *value)
{""")
save(L + "parity_lcd_calc.c", k)

# ------------------------------------------------------------------ display glue
d = load(L + "parity_display_emit_glue.inc")
d = d.rstrip(NL) + NL + """
/*
 * The whole crtc enable, driven by the reference's hsw_crtc_enable().  The atomic state is reduced to the
 * one new crtc state; the encoder (with its hooks bound as intel_ddi_init() binds them) comes from the DDI
 * glue, which also implements the three intel_encoders_*() dispatchers for this single encoder.
 * shared_dpll is non-NULL (a DP output always has one), so intel_enable_shared_dpll shows up as a step.
 */
struct intel_encoder *parity_ddi_seq_encoder(struct drm_i915_private *i915, int port, u32 saved_port_bits);

int parity_display_emit_crtc_enable(struct parity_lcd_emit *emit, const struct drm_display_mode *mode,
	const struct intel_link_m_n *m_n, int port, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp,
	int src_w, int src_h, u32 saved_port_bits)
{
	static struct drm_i915_private i915;
	static struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	struct intel_atomic_state state;
	static int some_dpll;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&state, 0, sizeof(state));
	i915.emit = emit;
	crtc.base.dev = &i915.drm;
	crtc.pipe = (enum pipe)pipe;
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.uapi.mode_changed = true;
	crtc_state.cpu_transcoder = cpu_transcoder;
	crtc_state.master_transcoder = INVALID_TRANSCODER;
	crtc_state.mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state.hsw_workaround_pipe = INVALID_PIPE;
	crtc_state.hw.adjusted_mode = *mode;
	drm_mode_set_crtcinfo(&crtc_state.hw.adjusted_mode, 0);
	crtc_state.pipe_src.x2 = src_w;
	crtc_state.pipe_src.y2 = src_h;
	crtc_state.output_types = BIT(INTEL_OUTPUT_EDP);
	crtc_state.output_format = INTEL_OUTPUT_FORMAT_RGB;
	crtc_state.port_clock = port_clock;
	crtc_state.lane_count = lanes;
	crtc_state.pipe_bpp = pipe_bpp;
	crtc_state.pixel_multiplier = 1;
	crtc_state.framestart_delay = 1;
	crtc_state.dp_m_n = *m_n;
	crtc_state.shared_dpll = &some_dpll;
	state.base.dev = &i915.drm;
	state.crtc_state = &crtc_state;
	if (parity_ddi_seq_encoder(&i915, port, saved_port_bits) == 0)
		return -EINVAL;

	hsw_crtc_enable(&state, &crtc);
	return crtc.active ? 0 : -EINVAL;
}
"""
save(L + "parity_display_emit_glue.inc", d)

# ------------------------------------------------------------------ DDI glue
x = load(L + "parity_ddi_emit_glue.inc")
x = x.rstrip(NL) + NL + """
/* ---- the enable sequence: one encoder, hooks bound as intel_ddi_init() binds them ---- */
static struct intel_digital_port seq_dig_port;
static struct drm_connector_state seq_conn_state;

struct drm_i915_private *parity_lcd_seq_dp_i915(const struct intel_dp *intel_dp)
{
	return to_i915(container_of(intel_dp, struct intel_digital_port, dp)->base.base.dev);
}

/*
 * encoder->set_signal_levels: intel_ddi_init() picks icl_combo_phy_set_signal_levels for a combo PHY on
 * display version >= 12.  Not ported: a named step.
 */
static void seq_set_signal_levels(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state)
{
	PARITY_LCD_STEP(to_i915(encoder->base.dev), "encoder->set_signal_levels (icl_combo_phy_set_signal_levels)");
}

struct intel_encoder *parity_ddi_seq_encoder(struct drm_i915_private *i915, int port, u32 saved_port_bits)
{
	if (port < 0 || port > 1)               /* combo PHY ports only */
		return 0;
	memset(&seq_dig_port, 0, sizeof(seq_dig_port));
	memset(&seq_conn_state, 0, sizeof(seq_conn_state));
	seq_dig_port.base.base.dev = &i915->drm;
	seq_dig_port.base.port = (enum port)port;
	seq_dig_port.saved_port_bits = saved_port_bits;
	seq_dig_port.base.enable = intel_enable_ddi;
	seq_dig_port.base.pre_pll_enable = intel_ddi_pre_pll_enable;
	seq_dig_port.base.pre_enable = intel_ddi_pre_enable;
	seq_dig_port.base.set_signal_levels = seq_set_signal_levels;
	seq_conn_state.colorspace = DRM_MODE_COLORIMETRY_DEFAULT;
	return &seq_dig_port.base;
}

/*
 * The reference's intel_encoders_*() walk the atomic state's connectors and call the hook of each encoder
 * on this crtc.  Here there is exactly one encoder; the dispatcher itself is recorded as a step so the
 * list shows where hsw_crtc_enable() hands over to the encoder.
 */
void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_pre_pll_enable");
	if (seq_dig_port.base.pre_pll_enable)
		seq_dig_port.base.pre_pll_enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}

void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_pre_enable");
	if (seq_dig_port.base.pre_enable)
		seq_dig_port.base.pre_enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}

void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	PARITY_LCD_STEP(to_i915(crtc->base.dev), "> intel_encoders_enable");
	if (seq_dig_port.base.enable)
		seq_dig_port.base.enable(state, &seq_dig_port.base, state->crtc_state, &seq_conn_state);
}
"""
save(L + "parity_ddi_emit_glue.inc", x)
print("done")
