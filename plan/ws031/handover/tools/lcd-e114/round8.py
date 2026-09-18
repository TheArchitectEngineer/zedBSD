#!/usr/bin/env python3
"""WS031 E-115 round 8: the PWM backlight (intel_backlight.c: the PWM layer + the CNP+ PCH PWM functions).
usage: round8.py <repo root>"""
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
c = rep(c, """struct intel_connector {
	struct { struct { int id; } base; const char *name; } base;
	struct { struct { struct { bool hobl, low_vswing; } edp; } vbt; } panel;
	int modeset_retry_work;
};""", """struct intel_connector;
struct intel_panel_bl_funcs {           /* intel_display_types.h */
	int (*setup)(struct intel_connector *connector, enum pipe pipe);
	u32 (*get)(struct intel_connector *connector, enum pipe pipe);
	void (*set)(const struct drm_connector_state *conn_state, u32 level);
	void (*disable)(const struct drm_connector_state *conn_state, u32 level);
	void (*enable)(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state, u32 level);
	u32 (*hz_to_pwm)(struct intel_connector *connector, u32 hz);
};
struct intel_panel {                    /* the members the kept functions use */
	struct {
		struct { bool hobl, low_vswing; } edp;
		struct { u16 pwm_freq_hz; bool present, active_low_pwm; u8 min_brightness; s8 controller; } backlight;
	} vbt;
	struct {
		bool present;
		u32 level, min, max;
		bool enabled, combination_mode, active_low_pwm;
		u32 pwm_level_min, pwm_level_max;
		bool pwm_enabled;
		u8 controller;
		void *device;               /* no backlight class device here: always NULL */
		const struct intel_panel_bl_funcs *funcs;
		const struct intel_panel_bl_funcs *pwm_funcs;
	} backlight;
};
struct intel_connector {
	struct { struct drm_device *dev; struct { int id; } base; const char *name; } base;
	struct intel_panel panel;
	int modeset_retry_work;
};""")
c = rep(c, "enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D };" + NL, "")
c = rep(c, "struct drm_crtc { struct drm_device *dev;", "enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D };" + NL + "typedef signed char s8;" + NL + "struct drm_crtc { struct drm_device *dev;")
c = rep(c, "		struct { struct { int which; } lock; } backlight;", "		struct { struct { int which; } lock; } backlight;" + NL +
        "		struct { u32 rawclk_freq; } runtime;  /* DISPLAY_RUNTIME_INFO: rawclk in kHz (read out by the caller) */")
save(L + "lcd_compat.h", c)

q = load(L + "lcd_seq_compat.h")
for n in ["intel_backlight_enable", "intel_backlight_disable"]:
    lines = [l for l in q.split(NL) if l.startswith("#define " + n + "(")]
    assert len(lines) == 1, n
    q = q.replace(lines[0] + NL, "")
q = q.replace("/* the PWM side of the backlight (intel_backlight.c) is not ported yet; the PPS side runs for real */" + NL, "")
save(L + "lcd_seq_compat.h", q)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "/* ---- state checkers [check] ---- */", """/* backlight: runtime info and switches fixed by the configuration */
#define DISPLAY_RUNTIME_INFO(i915) (&(i915)->display.runtime)
#define KHz(x) (1000 * (x))
#define DRM_SWITCH_POWER_CHANGING 3
#define FB_BLANK_UNBLANK 0
#define FB_BLANK_POWERDOWN 4

/* ---- state checkers [check] ---- */""")
m = rep(m, "void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);",
        "void intel_dmc_enable_pipe(struct drm_i915_private *i915, enum pipe pipe);" + NL +
        "void intel_backlight_enable(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);" + NL +
        "void intel_backlight_disable(const struct drm_connector_state *old_conn_state);")
save(L + "lcd_modeset_compat.h", m)

j = json.load(open(root + J))
j["new_files"].append({"out": "intel_backlight_port.c", "dir": "i915", "source": "display/intel_backlight.c",
    "path": "drivers/gpu/drm/i915/display/intel_backlight.c", "ranges": [],
    "functions": ["scale", "clamp_user_to_hw", "scale_hw_to_user", "intel_backlight_invert_pwm_level", "intel_backlight_set_pwm_level",
                  "intel_backlight_level_to_pwm", "intel_backlight_level_from_pwm", "bxt_get_backlight", "bxt_set_backlight",
                  "cnp_disable_backlight", "cnp_enable_backlight", "cnp_backlight_controller_is_valid", "cnp_hz_to_pwm",
                  "get_vbt_pwm_freq", "get_backlight_max_vbt", "get_backlight_min_vbt", "cnp_setup_backlight",
                  "intel_pwm_get_backlight", "intel_pwm_set_backlight", "intel_pwm_enable_backlight", "intel_pwm_disable_backlight",
                  "intel_pwm_setup_backlight", "__intel_backlight_enable", "intel_backlight_enable", "intel_backlight_disable"],
    "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_mreg_backlight.h"], "glue": "parity_backlight_glue.inc"})
j["macro_headers"].append({"out": "lcd_mreg_backlight.h", "dir": "i915", "source": "display/intel_backlight_regs.h",
    "path": "drivers/gpu/drm/i915/display/intel_backlight_regs.h", "roots": [], "exclude": j["macro_headers"][0]["exclude"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
open(root + L + "parity_backlight_glue.inc", "w").write("/* WS031: backlight glue (filled in below) */" + NL)
print("spec updated")
