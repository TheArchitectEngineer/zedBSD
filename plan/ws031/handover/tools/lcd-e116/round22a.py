#!/usr/bin/env python3
"""WS031 E-116 round 22a: the commit's outer part in the modeset object: glue, state, the two commit wrappers.
usage: round22a.py <repo root>"""
import sys, json
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
G = "plan/ws031/handover/tools/port_lcd_calc.py"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---- generator: the plane's min-cdclk hook; register roots of the observer
g = load(G)
g = rep(g, '"icl_plane_update_sel_fetch_arm", "icl_plane_update_arm", "icl_plane_disable_arm")',
        '"icl_plane_update_sel_fetch_arm", "icl_plane_update_arm", "icl_plane_disable_arm", "icl_plane_min_cdclk")')
save(G, g)
j = json.load(open(root + J))
for m in j["macro_headers"]:
    if m["out"] == "lcd_mreg_i915_reg.h":
        for r in ("ICL_PIPESTATUS", "PIPE_STATUS_UNDERRUN", "PIPE_STATUS_SOFT_UNDERRUN_XELPD", "PIPE_STATUS_HARD_UNDERRUN_XELPD",
                  "PIPE_STATUS_PORT_UNDERRUN_XELPD", "PIPE_FRMCOUNT_G4X", "GEN8_DE_PIPE_ISR", "GEN8_DE_PIPE_IMR", "GEN8_DE_PIPE_IIR",
                  "GEN8_DE_PIPE_IER", "GEN8_PIPE_VBLANK", "GEN8_PIPE_FIFO_UNDERRUN", "XELPD_PIPE_SOFT_UNDERRUN", "XELPD_PIPE_HARD_UNDERRUN"):
            if r not in m["roots"]:
                m["roots"].append(r)
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

# ---- glue: CDCLK requirement
open(root + L + "parity_cdclk_glue.inc", "w").write("""/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_cdclk_port.c: the reference's bxt_modeset_calc_cdclk()
 * (intel_compute_min_cdclk + bxt_compute_min_voltage_level + bxt_calc_cdclk / _pll_vco + the voltage level)
 * reduced to the one crtc of the modeset, followed by intel_cdclk_changed()'s comparison with the state the
 * normal initialisation left.  Nothing is programmed here: when the required state equals the current one the
 * reference's intel_set_cdclk_pre/post_plane_update() return without touching the hardware, and that is the
 * only case the modeset accepts.
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_ms_cdclk_check(struct parity_lcd_modeset *ms)
{
	struct drm_i915_private *i915 = &ms->i915;
	const struct intel_crtc_state *cs = &ms->crtc_state;
	int min_cdclk, cdclk, vco, level;

	/* intel_init_cdclk_hooks(): ADL-P past display stepping B0, not RPL-U */
	i915->display.cdclk.table = adlp_cdclk_table;

	/* intel_compute_min_cdclk(): the crtc's need, the bandwidth need (no forced minimum) */
	min_cdclk = intel_crtc_compute_min_cdclk(cs);
	if (min_cdclk < 0)
		return min_cdclk;
	ms->cdclk.crtc_min = min_cdclk;
	ms->cdclk.bw_min = parity_lcd_ms_bw_min_cdclk(ms);
	min_cdclk = max(ms->cdclk.bw_min, min_cdclk);
	if (min_cdclk > (int)i915->display.cdclk.max_cdclk_freq)
		return -EINVAL;

	/* bxt_modeset_calc_cdclk() */
	cdclk = bxt_calc_cdclk(i915, min_cdclk);
	vco = bxt_calc_cdclk_pll_vco(i915, cdclk);
	level = max_t(int, cs->min_voltage_level, tgl_calc_voltage_level(cdclk));
	if (cdclk == 0 || vco == 0)
		return -EINVAL;
	ms->cdclk.min_cdclk = min_cdclk;
	ms->cdclk.cdclk = cdclk;
	ms->cdclk.vco = vco;
	ms->cdclk.voltage_level = level;
	/* intel_cdclk_changed(): intel_cdclk_needs_modeset() (cdclk / vco / ref differ) or the voltage level differs */
	ms->cdclk.change_needed = cdclk != (int)i915->display.cdclk.hw.cdclk || vco != (int)i915->display.cdclk.hw.vco ||
		level != (int)i915->display.cdclk.hw.voltage_level;
	return 0;
}
""")
open(root + L + "parity_bw_glue.inc", "w").write("""/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_bw_port.c: the one crtc's share of the reference's
 * intel_bw_calc_min_cdclk() and of intel_bw_data_rate() (kB/s -> MB/s as intel_bw_check_qgv_points() rounds it).
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_ms_bw_min_cdclk(struct parity_lcd_modeset *ms)
{
	return intel_bw_crtc_min_cdclk(&ms->crtc_state);
}

unsigned int parity_lcd_ms_bw_data_rate(struct parity_lcd_modeset *ms)
{
	return DIV_ROUND_UP(intel_bw_crtc_data_rate(&ms->crtc_state), 1000);
}
""")

