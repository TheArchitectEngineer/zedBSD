#!/usr/bin/env python3
"""WS031 E-124 round 97: the structures the readout fills (port DPLL choice, PLL hardware state, the DBUF state the
watermark readout writes), and the declarations that clashed.
usage: round97.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chl = chr(10)
TAB = chr(9)

# ---- lcd_compat.h: the fields a readout fills ----
p = L + "lcd_compat.h"
s = open(p).read()
# my duplicate of a field the crtc already had
dup = (TAB + "/* the power domains the READOUT found this crtc using (intel_modeset_setup.c) */" + NL +
       TAB + "struct intel_power_domain_mask hw_readout_power_domains;" + NL +
       TAB + "struct intel_power_domain_mask enabled_power_domains;" + NL)
if s.count("struct intel_power_domain_mask enabled_power_domains;") > 1:
    s = s.replace(dup, TAB + "/* the power domains the READOUT found this crtc using (intel_modeset_setup.c) */" + NL +
                  TAB + "struct intel_power_domain_mask hw_readout_power_domains;" + NL, 1)
if "icl_port_dplls" not in s:
    s = s.replace("struct intel_crtc_state {", (
        "/* intel_display_types.h: which PLL of a port a crtc uses, and the state read back from it */" + NL +
        "enum icl_port_dpll_id { ICL_PORT_DPLL_DEFAULT, ICL_PORT_DPLL_MG_PHY, ICL_PORT_DPLL_COUNT };" + NL +
        "struct icl_port_dpll { struct intel_shared_dpll *pll; struct intel_dpll_hw_state hw_state; };" + NL +
        "struct intel_crtc_state {" + NL +
        TAB + "struct icl_port_dplls_dummy { int unused; } *icl_port_dplls_unused;" + NL +
        TAB + "struct icl_port_dpll icl_port_dplls[ICL_PORT_DPLL_COUNT];" + NL +
        TAB + "struct intel_dpll_hw_state dpll_hw_state;"), 1)
open(p, "w").write(s)

# ---- lcd_modeset_compat.h: the PLL's readout hook and the device's DBUF state ----
p = L + "lcd_modeset_compat.h"
s = open(p).read()
if "get_hw_state" not in s.split("struct intel_shared_dpll_funcs")[1][:400]:
    s = s.replace("struct intel_shared_dpll_funcs {",
                  "struct intel_shared_dpll_funcs {" + NL +
                  TAB + "bool (*get_hw_state)(struct drm_i915_private *i915, struct intel_shared_dpll *pll," + NL +
                  TAB * 2 + "struct intel_dpll_hw_state *hw_state);" + NL +
                  TAB + "int (*get_freq)(struct drm_i915_private *i915, const struct intel_shared_dpll *pll," + NL +
                  TAB * 2 + "const struct intel_dpll_hw_state *pll_state);", 1)
open(p, "w").write(s)

# ---- n1_compat.h: fix what clashed ----
p = L + "n1_compat.h"
s = open(p).read()
s = s.replace("void assert_enabled_transcoders(struct drm_i915_private *i915, u8 enabled_transcoders);" + NL, "")
s = s.replace("#define intel_display_power_get_in_set_if_enabled(i915, set, domain) " + NL +
              TAB + "(parity_n1_power_get_in_set_if_enabled(&(set)->mask, (domain)), true)",
              "#define intel_display_power_get_in_set_if_enabled(i915, set, domain) " + NL +
              TAB + "(parity_n1_power_get_in_set_if_enabled((set), (domain)), true)")
s = s.replace("void parity_n1_power_get_in_set_if_enabled(struct intel_power_domain_mask *mask,",
              "void parity_n1_power_get_in_set_if_enabled(struct intel_display_power_domain_set *set,")
s = s.replace("#define intel_display_power_put_all_in_set(i915, set) parity_n1_power_put_all_in_set(&(set)->mask)",
              "#define intel_display_power_put_all_in_set(i915, set) parity_n1_power_put_all_in_set(set)")
s = s.replace("void parity_n1_power_put_all_in_set(struct intel_power_domain_mask *mask);",
              "void parity_n1_power_put_all_in_set(struct intel_display_power_domain_set *set);")
if "display.dbuf" not in s:
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */",
                  "/* the DBUF state the watermark readout writes: the device's own (parity_wm_glue.inc) */" + NL +
                  "#define intel_atomic_get_dbuf_state(state) (&parity_lcd_wm->new_dbuf)" + NL + NL +
                  "#endif /* PARITY_N1_COMPAT_H */")
open(p, "w").write(s)
print("done")
