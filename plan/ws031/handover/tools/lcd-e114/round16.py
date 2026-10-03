#!/usr/bin/env python3
"""WS031 E-115 round 16: DBUF slice / MBUS helpers, plane data rates, the watermark glue and its hookup.
usage: round16.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
END = NL + "};" + NL
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(root + J))
for f in j["new_files"]:
    if f["out"] == "skl_watermark_port.c":
        f["ranges"] += [["struct dbuf_slice_conf_entry {", END, "struct dbuf_slice_conf_entry"],
                        ["static const struct dbuf_slice_conf_entry adlp_allowed_dbufs[] =", END, "adlp_allowed_dbufs[]"]]
        f["functions"] = ["intel_dbuf_enabled_slices", "check_mbus_joined", "adlp_check_mbus_joined", "compute_dbuf_slices",
                          "adlp_compute_dbuf_slices", "skl_compute_dbuf_slices"] + f["functions"]
    if f["out"] == "intel_atomic_plane_port.c":
        f["functions"] = ["intel_adjusted_rate", "intel_plane_pixel_rate", "intel_plane_relative_data_rate", "intel_plane_data_rate"]
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

w = load(L + "lcd_wm_compat.h")
w = rep(w, "/* [fixed] linear framebuffers only */", """/* the DBUF slice tables of other platforms are not reached (display version 13, not DG2) */
#define dg2_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)
#define tgl_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)
#define icl_compute_dbuf_slices(pipe, active_pipes, join_mbus) (0)

