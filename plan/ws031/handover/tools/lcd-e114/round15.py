#!/usr/bin/env python3
"""WS031 E-115 round 15.  usage: round15.py <repo root>"""
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
for h in j["macro_headers"]:
    h["exclude"] = sorted(set(h["exclude"] + ["for_each_intel_plane_on_crtc", "for_each_intel_plane", "for_each_intel_crtc"]))
    h["roots"] = [r for r in h["roots"] if r not in h["exclude"]]
for f in j["new_files"]:
    if f["out"] == "skl_watermark_port.c":
        f["ranges"].append(["struct skl_plane_ddb_iter {", END, "struct skl_plane_ddb_iter"])
        i = f["functions"].index("skl_build_plane_wm")
        f["functions"].insert(i, "skl_build_plane_wm_uv")
    if f["out"] == "drm_modes_hv_port.c":
        f["functions"] = ["drm_mode_copy"] + f["functions"]
j["new_files"].append({"out": "intel_crtc_port.c", "dir": "i915", "source": "display/intel_crtc.c", "path": "drivers/gpu/drm/i915/display/intel_crtc.c",
    "ranges": [], "functions": ["intel_usecs_to_scanlines"], "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

c = load(L + "lcd_compat.h")
c = rep(c, "struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed; };",
        "struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed, async_flip; };")
save(L + "lcd_compat.h", c)
p = load(L + "lcd_plane_compat.h")
p = rep(p, "struct intel_plane { struct drm_plane base; enum plane_id id; enum pipe pipe; };",
        "struct intel_plane { struct drm_plane base; enum plane_id id; enum pipe pipe; bool async_flip; };")
save(L + "lcd_plane_compat.h", p)
w = load(L + "lcd_wm_compat.h")
w = rep(w, "#define for_each_plane_id_on_crtc(crtc, p)", "#define for_each_intel_plane_on_crtc(dev, crtc, plane) for ((plane) = parity_lcd_wm->crtc_state->only_plane; (plane) != 0; (plane) = 0)" + NL +
        "#define for_each_plane_id_on_crtc(crtc, p)")
w = rep(w, "void drm_mode_get_hv_timing(", "int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode, int usecs);" + NL + "void drm_mode_get_hv_timing(")
save(L + "lcd_wm_compat.h", w)
print("done")