# ---- glue: watermark file -- global DBUF state of both commits
w = load(L + "parity_wm_glue.inc")
w = rep(w, "	ret = skl_crtc_allocate_ddb(&ms->state, &ms->crtc);" + NL + "	if (ret)" + NL + "		return ret;" + NL +
        "	return skl_crtc_allocate_plane_ddb(&ms->state, &ms->crtc);" + NL + "}" + NL,
        "	/* a change of the slices / the MBUS joining serializes the global state (intel_atomic_serialize_global_state) */" + NL +
        "	new_dbuf->base.changed = new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices ||" + NL +
        "		new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus || new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes;" + NL +
        "	ret = skl_crtc_allocate_ddb(&ms->state, &ms->crtc);" + NL + "	if (ret)" + NL + "		return ret;" + NL +
        "	return skl_crtc_allocate_plane_ddb(&ms->state, &ms->crtc);" + NL + "}" + NL + NL +
        "/* the same part of skl_compute_ddb() for the commit that turns the crtc off: no active pipe is left */" + NL +
        "void parity_lcd_ms_wm_compute_off(struct parity_lcd_modeset *ms)" + NL + "{" + NL +
        "	struct intel_dbuf_state *new_dbuf = &ms->wm.new_dbuf;" + NL +
        "	enum pipe pipe = ms->crtc.pipe;" + NL + NL +
        "	parity_lcd_wm = &ms->wm;" + NL +
        "	*new_dbuf = ms->wm.old_dbuf;" + NL +
        "	new_dbuf->active_pipes = ms->wm.old_dbuf.active_pipes & (u8)~BIT(pipe);" + NL +
        "	new_dbuf->joined_mbus = adlp_check_mbus_joined(new_dbuf->active_pipes);" + NL +
        "	new_dbuf->slices[pipe] = skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);" + NL +
        "	new_dbuf->enabled_slices = intel_dbuf_enabled_slices(new_dbuf);" + NL +
        "	new_dbuf->weight[pipe] = 0;" + NL +
        "	memset(&new_dbuf->ddb[pipe], 0, sizeof(new_dbuf->ddb[pipe]));" + NL +
        "	new_dbuf->base.changed = new_dbuf->enabled_slices != ms->wm.old_dbuf.enabled_slices ||" + NL +
        "		new_dbuf->joined_mbus != ms->wm.old_dbuf.joined_mbus || new_dbuf->active_pipes != ms->wm.old_dbuf.active_pipes;" + NL +
        "}" + NL)
save(L + "parity_wm_glue.inc", w)

# ---- glue: the plane's min cdclk; the encoder's power domain; the one encoder
p = load(L + "parity_plane_emit_glue.inc")
p = p.rstrip(NL) + NL + NL + """/* intel_plane_calc_min_cdclk(): plane->min_cdclk is icl_plane_min_cdclk on display version 11+ (skl_universal_plane_create) */
void parity_lcd_ms_plane_min_cdclk(struct parity_lcd_modeset *ms)
{
	ms->crtc_state.min_cdclk[ms->plane.id] = icl_plane_min_cdclk(&ms->crtc_state, &ms->plane_state);
}
"""
save(L + "parity_plane_emit_glue.inc", p)
d = load(L + "parity_ddi_emit_glue.inc")
d = rep(d, "	ddi_ms = ms;" + NL + "	encoder->enable = intel_enable_ddi;",
        "	ddi_ms = ms;" + NL +
        "	/* intel_ddi_init(): encoder->power_domain = intel_display_power_ddi_lanes_domain() = POWER_DOMAIN_PORT_DDI_LANES_A + port */" + NL +
        "	encoder->power_domain = POWER_DOMAIN_PORT_DDI_LANES_A + (int)encoder->port;" + NL +
        "	encoder->base.index = 0;" + NL +
        "	parity_lcd_only_encoder = &encoder->base;" + NL +
        "	encoder->enable = intel_enable_ddi;")
