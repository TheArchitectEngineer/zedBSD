#!/usr/bin/env python3
"""WS031: generate the first slice of the LCD state calculation from the fixed references.

  <out>/drm_edid_mode_port.c   drm_edid.c: the EDID quirk bits, drm_mode_do_interlace_quirk, drm_mode_detailed
  <out>/edid_ref_types.h       drm_edid.h: the EDID block structures and DRM_EDID_PT_* bits (textual extract)
  <out>/intel_link_port.c      intel_dp.c: link symbol size / clock, link_required, effective_data_rate, max_data_rate
  <out>/intel_display_port.c   intel_display.c: M/N computation; the WRITERS of the transcoder timing, pipe source
                               and M/N registers (their writes go through an emit hook)
  <out>/drm_dp_bw_port.c       drm_dp_helper.[ch]: the two channel-coding helpers
  <out>/drm_modes_port.c       drm_modes.c: drm_mode_set_crtcinfo
  <out>/lcd_trans_regs.h       enum transcoder + the register definitions those writers use (textual ranges)
  <out>/intel_dpll_port.c      intel_dpll_mgr.c: skl_wrpll_params, the ICL+ DP combo PLL tables,
                               ehl_combo_pll_div_frac_wa_needed, icl_calc_dp_combo_pll, icl_calc_dpll_state
  <out>/lcd_ref_types.h        struct intel_link_m_n, struct intel_dpll_hw_state, the DPLL_CFGCR / M-N macros
  <out>/intel_ddi_port.c       intel_ddi.c: DDI_BUF_CTL value, TRANS_MSA_MISC, TRANS_DDI_FUNC_CTL[2], hsw_chicken_trans_reg
  <out>/intel_vrr_port.c       intel_vrr.c: trans_vrr_ctl, intel_vrr_set_transcoder_timings
  <out>/lcd_ddi_types.h        enum port / phy / intel_output_type / intel_output_format (textual extract)
  <out>/lcd_ref_inlines.h      intel_crtc_has_type, intel_crtc_has_dp_encoder, intel_crtc_needs_modeset, transcoder_is_dsi
  <out>/lcd_ddi_regs.h         TRANS_DDI_FUNC_CTL[2], DDI_BUF_CTL, TRANS_MSA_MISC, TRANSCONF, TRANS_MULT, TRANS_VRR_*, CHICKEN_TRANS
  <out>/lcd_dp_msa.h           drm_dp.h: DP_MSA_MISC_* (textual extract)
  <out>/lcd_drm_colorspace.h   drm_connector.h: enum drm_colorspace (textual extract)
  <out>/skl_plane_port.c       skl_universal_plane.c: PLANE_CTL / COLOR_CTL / stride / surf / keys and the
                               icl_plane_update_noarm / _arm writers
  <out>/lcd_plane_types.h      enum plane_id; drm_intel_sprite_colorkey + I915_SET_COLORKEY_* (i915_drm.h)
  <out>/lcd_plane_regs.h       i915_reg.h: the Skylake+ plane register block
  <out>/lcd_psr_selfetch_regs.h intel_psr_regs.h: PLANE_SEL_FETCH_*
  <out>/lcd_drm_fourcc.h       drm_fourcc.h, whole (one include line dropped)
  <out>/lcd_drm_plane_defs.h   drm_blend.h / drm_mode.h / drm_color_mgmt.h: blend modes, rotation bits, colour enums

Function bodies are NOT retyped.  Every kept part is named in the generated file's header; a part
that is not found exactly once, or a substitution that does not apply exactly once, fails the run.

usage: port_lcd_calc.py <i915 reference dir (ubu-i915-src)> <drm reference dir> <output dir>
"""
import hashlib, json, os, re, sys

ref, drmref, out = sys.argv[1], sys.argv[2], sys.argv[3]
NL, TAB = chr(10), chr(9)
manifest = {"generator": "port_lcd_calc.py", "sources": {}, "outputs": {}, "kept": {}, "substitutions": []}

