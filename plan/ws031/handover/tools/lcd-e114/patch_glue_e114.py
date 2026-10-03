#!/usr/bin/env python3
"""WS031 E-114: per-file glue of the ONE modeset object; the E-113 recorder-only sequence entry points go away
(the sequence is now read from a trace of the integrated run).  usage: patch_glue_e114.py <repo root>"""
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
HEAD = ('#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_dp_compat.h"' + NL +
        '#include "lcd_plane_compat.h"' + NL + '#include "parity_lcd_modeset_int.h"' + NL)

g = load(G)
g = rep(g, '"icl_plane_update_sel_fetch_arm", "icl_plane_update_arm")', '"icl_plane_update_sel_fetch_arm", "icl_plane_update_arm", "icl_plane_disable_arm")')
save(G, g)
j = json.load(open(root + J))
j["extra"]["intel_ddi.c"] = ["intel_ddi_dp_voltage_max", "intel_ddi_dp_preemph_max"] + j["extra"]["intel_ddi.c"]
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

# ---- display glue: drop the E-113 recorder entry, add the modeset entries
d = load(L + "parity_display_emit_glue.inc")
a = d.index("/*" + NL + " * The whole crtc enable, driven by the reference's hsw_crtc_enable().")
d = d[:a].rstrip(NL) + NL + NL + HEAD + """
/*
 * The crtc enable / disable of the one modeset object, driven by the reference's hsw_crtc_enable() /
 * hsw_crtc_disable().  The atomic state is reduced to the one crtc state: it is the NEW state of the
 * enable and the OLD state of the disable.
 */
struct drm_i915_private *parity_lcd_cur_i915;

void parity_lcd_ms_crtc_enable(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = 0;
	hsw_crtc_enable(&ms->state, &ms->crtc);
}

void parity_lcd_ms_crtc_disable(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = 0;
	ms->state.old_crtc_state = &ms->crtc_state;
	hsw_crtc_disable(&ms->state, &ms->crtc);
	ms->crtc.active = false;                /* intel_old_crtc_state_disables() clears it after the hook */
}
"""
save(L + "parity_display_emit_glue.inc", d)

# ---- DDI glue
x = load(L + "parity_ddi_emit_glue.inc")
a = x.index("/* ---- the enable sequence: one encoder, hooks bound as intel_ddi_init() binds them ---- */")
x = x[:a].rstrip(NL) + NL + NL + HEAD + """
/*
 * ---- the one encoder of the modeset object: hooks bound as intel_ddi_init() / intel_ddi_init_dp_connector()
 * bind them for a combo-PHY DDI on display version 12+ (reference: intel_ddi.c, the `encoder->... =` and
 * `dig_port->dp.... =` assignments; buffer translations: intel_ddi_buf_trans_init()).
 */
static struct parity_lcd_modeset *ddi_ms;

void parity_lcd_ms_bind_encoder(struct parity_lcd_modeset *ms)
{
	struct intel_encoder *encoder = &ms->dig_port.base;
	struct intel_dp *intel_dp = &ms->dig_port.dp;

	ddi_ms = ms;
	encoder->enable = intel_enable_ddi;
	encoder->pre_pll_enable = intel_ddi_pre_pll_enable;
	encoder->pre_enable = intel_ddi_pre_enable;
	encoder->disable = intel_disable_ddi;
	encoder->post_disable = intel_ddi_post_disable;
	encoder->post_pll_disable = intel_ddi_post_pll_disable;
	encoder->enable_clock = icl_ddi_combo_enable_clock;
	encoder->disable_clock = icl_ddi_combo_disable_clock;
	encoder->set_signal_levels = icl_combo_phy_set_signal_levels;
	parity_lcd_ms_bind_buf_trans(encoder);
	intel_dp->prepare_link_retrain = intel_ddi_prepare_link_retrain;
	intel_dp->set_link_train = intel_ddi_set_link_train;
	intel_dp->set_idle_link_train = intel_ddi_set_idle_link_train;
	intel_dp->voltage_max = intel_ddi_dp_voltage_max;
	intel_dp->preemph_max = intel_ddi_dp_preemph_max;
}

/*
 * The reference's intel_encoders_*() walk the atomic state's connectors and call the hook of each encoder
 * on the crtc.  Here there is exactly one encoder.  A hook sees the state the reference would hand it:
 * the new crtc state on the way up, the old one on the way down.
 */
#define DDI_MS_DISPATCH(hook, crtc_state_ptr) do { \\
	struct intel_encoder *encoder__ = &ddi_ms->dig_port.base; \\
	if (encoder__->hook) \\
		encoder__->hook(state, encoder__, (crtc_state_ptr), &ddi_ms->conn_state); \\
} while (0)

void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(pre_pll_enable, state->crtc_state);
}

void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(pre_enable, state->crtc_state);
}

void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(enable, state->crtc_state);
}

void intel_encoders_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(disable, state->old_crtc_state);
}

void intel_encoders_post_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(post_disable, state->old_crtc_state);
}

void intel_encoders_post_pll_disable(struct intel_atomic_state *state, struct intel_crtc *crtc)
{
	DDI_MS_DISPATCH(post_pll_disable, state->old_crtc_state);
}
"""
save(L + "parity_ddi_emit_glue.inc", x)