save(L + "parity_ddi_emit_glue.inc", d)
e = load(L + "parity_display_emit_glue.inc")
e = rep(e, "struct drm_i915_private *parity_lcd_cur_i915;", "struct drm_i915_private *parity_lcd_cur_i915;" + NL + "struct drm_encoder *parity_lcd_only_encoder;")
save(L + "parity_display_emit_glue.inc", e)

# ---- the underrun decision also marks the reference's position for the observer
q = load(L + "lcd_seq_compat.h")
a = q.index("#define intel_set_cpu_fifo_underrun_reporting(i915, pipe, enable)")
b = q.index(NL, a) + 1
q = q[:a] + ("#define PARITY_LCD_OBSERVE(i915, point) do { if ((i915)->emit->observe != 0) (i915)->emit->observe((i915)->emit->ctx, (point)); } while (0)" + NL +
    "#define intel_set_cpu_fifo_underrun_reporting(i915, pipe, enable) do { " + BS + NL +
    '	PARITY_LCD_DECIDED(i915, "intel_set_cpu_fifo_underrun_reporting: underrun IRQ stays masked; the test owns and samples ICL_PIPESTATUS"); ' + BS + NL +
    "	PARITY_LCD_OBSERVE(i915, (enable) ? PARITY_LCD_OBS_UNDERRUN_ARM : PARITY_LCD_OBS_UNDERRUN_DISARM); } while (0)" + NL) + q[b:]
save(L + "lcd_seq_compat.h", q)

# ---- the modeset object
h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "	int wm_rc;" + NL, "	int wm_rc;" + NL +
        "	/* the CDCLK state this crtc requires (bxt_modeset_calc_cdclk) against the current one */" + NL +
        "	struct { int crtc_min, bw_min, min_cdclk, cdclk, vco, voltage_level, change_needed; } cdclk;" + NL +
        "	int cdclk_rc;" + NL +
        "	unsigned int bw_data_rate;              /* MB/s, as intel_bw_check_qgv_points() compares it */" + NL +
        "	/* the commit */" + NL +
        "	int dc_off_held; int dc_off_wakeref;" + NL +
        "	int stop_unconfirmed;                   /* the disable reported an error: nothing further was given back */" + NL +
        "	unsigned commits;" + NL)
h = rep(h, "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);",
        "void parity_lcd_ms_wm_compute_off(struct parity_lcd_modeset *ms);                         /* skl_watermark_port.c */" + NL +
        "int parity_lcd_ms_cdclk_check(struct parity_lcd_modeset *ms);                             /* intel_cdclk_port.c */" + NL +
        "int parity_lcd_ms_bw_min_cdclk(struct parity_lcd_modeset *ms);                            /* intel_bw_port.c */" + NL +
        "unsigned int parity_lcd_ms_bw_data_rate(struct parity_lcd_modeset *ms);" + NL +
        "void parity_lcd_ms_plane_min_cdclk(struct parity_lcd_modeset *ms);                        /* skl_plane_port.c */" + NL +
        "/* reference functions of the commit's outer part (generated files) */" + NL +
        "void intel_ddi_compute_min_voltage_level(struct intel_crtc_state *crtc_state);" + NL +
        "void intel_modeset_get_crtc_power_domains(struct intel_crtc_state *crtc_state, struct intel_power_domain_mask *old_domains);" + NL +
        "void intel_modeset_put_crtc_power_domains(struct intel_crtc *crtc, struct intel_power_domain_mask *domains);" + NL +
        "void intel_dbuf_pre_plane_update(struct intel_atomic_state *state);" + NL +
        "void intel_dbuf_post_plane_update(struct intel_atomic_state *state);" + NL +
        "void intel_mbus_dbox_update(struct intel_atomic_state *state);" + NL +
        "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);")
save(L + "parity_lcd_modeset_int.h", h)

