#!/usr/bin/env python3
"""WS031 E-116 round 28: the crtc state lacked the reference's dither decision (intel_modeset_pipe_config():
dither = pipe_bpp == 6*3 && !dither_force_disable).  Found from the first picture: PIPE_MISC was written without
dither although Linux shows "dither=yes, bpp=18" for this pipe.  usage: round28.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

r = load(L + "parity_lcd_modeset.c")
r = rep(r, "	ms.crtc_state.pipe_bpp = s->link.bpp;" + NL, "	ms.crtc_state.pipe_bpp = s->link.bpp;" + NL +
        "	/* intel_modeset_pipe_config(): \"Dithering seems to not pass-through bits correctly when it should, so only enable it" + NL +
        "	 * on 6bpc panels\"; dither_force_disable is set by DP compliance tests only */" + NL +
        "	ms.crtc_state.dither = ms.crtc_state.pipe_bpp == 6 * 3;" + NL)
save(L + "parity_lcd_modeset.c", r)
t = load("plan/ws031/tests/lcd-modeset-host-test.c")
t = rep(t, '	CHECK((lcd_fake_reg(&lcd, 0x164280) & 0x403u) == 0u, "A: DPCLKA_CFGCR0: DDI A takes DPLL0 and its clock is ungated");',
        '	CHECK((lcd_fake_reg(&lcd, 0x164280) & 0x403u) == 0u, "A: DPCLKA_CFGCR0: DDI A takes DPLL0 and its clock is ungated");' + NL +
        '	printf("  A: PIPE_MISC_A=0x%08x' + chr(92) + 'n", lcd_fake_reg(&lcd, 0x70030));' + NL +
        "	CHECK((lcd_fake_reg(&lcd, 0x70030) & 0xf0u) == 0x50u," + NL +
        '	      "A: PIPE_MISC: 6 bpc WITH dithering (the reference dithers exactly the 18 bpp pipes; Linux reports dither=yes for this one)");')
save("plan/ws031/tests/lcd-modeset-host-test.c", t)
g = load(L + "parity_lcd_regs.c")
g = rep(g, '	ROW("TRANSCONF", TRANSCONF(tr), 1, 0xc0000000u, 0xc0000000u);', '	ROW("TRANSCONF", TRANSCONF(tr), 1, 0xc0000000u, 0xc0000000u);' + NL +
        '	ROW("PIPE_MISC", PIPE_MISC(pipe), 0, 0u, 0u);        /* not in the register dump; Linux\'s display_info says dither=yes, bpp=18 */')
save(L + "parity_lcd_regs.c", g)
print("done")
