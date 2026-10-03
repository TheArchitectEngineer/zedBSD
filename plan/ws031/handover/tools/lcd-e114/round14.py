#!/usr/bin/env python3
"""WS031 E-115 round 14.  usage: round14.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
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

g = load(G)
g = rep(g, '    body = NL.join(between(stext, part[0], part[1], spec["source"], part[2]) for part in spec["ranges"])',
        '    # a 4th element = text to cut from the END of the range (an end marker that is not part of the wanted text)\n'
        '    body = NL.join((lambda t, p: t[:len(t) - len(p[3])] if len(p) > 3 else t)(between(stext, part[0], part[1], spec["source"], part[2]), part)\n'
        '                   for part in spec["ranges"])')
save(G, g)

j = json.load(open(root + J))
for h in j["range_headers"]:
    if h["out"] == "lcd_i915_fixed.h":
        h["ranges"] = [["typedef struct {", "#endif /* _I915_FIXED_H_ */", "the whole fixed-point header body", "#endif /* _I915_FIXED_H_ */"]]
for f in j["new_files"]:
    if f["out"] == "skl_watermark_port.c":
        i = f["functions"].index("skl_build_pipe_wm")
        f["functions"][i:i] = ["skl_build_plane_wm", "skl_max_wm0_lines", "skl_max_wm_level_for_vblank", "skl_is_vblank_too_short"]
    if f["out"] == "drm_modes_hv_port.c":
        f["functions"] = ["drm_mode_init", "drm_mode_get_hv_timing"]
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

c = load(L + "lcd_compat.h")
c = rep(c, "enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D };", "enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D, I915_MAX_PIPES };")
c = rep(c, "#define CRTC_NO_VSCAN (1 << 3)" + NL, "#define CRTC_NO_VSCAN (1 << 3)" + NL +
        "#define CRTC_STEREO_DOUBLE_ONLY (CRTC_STEREO_DOUBLE | CRTC_NO_DBLSCAN | CRTC_NO_VSCAN)" + NL)
c = rep(c, "struct drm_crtc { struct drm_device *dev; struct { int id; } base; const char *name; };",
        "struct drm_plane;" + NL + "struct drm_crtc { struct drm_device *dev; struct { int id; } base; const char *name; struct drm_plane *cursor; };")
c = rep(c, "		struct { u32 rawclk_freq; } runtime;", "		struct { u32 rawclk_freq; u8 pipe_mask; } runtime;")
c = rep(c, "	u8 nv12_planes, enabled_planes;" + NL, "	u8 nv12_planes, enabled_planes;" + NL + "	bool wm_level_disabled;" + NL)
save(L + "lcd_compat.h", c)

w = load(L + "lcd_wm_compat.h")
w = rep(w, "struct intel_global_state { int unused; };", "struct intel_atomic_state;" + NL + "struct intel_global_state { struct intel_atomic_state *state; };" + NL +
        "#define to_intel_atomic_state(s) (s)")
w = rep(w, "#define U16_MAX ((u16)0xffff)", "#ifndef UINT_MAX" + NL + "#define UINT_MAX 0xffffffffu" + NL + "#endif" + NL + "#define U16_MAX ((u16)0xffff)")
w = rep(w, "/* [fixed] linear framebuffers only */", """/* drm_format_info(): only the cursor's ARGB8888 is looked up (skl_cursor_allocation) */
static inline const struct drm_format_info *drm_format_info(u32 format)
{
	static const struct drm_format_info argb8888 = { .format = DRM_FORMAT_ARGB8888, .num_planes = 1, .cpp = { 4, 0, 0, 0 }, .has_alpha = true };
	return format == DRM_FORMAT_ARGB8888 ? &argb8888 : 0;
}

/* [fixed] linear framebuffers only */""")
save(L + "lcd_wm_compat.h", w)

p = load(L + "lcd_plane_compat.h")
p = rep(p, "struct intel_plane_state {" + NL + "	struct { struct drm_plane *plane; struct drm_rect src, dst; } uapi;",
        "struct intel_plane_state {" + NL + "	struct { struct drm_plane *plane; struct drm_rect src, dst; bool visible; } uapi;")
p = rep(p, "#endif /* PARITY_LCD_PLANE_COMPAT_H */", "struct intel_crtc_state;" + NL +
        "void skl_write_plane_wm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);   /* skl_watermark_port.c */" + NL + NL +
        "#endif /* PARITY_LCD_PLANE_COMPAT_H */")
save(L + "lcd_plane_compat.h", p)
m = load(L + "lcd_modeset_compat.h")
if "#define U16_MAX" not in m:
    m = rep(m, "#define KHz(x) (1000 * (x))", "#define KHz(x) (1000 * (x))" + NL + "#ifndef U16_MAX" + NL + "#define U16_MAX ((u16)0xffff)" + NL + "#define U32_MAX ((u32)0xffffffffu)" + NL + "#endif")
    save(L + "lcd_modeset_compat.h", m)
w = load(L + "lcd_wm_compat.h")
w = w.replace("#define U16_MAX ((u16)0xffff)" + NL + "#define U32_MAX ((u32)0xffffffffu)" + NL, "")
save(L + "lcd_wm_compat.h", w)
print("done")