a = load(L + "parity_lcd_modeset.h")
a = rep(a, "	uint32_t dmc_fw_mask;", """	int mbus_joined;                /* MBUS_CTL as found (the old global DBUF state) */
	/* the CDCLK state the normal initialisation left (display.cdclk.hw) and the platform limit */
	uint32_t cdclk_khz, cdclk_vco_khz, cdclk_ref_khz, cdclk_bypass_khz, cdclk_max_khz;
	uint8_t cdclk_voltage_level;
	/* memory bandwidth (MB/s) of the QGV point the initialisation left allowed (SAGV is kept off); 0 = unknown = refuse */
	uint32_t qgv_allowed_bw;
	uint32_t dmc_fw_mask;""")
a = rep(a, "	/* ownership */", """	/* the commit's outer part */
	int cdclk_rc, cdclk_crtc_min, cdclk_bw_min, cdclk_required_khz, cdclk_required_vco, cdclk_required_level, cdclk_change_needed;
	unsigned bw_data_rate;          /* MB/s this crtc needs */
	int dc_off_held;                /* POWER_DOMAIN_DC_OFF is held (inside a commit, or kept after an unconfirmed stop) */
	unsigned crtc_domains_held;     /* how many domains of get_crtc_power_domains() the crtc holds */
	uint8_t dbuf_slices_now; int mbus_joined_now;   /* the current global DBUF state */
	int stop_unconfirmed;
	unsigned commits;
	/* ownership */""")
a = rep(a, "int parity_lcd_modeset_enable(void);", """/*
 * The two commits of the test, each the reference's intel_atomic_commit_tail() reduced to this crtc:
 *   enable : DC_OFF get -> crtc power domains -> [CDCLK: no change] -> DBUF pre-plane (MBUS, slices) -> MBUS DBOX ->
 *            crtc enable -> plane update -> DBUF post-plane -> put unused domains -> DC_OFF put (async, 17 ms)
 *   disable: DC_OFF get -> domains to drop -> plane disable -> crtc disable -> DBUF pre / DBOX / post -> put the
 *            domains -> DC_OFF put
 * Adaptations (logged): after a first anomaly in the enable the plane is NOT armed; after an error in the disable
 * nothing further is given back (DBUF, power domains and DC_OFF stay as they are -- see stop_unconfirmed).
 */
int parity_lcd_modeset_commit_enable(void);
int parity_lcd_modeset_commit_disable(void);
/* the stages the commits are made of (kept for the word-level tests; the path to a picture is the two commits) */
int parity_lcd_modeset_enable(void);""")
save(L + "parity_lcd_modeset.h", a)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	    ops->panel == 0 || ops->power_get == 0 || ops->power_put == 0 || ops->lock == 0 || ops->step == 0)",
        "	    ops->panel == 0 || ops->power_get == 0 || ops->power_put == 0 || ops->power_put_async == 0 || ops->dbuf_slices_update == 0 ||" + NL +
        "	    ops->lock == 0 || ops->step == 0)")
r = rep(r, "	if (ms.prepared && (ms.crtc.active || ms.plane_armed))", "	if (ms.prepared && (ms.crtc.active || ms.plane_armed || ms.dc_off_held || ms.stop_unconfirmed))")
r = rep(r, "	ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;" + NL,
        "	ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;" + NL +
        "	ms.wm.old_dbuf.joined_mbus = cfg->mbus_joined != 0;" + NL +
        "	ms.i915.display.device_info.has_ddi = true;" + NL +
        "	ms.i915.display.cdclk.hw.cdclk = cfg->cdclk_khz;" + NL +
        "	ms.i915.display.cdclk.hw.vco = cfg->cdclk_vco_khz;" + NL +
        "	ms.i915.display.cdclk.hw.ref = cfg->cdclk_ref_khz;" + NL +
        "	ms.i915.display.cdclk.hw.bypass = cfg->cdclk_bypass_khz;" + NL +
        "	ms.i915.display.cdclk.hw.voltage_level = cfg->cdclk_voltage_level;" + NL +
        "	ms.i915.display.cdclk.max_cdclk_freq = cfg->cdclk_max_khz;" + NL)
