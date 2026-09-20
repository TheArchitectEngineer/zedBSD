#!/usr/bin/env python3
"""E-123 round 87 fixup: the pool's reset and the pipe release were inside the already-applied block."""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)
p = L + "parity_dpll_glue.inc"
s = open(p).read()
add = ""
if "parity_lcd_ms_release_pipe" not in s:
    add += ("/* the pipe's reference on every PLL of the pool goes back (intel_release_shared_dplls) */" + NL +
            "void parity_lcd_ms_release_pipe(enum pipe pipe)" + NL + "{" + NL +
            TAB + "int i;" + NL + NL +
            TAB + "for (i = 0; i < 2; i++) {" + NL +
            TAB * 2 + "parity_lcd_dpll_pool_state[i].pipe_mask &= (u8)~BIT(pipe);" + NL +
            TAB * 2 + "parity_lcd_dpll_pool[i].state.pipe_mask &= (u8)~BIT(pipe);" + NL +
            TAB + "}" + NL + "}" + NL + NL)
if "parity_lcd_dplls_reset" not in s:
    add += ("/* the device is (re)created: the pool starts empty, as intel_shared_dpll_init() leaves it */" + NL +
            "void parity_lcd_dplls_reset(void)" + NL + "{" + NL +
            TAB + "int i;" + NL + NL +
            TAB + "parity_lcd_dpll_pool_inited = 0;" + NL +
            TAB + "for (i = 0; i < 2; i++) {" + NL +
            TAB * 2 + "memset(&parity_lcd_dpll_pool[i], 0, sizeof(parity_lcd_dpll_pool[i]));" + NL +
            TAB * 2 + "memset(&parity_lcd_dpll_pool_state[i], 0, sizeof(parity_lcd_dpll_pool_state[i]));" + NL +
            TAB + "}" + NL + "}" + NL + NL)
if add:
    anchor = "int parity_lcd_ms_alloc_pll(struct parity_lcd_modeset *ms, const struct intel_dpll_hw_state *hw_state)"
    assert s.count(anchor) == 1
    s = s.replace(anchor, add + anchor)
# the allocation drops this pipe's earlier references first (intel_release_shared_dplls)
old = TAB + "parity_lcd_dpll_pool_init(ms);" + NL
if "parity_lcd_ms_release_pipe(ms->crtc.pipe);" not in s:
    assert s.count(old) == 1
    s = s.replace(old, old + TAB + "parity_lcd_ms_release_pipe(ms->crtc.pipe);" + NL)
open(p, "w").write(s)
print("fixed")
