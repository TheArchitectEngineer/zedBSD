#!/usr/bin/env python3
"""WS031 E-115 round 13: watermark compat, more functions.  usage: round13.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
END = NL + "};" + NL
FN = NL + "}" + NL
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(root + J))
for f in j["new_files"]:
    if f["out"] == "skl_watermark_port.c":
        f["functions"] = ["intel_dbuf_slice_size", "skl_ddb_entry_init", "mbus_ddb_offset", "skl_watermark_ipc_enabled",
                          "skl_needs_memory_bw_wa", "intel_get_linetime_us", "skl_cursor_allocation",
                          "skl_total_relative_data_rate", "intel_crtc_dbuf_weights", "skl_wm_check_vblank"] + f["functions"]
for h in j["range_headers"]:
    if h["out"] == "lcd_wm_ddb_types.h":
        h["ranges"] += [["static inline u16 skl_ddb_entry_size(", FN, "skl_ddb_entry_size"],
                        ["static inline bool skl_ddb_entry_equal(", FN, "skl_ddb_entry_equal"]]
j["range_headers"].append({"out": "lcd_i915_fixed.h", "dir": "i915", "source": "i915_fixed.h", "path": "drivers/gpu/drm/i915/i915_fixed.h",
    "ranges": [["typedef struct {", FN + NL + "#endif /* _I915_FIXED_H_ */", "the whole fixed-point header body"]]})
j["new_files"] += [
  {"out": "intel_wm_port.c", "dir": "i915", "source": "display/intel_wm.c", "path": "drivers/gpu/drm/i915/display/intel_wm.c", "ranges": [],
   "functions": ["intel_wm_plane_visible"], "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_plane_compat.h", "lcd_wm_compat.h"]},
  {"out": "intel_atomic_plane_port.c", "dir": "i915", "source": "display/intel_atomic_plane.c", "path": "drivers/gpu/drm/i915/display/intel_atomic_plane.c",
   "ranges": [], "functions": ["intel_adjusted_rate", "intel_plane_pixel_rate"],
   "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_plane_compat.h", "lcd_wm_compat.h"]},
  {"out": "drm_modes_hv_port.c", "dir": "drm", "source": "drm_modes.c", "path": "drivers/gpu/drm/drm_modes.c", "ranges": [],
   "functions": ["drm_mode_get_hv_timing"], "includes": ["lcd_compat.h"]},
]
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

c = load(L + "lcd_compat.h")
c = rep(c, "struct intel_crtc_state {" + NL + "	struct drm_crtc_state uapi;",
        '#include "lcd_plane_types.h"     /* reference, extracted: enum plane_id, I915_MAX_PLANES */' + NL +
        '#include "lcd_dbuf_slice_enum.h"  /* reference, extracted: enum dbuf_slice */' + NL +
        '#include "lcd_wm_types.h"        /* reference, extracted: skl_wm_level, skl_plane_wm, skl_pipe_wm */' + NL +
        '#include "lcd_wm_ddb_types.h"    /* reference, extracted: skl_ddb_entry + size / equal */' + NL +
        "struct intel_plane;" + NL + "struct intel_plane_state;" + NL +
        "struct intel_crtc_state {" + NL + "	struct drm_crtc_state uapi;")
c = rep(c, "	struct { struct drm_display_mode adjusted_mode; const struct drm_property_blob *degamma_lut, *gamma_lut, *ctm; } hw;",
        "	struct { struct drm_display_mode adjusted_mode, pipe_mode; const struct drm_property_blob *degamma_lut, *gamma_lut, *ctm; bool active; } hw;")
c = rep(c, "	u8 sync_mode_slaves_mask;" + NL, "	u8 sync_mode_slaves_mask;" + NL +
        "	/* watermarks / DDB of the crtc (intel_crtc_wm_state.skl), and the one plane of this path with its state */" + NL +
        "	struct { struct { struct skl_pipe_wm raw, optimal; struct skl_ddb_entry ddb;" + NL +
        "		struct skl_ddb_entry plane_ddb[I915_MAX_PLANES], plane_ddb_y[I915_MAX_PLANES]; } skl; } wm;" + NL +
        "	struct intel_plane *only_plane;" + NL +
        "	const struct intel_plane_state *only_plane_state;" + NL +
        "	u32 data_rate[I915_MAX_PLANES], data_rate_y[I915_MAX_PLANES], rel_data_rate[I915_MAX_PLANES], rel_data_rate_y[I915_MAX_PLANES];" + NL +
        "	u8 nv12_planes, enabled_planes;" + NL +
        "	struct { int scaler_id; } scaler_state;" + NL)
c = rep(c, "		struct { bool ignore_long_hpd; } hotplug;", "		struct { bool ignore_long_hpd; } hotplug;" + NL +
        "		struct { u16 skl_latency[8]; u8 num_levels; bool ipc_enabled; } wm;" + NL +
        "		struct { struct { u32 size; u8 slice_mask; } dbuf; } device_info;   /* the reference's DISPLAY_INFO()->dbuf */" + NL +
        "		struct { u8 block_time_us; } sagv;")
save(L + "lcd_compat.h", c)

open(root + L + "lcd_wm_compat.h", "w").write("""/*
 * WS031 Linux-parity — environment of the watermark / DDB code (skl_watermark_port.c and its helpers) on top
 * of the other LCD compat headers.  zedBSD project code.
 *
 * Scope: ONE pipe with ONE visible plane (the primary; no cursor plane is created in this path).  The
 * reference's iterators over "the planes of this crtc" / "the crtcs of this state" therefore visit that one
 * plane / crtc, and the global DBUF state is the one object the modeset carries.
 */