# ---- DPLL glue
p = load(L + "parity_dpll_glue.inc")
p = p.rstrip(NL) + NL + NL + HEAD + """
/*
 * The shared DPLL of the modeset object: the reference's adlp_plls[] entry for DPLL 0 / DPLL 1
 * ({ name, &combo_pll_funcs, id }, no power domain) with the two combo_pll_funcs members used here.
 * state.pipe_mask is what intel_reference_shared_dpll() leaves for the one crtc.
 */
static const struct intel_shared_dpll_funcs parity_combo_pll_funcs = {
	.enable = combo_pll_enable,
	.disable = combo_pll_disable,
};

void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id)
{
	ms->pll_info.name = dpll_id == DPLL_ID_ICL_DPLL1 ? "DPLL 1" : "DPLL 0";
	ms->pll_info.funcs = &parity_combo_pll_funcs;
	ms->pll_info.id = dpll_id == DPLL_ID_ICL_DPLL1 ? DPLL_ID_ICL_DPLL1 : DPLL_ID_ICL_DPLL0;
	ms->pll_info.power_domain = 0;
	ms->pll.info = &ms->pll_info;
	ms->pll.state.pipe_mask = (u8)BIT(ms->crtc.pipe);
	ms->crtc_state.shared_dpll = &ms->pll;
}
"""
save(L + "parity_dpll_glue.inc", p)

# ---- buffer translation glue
open(root + L + "parity_buf_trans_glue.inc", "w").write("""/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_ddi_buf_trans_port.c: what intel_ddi_buf_trans_init()
 * selects for an ADL-P combo PHY (`encoder->get_buf_trans = adlp_get_combo_buf_trans`).
 */
void parity_lcd_ms_bind_buf_trans(struct intel_encoder *encoder)
{
	encoder->get_buf_trans = adlp_get_combo_buf_trans;
}
""")
print("wrote parity_buf_trans_glue.inc")