r = rep(r, "	ms.crtc_state.active_planes = (u8)BIT(PLANE_PRIMARY);" + NL,
        "	ms.crtc_state.active_planes = (u8)BIT(PLANE_PRIMARY);" + NL +
        "	ms.crtc_state.uapi.encoder_mask = 1u << 0;        /* drm_encoder_mask() of the one encoder */" + NL)
r = rep(r, "	ms.crtc_state.hw.active = true;" + NL, "	ms.crtc_state.hw.active = true;" + NL + "	ms.crtc_state.hw.enable = true;" + NL)
r = rep(r, "	if (ms.wm_rc != 0)" + NL + "		return ms.wm_rc;" + NL + "	ms.prepared = 1;",
        """	if (ms.wm_rc != 0)
		return ms.wm_rc;
	/* the check phase, continued: the CDCLK this state requires, and the memory bandwidth */
	intel_ddi_compute_min_voltage_level(&ms.crtc_state);
	parity_lcd_ms_plane_min_cdclk(&ms);
	ms.cdclk_rc = parity_lcd_ms_cdclk_check(&ms);
	if (ms.cdclk_rc != 0)
		return ms.cdclk_rc;
	if (ms.cdclk.change_needed) {
		/* the reference would reprogram CDCLK around the planes (intel_set_cdclk_pre/post_plane_update): not connected */
		on_error(0, "the mode requires a CDCLK state other than the current one: CDCLK programming is not connected" "\\n");
		return -EINVAL;
	}
	ms.bw_data_rate = parity_lcd_ms_bw_data_rate(&ms);
	if (cfg->qgv_allowed_bw == 0u || ms.bw_data_rate > cfg->qgv_allowed_bw) {
		on_error(0, "the memory bandwidth of the allowed QGV point is unknown or below what the plane needs" "\\n");
		return -EINVAL;
	}
	ms.prepared = 1;""")
r = rep(r, "	out->errors = ms_errors;", """	out->cdclk_rc = ms.cdclk_rc;
	out->cdclk_crtc_min = ms.cdclk.crtc_min;
	out->cdclk_bw_min = ms.cdclk.bw_min;
	out->cdclk_required_khz = ms.cdclk.cdclk;
	out->cdclk_required_vco = ms.cdclk.vco;
	out->cdclk_required_level = ms.cdclk.voltage_level;
	out->cdclk_change_needed = ms.cdclk.change_needed;
	out->bw_data_rate = ms.bw_data_rate;
	out->dc_off_held = ms.dc_off_held;
	{
		enum intel_display_power_domain domain;

		for_each_power_domain(domain, &ms.crtc.enabled_power_domains.mask)
			out->crtc_domains_held++;
	}
	out->dbuf_slices_now = ms.wm.old_dbuf.enabled_slices;
	out->mbus_joined_now = ms.wm.old_dbuf.joined_mbus;
	out->stop_unconfirmed = ms.stop_unconfirmed;
	out->commits = ms.commits;
	out->errors = ms_errors;""")
