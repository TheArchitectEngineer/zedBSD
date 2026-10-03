#!/usr/bin/env python3
"""WS031 E-112: extend port_lcd_calc.py with the universal-plane word functions (skl_universal_plane.c).
usage: patch_generator_e112.py <path to port_lcd_calc.py>"""
import sys
NL, TAB = chr(10), chr(9)
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)

rep("  <out>/lcd_drm_colorspace.h   drm_connector.h: enum drm_colorspace (textual extract)" + NL,
    "  <out>/lcd_drm_colorspace.h   drm_connector.h: enum drm_colorspace (textual extract)" + NL +
    "  <out>/skl_plane_port.c       skl_universal_plane.c: PLANE_CTL / COLOR_CTL / stride / surf / keys and the" + NL +
    "                               icl_plane_update_noarm / _arm writers" + NL +
    "  <out>/lcd_plane_types.h      enum plane_id; drm_intel_sprite_colorkey + I915_SET_COLORKEY_* (i915_drm.h)" + NL +
    "  <out>/lcd_plane_regs.h       i915_reg.h: the Skylake+ plane register block" + NL +
    "  <out>/lcd_psr_selfetch_regs.h intel_psr_regs.h: PLANE_SEL_FETCH_*" + NL +
    "  <out>/lcd_drm_fourcc.h       drm_fourcc.h, whole (one include line dropped)" + NL +
    "  <out>/lcd_drm_plane_defs.h   drm_blend.h / drm_mode.h / drm_color_mgmt.h: blend modes, rotation bits, colour enums" + NL)

