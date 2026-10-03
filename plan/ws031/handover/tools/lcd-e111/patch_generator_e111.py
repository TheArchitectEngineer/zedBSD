#!/usr/bin/env python3
"""WS031 E-111a: extend port_lcd_calc.py -- hsw_configure_cpu_transcoder (the CALLER, so the order between the
writers is the reference's too), the DDI transcoder-function / MSA / DDI_BUF_CTL value functions, VRR timings.
usage: patch_generator_e111.py <path to port_lcd_calc.py>"""
import sys
NL, TAB = chr(10), chr(9)
Q = chr(34)
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:90]
    s = s.replace(old, new)

# ---- docstring
rep("  <out>/lcd_ref_types.h        struct intel_link_m_n, struct intel_dpll_hw_state, the DPLL_CFGCR / M-N macros" + NL,
    "  <out>/lcd_ref_types.h        struct intel_link_m_n, struct intel_dpll_hw_state, the DPLL_CFGCR / M-N macros" + NL +
    "  <out>/intel_ddi_port.c       intel_ddi.c: DDI_BUF_CTL value, TRANS_MSA_MISC, TRANS_DDI_FUNC_CTL[2], hsw_chicken_trans_reg" + NL +
    "  <out>/intel_vrr_port.c       intel_vrr.c: trans_vrr_ctl, intel_vrr_set_transcoder_timings" + NL +
    "  <out>/lcd_ddi_types.h        enum port / phy / intel_output_type / intel_output_format (textual extract)" + NL +
    "  <out>/lcd_ref_inlines.h      intel_crtc_has_type, intel_crtc_has_dp_encoder, intel_crtc_needs_modeset, transcoder_is_dsi" + NL +
    "  <out>/lcd_ddi_regs.h         TRANS_DDI_FUNC_CTL[2], DDI_BUF_CTL, TRANS_MSA_MISC, TRANSCONF, TRANS_MULT, TRANS_VRR_*, CHICKEN_TRANS" + NL +
    "  <out>/lcd_dp_msa.h           drm_dp.h: DP_MSA_MISC_* (textual extract)" + NL +
    "  <out>/lcd_drm_colorspace.h   drm_connector.h: enum drm_colorspace (textual extract)" + NL)

# ---- intel_dp.c: two more functions
rep("""for n in ("intel_dp_link_symbol_size", "intel_dp_link_symbol_clock", "intel_dp_link_required",
          "intel_dp_effective_data_rate", "intel_dp_max_data_rate"):""",
    """for n in ("intel_dp_is_uhbr", "intel_dp_link_symbol_size", "intel_dp_link_symbol_clock", "intel_dp_link_required",
          "intel_dp_effective_data_rate", "intel_dp_max_data_rate", "intel_dp_needs_vsc_sdp"):""")
rep('''    " - kept: intel_dp_link_symbol_size, intel_dp_link_symbol_clock, intel_dp_link_required,",
    "   intel_dp_effective_data_rate, intel_dp_max_data_rate;",''',
    '''    " - kept: intel_dp_is_uhbr, intel_dp_link_symbol_size, intel_dp_link_symbol_clock, intel_dp_link_required,",
    "   intel_dp_effective_data_rate, intel_dp_max_data_rate, intel_dp_needs_vsc_sdp;",''')

# ---- intel_display.c: the caller and its remaining callees
rep("""for n in ("intel_reduce_m_n_ratio", "compute_m_n", "intel_link_compute_m_n", "intel_set_m_n",
          "intel_cpu_transcoder_set_m1_n1", "intel_set_transcoder_timings", "intel_set_pipe_src_size"):""",
    """for n in ("intel_phy_is_tc", "intel_port_to_phy",
          "intel_reduce_m_n_ratio", "compute_m_n", "intel_link_compute_m_n", "intel_set_m_n",
          "intel_cpu_transcoder_has_m2_n2", "intel_cpu_transcoder_set_m1_n1", "intel_cpu_transcoder_set_m2_n2",
          "intel_set_transcoder_timings", "intel_set_pipe_src_size",
          "hsw_set_frame_start_delay", "hsw_set_transconf", "hsw_configure_cpu_transcoder"):""")
rep('''    " - kept: intel_reduce_m_n_ratio, compute_m_n, intel_link_compute_m_n, intel_set_m_n,",
    "   intel_cpu_transcoder_set_m1_n1, intel_set_transcoder_timings, intel_set_pipe_src_size;",''',
    '''    " - kept: intel_phy_is_tc, intel_port_to_phy, intel_reduce_m_n_ratio, compute_m_n, intel_link_compute_m_n,",
    "   intel_set_m_n, intel_cpu_transcoder_has_m2_n2, intel_cpu_transcoder_set_m1_n1,",
    "   intel_cpu_transcoder_set_m2_n2, intel_set_transcoder_timings, intel_set_pipe_src_size,",
    "   hsw_set_frame_start_delay, hsw_set_transconf, and their caller hsw_configure_cpu_transcoder (so the",
    "   order BETWEEN the writers is the reference's as well);",''')