r = r.rstrip(NL) + NL + """
/* ---- the two commits: intel_atomic_commit_tail() reduced to this crtc (the order and the callees are the reference's) ---- */
static void observe(int point)
{
	if (ms_ops != 0 && ms_ops->observe != 0)
		ms_ops->observe(ms_ops->ctx, point);
}

int parity_lcd_modeset_commit_enable(void)
{
	struct intel_power_domain_mask put_domains;
	int rc, prc = PARITY_LCD_MS_OK;

	if (!ms.prepared || ms.crtc.active || ms.dc_off_held)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	parity_lcd_wm = &ms.wm;
	ms.state.base.dev = &ms.i915.drm;
	ms.commits++;

	/* "During full modesets we write a lot of registers, wait for PLLs, etc. Doing that while DC states are enabled is not a good idea." */
	ms.dc_off_wakeref = intel_display_power_get(&ms.i915, POWER_DOMAIN_DC_OFF);
	ms.dc_off_held = 1;
	observe(PARITY_LCD_OBS_COMMIT_BEGIN);
	/* intel_atomic_prepare_plane_clear_colors(): only for modifiers with a clear-colour plane (this one is linear) */
	intel_modeset_get_crtc_power_domains(&ms.crtc_state, &put_domains);
	/* intel_commit_modeset_disables(): the old crtc state is inactive -- nothing to disable */
	/* intel_pmdemand_pre_plane_update(): returns for display version < 14 */
	/* intel_set_cdclk_pre_plane_update(): the required CDCLK state equals the current one (checked in prepare): returns */
	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it (SAGV off, max-bandwidth point; bandwidth checked in prepare)");
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);

	/* skl_commit_modeset_enables(): intel_enable_crtc(), then intel_update_crtc() -> the planes */
	rc = parity_lcd_modeset_enable();
	observe(PARITY_LCD_OBS_PIPE_ENABLED);
	if (rc == PARITY_LCD_MS_OK) {
		prc = parity_lcd_modeset_plane_update();
		observe(PARITY_LCD_OBS_PLANE_ARMED);
	} else {
		PARITY_LCD_DECIDED(&ms.i915, "plane update skipped: the crtc enable reported an anomaly, the buffer is not handed to the display");
	}

	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_post_plane_update: QGV points are not relaxed (SAGV stays off)");
	/* intel_set_cdclk_post_plane_update() / intel_pmdemand_post_plane_update(): as above */
	ms.wm.old_dbuf = ms.wm.new_dbuf;        /* the new global state is the current one from here on */
	observe(PARITY_LCD_OBS_COMMIT_END);
	/* "Delay re-enabling DC states by 17 ms to avoid the off->on->off toggling overhead at and above 60 FPS." */
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return rc != PARITY_LCD_MS_OK ? rc : prc;
}

int parity_lcd_modeset_commit_disable(void)
{
	static struct intel_crtc_state off_state;       /* the NEW state of this commit: the crtc inactive */
	struct intel_power_domain_mask put_domains;
	int rc, prc;

	if (!ms.prepared || !ms.crtc.active || ms.dc_off_held)
		return PARITY_LCD_MS_NOT_PREPARED;
	parity_lcd_cur_i915 = &ms.i915;
	ms.state.base.dev = &ms.i915.drm;
	ms.commits++;
	/* check phase: the global DBUF state without this pipe */
	parity_lcd_ms_wm_compute_off(&ms);
	off_state = ms.crtc_state;
	off_state.hw.active = false;
	off_state.hw.enable = false;

	ms.dc_off_wakeref = intel_display_power_get(&ms.i915, POWER_DOMAIN_DC_OFF);
	ms.dc_off_held = 1;
	observe(PARITY_LCD_OBS_COMMIT_BEGIN);
	intel_modeset_get_crtc_power_domains(&off_state, &put_domains);         /* nothing new; everything held goes to put_domains */

	/* intel_commit_modeset_disables() -> intel_old_crtc_state_disables(): the planes, then the crtc */
	prc = parity_lcd_modeset_plane_disable();
	observe(PARITY_LCD_OBS_PLANE_DISABLED);
	rc = parity_lcd_modeset_disable();
	observe(PARITY_LCD_OBS_PIPE_DISABLED);
	if (rc != PARITY_LCD_MS_OK || prc != PARITY_LCD_MS_OK) {
		/* the stop is not confirmed: shrinking the DBUF, dropping the pipe's power domains or letting DC states back
		 * in under a pipe that may still run is the one thing not to do.  Everything stays; the caller keeps the buffer. */
		ms.stop_unconfirmed = 1;
		PARITY_LCD_DECIDED(&ms.i915, "commit tail after a failed disable NOT run: DBUF slices, crtc power domains and DC_OFF stay held");
		return rc != PARITY_LCD_MS_OK ? rc : prc;
	}

	PARITY_LCD_DECIDED(&ms.i915, "intel_sagv_pre_plane_update: the QGV restriction stays as the initialisation left it");
	intel_dbuf_pre_plane_update(&ms.state);
	intel_mbus_dbox_update(&ms.state);
	/* skl_commit_modeset_enables(): nothing is enabled by this commit */
	intel_dbuf_post_plane_update(&ms.state);
	intel_modeset_put_crtc_power_domains(&ms.crtc, &put_domains);
	ms.wm.old_dbuf = ms.wm.new_dbuf;
	observe(PARITY_LCD_OBS_COMMIT_END);
	intel_display_power_put_async_delay(&ms.i915, POWER_DOMAIN_DC_OFF, ms.dc_off_wakeref, 17);
	ms.dc_off_held = 0;
	return PARITY_LCD_MS_OK;
}
"""
save(L + "parity_lcd_modeset.c", r)
print("done")