# ---- plane glue
q = load(L + "parity_plane_emit_glue.inc")
q = q.rstrip(NL) + NL + NL + HEAD + """
/*
 * The plane of the modeset object.  _prepare fills the framebuffer / plane state exactly as
 * parity_plane_emit() above does (same checks, same refusals) and computes ctl / color_ctl with the
 * reference's functions; _update runs icl_plane_update_noarm() then icl_plane_update_arm() (the
 * PLANE_SURF write arms the update: from then on the buffer may be scanned out); _disable runs
 * icl_plane_disable_arm().
 */
int parity_lcd_ms_plane_prepare(struct parity_lcd_modeset *ms, u32 fourcc, u64 modifier, u32 width, u32 height,
	u32 pitch, u32 surf_ggtt_offset)
{
	static const struct drm_format_info xrgb8888 = {
		.format = DRM_FORMAT_XRGB8888, .num_planes = 1, .cpp = { 4, 0, 0, 0 }, .has_alpha = false, .is_yuv = false,
	};

	if (fourcc != DRM_FORMAT_XRGB8888 || modifier != DRM_FORMAT_MOD_LINEAR)
		return -EINVAL;
	if (width == 0u || height == 0u || width > 8192u || height > 8192u || pitch < width * 4u || (pitch % 64u) != 0u ||
	    (surf_ggtt_offset & 0xfffu) != 0u)
		return -EINVAL;
	memset(&ms->plane, 0, sizeof(ms->plane));
	memset(&ms->plane_state, 0, sizeof(ms->plane_state));
	memset(&ms->fb, 0, sizeof(ms->fb));
	ms->plane.base.dev = &ms->i915.drm;
	ms->plane.id = PLANE_PRIMARY;
	ms->plane.pipe = ms->crtc.pipe;
	ms->fb.dev = &ms->i915.drm;
	ms->fb.format = &xrgb8888;
	ms->fb.modifier = modifier;
	ms->plane_state.uapi.plane = &ms->plane.base;
	ms->plane_state.uapi.src.x2 = (int)(width << 16);
	ms->plane_state.uapi.src.y2 = (int)(height << 16);
	ms->plane_state.uapi.dst.x2 = (int)width;
	ms->plane_state.uapi.dst.y2 = (int)height;
	ms->plane_state.hw.fb = &ms->fb;
	ms->plane_state.hw.rotation = DRM_MODE_ROTATE_0;
	ms->plane_state.hw.alpha = 0xffff;
	ms->plane_state.hw.pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
	ms->plane_state.hw.color_encoding = DRM_COLOR_YCBCR_BT709;
	ms->plane_state.hw.color_range = DRM_COLOR_YCBCR_LIMITED_RANGE;
	ms->plane_state.view.color_plane[0].scanout_stride = pitch;
	ms->plane_state.view.color_plane[0].mapping_stride = pitch;
	ms->plane_state.scaler_id = -1;
	ms->plane_state.ggtt_offset = surf_ggtt_offset;
	ms->plane_state.ctl = skl_plane_ctl(&ms->crtc_state, &ms->plane_state);
	ms->plane_state.color_ctl = glk_plane_color_ctl(&ms->crtc_state, &ms->plane_state);
	return 0;
}

void parity_lcd_ms_plane_update(struct parity_lcd_modeset *ms)
{
	icl_plane_update_noarm(&ms->plane, &ms->crtc_state, &ms->plane_state);
	ms->plane_armed = 1;                    /* set BEFORE the arming write: from here the buffer is not ours alone */
	icl_plane_update_arm(&ms->plane, &ms->crtc_state, &ms->plane_state);
}

void parity_lcd_ms_plane_disable(struct parity_lcd_modeset *ms)
{
	icl_plane_disable_arm(&ms->plane, &ms->crtc_state);
}
"""
save(L + "parity_plane_emit_glue.inc", q)

# ---- calc: the recorder-only enable sequence entry point goes away
k = load(L + "parity_lcd_calc.c")
a = k.index("int parity_display_emit_crtc_enable(struct parity_lcd_emit *emit")
b = k.index("unsigned parity_lcd_words_find(")
k = k[:a] + k[b:]
k = k.replace("static void state_to_mode(const struct parity_lcd_state *s, struct drm_display_mode *mode, struct intel_link_m_n *m_n);" + NL, "", 1)
save(L + "parity_lcd_calc.c", k)
h = load(L + "parity_lcd_calc.h")
a = h.index("/*" + NL + " * The modeset ENABLE sequence for an eDP / DP SST output")
b = h.index("/* index of the first entry that is the named step")
h = h[:a] + h[b:]
save(L + "parity_lcd_calc.h", h)
print("done")
