#!/usr/bin/env python3
"""WS031 E-115 round 12: watermark / DDB computation, allocation and writer (skl_watermark.c) -- first pass.
usage: round12.py <repo root>"""
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
j["range_headers"] += [
  {"out": "lcd_wm_types.h", "dir": "i915", "source": "display/intel_display_types.h", "path": "drivers/gpu/drm/i915/display/intel_display_types.h",
   "ranges": [["struct skl_wm_level {", END, "struct skl_wm_level"], ["struct skl_plane_wm {", END, "struct skl_plane_wm"],
              ["struct skl_pipe_wm {", END, "struct skl_pipe_wm"]]},
  {"out": "lcd_wm_ddb_types.h", "dir": "i915", "source": "display/intel_wm_types.h", "path": "drivers/gpu/drm/i915/display/intel_wm_types.h",
   "ranges": [["struct skl_ddb_entry {", END, "struct skl_ddb_entry"]]},
  {"out": "lcd_dbuf_types.h", "dir": "i915", "source": "display/skl_watermark.h", "path": "drivers/gpu/drm/i915/display/skl_watermark.h",
   "ranges": [["struct intel_dbuf_state {", END, "struct intel_dbuf_state"]]},
  {"out": "lcd_dbuf_slice_enum.h", "dir": "i915", "source": "display/intel_display_power.h", "path": "drivers/gpu/drm/i915/display/intel_display_power.h",
   "ranges": [["enum dbuf_slice {", END, "enum dbuf_slice"]]},
]
j["new_files"].append({"out": "skl_watermark_port.c", "dir": "i915", "source": "display/skl_watermark.c",
    "path": "drivers/gpu/drm/i915/display/skl_watermark.c",
    "ranges": [["struct skl_wm_params {", END, "struct skl_wm_params"]],
    "functions": ["skl_wm_latency", "skl_wm_method1", "skl_wm_method2", "skl_compute_wm_params", "skl_compute_plane_wm_params",
                  "skl_wm_has_lines", "skl_wm_max_lines", "skl_compute_plane_wm", "skl_compute_wm_levels", "tgl_compute_sagv_wm",
                  "skl_compute_transition_wm", "skl_build_plane_wm_single", "icl_build_plane_wm", "skl_build_pipe_wm",
                  "skl_check_wm_level", "skl_check_nv12_wm_level", "skl_need_wm_copy_wa", "use_minimal_wm0_only",
                  "skl_allocate_plane_ddb", "skl_crtc_allocate_plane_ddb", "skl_ddb_entry_for_slices", "intel_crtc_ddb_weight",
                  "skl_crtc_allocate_ddb", "skl_plane_wm_level", "skl_plane_trans_wm", "skl_write_wm_level", "skl_ddb_entry_write",
                  "skl_write_plane_wm"],
    "includes": ["lcd_compat.h", "lcd_seq_compat.h", "lcd_modeset_compat.h", "lcd_plane_compat.h", "lcd_wm_compat.h"],
    "glue": "parity_wm_glue.inc"})
j["macro_headers"].append({"out": "lcd_mreg_wm.h", "dir": "i915", "source": "display/skl_watermark_regs.h",
    "path": "drivers/gpu/drm/i915/display/skl_watermark_regs.h", "roots": [], "exclude": j["macro_headers"][0]["exclude"]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)

open(root + L + "lcd_wm_compat.h", "w").write("""/*
 * WS031 Linux-parity — environment of the watermark / DDB code (skl_watermark_port.c) on top of the other
 * LCD compat headers.  zedBSD project code.  (filled in as the extraction compiles)
 */
#ifndef PARITY_LCD_WM_COMPAT_H
#define PARITY_LCD_WM_COMPAT_H
#include "lcd_mreg_wm.h"

#endif /* PARITY_LCD_WM_COMPAT_H */
""")
open(root + L + "parity_wm_glue.inc", "w").write("/* WS031: watermark glue (filled in below) */" + NL)
print("spec updated")
