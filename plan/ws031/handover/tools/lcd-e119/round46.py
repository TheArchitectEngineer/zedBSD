#!/usr/bin/env python3
"""WS031 E-119 round 46: compat for the extracted update / vblank helpers.  usage: round46.py <repo root>"""
import sys, json
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

j = json.load(open(root + J))
if "lcd_mreg_display_types.h" not in [m["out"] for m in j["macro_headers"]]:
    j["macro_headers"].append({"dir": "i915", "out": "lcd_mreg_display_types.h", "source": "display/intel_display_types.h",
        "path": "drivers/gpu/drm/i915/display/intel_display_types.h", "exclude": [],
        "roots": ["I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP", "I915_MODE_FLAG_VRR"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

c = load(L + "lcd_compat.h")
c = rep(c, "struct drm_device { int unused; int switch_power_state; };",
        "struct drm_vblank_crtc;" + NL +
        "struct drm_device { int unused; int switch_power_state; struct drm_vblank_crtc *vblank; int vblank_time_lock; int event_lock; };")
c = rep(c, "struct drm_crtc { struct drm_device *dev; struct { int id; } base; const char *name; struct drm_plane *cursor; };",
        "struct drm_crtc_funcs;" + NL +
        "struct drm_crtc { struct drm_device *dev; struct { int id; } base; const char *name; struct drm_plane *cursor; const struct drm_crtc_funcs *funcs; };")
c = rep(c, "struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; struct intel_display_power_domain_set enabled_power_domains; };",
        "struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; struct intel_display_power_domain_set enabled_power_domains;" + NL +
        "	u8 mode_flags; int scanline_offset; int vmax_vblank_start;" + NL +
        "	struct { u32 min_vbl, max_vbl; int scanline_start; long long start_vbl_time; u32 start_vbl_count; } debug; };")
c = rep(c, "	u8 min_voltage_level;", "	u8 min_voltage_level;" + NL +
        "	bool update_m_n, update_lrr, preload_luts, do_async_flip; void *dsb; u8 mode_flags;")
save(L + "lcd_compat.h", c)
f = load(L + "lcd_flip_compat.h")
f = rep(f, "#define FLIP_OPS(i915) ((i915)->emit)", """#include "lcd_mreg_display_types.h"      /* reference, extracted: I915_MODE_FLAG_* */
/* display/intel_crtc.h: CONFIG_PROVE_LOCKING is off */
#define VBLANK_EVASION_TIME_US 100
/* the reference's non-debug build: dbg_vblank_evade() is empty (CONFIG_DRM_I915_DEBUG_VBLANK_EVADE off) */
#define dbg_vblank_evade(crtc, end) ((void)(crtc), (void)(end))
/* drm_vblank.h: what these helpers read */
struct drm_vblank_crtc { struct drm_display_mode hwmode; u32 max_vblank_count; };
struct drm_crtc_funcs { u32 (*get_vblank_counter)(struct drm_crtc *crtc); };
#define spin_lock_irqsave(l, f) ((void)(l), (f) = 0)
#define spin_unlock_irqrestore(l, f) ((void)(l), (void)(f))
int intel_get_crtc_scanline(struct intel_crtc *crtc);
u32 g4x_get_vblank_counter(struct drm_crtc *crtc);
void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state, bool vrr_enable);
#define FLIP_OPS(i915) ((i915)->emit)""")
save(L + "lcd_flip_compat.h", f)
print("done")
