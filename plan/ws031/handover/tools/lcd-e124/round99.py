#!/usr/bin/env python3
"""WS031 E-124 round 99: the readout's power-domain set, the device's DBUF object, and the last unported callees.
usage: round99.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

p = L + "lcd_compat.h"
s = open(p).read()
# the readout fills a power-domain SET, as the reference's crtc does
s = s.replace(TAB + "struct intel_power_domain_mask hw_readout_power_domains;",
              TAB + "struct intel_display_power_domain_set hw_readout_power_domains;")
# the DBUF object the watermark readout reads: display.dbuf.obj.state / display.dbuf.enabled_slices
s = s.replace(TAB * 2 + "/* the device's DBUF object the watermark readout fills (skl_wm_get_hw_state) */" + NL +
              TAB * 2 + "struct { struct intel_dbuf_state *obj; u8 enabled_slices; } dbuf;"
              "        /* bit n: firmware for enum intel_dmc_id n is loaded (from the DMC loader) */",
              TAB * 2 + "/* the device's DBUF object the watermark readout reads (skl_wm_get_hw_state) */" + NL +
              TAB * 2 + "struct { struct { struct intel_global_state *state; } obj; u8 enabled_slices; } dbuf;")
open(p, "w").write(s)

p = L + "n1_compat.h"
s = open(p).read()
if "hsw_ips_get_config" not in s:
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */",
                  "/* the readout callees of parts this path does not program (IPS, the panel fitter, the scaler) */" + NL +
                  "#define hsw_ips_get_config(cs) N1_STEP(\"hsw_ips_get_config\")" + NL +
                  "#define ilk_get_pfit_config(cs) N1_STEP(\"ilk_get_pfit_config\")" + NL +
                  "#define skl_scaler_get_config(cs) N1_STEP(\"skl_scaler_get_config\")" + NL +
                  "/* intel_crtc.c's vblank wait reaches the DRM helper: this path has no DRM vblank layer */" + NL +
                  "#undef drm_crtc_wait_one_vblank" + NL +
                  "#define drm_crtc_wait_one_vblank(crtc) N1_STEP(\"drm_crtc_wait_one_vblank\")" + NL + NL +
                  "#endif /* PARITY_N1_COMPAT_H */")
    open(p, "w").write(s)
print("done")