#ifndef PARITY_LCD_WM_COMPAT_H
#define PARITY_LCD_WM_COMPAT_H
#include "lcd_i915_fixed.h"           /* reference, extracted: uint_fixed_16_16_t arithmetic */
#include "lcd_mreg_wm.h"              /* reference, extracted macros: PLANE_WM, PLANE_BUF_CFG, ... */

#define U16_MAX ((u16)0xffff)
#define U32_MAX ((u32)0xffffffffu)
#define fls(x) ((x) ? 32 - __builtin_clz((unsigned int)(x)) : 0)
#ifndef hweight8
#define hweight8(x) ((unsigned int)__builtin_popcount((unsigned int)(x) & 0xffu))
#endif
#define max_t(t, a, b) ({ t _a = (t)(a); t _b = (t)(b); _a > _b ? _a : _b; })
#define DIV64_U64_ROUND_UP(n, d) ((u64)(((u64)(n) + (u64)(d) - 1u) / (u64)(d)))
#define div64_u64(n, d) ((u64)(n) / (u64)(d))
#define for_each_if(condition) if (!(condition)) {} else
#define IS_ERR(p) (0)
#define PTR_ERR(p) (0)
#define WARN_ON_ONCE(c) WARN_ON(c)
#define DISPLAY_INFO(i915) (&(i915)->display.device_info)
#define IS_KABYLAKE(i915) 0
#define IS_COFFEELAKE(i915) 0
#define IS_COMETLAKE(i915) 0
#define IS_DGFX(i915) 0

/* [fixed] linear framebuffers only */
#define intel_fb_is_ccs_modifier(modifier) (0)
#define intel_fb_is_tiled_modifier(modifier) ((modifier) != DRM_FORMAT_MOD_LINEAR)

/* ---- the global DBUF state and the atomic accessors, reduced to the modeset's one object ---- */
struct intel_global_state { int unused; };
#include "lcd_dbuf_types.h"           /* reference, extracted: struct intel_dbuf_state */
struct parity_lcd_wm_ctx { struct intel_dbuf_state old_dbuf, new_dbuf; struct intel_crtc_state *crtc_state; };
extern struct parity_lcd_wm_ctx *parity_lcd_wm;
#define intel_atomic_get_new_dbuf_state(state) (&parity_lcd_wm->new_dbuf)
#define intel_atomic_get_old_dbuf_state(state) (&parity_lcd_wm->old_dbuf)
#define intel_atomic_get_crtc_state(state, crtc) (parity_lcd_wm->crtc_state)
#define intel_atomic_lock_global_state(global_state) (0)
#define to_intel_plane_state(x) ((struct intel_plane_state *)(x))

/* ---- iterators over the one plane / crtc ---- */
#define intel_atomic_crtc_state_for_each_plane_state(plane, plane_state, crtc_state) \\
	for ((plane) = (crtc_state)->only_plane, (plane_state) = (crtc_state)->only_plane_state; (plane) != 0; (plane) = 0)
#define for_each_plane_id_on_crtc(crtc, p) for ((p) = PLANE_PRIMARY; (p) <= PLANE_PRIMARY; (p)++)   /* crtc->plane_ids_mask = the primary */
#define for_each_dbuf_slice(i915, slice) for ((slice) = DBUF_S1; (slice) < I915_MAX_DBUF_SLICES; (slice)++) for_each_if(DISPLAY_INFO(i915)->dbuf.slice_mask & BIT(slice))
#define for_each_dbuf_slice_in_mask(i915, slice, mask) for_each_dbuf_slice((i915), (slice)) for_each_if((mask) & BIT(slice))

/* prototypes of kept non-static reference functions called across the generated files */
bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
unsigned int intel_plane_pixel_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
void drm_mode_get_hv_timing(const struct drm_display_mode *mode, int *hdisplay, int *vdisplay);
void skl_write_plane_wm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);

#endif /* PARITY_LCD_WM_COMPAT_H */
""")
p = load(L + "lcd_plane_compat.h")
lines = [l for l in p.split(NL) if l.startswith("#define skl_write_plane_wm(")]
assert len(lines) == 1
i = p.index(lines[0]); e = p.index(NL, p.index(NL, i) + 1) + 1
p = p[:i] + p[e:]
save(L + "lcd_plane_compat.h", p)
print("done")