def rd(d, name):
    t = open(os.path.join(d, name)).read()
    manifest["sources"][name] = hashlib.sha256(t.encode()).hexdigest()
    return t

def wr(name, text):
    open(os.path.join(out, name), "w").write(text)
    manifest["outputs"][name] = hashlib.sha256(text.encode()).hexdigest()
    print("generated", name, len(text.split(NL)), "lines")

def func(text, name, src):
    """the definition of `name` (with a directly preceding comment), which must exist exactly once"""
    pat = re.compile(r"^(?:static |const |struct |enum |bool |int |void |u8 |u32 |i915_reg_t )[^\n;{]*\b" + re.escape(name) + r"\(", re.M)
    hits = []
    for cand in pat.finditer(text):
        brace, semi = text.find("{", cand.end()), text.find(";", cand.end())
        if brace != -1 and (semi == -1 or brace < semi):
            hits.append(cand.start())
    for cand in re.finditer(r"^" + re.escape(name) + r"\(", text, re.M):      # return type on the previous line
        brace, semi = text.find("{", cand.end()), text.find(";", cand.end())
        if brace != -1 and (semi == -1 or brace < semi):
            hits.append(text.rfind(NL, 0, cand.start() - 1) + 1)
    assert len(hits) == 1, (name, len(hits))
    start = hits[0]
    end = text.index(NL + "}" + NL, start) + 3
    pre = text[:start].rstrip(NL)
    if pre.endswith("*/"):
        c = pre.rfind("/*")
        seg = text[c:start]
        if seg.count(NL) < 60 and (NL + NL) not in seg.rstrip(NL):
            start = c
    manifest["kept"].setdefault(src, []).append(name)
    return text[start:end]

SPEC_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "port_lcd_modeset.json")
SPEC = json.load(open(SPEC_PATH))
manifest["sources"]["port_lcd_modeset.json"] = hashlib.sha256(open(SPEC_PATH, "rb").read()).hexdigest()

def extra(text, srcname, src):
    """the EXTRA functions port_lcd_modeset.json lists for this reference file"""
    ranges = "".join(between(text, r[0], r[1], src, r[2]) + NL for r in SPEC.get("extra_ranges", {}).get(srcname, []))
    return ranges + "".join(func(text, n, src) + NL for n in SPEC["extra"].get(srcname, []))

def protos(body):
    """`body` with forward declarations of its static functions inserted before the first function definition
    (generated text, not reference text; types and tables the keep-list put first stay in front)"""
    sig = r"(?m)^static [^;{}=]*?BS([^;{}]*?BS)BSs*BSnBS{".replace("BS", chr(92))
    out = []
    for m in re.finditer(sig, body):
        head = body[m.start():m.end()].rsplit("{", 1)[0].rstrip()
        if " inline " not in head.split("(")[0] + " ":
            out.append(head + ";")
    if not out:
        return body
    anyfn = r"(?m)^(?:static |const |struct |enum |bool |int |void |u8 |u32 |i915_reg_t )[^;{}=]*?BS([^;{}]*?BS)BSs*BSnBS{".replace("BS", chr(92))
    first = re.search(anyfn, body)
    at = first.start() if first else 0
    pre = body[:at].rstrip(NL)
    if pre.endswith("*/"):                      # keep a directly preceding comment with its function
        c = pre.rfind("/*")
        if (NL + NL) not in body[c:at].rstrip(NL):
            at = c
    return (body[:at] + "/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */" + NL +
            NL.join(out) + NL + NL + body[at:])

def macro_closure(text, roots, src, label, exclude=()):
    """the #define blocks (with continuation lines) of `roots` and of every macro of `text` they use, in file order"""
    defs, order = {}, []
    for m in re.finditer(r"(?m)^#[ \t]*define[ \t]+([A-Za-z_]\w*)(?:.*\\\n)*.*\n", text):
        if m.group(1) not in defs:
            defs[m.group(1)] = m.group(0)
            order.append(m.group(1))
    want, todo = set(), [r for r in roots]
    for r in roots:
        assert r in defs, (label, "no such macro", r)
    while todo:
        n = todo.pop()
        if n in want:
            continue
        want.add(n)
        for tok in set(re.findall(r"[A-Za-z_]\w*", defs[n].split(None, 2)[2] if len(defs[n].split(None, 2)) > 2 else "")):
            if tok in defs and tok not in want and tok not in exclude:
                todo.append(tok)
    manifest["kept"].setdefault(src, []).append(label + ": " + str(len(want)) + " macros from " + str(len(roots)) + " roots")
    return "".join(defs[n] for n in order if n in want)