/* [fixed] linear framebuffers only */""")
w = rep(w, "bool intel_wm_plane_visible(", "unsigned int intel_plane_relative_data_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state, int color_plane);" + NL +
        "unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state, int color_plane);" + NL +
        "bool intel_wm_plane_visible(")
save(L + "lcd_wm_compat.h", w)

open(root + L + "parity_wm_glue.inc", "w").write("""/*
 * WS031 Linux-parity — zedBSD glue at the end of skl_watermark_port.c: the reference's skl_compute_wm() /
 * skl_compute_ddb() reduced to the one crtc of the modeset.  The per-crtc work is the reference's own
 * (skl_build_pipe_wm, the DBUF slice / MBUS / weight computation, skl_crtc_allocate_ddb,
 * skl_crtc_allocate_plane_ddb); what is reduced is the iteration over crtcs and the global-state locking.
 * Not covered here: intel_compute_sagv_mask() (the SAGV / bandwidth side of the commit).
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

struct parity_lcd_wm_ctx *parity_lcd_wm;

int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms)
{
	struct intel_crtc_state *cs = &ms->crtc_state;
	struct intel_dbuf_state *new_dbuf = &ms->wm.new_dbuf;
	enum pipe pipe = ms->crtc.pipe;
	int ret;

	parity_lcd_wm = &ms->wm;
	ms->wm.crtc_state = cs;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = cs;
	/* what intel_plane_atomic_check() leaves for a visible plane: its data rates */
	cs->only_plane = &ms->plane;
	cs->only_plane_state = &ms->plane_state;
	cs->data_rate[ms->plane.id] = intel_plane_data_rate(cs, &ms->plane_state, 0);
	cs->rel_data_rate[ms->plane.id] = intel_plane_relative_data_rate(cs, &ms->plane_state, 0);

	ret = skl_build_pipe_wm(&ms->state, &ms->crtc);
	if (ret)
		return ret;

	/* skl_compute_ddb() */
	*new_dbuf = ms->wm.old_dbuf;
	new_dbuf->active_pipes = ms->wm.old_dbuf.active_pipes | (u8)BIT(pipe);
	new_dbuf->joined_mbus = adlp_check_mbus_joined(new_dbuf->active_pipes);        /* HAS_MBUS_JOINING: ADL-P */
	new_dbuf->slices[pipe] = skl_compute_dbuf_slices(&ms->crtc, new_dbuf->active_pipes, new_dbuf->joined_mbus);
	new_dbuf->enabled_slices = intel_dbuf_enabled_slices(new_dbuf);
	new_dbuf->weight[pipe] = intel_crtc_ddb_weight(cs);
	ret = skl_crtc_allocate_ddb(&ms->state, &ms->crtc);
	if (ret)
		return ret;
	return skl_crtc_allocate_plane_ddb(&ms->state, &ms->crtc);
}
""")
h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "	struct drm_framebuffer fb;" + NL, "	struct drm_framebuffer fb;" + NL +
        "	struct intel_plane cursor;              /* never shown; the reference reserves DDB space for it (skl_cursor_allocation) */" + NL +
        "	struct parity_lcd_wm_ctx wm;            /* old / new global DBUF state */" + NL +
        "	int wm_rc;" + NL)
h = rep(h, "void parity_lcd_ms_color_check(", "int parity_lcd_ms_wm_compute(struct parity_lcd_modeset *ms);                             /* skl_watermark_port.c */" + NL + "void parity_lcd_ms_color_check(")
h = rep(h, "struct parity_lcd_modeset {", '#include "lcd_wm_compat.h"' + NL + NL + "struct parity_lcd_modeset {")
save(L + "parity_lcd_modeset_int.h", h)

a = load(L + "parity_lcd_modeset.h")
a = rep(a, "	uint32_t dmc_fw_mask;", """	/* watermark / DDB inputs, as the normal initialisation read and keeps them */
	uint16_t wm_latency[8];         /* skl_setup_wm_latency(): usec per level */
	uint8_t wm_num_levels;
	int wm_ipc_enabled;
	uint8_t sagv_block_time_us;
	uint32_t dbuf_size;             /* DISPLAY_INFO()->dbuf.size / slice_mask of the platform */
	uint8_t dbuf_slice_mask;
	uint8_t dbuf_enabled_slices;    /* the slices enabled now (the old global DBUF state) */
	uint32_t dmc_fw_mask;""")
a = rep(a, "	/* ownership */", "	/* watermarks / DDB computed for the plane (software range [start, end) in DDB blocks) */" + NL +
        "	int wm_rc;" + NL + "	uint16_t ddb_start, ddb_end;" + NL + "	uint16_t wm0_blocks, wm0_lines; int wm0_enable;" + NL +
        "	uint8_t dbuf_slices_wanted; int mbus_joined;" + NL + "	/* ownership */")
save(L + "parity_lcd_modeset.h", a)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	ms.i915.display.dmc.fw_mask = cfg->dmc_fw_mask;" + NL, "	ms.i915.display.dmc.fw_mask = cfg->dmc_fw_mask;" + NL +
        "	memcpy(ms.i915.display.wm.skl_latency, cfg->wm_latency, sizeof(ms.i915.display.wm.skl_latency));" + NL +
        "	ms.i915.display.wm.num_levels = cfg->wm_num_levels;" + NL +
        "	ms.i915.display.wm.ipc_enabled = cfg->wm_ipc_enabled != 0;" + NL +
        "	ms.i915.display.sagv.block_time_us = cfg->sagv_block_time_us;" + NL +
        "	ms.i915.display.device_info.dbuf.size = cfg->dbuf_size;" + NL +
        "	ms.i915.display.device_info.dbuf.slice_mask = cfg->dbuf_slice_mask;" + NL +
        "	ms.i915.display.runtime.pipe_mask = 0x0f;" + NL +
        "	ms.wm.old_dbuf.enabled_slices = cfg->dbuf_enabled_slices;" + NL)
r = rep(r, "	if (rc != 0)" + NL + "		return rc;" + NL + "	ms.prepared = 1;",
        "	if (rc != 0)" + NL + "		return rc;" + NL +
        "	/* the check phase of the commit: watermarks and DDB for the new state (nothing is written) */" + NL +
        "	if (cfg->wm_num_levels == 0u || cfg->wm_num_levels > 8u || cfg->dbuf_size == 0u || cfg->dbuf_slice_mask == 0u)" + NL +
        "		return -EINVAL;" + NL +
        "	ms.crtc_state.hw.active = true;" + NL +
        "	ms.crtc_state.hw.pipe_mode = ms.crtc_state.hw.adjusted_mode;" + NL +
        "	ms.plane_state.uapi.visible = true;" + NL +
        "	ms.cursor.base.dev = &ms.i915.drm;" + NL +
        "	ms.cursor.id = PLANE_CURSOR;" + NL +
        "	ms.cursor.pipe = ms.crtc.pipe;" + NL +
        "	ms.crtc.base.cursor = &ms.cursor.base;" + NL +
        "	parity_lcd_cur_i915 = &ms.i915;" + NL +
        "	ms.wm_rc = parity_lcd_ms_wm_compute(&ms);" + NL +
        "	if (ms.wm_rc != 0)" + NL + "		return ms.wm_rc;" + NL + "	ms.prepared = 1;")
r = rep(r, "	out->errors = ms_errors;", "	out->wm_rc = ms.wm_rc;" + NL +
        "	out->ddb_start = ms.crtc_state.wm.skl.plane_ddb[PLANE_PRIMARY].start;" + NL +
        "	out->ddb_end = ms.crtc_state.wm.skl.plane_ddb[PLANE_PRIMARY].end;" + NL +
        "	out->wm0_enable = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].enable;" + NL +
        "	out->wm0_blocks = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].blocks;" + NL +
        "	out->wm0_lines = ms.crtc_state.wm.skl.optimal.planes[PLANE_PRIMARY].wm[0].lines;" + NL +
        "	out->dbuf_slices_wanted = ms.wm.new_dbuf.enabled_slices;" + NL +
        "	out->mbus_joined = ms.wm.new_dbuf.joined_mbus;" + NL + "	out->errors = ms_errors;")
save(L + "parity_lcd_modeset.c", r)
print("done")
