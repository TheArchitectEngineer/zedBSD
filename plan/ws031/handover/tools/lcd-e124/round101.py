#!/usr/bin/env python3
"""WS031 E-124 round 101: the readout's remaining objects — the encoder's get_config hook, the crtc state's own
mode, the plane accessor and the pixel-rate helper; the DBUF object sits on the device, not in display_info."""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)
spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))
have = j["extra"]["intel_display.c"]
for n in ["intel_crtc_compute_pixel_rate"]:
    if n not in have:
        have.append(n)
j["extra"]["intel_display.c"] = have
open(spec, "w").write(json.dumps(j, indent=1) + NL)

p = L + "lcd_compat.h"
s = open(p).read()
# the encoder's readout hooks (intel_ddi_init binds them for a DDI encoder)
if "(*get_config)" not in s:
    s = s.replace(TAB + "void (*enable_clock)(struct intel_encoder *, const struct intel_crtc_state *);",
        TAB + "/* the readout hooks the reference binds for a DDI encoder (intel_ddi_init) */" + NL +
        TAB + "bool (*get_hw_state)(struct intel_encoder *, enum pipe *pipe);" + NL +
        TAB + "void (*get_config)(struct intel_encoder *, struct intel_crtc_state *);" + NL +
        TAB + "void (*sync_state)(struct intel_encoder *, const struct intel_crtc_state *);" + NL +
        TAB + "void (*get_power_domains)(struct intel_encoder *, struct intel_crtc_state *);" + NL +
        TAB + "void (*enable_clock)(struct intel_encoder *, const struct intel_crtc_state *);", 1)
# the crtc state's own mode (hw.mode / uapi.mode of the reference; the readout fills it)
if "struct drm_display_mode mode;" not in s:
    s = s.replace(TAB + "struct { struct drm_display_mode adjusted_mode, pipe_mode;",
                  TAB + "struct { struct drm_display_mode mode, adjusted_mode, pipe_mode;", 1)
if "#define to_intel_plane(" not in s:
    s = s.replace("#define to_intel_crtc(x)", "#define to_intel_plane(x) ((struct intel_plane *)(x))" + NL +
                  "#define to_intel_crtc(x)", 1)
open(p, "w").write(s)
print("done")