def between(text, a, b, src, label):
    assert text.count(a) == 1, (label, "start", text.count(a))
    s = text.index(a)
    e = text.index(b, s) + len(b)
    manifest["kept"].setdefault(src, []).append(label)
    return text[s:e]

def lines_matching(text, regex, src, label, expect):
    got = [l for l in text.split(NL) if re.search(regex, l)]
    assert len(got) == expect, (label, len(got), expect)
    manifest["kept"].setdefault(src, []).append(label)
    return NL.join(got) + NL

def sub(text, old, new, label):
    n = text.count(old)
    assert n == 1, (label, n)
    manifest["substitutions"].append({"what": label, "applied": n})
    return text.replace(old, new)

def note(src_path, digest, lines):
    t = NL + NL + "/*" + NL + " * zedBSD WS031: generated from " + src_path + NL
    t += " * (sha256 " + digest + ") by plan/ws031/handover/tools/port_lcd_calc.py." + NL
    t += " * The function bodies are the reference text.  Kept / changed:" + NL
    for l in lines:
        t += " *" + (" " + l if l else "") + NL
    return t + " */" + NL

def first_comment_end(text):
    """end of the source's leading notice: the first comment, plus the next one when the first is only the SPDX line"""
    e = text.index("*/") + 2
    if text[:e].lstrip().startswith("/* SPDX-License-Identifier") and text[e:].lstrip().startswith("/*"):
        e = text.index("*/", e) + 2
    return e

# ======================================================================= EDID types
eh = rd(drmref, "drm_edid.h")
parts = [
    between(eh, "struct est_timings {", "} __attribute__((packed));" + NL, "drm_edid.h", "struct est_timings"),
    lines_matching(eh, r"^#define EDID_TIMING_(ASPECT|VFREQ)_", "drm_edid.h", "EDID_TIMING_* masks", 4),
    between(eh, "struct std_timing {", "} __attribute__((packed));" + NL, "drm_edid.h", "struct std_timing"),
    lines_matching(eh, r"^#define DRM_EDID_PT_", "drm_edid.h", "DRM_EDID_PT_* bits", 5),
    between(eh, "/* If detailed data is pixel timing */", "struct detailed_timing {", "drm_edid.h",
            "detailed_pixel_timing .. detailed_non_pixel"),
]
dt = between(eh, "struct detailed_timing {", "} __attribute__((packed));" + NL, "drm_edid.h", "struct detailed_timing")
ed = between(eh, "struct edid {", "} __attribute__((packed));" + NL, "drm_edid.h", "struct edid")
body = NL.join(parts)
# `between` for the pixel..non_pixel range ends with the opening line of detailed_timing: drop it, add the full struct
body = body[:body.rindex("struct detailed_timing {")] + dt + NL
body += lines_matching(eh, r"^#define DRM_EDID_(INPUT|FEATURE|DIGITAL|YCBCR|HDMI_DC|DSC)_", "drm_edid.h",
                       "DRM_EDID_* feature / input bits", len(re.findall(r"(?m)^#define DRM_EDID_(?:INPUT|FEATURE|DIGITAL|YCBCR|HDMI_DC|DSC)_", eh)))
