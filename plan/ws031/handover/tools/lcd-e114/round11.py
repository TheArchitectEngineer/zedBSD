#!/usr/bin/env python3
"""WS031 E-115 round 11: pipe colour management (intel_color.c) for the state without LUTs / CTM.
usage: round11.py <repo root>"""
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

c = load(L + "lcd_compat.h")
c = rep(c, "	u8 sync_mode_slaves_mask;" + NL, "	u8 sync_mode_slaves_mask;" + NL +
        "	/* colour management: no LUT / CTM blobs in this path (all NULL), modes computed by the reference's check */" + NL +
        "	void *dsb;" + NL +
        "	const struct drm_property_blob *pre_csc_lut, *post_csc_lut;" + NL +
        "	u32 gamma_mode, csc_mode;" + NL +
        "	u8 c8_planes;" + NL +
        "	struct { int unused; } csc, output_csc;" + NL)
c = rep(c, "	struct { struct drm_display_mode adjusted_mode; } hw;" + NL + "	int port_clock;",
        "	struct { struct drm_display_mode adjusted_mode; const struct drm_property_blob *degamma_lut, *gamma_lut, *ctm; } hw;" + NL + "	int port_clock;")
c = rep(c, "struct drm_crtc_state { struct drm_crtc *crtc;", "struct drm_property_blob { size_t length; void *data; };" + NL + "struct drm_crtc_state { struct drm_crtc *crtc;")
c = rep(c, "		struct { bool ignore_long_hpd; } hotplug;", "		struct { bool ignore_long_hpd; } hotplug;" + NL +
        "		struct { const struct intel_color_funcs *color; } funcs;")
c = rep(c, "struct drm_i915_private {", "struct intel_crtc_state;" + NL +
        "struct intel_color_funcs {             /* intel_color.c: the members this path uses */" + NL +
        "	void (*color_commit_noarm)(const struct intel_crtc_state *crtc_state);" + NL +
        "	void (*color_commit_arm)(const struct intel_crtc_state *crtc_state);" + NL +
        "	void (*load_luts)(const struct intel_crtc_state *crtc_state);" + NL + "};" + NL + "struct drm_i915_private {")
save(L + "lcd_compat.h", c)

q = load(L + "lcd_seq_compat.h")
for n in ["intel_color_load_luts", "intel_color_commit_noarm", "intel_color_commit_arm"]:
    lines = [l for l in q.split(NL) if l.startswith("#define " + n + "(")]
    assert len(lines) == 1, n
    q = q.replace(lines[0] + NL, "")
q = rep(q, "/* intel_psr_disable(): returns when crtc_state->has_psr is false */", """/* colour: the LUT / CSC loaders are reached only with a LUT blob, a non-8-bit gamma mode or a CSC enable bit */
#define glk_load_degamma_lut(cs, blob) PARITY_LCD_GUARD((blob) == 0, "glk_load_degamma_lut (a degamma LUT)")
#define ilk_load_lut_8(cs, blob) PARITY_LCD_GUARD((blob) == 0, "ilk_load_lut_8 (a gamma LUT): returns at once without a blob")
#define icl_program_gamma_superfine_segment(cs) PARITY_LCD_GUARD(0, "icl_program_gamma_superfine_segment (12-bit multi-segment gamma)")
#define icl_program_gamma_multi_segment(cs) PARITY_LCD_GUARD(0, "icl_program_gamma_multi_segment (12-bit multi-segment gamma)")
#define ivb_load_lut_ext_max(cs) PARITY_LCD_GUARD(0, "ivb_load_lut_ext_max (10 / 12-bit gamma)")
#define glk_load_lut_ext2_max(cs) PARITY_LCD_GUARD(0, "glk_load_lut_ext2_max (10 / 12-bit gamma)")
#define bdw_load_lut_10(cs, blob, index) PARITY_LCD_GUARD(0, "bdw_load_lut_10 (10-bit gamma)")
#define PAL_PREC_INDEX_VALUE(x) (x)
#define ilk_update_pipe_csc(crtc, csc) PARITY_LCD_GUARD(0, "ilk_update_pipe_csc (a CTM)")
#define icl_update_output_csc(crtc, csc) PARITY_LCD_GUARD(0, "icl_update_output_csc (YCbCr output / limited range)")
#define intel_dsb_commit(dsb, wait) PARITY_LCD_GUARD(0, "intel_dsb_commit (a DSB)")
#define LEGACY_LUT_LENGTH 256
#define drm_color_lut_size(blob) ((int)((blob)->length / 8u))   /* sizeof(struct drm_color_lut): 4 x u16 */
/* intel_psr_disable(): returns when crtc_state->has_psr is false */""")
save(L + "lcd_seq_compat.h", q)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);",
        "void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);" + NL +
        "void intel_color_load_luts(const struct intel_crtc_state *crtc_state);" + NL +
        "void intel_color_commit_noarm(const struct intel_crtc_state *crtc_state);" + NL +
        "void intel_color_commit_arm(const struct intel_crtc_state *crtc_state);")
save(L + "lcd_modeset_compat.h", m)

open(root + L + "parity_color_glue.inc", "w").write("""/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_color_port.c: the colour hooks of display version 12+
 * (the reference's tgl_color_funcs: icl_color_commit_noarm / icl_color_commit_arm / icl_load_luts) and the
 * part of its .color_check (icl_color_check) that computes the two mode words.  No LUT and no CTM exist in
 * this path, so check_luts / intel_assign_luts / icl_assign_csc have nothing to work on.
 */
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

static const struct intel_color_funcs parity_tgl_color_funcs = {
	.color_commit_noarm = icl_color_commit_noarm,
	.color_commit_arm = icl_color_commit_arm,
	.load_luts = icl_load_luts,
};

void parity_lcd_ms_color_check(struct parity_lcd_modeset *ms)
{
	ms->i915.display.funcs.color = &parity_tgl_color_funcs;
	ms->crtc_state.gamma_mode = icl_gamma_mode(&ms->crtc_state);
	ms->crtc_state.csc_mode = icl_csc_mode(&ms->crtc_state);
}
""")
h = load(L + "parity_lcd_modeset_int.h")
h = rep(h, "int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms);",
        "void parity_lcd_ms_color_check(struct parity_lcd_modeset *ms);                         /* intel_color_port.c */" + NL +
        "int parity_lcd_ms_backlight_setup(struct parity_lcd_modeset *ms);")
save(L + "parity_lcd_modeset_int.h", h)
r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	parity_lcd_ms_bind_encoder(&ms);" + NL, "	parity_lcd_ms_bind_encoder(&ms);" + NL + "	parity_lcd_ms_color_check(&ms);" + NL)
save(L + "parity_lcd_modeset.c", r)

j = json.load(open(root + J))
j["new_files"].append({"out": "intel_color_port.c", "dir": "i915", "source": "display/intel_color.c",
    "path": "drivers/gpu/drm/i915/display/intel_color.c", "ranges": [],
    "functions": ["lut_is_legacy", "icl_gamma_mode", "icl_csc_mode", "icl_load_csc_matrix", "icl_load_luts",
                  "icl_color_commit_noarm", "icl_color_commit_arm", "intel_color_load_luts", "intel_color_commit_noarm",
                  "intel_color_commit_arm"],
    "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_mreg_color.h"], "glue": "parity_color_glue.inc"})
j["macro_headers"].append({"out": "lcd_mreg_color.h", "dir": "i915", "source": "display/intel_color_regs.h",
    "path": "drivers/gpu/drm/i915/display/intel_color_regs.h", "roots": [], "exclude": j["macro_headers"][0]["exclude"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated")