rep("""   '#include "lcd_trans_regs.h"' + NL + NL + body + NL +
   '#include "parity_display_emit_glue.inc"'""",
    """   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + NL + body + NL +
   '#include "parity_display_emit_glue.inc"'""")

# ---- new outputs, before the manifest is written
new = '''
# ======================================================================= E-111: DDI / VRR writers, enums, inlines, registers
ddi = rd(os.path.join(ref, "display"), "intel_ddi.c")
vrr = rd(os.path.join(ref, "display"), "intel_vrr.c")
dh = rd(os.path.join(ref, "display"), "intel_display.h")
dph = rd(drmref, "drm_dp.h")
ch = rd(drmref, "drm_connector.h")

body = ""
for n in ("ddi_buf_phy_link_rate", "intel_ddi_init_dp_buf_reg", "intel_ddi_set_dp_msa", "bdw_trans_port_sync_master_select",
          "intel_ddi_transcoder_func_reg_val_get", "intel_ddi_enable_transcoder_func", "hsw_chicken_trans_reg"):
    body += func(ddi, n, "display/intel_ddi.c") + NL
lic = ddi[:first_comment_end(ddi)]
wr("intel_ddi_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c", manifest["sources"]["intel_ddi.c"], [
    " - kept: ddi_buf_phy_link_rate, intel_ddi_init_dp_buf_reg, intel_ddi_set_dp_msa,",
    "   bdw_trans_port_sync_master_select, intel_ddi_transcoder_func_reg_val_get,",
    "   intel_ddi_enable_transcoder_func, hsw_chicken_trans_reg;",
    " - the includes are replaced by lcd_compat.h (register writes go through its emit hook);",
    " - parity_ddi_emit_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_dp_msa.h"' + NL + NL + body + NL +
   '#include "parity_ddi_emit_glue.inc"' + TAB + "/* zedBSD: builds the encoder / crtc state and records the words */" + NL)

body = func(vrr, "trans_vrr_ctl", "display/intel_vrr.c") + NL + func(vrr, "intel_vrr_set_transcoder_timings", "display/intel_vrr.c") + NL
lic = vrr[:first_comment_end(vrr)]
wr("intel_vrr_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_vrr.c", manifest["sources"]["intel_vrr.c"], [
    " - kept: trans_vrr_ctl, intel_vrr_set_transcoder_timings;",
    " - the includes are replaced by lcd_compat.h (register writes go through its emit hook)."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + NL + body)

enums = (between(lim, "enum port {", NL + "};" + NL, "display/intel_display_limits.h", "enum port") + NL +
         between(dh, "enum phy {", NL + "};" + NL, "display/intel_display.h", "enum phy") + NL +
         between(ty, "enum intel_output_type {", NL + "};" + NL, "display/intel_display_types.h", "enum intel_output_type") + NL +
         between(ty, "enum intel_output_format {", NL + "};" + NL, "display/intel_display_types.h", "enum intel_output_format"))
wr("lcd_ddi_types.h", "/*" + NL +
   " * zedBSD WS031: enum port, enum phy, enum intel_output_type and enum intel_output_format extracted textually" + NL +
   " * from the Linux v6.8.12 i915 reference (display/intel_display_limits.h, display/intel_display.h,  display/" + NL +
   " * intel_display_types.h: MIT; Copyright Intel Corporation -- the full notices are kept in intel_ddi_port.c and" + NL +
   " * intel_display_port.c) by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_DDI_TYPES_H" + NL + "#define PARITY_LCD_DDI_TYPES_H" + NL + NL + enums + NL +
   "#endif /* PARITY_LCD_DDI_TYPES_H */" + NL)

inl = (func(dh, "transcoder_is_dsi", "display/intel_display.h") + NL +
       func(ty, "intel_crtc_has_type", "display/intel_display_types.h") + NL +
       func(ty, "intel_crtc_has_dp_encoder", "display/intel_display_types.h") + NL +
       func(ty, "intel_crtc_needs_modeset", "display/intel_display_types.h"))
wr("lcd_ref_inlines.h", "/*" + NL +
   " * zedBSD WS031: the inline helpers transcoder_is_dsi (display/intel_display.h), intel_crtc_has_type," + NL +
   " * intel_crtc_has_dp_encoder and intel_crtc_needs_modeset (display/intel_display_types.h) extracted textually" + NL +
   " * from the Linux v6.8.12 i915 reference (MIT; Copyright Intel Corporation -- the full notice is kept in" + NL +
   " * intel_display_port.c) by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_REF_INLINES_H" + NL + "#define PARITY_LCD_REF_INLINES_H" + NL + NL + inl + NL +
   "#endif /* PARITY_LCD_REF_INLINES_H */" + NL)

def span(text, a, b, src, label):
    """from the single occurrence of `a` to the end of the first later line containing `b`"""
    assert text.count(a) == 1, (label, "start", text.count(a))
    s0 = text.index(a)
    e0 = text.index(NL, text.index(b, s0)) + 1
    manifest["kept"].setdefault(src, []).append(label)
    return text[s0:e0]

regs = (lines_matching(rg0, r"^#define _TRANS_MULT_A\s", "i915_reg.h", "_TRANS_MULT_A", 1) +
        lines_matching(rg0, r"^#define TRANS_MULT\(trans\)", "i915_reg.h", "TRANS_MULT", 1) + NL +
        span(rg0, "/* VRR registers */", "define   VRR_FLIPLINE_MASK", "i915_reg.h", "TRANS_VRR_* registers") + NL +
        span(rg0, "#define _TRANSACONF", "TRANSCONF_DITHER_TYPE_TEMP", "i915_reg.h", "TRANSCONF bits") +
        lines_matching(rg0, r"^#define TRANSCONF\(trans\)", "i915_reg.h", "TRANSCONF", 1) + NL +
        span(rg0, "#define _CHICKEN_TRANS_A", "PSR2_VSC_ENABLE_PROG_HEADER", "i915_reg.h", "CHICKEN_TRANS") + NL +
        span(rg0, "/* Per-pipe DDI Function Control */", "define  PORT_SYNC_MODE_MASTER_SELECT(x)", "i915_reg.h", "TRANS_DDI_FUNC_CTL / CTL2") + NL +
        span(rg0, "#define _DDI_BUF_CTL_A", "DDI_INIT_DISPLAY_DETECTED", "i915_reg.h", "DDI_BUF_CTL") + NL +
        span(rg0, "#define _TRANSA_MSA_MISC", "#define TRANS_MSA_MISC(tran)", "i915_reg.h", "TRANS_MSA_MISC"))
wr("lcd_ddi_regs.h", "/*" + NL +
   " * zedBSD WS031: the TRANS_MULT, TRANS_VRR_*, TRANSCONF, CHICKEN_TRANS, TRANS_DDI_FUNC_CTL[2], DDI_BUF_CTL and" + NL +
   " * TRANS_MSA_MISC register definitions extracted textually from the Linux v6.8.12 i915 reference (i915_reg.h:" + NL +
   " * MIT permission notice; Copyright Intel Corporation -- the full notice is kept in intel_ddi_port.c) by" + NL +
   " * tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_DDI_REGS_H" + NL + "#define PARITY_LCD_DDI_REGS_H" + NL + NL + regs + NL +
   "#endif /* PARITY_LCD_DDI_REGS_H */" + NL)

msa = span(dph, "#define DP_MSA_MISC_SYNC_CLOCK", "#define DP_MSA_MISC_COLOR_VSC_SDP", "drm_dp.h", "DP_MSA_MISC_*")
lic = dph[:first_comment_end(dph)]
wr("lcd_dp_msa.h", lic + NL + NL + "/*" + NL +
   " * zedBSD WS031: the DP_MSA_MISC_* definitions extracted textually from Linux v6.8.12" + NL +
   " * include/drm/display/drm_dp.h (sha256 " + manifest["sources"]["drm_dp.h"] + ") by" + NL +
   " * tools/port_lcd_calc.py.  The copyright / permission notice above is the source file's own." + NL +
   " * Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_DP_MSA_H" + NL + "#define PARITY_LCD_DP_MSA_H" + NL + NL + msa + NL + "#endif /* PARITY_LCD_DP_MSA_H */" + NL)

cs = between(ch, "enum drm_colorspace {", NL + "};" + NL, "drm_connector.h", "enum drm_colorspace")
lic = ch[:first_comment_end(ch)]
wr("lcd_drm_colorspace.h", lic + NL + NL + "/*" + NL +
   " * zedBSD WS031: enum drm_colorspace extracted textually from Linux v6.8.12 include/drm/drm_connector.h" + NL +
   " * (sha256 " + manifest["sources"]["drm_connector.h"] + ") by tools/port_lcd_calc.py." + NL +
   " * The copyright / permission notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_DRM_COLORSPACE_H" + NL + "#define PARITY_LCD_DRM_COLORSPACE_H" + NL + NL + cs + NL +
   "#endif /* PARITY_LCD_DRM_COLORSPACE_H */" + NL)

'''
rep('open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")', new + 'open(os.path.join(out, "port_lcd_calc.manifest.json"), "w")')
rep('(?:static |const |struct |enum |bool |int |void |u8 |u32 )', '(?:static |const |struct |enum |bool |int |void |u8 |u32 |i915_reg_t )')
open(p, "w").write(s)
print("generator patched")