body += NL + ed
lic = eh[:first_comment_end(eh)]
wr("edid_ref_types.h", lic + NL + NL + "/*" + NL +
   " * zedBSD WS031: EDID block structures and bit definitions extracted textually from Linux v6.8.12" + NL +
   " * include/drm/drm_edid.h (sha256 " + manifest["sources"]["drm_edid.h"] + ") by" + NL +
   " * tools/port_lcd_calc.py.  The copyright / permission notice above is the source file's own." + NL +
   " * Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_EDID_REF_TYPES_H" + NL + "#define PARITY_EDID_REF_TYPES_H" + NL + NL + body + NL +
   "#endif /* PARITY_EDID_REF_TYPES_H */" + NL)

# ======================================================================= drm_edid.c: detailed timing -> mode
ec = rd(drmref, "drm_edid.c")
quirks = lines_matching(ec, r"^#define EDID_QUIRK_[A-Z0-9_]+\s+\(1 << \d+\)", "drm_edid.c", "EDID_QUIRK_* bits", 13)
body = quirks + NL + func(ec, "drm_mode_do_interlace_quirk", "drm_edid.c") + NL + func(ec, "drm_mode_detailed", "drm_edid.c")
lic = ec[:first_comment_end(ec)]
wr("drm_edid_mode_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/drm_edid.c", manifest["sources"]["drm_edid.c"], [
    " - kept, in the reference order: the EDID_QUIRK_* bit definitions, drm_mode_do_interlace_quirk(),",
    "   drm_mode_detailed();",
    " - the includes are replaced by lcd_compat.h (connector / mode objects, logging, drm_mode_create);",
    " - parity_edid_mode_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and drm/ includes */" + NL + NL + body + NL +
   '#include "parity_edid_mode_glue.inc"' + TAB + "/* zedBSD: picks the preferred detailed timing */" + NL)

# ======================================================================= link budget, M/N, register emission
# One generated file per SOURCE file, so every file carries exactly the notice of the text inside it.
dpc = rd(os.path.join(ref, "display"), "intel_dp.c")
dis = rd(os.path.join(ref, "display"), "intel_display.c")
hc = rd(drmref, "drm_dp_helper.c")
hh = rd(drmref, "drm_dp_helper.h")
dm = rd(drmref, "drm_modes.c")

# ---- drm_dp_helper.[ch]: the channel-coding helpers
body = (func(hh, "drm_dp_is_uhbr_rate", "drm_dp_helper.h") + NL +
        func(hc, "drm_dp_bw_channel_coding_efficiency", "drm_dp_helper.c") + NL)
lic = hc[:first_comment_end(hc)]
wr("drm_dp_bw_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c", manifest["sources"]["drm_dp_helper.c"], [
    " - kept: drm_dp_bw_channel_coding_efficiency (drm_dp_helper.c) and the inline drm_dp_is_uhbr_rate",
    "   (include/drm/display/drm_dp_helper.h, sha256 " + manifest["sources"]["drm_dp_helper.h"] + ",",
    "   same copyright holder and permission notice as above);",
    " - the includes are replaced by lcd_compat.h; drm_dp_is_uhbr_rate loses `static inline` so the",
    "   other generated files can call it."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and drm/ includes */" + NL + NL +
   sub(body, "static inline bool drm_dp_is_uhbr_rate(int link_rate)", "bool drm_dp_is_uhbr_rate(int link_rate)",
       "drm_dp_is_uhbr_rate: static inline -> external"))

# ---- intel_dp.c: link budget
body = ""
for n in ("intel_dp_is_uhbr", "intel_dp_link_symbol_size", "intel_dp_link_symbol_clock", "intel_dp_link_required",
          "intel_dp_effective_data_rate", "intel_dp_max_data_rate", "intel_dp_needs_vsc_sdp"):
    body += func(dpc, n, "display/intel_dp.c") + NL
body += extra(dpc, "intel_dp.c", "display/intel_dp.c")
body = protos(body)
lic = dpc[:first_comment_end(dpc)]
wr("intel_link_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c", manifest["sources"]["intel_dp.c"], [
    " - kept: intel_dp_is_uhbr, intel_dp_link_symbol_size, intel_dp_link_symbol_clock, intel_dp_link_required,",
    "   intel_dp_effective_data_rate, intel_dp_max_data_rate, intel_dp_needs_vsc_sdp;",
    " - the includes are replaced by lcd_compat.h."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_dp_compat.h"' + NL + NL + body)

# ---- intel_display.c: M/N computation, and the functions that WRITE the transcoder / pipe source / M-N registers
body = ""
for n in ("intel_phy_is_tc", "intel_port_to_phy",
          "intel_reduce_m_n_ratio", "compute_m_n", "intel_link_compute_m_n", "intel_set_m_n",
          "intel_cpu_transcoder_has_m2_n2", "intel_cpu_transcoder_set_m1_n1", "intel_cpu_transcoder_set_m2_n2",
          "intel_set_transcoder_timings", "intel_set_pipe_src_size",
          "hsw_set_frame_start_delay", "hsw_set_transconf", "hsw_configure_cpu_transcoder", "hsw_crtc_enable"):
    body += func(dis, n, "display/intel_display.c") + NL
body += extra(dis, "intel_display.c", "display/intel_display.c")
body = protos(body)
lic = dis[:first_comment_end(dis)]
wr("intel_display_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display.c", manifest["sources"]["intel_display.c"], [
    " - kept: intel_phy_is_tc, intel_port_to_phy, intel_reduce_m_n_ratio, compute_m_n, intel_link_compute_m_n,",
    "   intel_set_m_n, intel_cpu_transcoder_has_m2_n2, intel_cpu_transcoder_set_m1_n1,",
    "   intel_cpu_transcoder_set_m2_n2, intel_set_transcoder_timings, intel_set_pipe_src_size,",
    "   hsw_set_frame_start_delay, hsw_set_transconf, their caller hsw_configure_cpu_transcoder, and ITS caller",
    "   hsw_crtc_enable (so the order between the writers and the steps around them is the reference's);",
    " - callees of hsw_crtc_enable that are not ported are named steps (lcd_seq_compat.h), never dropped;",
    " - the includes are replaced by lcd_compat.h (register writes go through its emit hook, so the",
    "   same text fills a word list in the tests and, later, the hardware);",
    " - parity_display_emit_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + NL + body + NL +
   '#include "parity_display_emit_glue.inc"' + TAB + "/* zedBSD: builds the crtc state and records the words */" + NL)

# ---- drm_modes.c: crtc timing derivation
lic = dm[:first_comment_end(dm)]
wr("drm_modes_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/drm_modes.c", manifest["sources"]["drm_modes.c"], [
    " - kept: drm_mode_set_crtcinfo;",
    " - the includes are replaced by lcd_compat.h; the EXPORT_SYMBOL line is dropped."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and drm/ includes */" + NL + NL +
   func(dm, "drm_mode_set_crtcinfo", "drm_modes.c") + NL)

# ---- register definitions the emission needs (textual ranges of i915_reg.h) + enum transcoder
rg0 = rd(ref, "i915_reg.h")
lim = rd(os.path.join(ref, "display"), "intel_display_limits.h")
trans = (between(lim, "enum transcoder {", NL + "};" + NL, "display/intel_display_limits.h", "enum transcoder") + NL +
         between(rg0, "/* Pipe/transcoder A timing regs */", "#define TRANS_MULT(trans)" + TAB + "_MMIO_TRANS2((trans), _TRANS_MULT_A)" + NL,
                 "i915_reg.h", "transcoder timing registers") + NL +
         between(rg0, "#define _PIPEA_DATA_M1" + TAB + TAB + "0x60030", "#define PIPE_LINK_N2(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_N2)" + NL,
                 "i915_reg.h", "PIPE_DATA/LINK M/N registers") + NL +
         between(rg0, "#define _TRANS_A_SET_CONTEXT_LATENCY" + TAB + TAB + "0x6007C",
                 "#define  TRANS_SET_CONTEXT_LATENCY_VALUE(x)" + TAB + "REG_FIELD_PREP(TRANS_SET_CONTEXT_LATENCY_MASK, (x))" + NL,
                 "i915_reg.h", "TRANS_SET_CONTEXT_LATENCY"))
wr("lcd_trans_regs.h", "/*" + NL +
   " * zedBSD WS031: `enum transcoder` and the transcoder timing / pipe source / M-N / context-latency register" + NL +
   " * definitions extracted textually from the Linux v6.8.12 i915 reference (display/intel_display_limits.h:" + NL +
   " * SPDX MIT; i915_reg.h: MIT permission notice; Copyright Intel Corporation -- the full notice is kept in" + NL +
   " * intel_display_port.c) by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_TRANS_REGS_H" + NL + "#define PARITY_LCD_TRANS_REGS_H" + NL + NL + trans + NL +
   "#endif /* PARITY_LCD_TRANS_REGS_H */" + NL)

# ======================================================================= combo PLL
pm = rd(os.path.join(ref, "display"), "intel_dpll_mgr.c")
body = (between(pm, "struct skl_wrpll_params {", "};" + NL, "display/intel_dpll_mgr.c", "struct skl_wrpll_params") + NL +
        func(pm, "ehl_combo_pll_div_frac_wa_needed", "display/intel_dpll_mgr.c") + NL +
        between(pm, "struct icl_combo_pll_params {", "};" + NL, "display/intel_dpll_mgr.c", "struct icl_combo_pll_params") + NL +
        between(pm, "/*" + NL + " * These values alrea already adjusted: they're the bits we write to the" + NL,
                "static const struct icl_combo_pll_params icl_dp_combo_pll_19_2MHz_values[] = {",
                "display/intel_dpll_mgr.c", "icl_dp_combo_pll_24MHz_values"))
t19 = between(pm, "static const struct icl_combo_pll_params icl_dp_combo_pll_19_2MHz_values[] = {", NL + "};" + NL,
              "display/intel_dpll_mgr.c", "icl_dp_combo_pll_19_2MHz_values")
body = body[:body.rindex("static const struct icl_combo_pll_params icl_dp_combo_pll_19_2MHz_values[] = {")] + t19 + NL
body += func(pm, "icl_calc_dp_combo_pll", "display/intel_dpll_mgr.c") + NL + func(pm, "icl_calc_dpll_state", "display/intel_dpll_mgr.c")
body += NL + extra(pm, "intel_dpll_mgr.c", "display/intel_dpll_mgr.c")
body = protos(body)
lic = pm[:first_comment_end(pm)]
wr("intel_dpll_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dpll_mgr.c", manifest["sources"]["intel_dpll_mgr.c"], [
    " - kept, in the reference order: struct skl_wrpll_params, ehl_combo_pll_div_frac_wa_needed,",
    "   struct icl_combo_pll_params, icl_dp_combo_pll_24MHz_values[], icl_dp_combo_pll_19_2MHz_values[],",
    "   icl_calc_dp_combo_pll, icl_calc_dpll_state;",
    " - the includes are replaced by lcd_compat.h;",
    " - parity_dpll_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and i915 includes */" + NL +
   '#include "lcd_modeset_compat.h"' + NL + NL + body + NL +
   '#include "parity_dpll_glue.inc"' + TAB + "/* zedBSD: entry point */" + NL)

# ======================================================================= lcd_ref_types.h
ty = rd(os.path.join(ref, "display"), "intel_display_types.h")
ph = rd(os.path.join(ref, "display"), "intel_dpll_mgr.h")
rg = rd(ref, "i915_reg.h")
body = (between(ty, "struct intel_link_m_n {", "};" + NL, "display/intel_display_types.h", "struct intel_link_m_n") + NL +
        between(ph, "struct intel_dpll_hw_state {", NL + "};" + NL, "display/intel_dpll_mgr.h", "struct intel_dpll_hw_state") + NL +
        lines_matching(rg, r"^#define\s+(DATA_LINK_M_N_MASK|DATA_LINK_N_MAX|TU_SIZE_MASK|TU_SIZE\(x\))\s", "i915_reg.h", "M/N limits", 4) +
        lines_matching(rg, r"^#define\s+(DPLL_CFGCR0_DCO_FRACTION\(x\)|DPLL_CFGCR1_QDIV_RATIO\(x\)|DPLL_CFGCR1_QDIV_MODE\(x\)|DPLL_CFGCR1_KDIV\(x\)|DPLL_CFGCR1_PDIV\(x\)|DPLL_CFGCR1_CENTRAL_FREQ_8400|TGL_DPLL_CFGCR1_CFSELOVRD_NORMAL_XTAL|TGL_DPLL0_DIV0_AFC_STARTUP_MASK|TGL_DPLL0_DIV0_AFC_STARTUP\(val\))",
                       "i915_reg.h", "DPLL_CFGCR / DIV0 fields", 9))
wr("lcd_ref_types.h", "/*" + NL +
   " * zedBSD WS031: structures and register-field macros extracted textually from the Linux v6.8.12 i915" + NL +
   " * reference (display/intel_display_types.h, display/intel_dpll_mgr.h, i915_reg.h: MIT permission" + NL +
   " * notices, Copyright Intel Corporation; the full notices are kept in intel_link_port.c and" + NL +
   " * intel_dpll_port.c) by tools/port_lcd_calc.py.  Do not edit by hand." + NL + " */" + NL +
   "#ifndef PARITY_LCD_REF_TYPES_H" + NL + "#define PARITY_LCD_REF_TYPES_H" + NL + NL + body + NL +
   "#endif /* PARITY_LCD_REF_TYPES_H */" + NL)


# ======================================================================= E-111: DDI / VRR writers, enums, inlines, registers
ddi = rd(os.path.join(ref, "display"), "intel_ddi.c")
vrr = rd(os.path.join(ref, "display"), "intel_vrr.c")
dh = rd(os.path.join(ref, "display"), "intel_display.h")
dph = rd(drmref, "drm_dp.h")
ch = rd(drmref, "drm_connector.h")

body = ""
for n in ("ddi_buf_phy_link_rate", "intel_ddi_init_dp_buf_reg", "intel_ddi_set_dp_msa", "bdw_trans_port_sync_master_select",
          "intel_ddi_transcoder_func_reg_val_get", "intel_ddi_enable_transcoder_func", "intel_ddi_config_transcoder_func",
          "hsw_chicken_trans_reg", "tgl_ddi_pre_enable_dp", "intel_ddi_pre_enable_dp", "intel_ddi_pre_enable",
          "intel_enable_ddi_dp", "intel_enable_ddi", "intel_ddi_pre_pll_enable"):
    body += func(ddi, n, "display/intel_ddi.c") + NL
body += extra(ddi, "intel_ddi.c", "display/intel_ddi.c")
body = protos(body)
lic = ddi[:first_comment_end(ddi)]
wr("intel_ddi_port.c", lic + note("Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c", manifest["sources"]["intel_ddi.c"], [
    " - kept: ddi_buf_phy_link_rate, intel_ddi_init_dp_buf_reg, intel_ddi_set_dp_msa,",
    "   bdw_trans_port_sync_master_select, intel_ddi_transcoder_func_reg_val_get,",
    "   intel_ddi_enable_transcoder_func, intel_ddi_config_transcoder_func, hsw_chicken_trans_reg, and the DP",
    "   enable-sequence callers tgl_ddi_pre_enable_dp, intel_ddi_pre_enable_dp, intel_ddi_pre_enable,",
    "   intel_enable_ddi_dp, intel_enable_ddi, intel_ddi_pre_pll_enable (their unported callees are named steps,",
    "   lcd_seq_compat.h);",
    " - the includes are replaced by lcd_compat.h (register writes go through its emit hook);",
    " - parity_ddi_emit_glue.inc (zedBSD code) is included at the end of the file."]) + NL +
   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/, drm/ and i915 includes */" + NL +
   '#include "lcd_trans_regs.h"' + NL + '#include "lcd_ddi_regs.h"' + NL + '#include "lcd_dp_msa.h"' + NL + '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_dp_compat.h"' + NL + NL + body + NL +
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
               "icl_plane_update_sel_fetch_arm", "icl_plane_update_arm", "icl_plane_disable_arm")
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

ck = (lines_matching(idr, r"^#define I915_SET_COLORKEY_(DESTINATION|SOURCE)\b", "i915_drm.h", "I915_SET_COLORKEY_*", 2) + NL +
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
                           "drm_fourcc.h: #include \"drm.h\" dropped"))

defs = (lines_matching(bl, r"^#define DRM_MODE_BLEND_", "drm_blend.h", "DRM_MODE_BLEND_*", 3) + NL +
        lines_matching(um, r"^#define DRM_MODE_(ROTATE|REFLECT)_(0|90|180|270|X|Y)\s", "uapi drm_mode.h", "DRM_MODE_ROTATE_* / REFLECT_*", 6) +
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


# ======================================================================= E-114: table-driven modeset bodies
for spec in SPEC["new_files"]:
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    body = ""
    for part in spec.get("ranges", []):
        body += between(stext, part[0], part[1], spec["source"], part[2]) + NL
    for n in spec["functions"]:
        body += func(stext, n, spec["source"]) + NL
    body = protos(body)
    lic = stext[:first_comment_end(stext)]
    text = lic + note("Linux v6.8.12 " + spec["path"], manifest["sources"][os.path.basename(spec["source"])],
        [" - kept: " + ", ".join(spec["functions"][i:i + 4]) + ("," if i + 4 < len(spec["functions"]) else ";")
         if i == 0 else "   " + ", ".join(spec["functions"][i:i + 4]) + ("," if i + 4 < len(spec["functions"]) else ";")
         for i in range(0, len(spec["functions"]), 4)] +
        [" - the includes are replaced by: " + ", ".join(spec["includes"]) + ";"] +
        ([" - " + spec["glue"] + " (zedBSD code) is included at the end of the file."] if spec.get("glue") else [])) + NL
    for inc in spec["includes"]:
        text += '#include "' + inc + '"' + NL
    text += NL + body
    if spec.get("glue"):
        text += NL + '#include "' + spec["glue"] + '"' + NL
    wr(spec["out"], text)

for spec in SPEC.get("range_headers", []):
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    guard = "PARITY_" + re.sub(r"\W", "_", spec["out"]).upper()
    lic = stext[:first_comment_end(stext)]
    body = NL.join(between(stext, part[0], part[1], spec["source"], part[2]) for part in spec["ranges"])
    wr(spec["out"], lic + NL + NL + "/*" + NL +
       " * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference " + spec["path"] + NL +
       " * (sha256 " + manifest["sources"][os.path.basename(spec["source"])] + ") by tools/port_lcd_calc.py: " + NL +
       " * " + ", ".join(part[2] for part in spec["ranges"]) + "." + NL +
       " * The notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
       "#ifndef " + guard + NL + "#define " + guard + NL + NL + body + NL + "#endif /* " + guard + " */" + NL)

for spec in SPEC["macro_headers"]:
    base = ref if spec["dir"] == "i915" else drmref
    stext = rd(os.path.join(base, os.path.dirname(spec["source"])), os.path.basename(spec["source"]))
    guard = "PARITY_" + re.sub(r"\W", "_", spec["out"]).upper()
    lic = stext[:first_comment_end(stext)]
    wr(spec["out"], lic + NL + NL + "/*" + NL +
       " * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference " + spec["path"] + NL +
       " * (sha256 " + manifest["sources"][os.path.basename(spec["source"])] + ") by tools/port_lcd_calc.py:" + NL +
       " * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use." + NL +
       " * The notice above is the source file's own.  Do not edit by hand." + NL + " */" + NL +
       "#ifndef " + guard + NL + "#define " + guard + NL + NL +
       macro_closure(stext, spec["roots"], spec["source"], spec["out"], spec.get("exclude", [])) + NL + "#endif /* " + guard + " */" + NL)

open(os.path.join(out, "port_lcd_calc.manifest.json"), "w").write(json.dumps(manifest, indent=1, sort_keys=True) + NL)
print("manifest written")
