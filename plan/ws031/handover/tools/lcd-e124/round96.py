#!/usr/bin/env python3
"""WS031 E-124 round 96: the readout text's cross-unit declarations, and the last unported callees.
The generated readout functions live in the unit of their reference file, so the units that call them need their
prototypes; the remaining callees belong to subsystems this path does not port and are recorded steps.
usage: round96.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

# the duplicate field the crtc already had
p = L + "lcd_compat.h"
s = open(p).read()
first = s.find("struct intel_power_domain_mask enabled_power_domains;")
second = s.find("struct intel_power_domain_mask enabled_power_domains;", first + 1)
if second != -1:
    s = s[:second] + s[second:].replace(TAB + "struct intel_power_domain_mask enabled_power_domains;" + NL, "", 1)
    open(p, "w").write(s)

p = L + "n1_compat.h"
s = open(p).read()
if "parity_n1_protos" not in s:
    add = (NL + "/* ---- prototypes of the generated readout functions the other units call (parity_n1_protos) ---- */" + NL +
           "struct intel_crtc_state;" + NL +
           "void intel_cpu_transcoder_get_m1_n1(struct intel_crtc *crtc, enum transcoder cpu_transcoder," + NL +
           TAB + "struct intel_link_m_n *m_n);" + NL +
           "void intel_cpu_transcoder_get_m2_n2(struct intel_crtc *crtc, enum transcoder cpu_transcoder," + NL +
           TAB + "struct intel_link_m_n *m_n);" + NL +
           "int intel_crtc_dotclock(const struct intel_crtc_state *pipe_config);" + NL +
           "bool intel_pipe_is_interlaced(const struct intel_crtc_state *crtc_state);" + NL +
           "void intel_plane_disable_noatomic(struct intel_crtc *crtc, struct intel_plane *plane);" + NL +
           "void assert_enabled_transcoders(struct drm_i915_private *i915, u8 enabled_transcoders);" + NL +
           "struct intel_shared_dpll *intel_get_shared_dpll_by_id(struct drm_i915_private *i915, enum intel_dpll_id id);" + NL +
           "bool intel_dpll_get_hw_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll," + NL +
           TAB + "struct intel_dpll_hw_state *hw_state);" + NL +
           "int intel_dpll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll," + NL +
           TAB + "const struct intel_dpll_hw_state *pll_state);" + NL +
           "bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb, const struct skl_ddb_entry *entries," + NL +
           TAB + "int num_entries, int ignore_idx);" + NL +
           "u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915, const struct skl_ddb_entry *entry);" + NL +
           "bool skl_ddb_entries_overlap(const struct skl_ddb_entry *a, const struct skl_ddb_entry *b);" + NL + NL +
           "/* ---- the last callees of other subsystems: recorded steps ---- */" + NL +
           "#define intel_color_get_config(cs) N1_STEP(\"intel_color_get_config\")" + NL +
           "#define intel_psr_get_config(encoder, cs) N1_STEP(\"intel_psr_get_config\")" + NL +
           "#define intel_audio_codec_get_config(encoder, cs) N1_STEP(\"intel_audio_codec_get_config\")" + NL +
           "#define intel_dp_sync_state(encoder, cs) N1_STEP(\"intel_dp_sync_state\")" + NL +
           "#define intel_tc_port_sanitize_mode(dig_port, cs) N1_STEP(\"intel_tc_port_sanitize_mode\")" + NL +
           "#define icl_set_active_port_dpll(cs, port_dpll_id) N1_STEP(\"icl_set_active_port_dpll\")" + NL +
           "#define intel_dpll_readout_hw_state parity_n1_dpll_readout_hw_state" + NL)
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */", add + NL + "#endif /* PARITY_N1_COMPAT_H */")
    open(p, "w").write(s)
print("done")
