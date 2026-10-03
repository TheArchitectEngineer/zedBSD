#!/usr/bin/env python3
"""WS031 E-116 round 20: compat for the commit's outer part (power-domain set, DBUF/MBUS, CDCLK requirement).
usage: round20.py <repo root>"""
import sys, json
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(root + J))
for n in ("icl_ddi_min_voltage_level", "jsl_ddi_min_voltage_level"):
    if n not in j["extra"]["intel_ddi.c"]:
        i = j["extra"]["intel_ddi.c"].index("tgl_ddi_min_voltage_level")
        j["extra"]["intel_ddi.c"].insert(i, n)
if "lcd_mreg_power.h" not in [m["out"] for m in j["macro_headers"]]:
    j["macro_headers"].append({"dir": "i915", "out": "lcd_mreg_power.h", "source": "display/intel_display_power.h",
        "path": "drivers/gpu/drm/i915/display/intel_display_power.h", "exclude": [],
        "roots": ["POWER_DOMAIN_PIPE", "POWER_DOMAIN_PIPE_PANEL_FITTER", "POWER_DOMAIN_TRANSCODER"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

c = load(L + "lcd_compat.h")
c = rep(c, "struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed, async_flip; };",
        "struct drm_crtc_state { struct drm_crtc *crtc; bool mode_changed, active_changed, connectors_changed, async_flip; u32 encoder_mask; };")
c = rep(c, "struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; };",
        "/* linux/bitmap.h / bitops.h, as far as the power-domain set needs them */" + NL +
        "#define PARITY_BITS_PER_LONG (8u * (unsigned)sizeof(unsigned long))" + NL +
        "#define DECLARE_BITMAP(name, bits) unsigned long name[((bits) + PARITY_BITS_PER_LONG - 1u) / PARITY_BITS_PER_LONG]" + NL +
        "static inline void set_bit(unsigned nr, unsigned long *p) { p[nr / PARITY_BITS_PER_LONG] |= 1ul << (nr % PARITY_BITS_PER_LONG); }" + NL +
        "static inline void clear_bit(unsigned nr, unsigned long *p) { p[nr / PARITY_BITS_PER_LONG] &= ~(1ul << (nr % PARITY_BITS_PER_LONG)); }" + NL +
        "static inline bool test_bit(unsigned nr, const unsigned long *p) { return (p[nr / PARITY_BITS_PER_LONG] >> (nr % PARITY_BITS_PER_LONG)) & 1ul; }" + NL +
        "static inline void bitmap_zero(unsigned long *dst, unsigned nbits)" + NL +
        "{ unsigned i; for (i = 0; i < (nbits + PARITY_BITS_PER_LONG - 1u) / PARITY_BITS_PER_LONG; i++) dst[i] = 0ul; }" + NL +
        "static inline void bitmap_andnot(unsigned long *dst, const unsigned long *a, const unsigned long *b, unsigned nbits)" + NL +
        "{ unsigned i; for (i = 0; i < (nbits + PARITY_BITS_PER_LONG - 1u) / PARITY_BITS_PER_LONG; i++) dst[i] = a[i] & ~b[i]; }" + NL +
        "static inline bool bitmap_subset(const unsigned long *a, const unsigned long *b, unsigned nbits)" + NL +
        "{ unsigned i; for (i = 0; i < (nbits + PARITY_BITS_PER_LONG - 1u) / PARITY_BITS_PER_LONG; i++) if (a[i] & ~b[i]) return false; return true; }" + NL +
        "#ifndef for_each_if" + NL + "#define for_each_if(condition) if (!(condition)) {} else" + NL + "#endif" + NL +
        "#define IS_ENABLED(option) 0            /* CONFIG_DRM_I915_DEBUG_RUNTIME_PM: off (no per-domain wakeref tracking) */" + NL +
        '#include "lcd_power_domain_enum.h"      /* reference, extracted: enum intel_display_power_domain */' + NL +
        '#include "lcd_power_domain_set_types.h" /* reference, extracted: the power-domain mask / set and for_each_power_domain() */' + NL +
        '#include "lcd_mreg_power.h"             /* reference, extracted: POWER_DOMAIN_PIPE() / _TRANSCODER() / _PIPE_PANEL_FITTER() */' + NL +
        "struct intel_crtc { struct drm_crtc base; enum pipe pipe; bool active; struct intel_display_power_domain_set enabled_power_domains; };")
c = rep(c, "	struct { struct drm_device *dev; struct { int id; } base; const char *name; } base;" + NL + "	int type;                   /* enum intel_output_type */",
        "	struct drm_encoder base;" + NL + "	int type;                   /* enum intel_output_type */" + NL +
        "	int power_domain;           /* enum intel_display_power_domain: intel_ddi_init() sets the port's DDI lanes domain */")
c = rep(c, "struct intel_encoder {", "struct drm_encoder { struct drm_device *dev; struct { int id; } base; const char *name; unsigned index; };" + NL + "struct intel_encoder {")
c = rep(c, "		struct { int invert_brightness; } params;",
        "		/* the CDCLK state the normal initialisation left: table of the platform, reference / bypass clock, limit */" + NL +
        "		struct { const struct intel_cdclk_vals *table; struct { unsigned int cdclk, vco, ref, bypass; u8 voltage_level; } hw; unsigned int max_cdclk_freq; } cdclk;" + NL +
        "		struct { int invert_brightness; } params;")
c = rep(c, "struct drm_i915_private {", "struct intel_cdclk_vals;" + NL + "struct drm_i915_private {")
c = rep(c, "	bool double_wide, fec_enable, has_psr, has_audio, enhanced_framing;",
        "	bool double_wide, fec_enable, has_psr, has_audio, enhanced_framing;" + NL +
        "	u8 min_voltage_level;" + NL + "	int min_cdclk[I915_MAX_PLANES];")
save(L + "lcd_compat.h", c)

w = load(L + "lcd_wm_compat.h")
w = rep(w, "#define for_each_if(condition) if (!(condition)) {} else" + NL, "")
w = rep(w, "struct intel_global_state { struct intel_atomic_state *state; };", "struct intel_global_state { struct intel_atomic_state *state; bool changed; };")
w = rep(w, "/* [fixed] linear framebuffers only */", """/* gen9_dbuf_slices_update(): the body the normal initialisation already uses (it owns the enabled-slices state and the
 * power-domains lock); reached through the ops so that the model and the real device see the same request */
#define gen9_dbuf_slices_update(i915, req_slices) (i915)->emit->dbuf_slices_update((i915)->emit->ctx, (unsigned)(req_slices))

/* [fixed] linear framebuffers only */""")
save(L + "lcd_wm_compat.h", w)

q = load(L + "lcd_seq_compat.h")
q = rep(q, "/* DSC: every one of these returns at once", """/* reached only with DSC (crtc_state->dsc.compression_enable): an error if it ever is */
#define intel_dsc_power_domain(crtc, cpu_transcoder) (parity_lcd_error("intel_dsc_power_domain reached: DSC is not part of this path" "\\n"), POWER_DOMAIN_DISPLAY_CORE)
#define intel_vdsc_min_cdclk(cs) (parity_lcd_error("intel_vdsc_min_cdclk reached: DSC is not part of this path" "\\n"), 0)
#define hsw_crtc_state_ips_capable(cs) (false)   /* behind IS_BROADWELL() */
#define IS_VALLEYVIEW(i915) 0
/* the one encoder of the modeset (drm_encoder_mask() = 1 << index) */
extern struct drm_encoder *parity_lcd_only_encoder;
#define drm_for_each_encoder_mask(encoder, dev, mask) """ + BS + """
	for ((encoder) = parity_lcd_only_encoder; (encoder) != 0; (encoder) = 0) for_each_if((mask) & (1u << (encoder)->index))

/* DSC: every one of these returns at once""")
save(L + "lcd_seq_compat.h", q)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "#define intel_display_power_put(i915, domain, wakeref)",
        "#define intel_display_power_put_async_delay(i915, domain, wakeref, delay_ms) (i915)->emit->power_put_async((i915)->emit->ctx, (int)(domain), (wakeref), (delay_ms))" + NL +
        "#define intel_display_power_put(i915, domain, wakeref)")
if "__maybe_unused" not in m and "__maybe_unused" not in c:
    m = rep(m, "typedef int intel_wakeref_t;", "typedef int intel_wakeref_t;" + NL + "#define __maybe_unused __attribute__((unused))")
save(L + "lcd_modeset_compat.h", m)

o = load(L + "parity_lcd_ops.h")
o = rep(o, "	void (*power_put)(void *ctx, int domain, int wakeref);",
        "	void (*power_put)(void *ctx, int domain, int wakeref);" + NL +
        "	/* intel_display_power_put_async_delay(): the reference drops DC_OFF this way at the end of a commit */" + NL +
        "	void (*power_put_async)(void *ctx, int domain, int wakeref, int delay_ms);" + NL +
        "	/* gen9_dbuf_slices_update(): request exactly these DBUF slices (bit n = slice n+1) */" + NL +
        "	void (*dbuf_slices_update)(void *ctx, unsigned req_slices);" + NL +
        "	/* optional: a named point of the commit was reached (enum parity_lcd_observe); the real device samples here */" + NL +
        "	void (*observe)(void *ctx, int point);")
o = rep(o, "struct parity_lcd_emit {", """/* points of the commit at which the caller may look at the hardware (no writes of the path depend on them) */
enum parity_lcd_observe {
	PARITY_LCD_OBS_COMMIT_BEGIN = 0,        /* DC_OFF held, nothing written yet */
	PARITY_LCD_OBS_UNDERRUN_ARM,            /* where the reference clears the pipe's underrun status and unmasks its interrupt */
	PARITY_LCD_OBS_PIPE_ENABLED,            /* the crtc enable returned */
	PARITY_LCD_OBS_PLANE_ARMED,             /* PLANE_SURF written */
	PARITY_LCD_OBS_PLANE_DISABLED,          /* the plane disable was armed */
	PARITY_LCD_OBS_UNDERRUN_DISARM,         /* where the reference masks the underrun interrupt again */
	PARITY_LCD_OBS_PIPE_DISABLED,           /* the crtc disable returned */
	PARITY_LCD_OBS_COMMIT_END,              /* before DC_OFF is dropped */
	PARITY_LCD_OBS_NUM
};

struct parity_lcd_emit {""")
save(L + "parity_lcd_ops.h", o)
print("done")