new = '''
# ======================================================================= E-112: universal plane words
sp = rd(os.path.join(ref, "display"), "skl_universal_plane.c")
psr = rd(os.path.join(ref, "display"), "intel_psr_regs.h")
fcc = rd(drmref, "drm_fourcc.h")
bl = rd(drmref, "drm_blend.h")
um = rd(drmref, "uapi_drm_mode.h")
cm = rd(drmref, "drm_color_mgmt.h")
idr = rd(drmref, "i915_drm.h")

body = ""
PLANE_FUNCS = ("icl_hdr_plane_mask", "icl_is_hdr_plane", "skl_plane_stride_mult", "skl_plane_stride",
               "skl_plane_ctl_format", "skl_plane_ctl_alpha", "glk_plane_color_ctl_alpha", "skl_plane_ctl_tiling",
               "skl_plane_ctl_rotate", "icl_plane_ctl_flip", "adlp_plane_ctl_arb_slots", "skl_plane_ctl_crtc",
               "skl_plane_ctl", "glk_plane_color_ctl_crtc", "glk_plane_color_ctl", "skl_surf_address", "skl_plane_surf",
               "skl_plane_aux_dist", "skl_plane_keyval", "skl_plane_keymsk", "skl_plane_keymax", "icl_plane_color_plane",
               "icl_plane_update_sel_fetch_noarm", "icl_plane_update_noarm", "icl_plane_disable_sel_fetch_arm",
               "icl_plane_update_sel_fetch_arm", "icl_plane_update_arm")
for n in PLANE_FUNCS:
    body += func(sp, n, "display/skl_universal_plane.c") + NL
lic = sp[:first_comment_end(sp)]
wr("skl_plane_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/skl_universal_plane.c", manifest["sources"]["skl_universal_plane.c"],
    [" - kept: " + ", ".join(PLANE_FUNCS[0:5]) + ","] +
    ["   " + ", ".join(PLANE_FUNCS[i:i + 5]) + ("," if i + 5 < len(PLANE_FUNCS) else ";") for i in range(5, len(PLANE_FUNCS), 5)] +
    [" - the includes are replaced by lcd_compat.h + lcd_plane_compat.h (register writes go through the emit hook;",
     "   callees that are not ported -- skl_write_plane_wm, the scaler and CSC programming -- are recorded as",
     "   named steps there, never silently dropped);",
     " - parity_plane_emit_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_plane_compat.h"' + NL + NL + body + NL +
   '#include "parity_plane_emit_glue.inc"' + TAB + "/* zedBSD: builds the plane / fb state and records the words */" + NL)

pt = (between(lim, "enum plane_id {", NL + "};" + NL, "display/intel_display_limits.h", "enum plane_id") + NL)
wr("lcd_plane_types.h", "/*" + NL +
   " * zedBSD WS031: enum plane_id extracted textually from the Linux v6.8.12 i915 reference" + NL +
   " * (display/intel_display_limits.h: SPDX MIT, Copyright Intel Corporation -- full notice in skl_plane_port.c)" + NL +
   " * by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_PLANE_TYPES_H" + NL + "#define PARITY_LCD_PLANE_TYPES_H" + NL + NL + pt + NL +
   "#endif /* PARITY_LCD_PLANE_TYPES_H */" + NL)

ck = (lines_matching(idr, r"^#define I915_SET_COLORKEY_(DESTINATION|SOURCE)\\b", "i915_drm.h", "I915_SET_COLORKEY_*", 2) + NL +
      between(idr, "struct drm_intel_sprite_colorkey {", NL + "};" + NL, "i915_drm.h", "struct drm_intel_sprite_colorkey"))
lic = idr[:first_comment_end(idr)]
wr("lcd_i915_colorkey.h", lic + NL + NL + "/*" + NL +
   " * zedBSD WS031: struct drm_intel_sprite_colorkey and two I915_SET_COLORKEY_* flags extracted textually from" + NL +
   " * Linux v6.8.12 include/uapi/drm/i915_drm.h (sha256 " + manifest["sources"]["i915_drm.h"] + ")" + NL +
   " * by tools/port_lcd_calc.py.  The copyright / permission notice above is the source file's own." + NL +
   " * Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_I915_COLORKEY_H" + NL + "#define PARITY_LCD_I915_COLORKEY_H" + NL + NL + ck + NL +
   "#endif /* PARITY_LCD_I915_COLORKEY_H */" + NL)

pr = between(rg0, "/* Skylake plane registers */", NL + "/* VBIOS regs */", "i915_reg.h", "Skylake+ plane register block")
pr = pr[:pr.rindex("/* VBIOS regs */")]
wr("lcd_plane_regs.h", "/*" + NL +
   " * zedBSD WS031: the Skylake+ universal plane register block extracted textually from the Linux v6.8.12 i915" + NL +
   " * reference (i915_reg.h: MIT permission notice; Copyright Intel Corporation -- the full notice is kept in" + NL +
   " * intel_ddi_port.c) by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_PLANE_REGS_H" + NL + "#define PARITY_LCD_PLANE_REGS_H" + NL + NL + pr + NL +
   "#endif /* PARITY_LCD_PLANE_REGS_H */" + NL)

sf = between(psr, "#define _SEL_FETCH_PLANE_BASE_1_A", NL + "#define _ALPM_CTL_A", "display/intel_psr_regs.h", "PLANE_SEL_FETCH_*")
sf = sf[:sf.rindex("#define _ALPM_CTL_A")]
lic = psr[:first_comment_end(psr)]
wr("lcd_psr_selfetch_regs.h", lic + NL + NL + "/*" + NL +
   " * zedBSD WS031: the PLANE_SEL_FETCH_* register definitions extracted textually from the Linux v6.8.12 i915" + NL +
   " * reference display/intel_psr_regs.h (sha256 " + manifest["sources"]["intel_psr_regs.h"] + ")" + NL +
   " * by tools/port_lcd_calc.py.  The notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_PSR_SELFETCH_REGS_H" + NL + "#define PARITY_LCD_PSR_SELFETCH_REGS_H" + NL + NL + sf + NL +
   "#endif /* PARITY_LCD_PSR_SELFETCH_REGS_H */" + NL)

wr("lcd_drm_fourcc.h", sub(fcc, '#include "drm.h"' + NL, "/* zedBSD: the drm.h include is dropped; lcd_plane_compat.h supplies __u32 / __u64 */" + NL,
                           "drm_fourcc.h: #include \\"drm.h\\" dropped"))

defs = (lines_matching(bl, r"^#define DRM_MODE_BLEND_", "drm_blend.h", "DRM_MODE_BLEND_*", 3) + NL +
        lines_matching(um, r"^#define DRM_MODE_(ROTATE|REFLECT)_(0|90|180|270|X|Y)\\s", "uapi drm_mode.h", "DRM_MODE_ROTATE_* / REFLECT_*", 6) +
        span(um, "#define DRM_MODE_ROTATE_MASK", "DRM_MODE_ROTATE_270)", "uapi drm_mode.h", "DRM_MODE_ROTATE_MASK") +
        span(um, "#define DRM_MODE_REFLECT_MASK", "DRM_MODE_REFLECT_Y)", "uapi drm_mode.h", "DRM_MODE_REFLECT_MASK") + NL +
        func(bl, "drm_rotation_90_or_270", "drm_blend.h") + NL +
        between(cm, "enum drm_color_encoding {", NL + "};" + NL, "drm_color_mgmt.h", "enum drm_color_encoding") + NL +
        between(cm, "enum drm_color_range {", NL + "};" + NL, "drm_color_mgmt.h", "enum drm_color_range"))
wr("lcd_drm_plane_defs.h", "/*" + NL +
   " * zedBSD WS031: plane-related DRM definitions extracted textually from Linux v6.8.12 by tools/port_lcd_calc.py:" + NL +
   " *   include/drm/drm_blend.h        (sha256 " + manifest["sources"]["drm_blend.h"] + "): DRM_MODE_BLEND_*, drm_rotation_90_or_270" + NL +
   " *   include/uapi/drm/drm_mode.h    (sha256 " + manifest["sources"]["uapi_drm_mode.h"] + "): DRM_MODE_ROTATE_* / REFLECT_*" + NL +
   " *   include/drm/drm_color_mgmt.h   (sha256 " + manifest["sources"]["drm_color_mgmt.h"] + "): enum drm_color_encoding / drm_color_range" + NL +
   " * Each source file carries its own copyright / permission notice (kept unmodified in" + NL +
   " * plan/ws031/linux-parity/linux-reference/drm-v6.8.12/); three sources in one extract is recorded in the" + NL +
   " * provenance ledger as not yet audited.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_DRM_PLANE_DEFS_H" + NL + "#define PARITY_LCD_DRM_PLANE_DEFS_H" + NL + NL + defs + NL +
   "#endif /* PARITY_LCD_DRM_PLANE_DEFS_H */" + NL)

'''
rep('open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")', new + 'open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")')
open(p, "w").write(s)
print("generator patched (E-112)")
